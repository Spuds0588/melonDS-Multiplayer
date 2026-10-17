// Browser host mode: the same frontend, in a browser the user already has.
//
// Why this exists. Online play is PeerJS, which is WebRTC, and WebRTC is not
// something a WebView necessarily has: Debian and Ubuntu build webkit2gtk with
// `ENABLE_WEB_RTC` off, so on Linux the app's own window can load PeerJS and
// still have no `RTCPeerConnection` (measured 2026-10-17 - see `enable_webrtc`
// in lib.rs and the BUILD.md note). Rather than lose online play on the primary
// development platform, the emulator stays where it is and the *page* moves:
// this module serves the frontend on loopback and carries the same command
// interface over a WebSocket, so a host can open the UI in Chrome or Firefox,
// where WebRTC exists, and host from there.
//
// The two transports are deliberately the same contract. `browser-host.js`
// defines `window.__TAURI__.core.invoke` on top of this socket, and every
// command answers with the shape the Tauri IPC answers with: the same command
// names, the same argument objects, the same JSON payloads, and the same raw
// bytes for the two bulk reads (`get_frames`, `get_audio`). Nothing in
// `index.html` knows which one it is on, which is what keeps a second transport
// from becoming a second frontend.
//
// Scope and safety, because this is a socket that can start emulators and press
// their buttons:
//   * it binds 127.0.0.1 only, and only while the user asks for it (the Online
//     panel's "Open in a browser"); nothing is listening before that;
//   * every command needs a token minted at start and handed out only inside
//     the URL the app opens - a page that guesses the port still cannot command
//     anything, and the token is dropped when the host is stopped;
//   * the files it serves are the frontend the binary already contains, and
//     nothing else: no directory walking, no path from the request is ever
//     opened.
//
// Frame traffic is the reason the socket is not JSON: a four-console session is
// 1.5 MB per read at 60 Hz. Frames and audio go out as binary WebSocket frames
// whose first eight bytes are the request id, so the page can match them to the
// promise that asked. Everything else is a text frame of JSON.

