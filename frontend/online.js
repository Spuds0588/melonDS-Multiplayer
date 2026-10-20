/* Online play transport: a host streams each player their own console, and
 * gets that player's input back.
 *
 * Same shape as the sister project (mgba-splitscreen/src/online.js), for the
 * same reasons, but carrying DS video and audio:
 *
 *   - The host stays authoritative. It owns the emulator session and every
 *     console in it; a guest sends nothing but input. Two consoles can be run
 *     by one machine or by four people in four cities and the emulation does
 *     not change either way.
 *   - Transport is PeerJS (WebRTC DataChannels). Payloads go peer to peer; only
 *     the signalling handshake touches the broker. PeerJS is vendored in
 *     `vendor/peerjs.min.js`, so no CDN and no npm build step.
 *   - Video is per seat, JPEG, latest frame wins. A DS console is two 256x192
 *     screens; they are composited into one 256x384 image per guest, so a guest
 *     sees a whole console the way they would hold one. JPEG at quality ~0.5 is
 *     15-25 KB a frame against 768 KB of raw RGBA - the sister project measured
 *     what raw frames do to a reliable DataChannel (a backlog that grows
 *     without bound, guests minutes behind at 3-4 fps) and 30 fps of JPEG is
 *     what replaced it. `toBlob` is used rather than `toDataURL` so the encode
 *     does not block the host's frame pump and no base64 ever exists.
 *   - Audio is per seat too, coalesced, never dropped. The SPU produces ~60
 *     chunks/s; the host batches ~30 ms of one console into one send, and a
 *     guest that falls behind drops its OLDEST samples, so it hears slightly
 *     compressed recent audio instead of ever-staler garbage.
 *   - Two ways in: a magic link (per-seat capability token in the URL fragment,
 *     so it never reaches a referrer) and a six-digit join code derived from
 *     the peer id, which the host approves by hand. A code carries no secret,
 *     so approval is what keeps a guessed code from taking a seat.
 */
