/* Browser host mode: the frontend, in the browser the user already has.
 *
 * Loaded before the app's own script. If the Tauri API is already there
 * (the desktop window), this returns immediately and nothing below exists. In a
 * browser it defines `window.__TAURI__.core.invoke` on top of the loopback
 * WebSocket the app serves, which is all `index.html` ever asks for - the same
 * command names, the same argument objects, the same payloads, including the
 * raw bytes of `get_frames` and `get_audio`.
 *
 * Why this exists at all: online play is PeerJS, PeerJS is WebRTC, and WebRTC
 * is not something a WebView necessarily has - Debian and Ubuntu build
 * webkit2gtk with WebRTC compiled out. The emulator stays in the app and the
 * page runs in Chrome or Firefox, so a Linux host can still host.
 *
 * Failure is loud on purpose: if the socket never opens, the page says so
 * instead of looking like a Tauri window with a dead backend.
 */
(() => {
  "use strict";
  if (window.__TAURI__) return;            // the desktop window: nothing to bridge

  // The token from the URL the app opened: it is the whole credential for this
  // socket, and it is stripped from the address bar immediately so it cannot be
  // shared by accident (a bookmark, a screenshot, a pasted "look at my game").
  const params = new URLSearchParams(window.location.search);
  const token = params.get("token") || "";
  if (token) {
    params.delete("token");
    const clean = window.location.pathname + (params.toString() ? "?" + params.toString() : "");
    window.history.replaceState(null, "", clean + window.location.hash);
  }

  if (typeof WebSocket !== "function") {
    document.body.dataset.bridge = "unsupported";
    return;
  }

  const scheme = window.location.protocol === "https:" ? "wss" : "ws";
  const url = `${scheme}://${window.location.host}/ws?token=${encodeURIComponent(token)}`;
  const socket = new WebSocket(url);
  socket.binaryType = "arraybuffer";

  let nextId = 1;
  const pending = new Map();               // id -> { resolve, reject }
  const state = { open: false, failed: null, requests: 0 };

  function settle(id, fn) {
    const entry = pending.get(id);
    if (!entry) return;
    pending.delete(id);
    fn(entry);
  }

  socket.addEventListener("open", () => {
    state.open = true;
    window.dispatchEvent(new CustomEvent("md-bridge-open"));
  });

  socket.addEventListener("message", (event) => {
    // Binary is a bulk read: eight bytes of request id, then the payload the
    // desktop path carries over the Tauri IPC instead.
    if (typeof event.data !== "string") {
      const view = new DataView(event.data);
      const id = Number(view.getBigUint64(0, true));
      settle(id, (entry) => entry.resolve(event.data.slice(8)));
      return;
    }
    let reply = null;
    try { reply = JSON.parse(event.data); } catch (_) { return; }
    if (!reply || typeof reply.id !== "number") return;
    settle(reply.id, (entry) => {
      if (reply.ok) entry.resolve(reply.value);
      else entry.reject(new Error(reply.error || "command failed"));
    });
  });

  const drop = (why) => {
    state.failed = why;
    state.open = false;
    for (const entry of pending.values()) entry.reject(new Error(why));
    pending.clear();
    window.dispatchEvent(new CustomEvent("md-bridge-closed", { detail: why }));
  };
  socket.addEventListener("close", () => drop(state.failed || "the app's browser host closed"));
  socket.addEventListener("error", () => drop("cannot reach the app's browser host"));

  function invoke(cmd, args) {
    if (state.failed) return Promise.reject(new Error(state.failed));
    if (socket.readyState !== WebSocket.OPEN) {
      return new Promise((resolve, reject) => {
        const wait = setTimeout(() => reject(new Error("the app's browser host is not answering")), 10000);
        socket.addEventListener("open", () => {
          clearTimeout(wait);
          invoke(cmd, args).then(resolve, reject);
        }, { once: true });
        socket.addEventListener("error", () => {
          clearTimeout(wait);
          reject(new Error("cannot reach the app's browser host"));
        }, { once: true });
      });
    }
    const id = nextId++;
    state.requests++;
    return new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject });
      socket.send(JSON.stringify({ id, cmd, args: args || {} }));
    });
  }

  // `hasDialog` in index.html is `__TAURI__.dialog.open`; leaving it undefined
  // is what makes the Browse button explain itself instead of doing nothing.
  window.__TAURI__ = { core: { invoke } };
  window.__mdBrowserHost = state;
  window.mdBridgeStatus = () => ({
    open: state.open,
    failed: state.failed,
    requests: state.requests,
    port: Number(window.location.port) || 0,
  });
})();