use base64::Engine;
use parking_lot::Mutex;
use serde_json::{json, Value};
use sha1::{Digest, Sha1};
use std::io::{ErrorKind, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;
use std::time::Duration;
use tauri::Manager;

/// RFC 6455's handshake salt: `base64(sha1(key . GUID))`.
const WS_GUID: &str = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/// The frontend, as the binary already carries it (tauri embeds the same files
/// for the app's own window). `include_str!` rather than a directory read: an
/// installed bundle has no `frontend/` next to the binary, and a served file
/// that differs from the bundled one is a bug waiting to happen.
const INDEX_HTML: &str = include_str!("../../frontend/index.html");
const ONLINE_JS: &str = include_str!("../../frontend/online.js");
const GUEST_HTML: &str = include_str!("../../frontend/guest.html");
const BROWSER_HOST_JS: &str = include_str!("../../frontend/browser-host.js");
const PEERJS_JS: &str = include_str!("../../frontend/vendor/peerjs.min.js");

#[derive(Clone)]
pub struct BrowserHost {
    pub port: u16,
    pub token: String,
}

/// The running server, once one has been started. Holding the `Arc` rather than
/// the values means the port and token in it are the live ones, and there is no
/// second copy to keep in step.
static HOST: std::sync::OnceLock<Arc<Mutex<BrowserHost>>> = std::sync::OnceLock::new();

/// Starts the loopback server if it is not already up, and answers with the URL
/// to open. Idempotent: a second call returns the same port and token, so the
/// panel can ask freely.
pub fn start() -> Result<BrowserHost, String> {
    if let Some(host) = current() {
        return Ok(host);
    }

    // MD_BROWSER_PORT pins the port (a bookmarkable URL); 0 lets the OS pick a
    // free one, which is the default because a fixed port is a thing to
    // collide with.
    let requested: u16 = std::env::var("MD_BROWSER_PORT")
        .ok()
        .and_then(|v| v.trim().parse().ok())
        .unwrap_or(0);

    let listener = TcpListener::bind(("127.0.0.1", requested))
        .map_err(|err| format!("cannot listen on 127.0.0.1:{requested}: {err}"))?;
    let port = listener
        .local_addr()
        .map_err(|err| format!("no local address for the browser host: {err}"))?
        .port();

    // A fresh token per start: the URL is the credential, so a page left open
    // from a previous run cannot command a new one.
    let shared = Arc::new(Mutex::new(BrowserHost {
        port,
        token: random_token(),
    }));
    if HOST.set(shared.clone()).is_err() {
        // Two starts raced; the first one won and is already listening.
        return current().ok_or_else(|| "browser host state was lost".to_string());
    }
    let app = APP.get().cloned();
    let state = shared.clone();

    std::thread::Builder::new()
        .name("browser-host".into())
        .spawn(move || {
            for stream in listener.incoming() {
                let Ok(stream) = stream else { continue };
                let Some(app) = app.clone() else { continue };
                let state = state.clone();
                std::thread::Builder::new()
                    .name("browser-host-conn".into())
                    .spawn(move || {
                        if let Err(err) = serve(app, stream, state) {
                            log::debug!("browser host: connection ended: {err}");
                        }
                    })
                    .ok();
            }
        })
        .map_err(|err| format!("cannot start the browser host thread: {err}"))?;

    let host = shared.lock().clone();
    Ok(host)
}

/// The running server, if any.
pub fn current() -> Option<BrowserHost> {
    HOST.get().map(|host| host.lock().clone())
}

pub fn url_for(host: &BrowserHost) -> String {
    format!("http://127.0.0.1:{}/?token={}", host.port, host.token)
}

fn random_token() -> String {
    // Same source as the frontend's invite tokens: the OS, through uuid's v4
    // (which is drawn from getrandom). 128 bits, hex.
    uuid::Uuid::new_v4().simple().to_string() + &uuid::Uuid::new_v4().simple().to_string()[..16]
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------

/// What one WebSocket request produced. `Bytes` exists for the two bulk reads,
/// which are the whole reason the payloads are not JSON.
enum Reply {
    Json(Value),
    Bytes(Vec<u8>),
}

fn arg_str(args: &Value, key: &str) -> Result<String, String> {
    args.get(key)
        .and_then(|v| v.as_str())
        .map(|s| s.to_string())
        .ok_or_else(|| format!("missing string argument '{key}'"))
}

fn arg_i64(args: &Value, key: &str) -> Result<i64, String> {
    args.get(key)
        .and_then(|v| v.as_i64())
        .ok_or_else(|| format!("missing number argument '{key}'"))
}

/// One command, by the name the frontend already uses.
///
/// The command functions are called exactly as Tauri calls them (a `State` is
/// what `app.state()` hands back), so there is no second implementation of any
/// command to keep in step - only this list of names, which is the same list
/// `generate_handler!` carries.
fn dispatch(app: &tauri::AppHandle, cmd: &str, args: &Value) -> Result<Reply, String> {
    let state = app.state::<crate::AppState>();
    Ok(match cmd {
        "session_info" => Reply::Json(json!(crate::session_info(state)?)),
        "start_session" => {
            let players = arg_i64(args, "players")? as u8;
            let rom_path = arg_str(args, "romPath")?;
            Reply::Json(json!(crate::start_session(
                state, players, rom_path
            )?))
        }
        "stop_session" => {
            crate::stop_session(state)?;
            Reply::Json(Value::Null)
        }
        "per_game_state" => Reply::Json(crate::per_game_state(state)?),
        "set_per_game_fix" => {
            let enabled = args
                .get("enabled")
                .and_then(|v| v.as_bool())
                .ok_or_else(|| "missing boolean argument 'enabled'".to_string())?;
            Reply::Json(crate::set_per_game_fix(state, enabled)?)
        }
        "get_frames" => Reply::Bytes(crate::frames_payload(state.inner())),
        "get_audio" => {
            let seats: Vec<u32> = args
                .get("seats")
                .and_then(|v| v.as_array())
                .map(|a| a.iter().filter_map(|v| v.as_u64()).map(|v| v as u32).collect())
                .unwrap_or_default();
            Reply::Bytes(crate::audio_payload(state.inner(), &seats))
        }
        "set_input" => {
            let input: crate::PlayerInput = serde_json::from_value(
                args.get("input")
                    .cloned()
                    .ok_or_else(|| "missing argument 'input'".to_string())?,
            )
            .map_err(|err| format!("bad input: {err}"))?;
            crate::set_input(state, input)?;
            Reply::Json(Value::Null)
        }
        "link_status" => Reply::Json(crate::link_status(state)?),
        "link_selftest" => Reply::Json(crate::link_selftest(state)?),
        "set_jit" => {
            let enabled = args
                .get("enabled")
                .and_then(|v| v.as_bool())
                .ok_or_else(|| "missing boolean argument 'enabled'".to_string())?;
            crate::set_jit(state, enabled)?;
            Reply::Json(Value::Null)
        }
        "set_view_mode" => Reply::Json(json!(tauri::async_runtime::block_on(
            crate::set_view_mode(arg_str(args, "mode")?)
        )?)),
        "save_state" => Reply::Json(json!(tauri::async_runtime::block_on(
            crate::save_state(arg_str(args, "savePath")?)
        )?)),
        "load_state" => Reply::Json(json!(tauri::async_runtime::block_on(
            crate::load_state(arg_str(args, "savePath")?)
        )?)),
        "ui_config" => Reply::Json(crate::ui_config()?),
        "ui_heartbeat" => {
            crate::ui_heartbeat(arg_i64(args, "seq")? as u64);
            Reply::Json(Value::Null)
        }
        "frontend_log" => {
            crate::frontend_log(arg_str(args, "level")?, arg_str(args, "message")?);
            Reply::Json(Value::Null)
        }
        "list_roms" => Reply::Json(list_roms(args)),
        "quit_app" => {
            crate::quit_app(app.clone())?;
            Reply::Json(Value::Null)
        }
        other => return Err(format!("no such command '{other}'")),
    })
}

/// Candidate ROMs for a browser that has no file dialog.
///
/// The desktop app picks a ROM with the native dialog; a page in Chrome cannot,
/// and asking someone to type an absolute path on the honour system is a bad
/// first experience. This lists `*.nds` in a directory the page asks about (or
/// `MD_ROM_DIR`, or the process's working directory), one level deep, capped:
/// enough to turn a text field into a picker, never a filesystem browser.
fn list_roms(args: &Value) -> Value {
    let dir = args
        .get("dir")
        .and_then(|v| v.as_str())
        .map(|s| s.to_string())
        .filter(|s| !s.is_empty())
        .or_else(|| std::env::var("MD_ROM_DIR").ok().filter(|s| !s.is_empty()))
        .or_else(|| {
            std::env::current_dir()
                .ok()
                .map(|p| p.to_string_lossy().to_string())
        });

    let Some(dir) = dir else {
        return json!({ "dir": Value::Null, "roms": [] });
    };
    let path = std::path::Path::new(&dir);
    let mut roms: Vec<String> = Vec::new();
    if let Ok(entries) = std::fs::read_dir(path) {
        for entry in entries.flatten() {
            let p = entry.path();
            let is_rom = p
                .extension()
                .map(|e| e.eq_ignore_ascii_case("nds") || e.eq_ignore_ascii_case("dsi"))
                .unwrap_or(false);
            if is_rom {
                roms.push(p.to_string_lossy().to_string());
                if roms.len() >= 200 {
                    break;
                }
            }
        }
    }
    roms.sort();
    json!({ "dir": dir, "roms": roms })
}

// ---------------------------------------------------------------------------
// The HTTP + WebSocket server
// ---------------------------------------------------------------------------

/// Set once at startup by `attach`, so connection threads can reach the app.
static APP: std::sync::OnceLock<tauri::AppHandle> = std::sync::OnceLock::new();

/// Hands the module the app handle it needs to run commands. Called once from
/// `run()`; before that, `start()` has nothing to serve.
pub fn attach(app: tauri::AppHandle) {
    let _ = APP.set(app);
}

fn serve(app: tauri::AppHandle, mut stream: TcpStream, shared: Arc<Mutex<BrowserHost>>) -> std::io::Result<()> {
    stream.set_nodelay(true).ok();
    stream.set_read_timeout(Some(Duration::from_secs(60))).ok();

    let (method, target, headers) = match read_request(&mut stream)? {
        Some(v) => v,
        None => return Ok(()), // a port scan, or a client that opened and left
    };
    if method != "GET" {
        return respond_status(&mut stream, 405, "Method Not Allowed");
    }

    let path = target.split('?').next().unwrap_or("/");
    let query = target.split_once('?').map(|(_, q)| q).unwrap_or("");
    let token = param(query, "token").unwrap_or_default();

    // The upgrade is checked so a wrong token is answered with 403 rather than
    // an upgrade that closes later - the page can then say why.
    if path == "/ws" {
        let good = {
            let host = shared.lock();
            !token.is_empty() && constant_time_eq(token.as_bytes(), host.token.as_bytes())
        };
        if !good {
            return respond_status(&mut stream, 403, "Forbidden");
        }
        let Some(key) = header(&headers, "sec-websocket-key") else {
            return respond_status(&mut stream, 400, "Bad Request");
        };
        ws_handshake(&mut stream, key)?;
        log::info!("browser host: a page attached");
        return ws_serve(app, stream);
    }

    let file = match path {
        "/" | "/index.html" => Some((INDEX_HTML, "text/html; charset=utf-8")),
        "/online.js" => Some((ONLINE_JS, "text/javascript; charset=utf-8")),
        "/guest.html" => Some((GUEST_HTML, "text/html; charset=utf-8")),
        "/browser-host.js" => Some((BROWSER_HOST_JS, "text/javascript; charset=utf-8")),
        "/vendor/peerjs.min.js" => Some((PEERJS_JS, "text/javascript; charset=utf-8")),
        _ => None,
    };
    match file {
        Some((body, ctype)) => {
            let head = format!(
                "HTTP/1.1 200 OK\r\nContent-Type: {ctype}\r\nContent-Length: {}\r\n\
                 Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                body.len()
            );
            stream.write_all(head.as_bytes())?;
            stream.write_all(body.as_bytes())?;
            stream.flush()
        }
        None => respond_status(&mut stream, 404, "Not Found"),
    }
}

/// One header value, case-insensitively.
fn header<'a>(headers: &'a [(String, String)], name: &str) -> Option<&'a str> {
    headers
        .iter()
        .find(|(k, _)| k.eq_ignore_ascii_case(name))
        .map(|(_, v)| v.as_str())
}