(() => {
  const PROTOCOL = 1;
  const DS_W = 256, DS_H = 192;
  const STREAM_H = DS_H * 2;      // both screens, stacked like the console
  const VIDEO_MIN_INTERVAL = 1000 / 30;
  const AUDIO_BATCH_MS = 30;
  const AUDIO_MAX_PENDING = 16384;
  const CODE_PREFIX = 'mds-';
  const JOIN_REQUEST_TTL_MS = 30000;
  const INVITE_TTL_MS = 10 * 60 * 1000;
  // How long a guest waits for the datachannel to open before it says so. A
  // join that cannot complete has to end in a reason, not in a spinner.
  const GUEST_CONNECT_TIMEOUT_MS = 20000;
  // A guest that vanishes without closing its datachannel - a closed tab, a
  // laptop lid, a dead wifi link - otherwise keeps its seat forever: the host
  // goes on encoding and sending into a channel nobody is draining. Measured
  // 2026-10-20: 30+ seconds after the guest's tab closed the host still
  // reported `guests: 1` and its send counter kept climbing. The host pings and
  // drops a connection it has not heard from; a guest only counts as silent
  // once it has answered a ping, so a guest page older than the ping protocol
  // is never dropped by it.
  const HOST_PING_MS = 2500;
  const HOST_PEER_TIMEOUT_MS = 8000;

  const state = {
    role: 'idle',              // idle | host | guest
    peer: null,
    hostConn: null,            // guest side: the connection to the host
    connections: [],           // host side: [{conn, player, name}]
    pending: [],               // host side: join requests awaiting a decision
    code: null,
    session: null,
    seat: null,                // guest side: which console this guest plays
    players: 2,
    callbacks: {},
    // Video: one compositing canvas and one encode per seat. Encoding is
    // serialized per seat and only the newest frame is kept while an encode is
    // in flight, so a slow link can never build a backlog.
    encoders: new Map(),
    videoStats: new Map(),     // seat -> { sent, bytes, lastSent }
    audioPending: new Map(),   // seat -> { chunks: [], rate, lastSent, busy }
    guestFrames: 0,
    guestBytes: 0,
    guestFps: 0,
    guestWindow: 0,
    guestTimer: null,
    timer: null,
  };

  function report(text) { state.callbacks.status?.(text); }

  function log(...args) { console.log('[online]', ...args); }

  function randomToken(size = 32) {
    const bytes = new Uint8Array(size);
    crypto.getRandomValues(bytes);
    return [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');
  }

  function randomCode() {
    return String(100000 + Math.floor(Math.random() * 900000));
  }

  function query(name) {
    try { return new URLSearchParams(window.location.search).get(name); } catch (_) { return null; }
  }

  function fragment(name) {
    try {
      const raw = window.location.hash.startsWith('#') ? window.location.hash.slice(1) : window.location.hash;
      return new URLSearchParams(raw).get(name);
    } catch (_) { return null; }
  }

  function send(conn, message) {
    try {
      if (conn && conn.open) conn.send(message);
    } catch (err) {
      console.warn('[online] send failed:', err);
    }
  }

  function peerConstructor() {
    if (typeof window.Peer === 'function') return window.Peer;
    throw new Error('PeerJS did not load (vendor/peerjs.min.js)');
  }

  // PeerJS drops its own broker socket on its own - a host left idle for hours,
  // a laptop that slept, a network change - and when it does it sets `peer.id`
  // to null and marks itself disconnected. Existing guests keep streaming (a
  // datachannel is peer to peer) but the host is no longer registered, so the
  // next guest's connection attempt goes nowhere while the panel still shows a
  // join code and an invite link. Observed on the browser host 2026-10-20:
  // `stats().peerId` read null while a seat was mid-stream. `reconnect()`
  // restores the same id, so recovery costs the host nothing but a retry.
  function watchSignalling(peer, role) {
    let attempt = 0;
    let timer = null;
    const retry = () => {
      timer = null;
      if (state.peer !== peer || peer.destroyed) return;
      attempt += 1;
      if (attempt > 6) {
        report(`Online ${role}: the signalling server is unreachable - ${role === 'host' ? 'new guests cannot join until it is back' : 'the host cannot be reached'}`);
        return;
      }
      try { peer.reconnect(); } catch (err) { log('reconnect threw', err); }
      timer = setTimeout(retry, Math.min(15000, 1000 * 2 ** (attempt - 1)));
    };
    peer.on('disconnected', () => {
      log('signalling down, reconnecting');
      report(`Online ${role}: lost the signalling server - reconnecting`);
      // The panel is repainted by host events, and this is one: the code and
      // the invites on screen are not usable until the socket is back.
      if (role === 'host') publishHostState();
      if (timer) clearTimeout(timer);
      attempt = 0;
      timer = setTimeout(retry, 1000);
    });
    peer.on('open', () => {
      if (timer) { clearTimeout(timer); timer = null; }
      attempt = 0;
      if (role === 'host') publishHostState();
    });
  }

  function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

  async function openPeer(id) {
    const Peer = peerConstructor();
    const peer = new Peer(id, { debug: 0 });
    const opened = new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('PeerJS broker timeout')), 15000);
      peer.on('open', () => { clearTimeout(timer); resolve(); });
      peer.on('error', (err) => { clearTimeout(timer); reject(err); });
    });
    // `unavailable-id` is an answer, not a failure: the caller retries with a
    // fresh code (see startHost).
    await opened;
    return peer;
  }

  // ---------------------------------------------------------------- host side

  function seatsInUse() {
    return new Set(state.connections.map((c) => c.player));
  }

  function lowestFreeSeat() {
    const used = seatsInUse();
    for (let p = 1; p < state.players; p++) if (!used.has(p) && !state.pending.some((q) => q.seat === p)) return p;
    return -1;
  }

  function hostState() {
    return {
      role: state.role,
      code: state.code,
      players: state.players,
      session: state.session,
      seats: state.connections.map((c) => ({ player: c.player, name: c.name })),
      pending: state.pending.map((q) => ({ id: q.id, at: q.at, suggested: q.seat })),
      invites: state.callbacks.buildInvites ? state.callbacks.buildInvites() : [],
    };
  }

  function publishHostState() {
    state.callbacks.hostState?.(hostState());
  }

  function hostMessage(entry, message) {
    if (!message || typeof message !== 'object') return;
    const conn = entry.conn;

    if (message.type === 'hello') {
      // Magic-link join: the seat and its capability arrive together, and the
      // token is checked against this seat's own invite (consuming one URL does
      // not consume another's).
      const player = Number(message.player);
      const check = state.callbacks.checkInvite?.(player, message.token);
      if (!check || !check.ok) {
        send(conn, { type: 'error', message: (check && check.reason) || 'This invite is not valid' });
        conn.close();
        return;
      }
      if (seatsInUse().has(player)) {
        send(conn, { type: 'error', message: `Player ${player + 1} is already connected` });
        conn.close();
        return;
      }
      state.callbacks.consumeInvite?.(player);
      admit(entry, player, 'invite');
      return;
    }

    if (message.type === 'join_request') {
      // Code join: no secret, so the host decides. Auto-declines at full so a
      // guessed code cannot even make the host look up.
      const seat = lowestFreeSeat();
      if (seat < 0) {
        send(conn, { type: 'error', message: 'The game is full' });
        conn.close();
        return;
      }
      if (state.pending.length >= 2) {
        send(conn, { type: 'error', message: 'The host is busy - try again in a moment' });
        conn.close();
        return;
      }
      const request = { id: randomToken(4), at: Date.now(), seat, conn, entry };
      state.pending.push(request);
      report(`Online host: Player ${seat + 1}'s seat requested - approve or deny`);
      publishHostState();
      const timer = setTimeout(() => denyRequest(request.id, 'The host did not answer'), JOIN_REQUEST_TTL_MS);
      request.timer = timer;
      return;
    }

    if (message.type === 'input' && Number.isInteger(entry.player)) {
      const buttons = Number.isInteger(message.buttons) ? message.buttons >>> 0 : 0;
      const touch = Array.isArray(message.touch) && message.touch.length === 2
        ? [Number(message.touch[0]) | 0, Number(message.touch[1]) | 0]
        : null;
      state.callbacks.input?.(entry.player, buttons, touch);
    }
  }

  function admit(entry, player, how) {
    entry.player = player;
    state.connections.push(entry);
    state.pending = state.pending.filter((q) => q.entry !== entry);
    send(entry.conn, {
      type: 'welcome',
      protocol: PROTOCOL,
      player,
      players: state.players,
      w: DS_W,
      h: STREAM_H,
    });
    report(`Online host: Player ${player + 1} connected (${how})`);
    log('admitted seat', player, 'via', how);
    publishHostState();
    state.callbacks.seatJoined?.(player);
  }

  function denyRequest(id, reason) {
    const request = state.pending.find((q) => q.id === id);
    if (!request) return;
    clearTimeout(request.timer);
    state.pending = state.pending.filter((q) => q !== request);
    send(request.conn, { type: 'error', message: reason || 'The host declined' });
    try { request.conn.close(); } catch (_) {}
    report(`Online host: declined a join (${reason || 'declined'})`);
    publishHostState();
  }

  function approveRequest(id, seat) {
    const request = state.pending.find((q) => q.id === id);
    if (!request) return false;
    const want = Number.isInteger(seat) ? seat : request.seat;
    if (want < 1 || want >= state.players || seatsInUse().has(want)) {
      denyRequest(id, 'That seat is taken');
      return false;
    }
    clearTimeout(request.timer);
    admit(request.entry, want, 'code');
    return true;
  }

  function setupHostConnection(conn) {
    const entry = { conn, player: null, name: '', lastSeen: performance.now(), answersPings: false };
    conn.on('open', () => log('guest connection open'));
    conn.on('data', (message) => {
      if (message && typeof message === 'object') {
        entry.lastSeen = performance.now();
        if (message.type === 'pong') { entry.answersPings = true; return; }
      }
      hostMessage(entry, message);
    });
    conn.on('close', () => dropConnection(entry));
    conn.on('error', (err) => { console.warn('[online] guest connection error', err); dropConnection(entry); });
  }

  function dropConnection(entry) {
    const was = state.connections.includes(entry);
    state.connections = state.connections.filter((c) => c !== entry);
    state.pending = state.pending.filter((q) => q.entry !== entry);
    if (entry.player != null) {
      state.encoders.delete(entry.player);
      state.audioPending.delete(entry.player);
      // The counters go with the seat: `stats().seats` is what a driver and the
      // panel read to say who is connected, so a departed seat must not linger
      // there as a stream that stopped moving.
      state.videoStats.delete(entry.player);
      state.callbacks.seatLeft?.(entry.player, was);
    }
    publishHostState();
  }

  // The host's stream loop. It pulls from the page rather than being pushed at,
  // so the emulation pump and the stream cadence stay independent: a slow link
  // slows the stream, never the emulation.
  function tick() {
    if (state.role !== 'host' || !state.connections.length) return;
    const now = performance.now();
    // Liveness first: a seat whose guest has gone silent is dropped, so the
    // console becomes local again instead of streaming into nothing.
    for (const entry of [...state.connections]) {
      if (entry.answersPings && now - entry.lastSeen > HOST_PEER_TIMEOUT_MS) {
        log('guest went silent - dropping its seat', entry.player);
        const seat = entry.player;
        try { entry.conn.close(); } catch (_) {}
        dropConnection(entry);
        // Reported after the drop, because the page's own seatLeft handler
        // repaints the status line and would otherwise erase the reason.
        report(`Online host: Player ${seat == null ? '?' : seat + 1} went silent - their console is local again`);
        continue;
      }
      if (now - (entry.lastPing || 0) >= HOST_PING_MS) {
        entry.lastPing = now;
        send(entry.conn, { type: 'ping', at: Math.round(now) });
      }
    }
    // Adaptive cap: the encode share has to stay bounded as seats are added.
    const interval = Math.max(VIDEO_MIN_INTERVAL, (1000 / 30) * Math.ceil(state.connections.length / 2));
    for (const entry of state.connections) {
      if (entry.player == null) continue;
      const seat = entry.player;
      const stats = state.videoStats.get(seat) || { sent: 0, bytes: 0, lastSent: 0 };
      state.videoStats.set(seat, stats);
      if (entry.paused || now - stats.lastSent < interval) continue;
      const enc = encoderFor(seat);
      if (enc.busy) { enc.wanted = true; continue; }
      stats.lastSent = now;
      encodeSeat(seat)
        .then((blob) => {
          if (!blob) return;
          stats.sent++;
          stats.bytes += blob.size;
          send(entry.conn, { type: 'frame', seat, w: DS_W, h: STREAM_H, data: blob });
        })
        .catch((err) => console.warn('[online] encode failed', err));
    }
    pumpAudio(now);
  }

  function encoderFor(seat) {
    let enc = state.encoders.get(seat);
    if (!enc) {
      const canvas = document.createElement('canvas');
      canvas.width = DS_W;
      canvas.height = STREAM_H;
      enc = { canvas, ctx: canvas.getContext('2d'), busy: false, wanted: false };
      state.encoders.set(seat, enc);
    }
    return enc;
  }

  // One seat's two screens into one JPEG. The page owns the console's canvases;
  // this asks for them by seat and draws them stacked.
  function encodeSeat(seat) {
    const enc = encoderFor(seat);
    enc.busy = true;
    return new Promise((resolve) => {
      const screens = state.callbacks.frameSource?.(seat);
      if (!screens || !screens.top) { enc.busy = false; resolve(null); return; }
      enc.ctx.clearRect(0, 0, DS_W, STREAM_H);
      enc.ctx.drawImage(screens.top, 0, 0, DS_W, DS_H);
      enc.ctx.drawImage(screens.bottom || screens.top, 0, DS_H, DS_W, DS_H);
      enc.canvas.toBlob((blob) => {
        enc.busy = false;
        resolve(blob);
      }, 'image/jpeg', 0.5);
    });
  }

  // ---- Audio: coalesce per seat, never drop ----
  function pumpAudio(now) {
    for (const entry of state.connections) {
      if (entry.player == null) continue;
      let pending = state.audioPending.get(entry.player);
      if (!pending) { pending = { rate: 48000, chunks: [], lastSent: 0 }; state.audioPending.set(entry.player, pending); }
      const chunk = state.callbacks.audioSource?.(entry.player, pending.rate);
      if (chunk && chunk.length) {
        // The chunk is [u32 rate][stereo s16]; the rate can change (the DS's
        // sound bias switch), so a change flushes the old-rate batch first.
        const rate = new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength).getUint32(0, true);
        if (pending.chunks.length && rate !== pending.rate) flushAudio(entry);
        pending.rate = rate;
        pending.chunks.push(chunk.subarray(4));
        let total = 0;
        for (const c of pending.chunks) total += c.length;
        while (total > AUDIO_MAX_PENDING && pending.chunks.length > 1) {
          total -= pending.chunks.shift().length;
        }
      }
      if (pending.chunks.length && now - pending.lastSent >= AUDIO_BATCH_MS) flushAudio(entry);
    }
  }

  function flushAudio(entry) {
    const pending = state.audioPending.get(entry.player);
    if (!pending || !pending.chunks.length) return;
    let total = 4;
    for (const c of pending.chunks) total += c.length;
    const merged = new Uint8Array(total);
    new DataView(merged.buffer).setUint32(0, pending.rate, true);
    let off = 4;
    for (const c of pending.chunks) { merged.set(c, off); off += c.length; }
    pending.chunks = [];
    pending.lastSent = performance.now();
    send(entry.conn, { type: 'audio', rate: pending.rate, data: merged.buffer });
  }

  async function startHost(players) {
    if (state.role === 'host') return state.code;
    stop();
    state.players = Math.max(2, Math.min(4, players | 0 || 2));
    state.session = randomToken(8);
    state.role = 'host';
    try {
      let peer = null;
      for (let attempt = 0; attempt < 5 && !peer; attempt++) {
        const code = randomCode();
        try {
          peer = await openPeer(CODE_PREFIX + code);
          state.code = code;
        } catch (err) {
          // `unavailable-id` means somebody else holds that code right now;
          // every other error is fatal.
          if (err && /unavailable|taken/i.test(String(err.type || err.message || err))) continue;
          throw err;
        }
      }
      if (!peer) throw new Error('no free join code from the broker');
      state.peer = peer;
      watchSignalling(peer, 'host');
      peer.on('connection', setupHostConnection);
      peer.on('error', (err) => report(`Online host: ${err.message || err}`));
      state.timer = setInterval(tick, 1000 / 30);
      report(`Online host ready - join code ${state.code}`);
      log('hosting as', peer.id, 'players', state.players);
      publishHostState();
      return state.code;
    } catch (err) {
      state.role = 'idle';
      state.code = null;
      report(`Online unavailable: ${err.message || err}`);
      return null;
    }
  }

  // --------------------------------------------------------------- guest side

  function guestMessage(message) {
    if (!message || typeof message !== 'object') return;
    // The host's liveness question. Answering it is what lets the host notice a
    // guest that vanished (see HOST_PING_MS) instead of streaming into a dead
    // channel for the rest of the session.
    if (message.type === 'ping') {
      send(state.hostConn, { type: 'pong', at: message.at });
      return;
    }
    if (message.type === 'welcome') {
      state.seat = message.player;
      state.players = message.players || 2;
      report(`Online guest: connected as Player ${message.player + 1}`);
      state.callbacks.welcome?.(message);
      return;
    }
    if (message.type === 'frame' && message.data) {
      state.callbacks.frame?.(message.data, message.w, message.h);
      return;
    }
    if (message.type === 'audio' && message.data) {
      const bytes = message.data instanceof ArrayBuffer ? new Uint8Array(message.data) : new Uint8Array(message.data);
      state.callbacks.audio?.(bytes);
      return;
    }
    if (message.type === 'error') {
      const reason = message.message || 'the host refused the connection';
      // Before a seat is handed out there is nothing to keep: reset so the
      // player can try another code instead of reloading the page.
      if (state.seat == null) { guestAbort(`${reason} (refused)`); return; }
      report(`Online guest: ${reason}`);
      state.callbacks.error?.(reason);
    }
  }

  // One phrasing per failure the guest can actually hit, so the page can show a
  // reason the player can act on instead of "connecting…" forever.
  function joinFailureReason(err) {
    const text = String((err && (err.type || err.message)) || err || '');
    if (/peer-unavailable/i.test(text)) return 'no host is answering that code (unavailable) - check the code and that the host is still hosting';
    if (/network|server-error|socket-error|socket-closed|ssl/i.test(text)) return 'cannot reach the signalling server (unavailable) - a proxy or firewall may be blocking it';
    if (/browser-incompatible/i.test(text)) return 'this browser has no WebRTC (unavailable)';
    if (/unavailable-id/i.test(text)) return 'that join code is unavailable - the host may have just restarted';
    return `the connection failed (unavailable): ${text || 'no reason given'}`;
  }

  // Abandon a guest join: stop the peer, drop the connection, go back to idle so
  // the page can retry, and report once. Duplicate errors from the peer and the
  // connection therefore cost one message, not two.
  function guestAbort(reason) {
    if (state.role !== 'guest') return;
    if (state.guestTimer) { clearTimeout(state.guestTimer); state.guestTimer = null; }
    const conn = state.hostConn;
    state.hostConn = null;
    state.role = 'idle';
    state.seat = null;
    try { conn?.close(); } catch (_) {}
    try { state.peer?.destroy(); } catch (_) {}
    state.peer = null;
    log('guest join aborted:', reason);
    report(`Online guest: ${reason}`);
    state.callbacks.error?.(reason);
  }

  async function startGuest() {
    if (state.role === 'guest') return true;
    const peerId = query('peer');
    const direct = query('code') || fragment('code');
    const token = fragment('token') || query('token');
    const player = Number(fragment('player') || query('player'));
    const expires = Number(query('expires') || fragment('expires') || 0);
    const session = fragment('session') || query('session');
    const target = peerId || (direct ? CODE_PREFIX + String(direct).replace(/\D/g, '') : null);
    if (!target) { report('Online: this link has no host in it'); return false; }
    if (token && (!Number.isInteger(player) || player < 1 || !session || !expires || Date.now() >= expires)) {
      report('Online invite is missing, expired, or invalid');
      return false;
    }
    state.role = 'guest';
    try {
      state.peer = await openPeer();
      // The peer answers questions the datachannel never will: a code nobody
      // holds comes back here as `peer-unavailable`, while the connection just
      // stays closed.
      state.peer.on('error', (err) => guestAbort(joinFailureReason(err)));
      const conn = state.peer.connect(target, { reliable: true });
      state.hostConn = conn;
      let opened = false;
      state.guestTimer = setTimeout(() => {
        if (!opened) guestAbort(`the host did not answer within ${Math.round(GUEST_CONNECT_TIMEOUT_MS / 1000)} s (unavailable)`);
      }, GUEST_CONNECT_TIMEOUT_MS);
      const stopWaiting = () => {
        opened = true;
        if (state.guestTimer) { clearTimeout(state.guestTimer); state.guestTimer = null; }
      };
      conn.on('open', () => {
        stopWaiting();
        if (token) {
          send(conn, { type: 'hello', token, player, session, protocol: PROTOCOL });
          report('Online guest: connecting to host…');
        } else {
          send(conn, { type: 'join_request', protocol: PROTOCOL });
          report('Online guest: asking the host to accept…');
        }
      });
      conn.on('data', guestMessage);
      conn.on('close', () => {
        if (!opened) { guestAbort('the host closed the connection before the stream started (refused)'); return; }
        report('Online guest: host disconnected');
      });
      conn.on('error', (err) => {
        if (!opened) { guestAbort(joinFailureReason(err)); return; }
        report(`Online guest: ${err.message || 'connection failed'}`);
      });
      return true;
    } catch (err) {
      guestAbort(joinFailureReason(err));
      return false;
    }
  }

  function sendInput(buttons, touch) {
    if (state.role !== 'guest' || !state.hostConn) return false;
    send(state.hostConn, { type: 'input', buttons: buttons >>> 0, touch: touch || null });
    return true;
  }

  function noteGuestFrame(bytes) {
    const now = performance.now();
    if (!state.guestWindow || now - state.guestWindow >= 1000) {
      state.guestWindow = now;
      state.guestFps = 0;
    }
    state.guestFrames++;
    state.guestFps++;
    state.guestBytes += bytes;
  }

  // ------------------------------------------------------------------ shared

  function stop() {
    if (state.timer) { clearInterval(state.timer); state.timer = null; }
    for (const request of state.pending) clearTimeout(request.timer);
    state.pending = [];
    for (const entry of state.connections) {
      try { entry.conn.close(); } catch (_) {}
    }
    state.connections = [];
    try { state.hostConn?.close(); } catch (_) {}
    state.hostConn = null;
    try { state.peer?.destroy(); } catch (_) {}
    state.peer = null;
    if (state.guestTimer) { clearTimeout(state.guestTimer); state.guestTimer = null; }
    state.encoders.clear();
    state.audioPending.clear();
    state.videoStats.clear();
    state.role = 'idle';
    state.code = null;
    state.seat = null;
  }

  function init(callbacks) {
    state.callbacks = callbacks || {};
    // A guest link opens straight into the guest role; the host role is always
    // an explicit choice on the host's own machine.
    if (query('online') === 'join') startGuest();
  }

  window.mdOnline = {
    init,
    startHost,
    stop,
    startGuest,
    sendInput,
    approveRequest,
    denyRequest,
    hostState,
    noteGuestFrame,
    // Introspection for the UI and for a driver: what the host is actually
    // sending is the number that decides whether a stream is healthy.
    stats: () => ({
      role: state.role,
      code: state.code,
      // The broker id is what a magic link points at; a completed session is
      // "copy this link", not "read me a number".
      peerId: state.peer ? state.peer.id : null,
      seats: [...state.videoStats.entries()].map(([seat, s]) => ({ seat, sent: s.sent, bytes: s.bytes })),
      guests: state.connections.length,
      pending: state.pending.length,
      guest: { frames: state.guestFrames, bytes: state.guestBytes, fps: state.guestFps, seat: state.seat },
      // Whether the broker still knows this peer. A host whose signalling socket
      // has dropped keeps streaming to its existing guests but cannot be found
      // by a new one, so the panel has to be able to say so.
      signalling: !state.peer ? 'none' : state.peer.disconnected ? 'down' : state.peer.open ? 'open' : 'connecting',
    }),
    isHost: () => state.role === 'host',
    isGuest: () => state.role === 'guest',
    videoMinInterval: VIDEO_MIN_INTERVAL,
  };
})();