/// One query parameter. No percent-decoding: the only parameter that matters is
/// the token, which is hex by construction.
fn param(query: &str, name: &str) -> Option<String> {
    query.split('&').find_map(|pair| {
        let (k, v) = pair.split_once('=')?;
        (k == name).then(|| v.to_string())
    })
}

/// Reads the request line and headers. `None` for a connection that closed
/// before saying anything.
#[allow(clippy::type_complexity)]
fn read_request(
    stream: &mut TcpStream,
) -> std::io::Result<Option<(String, String, Vec<(String, String)>)>> {
    let mut buf = Vec::with_capacity(1024);
    let mut byte = [0u8; 1];
    while buf.len() < 16384 {
        match stream.read(&mut byte) {
            Ok(0) => {
                return if buf.is_empty() {
                    Ok(None)
                } else {
                    Err(std::io::Error::new(ErrorKind::UnexpectedEof, "short request"))
                }
            }
            Ok(_) => {
                buf.push(byte[0]);
                if buf.ends_with(b"\r\n\r\n") {
                    break;
                }
            }
            Err(err) if err.kind() == ErrorKind::WouldBlock || err.kind() == ErrorKind::TimedOut => {
                return Err(err)
            }
            Err(err) => return Err(err),
        }
    }

    let text = String::from_utf8_lossy(&buf).to_string();
    let mut lines = text.split("\r\n");
    let request_line = lines.next().unwrap_or_default();
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or_default().to_string();
    let target = parts.next().unwrap_or("/").to_string();

    let mut headers = Vec::new();
    for line in lines {
        if line.is_empty() {
            break;
        }
        if let Some((name, value)) = line.split_once(':') {
            headers.push((name.trim().to_string(), value.trim().to_string()));
        }
    }
    Ok(Some((method, target, headers)))
}

fn respond_status(stream: &mut TcpStream, code: u16, text: &str) -> std::io::Result<()> {
    let body = format!("{code} {text}\n");
    let head = format!(
        "HTTP/1.1 {code} {text}\r\nContent-Type: text/plain; charset=utf-8\r\n\
         Content-Length: {}\r\nConnection: close\r\n\r\n",
        body.len()
    );
    stream.write_all(head.as_bytes())?;
    stream.write_all(body.as_bytes())?;
    stream.flush()
}

fn ws_handshake(stream: &mut TcpStream, key: &str) -> std::io::Result<()> {
    let mut hasher = Sha1::new();
    hasher.update(key.as_bytes());
    hasher.update(WS_GUID.as_bytes());
    let accept = base64::engine::general_purpose::STANDARD.encode(hasher.finalize());
    let reply = format!(
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\
         Sec-WebSocket-Accept: {accept}\r\n\r\n"
    );
    stream.write_all(reply.as_bytes())?;
    stream.flush()
}

/// The socket, after the upgrade: read a request, answer it, repeat.
fn ws_serve(app: tauri::AppHandle, mut stream: TcpStream) -> std::io::Result<()> {
    let mut fragment = Vec::new();
    loop {
        let Some(frame) = read_frame(&mut stream)? else {
            return Ok(()); // clean close
        };
        match frame.opcode {
            0x1 | 0x2 => {
                // Text frames carry commands. Binary frames from the page would
                // be nothing this protocol defines, so they are ignored rather
                // than guessed at.
                if frame.fin {
                    if frame.opcode == 0x1 {
                        handle_request(&app, &mut stream, &frame.payload)?;
                    }
                } else {
                    fragment.extend_from_slice(&frame.payload);
                }
            }
            0x0 => {
                fragment.extend_from_slice(&frame.payload);
                if frame.fin {
                    let payload = std::mem::take(&mut fragment);
                    handle_request(&app, &mut stream, &payload)?;
                }
            }
            0x8 => {
                let _ = write_frame(&mut stream, 0x8, &frame.payload);
                return Ok(());
            }
            0x9 => write_frame(&mut stream, 0xA, &frame.payload)?,
            _ => {}
        }
    }
}

fn handle_request(app: &tauri::AppHandle, stream: &mut TcpStream, payload: &[u8]) -> std::io::Result<()> {
    let text = String::from_utf8_lossy(payload).to_string();
    let request: Value = match serde_json::from_str(&text) {
        Ok(v) => v,
        Err(err) => {
            log::warn!("browser host: unreadable request: {err}");
            return Ok(());
        }
    };
    let id = request.get("id").and_then(|v| v.as_u64()).unwrap_or(0);
    let cmd = request.get("cmd").and_then(|v| v.as_str()).unwrap_or("");
    let args = request.get("args").cloned().unwrap_or(Value::Null);

    match dispatch(app, cmd, &args) {
        Ok(Reply::Json(value)) => {
            let reply = json!({ "id": id, "ok": true, "value": value });
            write_frame(stream, 0x1, reply.to_string().as_bytes())
        }
        Ok(Reply::Bytes(bytes)) => {
            // 8-byte id, then the payload: the page resolves the promise with
            // the bytes and never parses them.
            let mut out = Vec::with_capacity(8 + bytes.len());
            out.extend_from_slice(&id.to_le_bytes());
            out.extend_from_slice(&bytes);
            write_frame(stream, 0x2, &out)
        }
        Err(err) => {
            let reply = json!({ "id": id, "ok": false, "error": err });
            write_frame(stream, 0x1, reply.to_string().as_bytes())
        }
    }
}

struct Frame {
    fin: bool,
    opcode: u8,
    payload: Vec<u8>,
}

/// One WebSocket frame. `None` on a clean end of stream.
fn read_frame(stream: &mut TcpStream) -> std::io::Result<Option<Frame>> {
    let mut head = [0u8; 2];
    if let Err(err) = stream.read_exact(&mut head) {
        if err.kind() == ErrorKind::UnexpectedEof {
            return Ok(None);
        }
        return Err(err);
    }
    let fin = head[0] & 0x80 != 0;
    let opcode = head[0] & 0x0F;
    let masked = head[1] & 0x80 != 0;
    let len = (head[1] & 0x7F) as usize;

    let len = match len {
        126 => {
            let mut b = [0u8; 2];
            stream.read_exact(&mut b)?;
            u16::from_be_bytes(b) as usize
        }
        127 => {
            let mut b = [0u8; 8];
            stream.read_exact(&mut b)?;
            u64::from_be_bytes(b) as usize
        }
        n => n,
    };

    // A page's frames are always masked (RFC 6455 requires it), but a masked or
    // unmasked frame is read the same way here: the mask is applied if present.
    let mask = if masked {
        let mut m = [0u8; 4];
        stream.read_exact(&mut m)?;
        Some(m)
    } else {
        None
    };

    let mut payload = vec![0u8; len];
    stream.read_exact(&mut payload)?;
    if let Some(mask) = mask {
        for (i, byte) in payload.iter_mut().enumerate() {
            *byte ^= mask[i % 4];
        }
    }
    Ok(Some(Frame { fin, opcode, payload }))
}

fn write_frame(stream: &mut TcpStream, opcode: u8, payload: &[u8]) -> std::io::Result<()> {
    let mut head = Vec::with_capacity(10);
    head.push(0x80 | opcode);
    let len = payload.len();
    if len < 126 {
        head.push(len as u8);
    } else if len <= u16::MAX as usize {
        head.push(126);
        head.extend_from_slice(&(len as u16).to_be_bytes());
    } else {
        head.push(127);
        head.extend_from_slice(&(len as u64).to_be_bytes());
    }
    stream.write_all(&head)?;
    stream.write_all(payload)?;
    stream.flush()
}

/// Length-independent comparison for the token: a `==` on a secret leaks where
/// it stopped matching through timing, and this runs on a socket that can start
/// emulators.
fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}
