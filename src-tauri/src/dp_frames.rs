//! The 802.11 frame codec for the Download Play joiner: parsing what the
//! host's game wrote (beacons, management responses, commands, ACKs) and
//! building the frames a real client sends back (authentication, association,
//! replies).
//!
//! Owner of the wire format and nothing else. This module has no bus, no
//! timers, no atomics and no session state: every function here is pure over
//! the bytes in and the bytes out, which is why the protocol can be exercised
//! without an emulator. The step machine in `dp_joiner` owns *when* these run;
//! this module owns *what the bytes mean*.
//!
//! Gating note: none of this runs unless the joiner is armed (see
//! `dp_joiner`'s module docs) - the codec itself neither knows nor cares.

/// The `fc=0080` beacon's own fields, as the host's game wrote them. The client
/// echoes the SSID and rates out of this rather than inventing them, so the
/// association request carries the room the host actually advertised.
#[derive(Debug, Clone, PartialEq)]
pub struct HostBeacon {
    /// Transmitter address (frame addr2) - who to answer.
    pub mac: [u8; 6],
    /// BSSID (frame addr3) - the identity the host answers as.
    pub bssid: [u8; 6],
    /// The host's channel byte, as carried on the wire.
    pub channel: u8,
    /// The host's US-timer value at the beacon (body offset 0, 8 bytes).
    pub clock: u64,
    /// Beacon interval (body offset 8, 2 bytes): how often the host's game
    /// transmits this beacon. The firmware hands it to a downloaded child in
    /// the 027FFC42h handoff block, so it is captured here rather than
    /// invented there.
    pub interval: u16,
    /// Capability information.
    pub capability: u16,
    /// SSID information element (id 0), verbatim.
    pub ssid: Vec<u8>,
    /// The four-byte game serial (Nintendo's `0040xxxx`) out of the beacon's
    /// proprietary `DD` tag, or all-zero when the beacon has none. A real DS
    /// client echoes this in the first four bytes of its own 32-byte SSID (see
    /// `client_ssid`), so the host can tell a client is asking for *its* title -
    /// and it is the one field the first live runs got wrong.
    pub serial: [u8; 4],
    /// The two bytes that follow the serial in the beacon's `DD` tag (data
    /// offset 16). Every captured client echoes these into its own SSID's third
    /// and fourth words: MPH's beacon advertises `12 23` here and its clients
    /// send `0A 00 40 00 12 23 00 00`, NSMB's `DA A3` -> `50 01 40 00 DA A3 00
    /// 00`, and a third title's `A9 D4` -> `25 00 00 00 A9 D4 00 00`. The first
    /// version of this joiner left these zero, and the host's association
    /// response refused the client (`status 0001`, AID `0000`).
    pub token: [u8; 2],
    /// The four supported-rate bytes. This is *not* a standard information
    /// element: Nintendo's own beacon/association layout (`WifiAP::RecvPacket`)
    /// puts the four rate bytes straight after the fixed fields, with the IEs
    /// following *after* them. Reading them as an IE id would mistake the first
    /// two bytes for a two-byte rates field, which is why the first live run
    /// logged "rates 2 bytes" out of a beacon that carried four.
    pub rates: Vec<u8>,
    /// Total length of the proprietary `DD` tag (id + length + data), or 0
    /// when the beacon carries none. GBATEK's firmware handoff stages a
    /// "beacon size" computed off this tag's size, so the tag length is
    /// captured rather than the size guessed.
    pub dd_len: usize,
}

/// What one frame in the joiner's inbox was.
#[derive(Debug, PartialEq)]
pub enum Rx {
    Beacon(HostBeacon),
    /// An authentication response (fc=0x00B0) addressed to this console: the
    /// transaction sequence the host is answering and its status code. Status
    /// is the whole question - a host can *answer* an authentication and still
    /// refuse it (non-zero status), and that refusal is what makes the
    /// following association request a class-2 frame from a station the host
    /// does not consider authenticated.
    AuthResponse { seq: u16, status: u16 },
    /// An association response (fc=0x0010) addressed to this console: the
    /// status the host gave the association and the AID it assigned. A status
    /// of 0 with a nonzero AID is the host *registering* a client - the same
    /// condition its own `[HOST] syncing client` line and `IsMPClient` require -
    /// and anything else is not one. The first live Star Fox run got
    /// `status=0001, aid=0000`, which the earlier code could not see because it
    /// never read the field.
    AssocResponse { status: u16, aid: u16 },
    /// A deauthentication (fc=0x00C0) addressed to this console, with the
    /// reason code the host put in the body - the host's own words for why it
    /// refused.
    Deauth { reason: u16 },
    /// One of the host's own commands (`fc=0x0228`, addressed to the multicast
    /// command MAC `03:09:BF:00:00:00`): the Download Play CMD DATA header,
    /// per GBATEK - reply time at body 0, slave flags at body 2, size in
    /// halfwords at 4, flags at 5, and the command at 6 (`01` NameRequest,
    /// `03` RSA, `04` DataPacket, `05` Done). This is the frame the client
    /// answers.
    Command {
        command: u8,
        size: u8,
        flags: u8,
        slave_flags: u16,
        /// For a `04` DataPacket only: the packet number GBATEK carries at body
        /// `09h`, and the image data at `0Bh`. Those two fields are the download
        /// itself - everything before this pass answered a DataPacket with an
        /// RsaReply, so the host re-sent one packet forever.
        packet: Option<u16>,
        data: Vec<u8>,
        /// For a `03` RSA command only: the whole image's size in bytes, summed
        /// from the RSA signature frame's header/ARM9/ARM7 sizes. The joiner
        /// compares its collected bytes against this to know it has everything.
        rsa_total: Option<u64>,
        /// For a `03` RSA command only: the same frame's load map - where the
        /// child image's segments go and where the client jumps. Carried
        /// alongside the total because the total alone can judge the transfer
        /// finished but cannot boot it.
        rsa_boot: Option<BootInfo>,
    },
    /// The hardware-generated CMD ACK (`fc=0x0218`): its error flags at body 2,
    /// bit N set when slave N did not answer. Bit N *clear* is the host's own
    /// statement that it collected slave N's reply - the only feedback the
    /// protocol gives a client, and what this joiner advances its username
    /// snippet on.
    CmdAck { error_flags: u16 },
    /// Anything else: the host's own later traffic, or a peer's frame.
    Other { fc: u16, from: [u8; 6] },
}

/// One queued packet as `md_link_recv_packet` hands it over: a 12-byte TX
/// header (rate at 8, channel at 9, frame length at 10) followed by the 802.11
/// frame, whose last four bytes are the wire's checksum slot.
pub(crate) fn frame(pkt: &[u8]) -> Option<&[u8]> {
    if pkt.len() < 12 + 24 {
        return None;
    }
    let framelen = u16::from_le_bytes([pkt[10], pkt[11]]) as usize;
    if 12 + framelen > pkt.len() {
        return None;
    }
    Some(&pkt[12..12 + framelen])
}

pub(crate) fn fc(pkt: &[u8]) -> Option<u16> {
    let f = frame(pkt)?;
    Some(u16::from_le_bytes([f[0], f[1]]))
}

pub(crate) fn addr(pkt: &[u8], off: usize) -> Option<[u8; 6]> {
    let f = frame(pkt)?;
    if f.len() < off + 6 {
        return None;
    }
    let mut a = [0u8; 6];
    a.copy_from_slice(&f[off..off + 6]);
    Some(a)
}

/// One little-endian 16-bit field of a management frame's body, or zero when
/// the frame is too short to carry it. `off` is relative to the body (the byte
/// after the 24-byte header): an auth body is algorithm(0), sequence(2),
/// status(4); a deauth body is reason(0).
pub(crate) fn body_u16(pkt: &[u8], off: usize) -> u16 {
    match frame(pkt) {
        Some(f) if f.len() >= 24 + off + 2 => u16::from_le_bytes([f[24 + off], f[24 + off + 1]]),
        _ => 0,
    }
}

/// One byte of a frame's body, or zero when the frame is too short to carry it.
pub(crate) fn body_u8(pkt: &[u8], off: usize) -> u8 {
    match frame(pkt) {
        Some(f) if f.len() >= 24 + off + 1 => f[24 + off],
        _ => 0,
    }
}

/// A `04` DataPacket's own fields, per GBATEK's CMD DATA: the packet number at
/// body `09h`, then the image data at `0Bh` up to the end the command's `04h`
/// byte states ("size in halfwords of [06h..end, excluding footer]"). `None`
/// when the frame is too short to carry that header, so a truncated command can
/// never be read as a packet; a packet whose stated size is shorter than its own
/// header yields no data rather than a slice out of the header.
pub(crate) fn data_packet(pkt: &[u8]) -> Option<(u16, Vec<u8>)> {
    let f = frame(pkt)?;
    let body = &f[24..];
    if body.len() < 0x0B {
        return None;
    }
    let packet = u16::from_le_bytes([body[9], body[10]]);
    // The stated size covers [06h..end]: command, two unknown, two packet-number
    // bytes, the data, and finally GBATEK's single "Unused (zero)" padding byte.
    // Dropping that last byte is what makes a full packet come out at the 1F8h
    // the spec's maximum names - measured on a live 510-byte packet's own frame,
    // whose body is 518 bytes: 6 header + 510 stated + 2 footer.
    let end = (6 + body[4] as usize * 2).min(body.len());
    let data_end = end.saturating_sub(1);
    let data = if data_end > 0x0B {
        body[0x0B..data_end].to_vec()
    } else {
        Vec::new()
    };
    Some((packet, data))
}

/// The image's total size out of the host's RSA signature frame, which GBATEK
/// places at body `07h` (command `03h`): the 160h-byte header's size at `14h`,
/// the ARM9 size at `24h` and the ARM7 size at `34h`. A downloader compares the
/// bytes it has collected against this sum to know the transfer is complete -
/// and the packet map says packet 0 is the header, then ARM9, then ARM7. `None`
/// when the frame is too short or the sum is zero, so an unparsed RSA frame can
/// never make the joiner declare a download finished.
pub(crate) fn rsa_total_bytes(pkt: &[u8]) -> Option<u64> {
    let f = frame(pkt)?;
    let body = &f[24..];
    let rd32 = |off: usize| -> Option<u32> {
        let b = 7 + off;
        (body.len() >= b + 4)
            .then(|| u32::from_le_bytes([body[b], body[b + 1], body[b + 2], body[b + 3]]))
    };
    let total = u64::from(rd32(0x14)?) + u64::from(rd32(0x24)?) + u64::from(rd32(0x34)?);
    (total > 0).then_some(total)
}

/// The child image's load map, out of the same RSA signature frame: the
/// ARM9/ARM7 entrypoints at `00h`/`04h`, the sizes at `14h`/`24h`/`34h`, and
/// the **actual** header/ARM9/ARM7 destinations at `10h`/`20h`/`30h`.
///
/// GBATEK gives each destination as a temp/actual pair (`0Ch`/`10h`,
/// `1Ch`/`20h`, `2Ch`/`30h`) that are usually identical - except ARM7, which a
/// real client stages at a temp address in Main RAM (`2Ch`, e.g. `22C0000h`) to
/// SHA1 it from the ARM9 side and then relocates to the entry the frame names
/// (`30h`, e.g. `2380000h`). Booting from the temp address leaves the ARM7
/// entry pointing at unwritten memory, which faults the child at once; so the
/// write uses the actual destination, and only reading `2Ch` (as the first live
/// run did) is the bug this fixes.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct BootInfo {
    pub arm9_entry: u32,
    pub arm7_entry: u32,
    pub header_dest: u32,
    pub header_size: u32,
    pub arm9_dest: u32,
    pub arm9_size: u32,
    pub arm7_dest: u32,
    /// The ARM7's *temp* destination (`2Ch`). A real client stages the ARM7
    /// image here in Main RAM so the ARM9 side can SHA1 it, then relocates it
    /// to `arm7_dest`; the staging copy is written as well, because the ARM9
    /// boot code reads it.
    pub arm7_temp: u32,
    pub arm7_size: u32,
}

/// The load map out of the host's RSA frame, or `None` when the frame is too
/// short or states no sizes - an unparsed frame can never lead to a boot.
pub fn rsa_boot_info(pkt: &[u8]) -> Option<BootInfo> {
    let f = frame(pkt)?;
    let body = &f[24..];
    let rd32 = |off: usize| -> Option<u32> {
        let b = 7 + off;
        (body.len() >= b + 4)
            .then(|| u32::from_le_bytes([body[b], body[b + 1], body[b + 2], body[b + 3]]))
    };
    let info = BootInfo {
        arm9_entry: rd32(0x00)?,
        arm7_entry: rd32(0x04)?,
        header_dest: rd32(0x10)?,
        header_size: rd32(0x14)?,
        arm9_dest: rd32(0x20)?,
        arm9_size: rd32(0x24)?,
        arm7_dest: rd32(0x30)?,
        arm7_temp: rd32(0x2C)?,
        arm7_size: rd32(0x34)?,
    };
    (info.header_size >= 0x160 && info.arm9_size > 0).then_some(info)
}

/// Find the information elements the client needs: the SSID (id 0) and the
/// four-byte game serial inside the proprietary `DD` tag. Everything else - the
/// DS parameter set, the TIM, anything unknown - is skipped rather than
/// rejected, and a malformed tail simply ends the scan.
///
/// The `DD` tag's own layout, identical across captured host beacons (MPH, NSMB
/// and Star Fox): the serial sits at data offset 12. In MPH's beacon the tag is
/// `DD 18 00 09 BF 00 0A 00 DC E4 01 00 00 00 0A 00 40 00 ...` and that host's
/// own clients put `0A 00 40 00` at the head of their SSID; NSMB's is
/// `50 01 40 00`, Star Fox's `18 02 40 00`.
pub(crate) fn parse_ies(ies: &[u8]) -> (Vec<u8>, [u8; 4], [u8; 2], usize) {
    let mut ssid = Vec::new();
    let mut serial = [0u8; 4];
    let mut token = [0u8; 2];
    let mut dd_len = 0usize;
    let mut i = 0usize;
    while i + 2 <= ies.len() {
        let id = ies[i];
        let len = ies[i + 1] as usize;
        let start = i + 2;
        let end = start + len;
        if end > ies.len() {
            break;
        }
        if id == 0 {
            ssid = ies[start..end].to_vec();
        } else if id == 0xDD && len >= 16 {
            serial.copy_from_slice(&ies[start + 12..start + 16]);
            if len >= 18 {
                token.copy_from_slice(&ies[start + 16..start + 18]);
            }
            // Total tag length (id + length + data): GBATEK's handoff
            // "beacon size" is computed off this tag's size, so the first
            // well-formed DD tag's length is kept, not the last.
            if dd_len == 0 {
                dd_len = end - i;
            }
        }
        i = end;
    }
    (ssid, serial, token, dd_len)
}

/// The beacon this packet is, or `None` when it is not a beacon (or is too
/// short to be one).
pub fn parse_beacon(pkt: &[u8]) -> Option<HostBeacon> {
    if fc(pkt)? != 0x0080 {
        return None;
    }
    let f = frame(pkt)?;
    let body = &f[24..];
    // Timestamp(8) + beacon interval(2) + capability(2) + the four raw rate
    // bytes, before any information elements.
    if body.len() < 16 {
        return None;
    }
    let clock = u64::from_le_bytes([
        body[0], body[1], body[2], body[3], body[4], body[5], body[6], body[7],
    ]);
    let interval = u16::from_le_bytes([body[8], body[9]]);
    let capability = u16::from_le_bytes([body[10], body[11]]);
    // Nintendo's layout: four raw rate bytes follow the fixed fields, and the
    // information elements (DS parameter set, TIM, SSID) follow those.
    let rates = body[12..16].to_vec();
    let (ssid, serial, token, dd_len) = parse_ies(&body[16..]);
    Some(HostBeacon {
        mac: addr(pkt, 10)?,
        bssid: addr(pkt, 16)?,
        channel: pkt[9],
        clock,
        interval,
        capability,
        ssid,
        serial,
        token,
        rates,
        dd_len,
    })
}

/// Classify one inbox packet from the joiner's point of view. `us` is this
/// console's own MAC: a response that is not addressed to us is somebody
/// else's and is reported as `Other`.
pub fn classify(pkt: &[u8], us: &[u8; 6]) -> Option<Rx> {
    if let Some(b) = parse_beacon(pkt) {
        return Some(Rx::Beacon(b));
    }
    let f = fc(pkt)?;
    let a1 = addr(pkt, 4)?;
    let from = addr(pkt, 10)?;
    match f {
        0x00B0 if a1 == *us => Some(Rx::AuthResponse {
            seq: body_u16(pkt, 2),
            status: body_u16(pkt, 4),
        }),
        0x0010 if a1 == *us => Some(Rx::AssocResponse {
            status: body_u16(pkt, 2),
            aid: body_u16(pkt, 4),
        }),
        0x00C0 if a1 == *us => Some(Rx::Deauth { reason: body_u16(pkt, 0) }),
        // The host's own frames: a command is addressed to the multicast command
        // MAC and an ack to the multicast ack MAC, so neither matches `us` and
        // both are classified on the frame control alone. The caller checks the
        // sender is the host it follows.
        // ... and only when the frame actually carries the CMD DATA header. A
        // truncated `fc=0228` frame (the host writes a 6-byte body when it has
        // no client to address) would otherwise have its command byte read out
        // of the wire's checksum slot - the first live run of this path logged
        // command bytes like `37`, `FD` and `BA` that no protocol defines, and
        // answered them with dummies.
        0x0228 if frame(pkt).map_or(false, |f| f.len() >= 24 + 4 + 7) => {
            let command = body_u8(pkt, 6);
            let (packet, data) = match (command, data_packet(pkt)) {
                (0x04, Some((packet, data))) => (Some(packet), data),
                _ => (None, Vec::new()),
            };
            let rsa_total = (command == 0x03).then(|| rsa_total_bytes(pkt)).flatten();
            let rsa_boot = (command == 0x03).then(|| rsa_boot_info(pkt)).flatten();
            Some(Rx::Command {
                command,
                size: body_u8(pkt, 4),
                flags: body_u8(pkt, 5),
                slave_flags: body_u16(pkt, 2),
                packet,
                data,
                rsa_total,
                rsa_boot,
            })
        }
        0x0218 => Some(Rx::CmdAck { error_flags: body_u16(pkt, 2) }),
        _ => Some(Rx::Other { fc: f, from }),
    }
}

/// Wrap an 802.11 frame in the 12-byte transmit header the bridge's send path
/// expects (the same layout a game's own TX buffer has), **including the
/// hardware's four-byte FCS slot as part of the frame's own length**.
///
/// That slot is not decoration. GBATEK, `W_TXBUF_LOCn[0Ah]`: "TX length includes
/// the length of a 4-byte 'FCS' ... the hardware generates the FCS for you, but
/// you still must include it in the packet length", and
/// `LocalMP::ApplyWireFCS` writes the generated CRC into the frame's last four
/// bytes at send time. Building the packet *without* the slot makes the bus
/// write the CRC over the last four bytes of the body instead - and the first
/// live run proved what that costs: a real client's authentication request is
/// `len=46` (`24 header + 6 body + 4 FCS`), ours was `len=42`, so the host read
/// its sequence number out of the checksum and refused with status `000E`
/// ("authentication transaction sequence number out of expected sequence"),
/// then every association request was a body full of CRC bytes. The reference is
/// a real client's own frame in `logs/mph4b_app_4con.log.gz`:
/// `p2 tx len=46 fc=00B0 ... sync=5A5A000000010000` (algorithm 0, sequence 1,
/// status 0), answered `p1 tx len=46 ... sync=5A5A000000020000` (status 0).
pub(crate) fn wrap(channel: u8, frame: &[u8]) -> Vec<u8> {
    const FCS: usize = 4;
    let mut pkt = vec![0u8; 12 + frame.len() + FCS];
    pkt[8] = 0x14; // the 2Mbps rate the firmware uses for management frames
    pkt[9] = channel;
    pkt[10..12].copy_from_slice(&((frame.len() + FCS) as u16).to_le_bytes());
    pkt[12..12 + frame.len()].copy_from_slice(frame);
    pkt
}

/// A management frame's 24-byte 802.11 header. The sequence-control word is
/// the one the emulated hardware would have assigned (see `next_seq`): a real
/// client's own authentication traces as `seq=0000` and its association as
/// `seq=0010`, because every transmit takes a fresh number.
pub(crate) fn mgmt_header(fc: u16, dst: &[u8; 6], src: &[u8; 6], bssid: &[u8; 6], seq: u16) -> Vec<u8> {
    let mut f = Vec::with_capacity(24 + 48);
    f.extend_from_slice(&fc.to_le_bytes());
    f.extend_from_slice(&0u16.to_le_bytes()); // duration
    f.extend_from_slice(dst);
    f.extend_from_slice(src);
    f.extend_from_slice(bssid);
    f.extend_from_slice(&seq.to_le_bytes());
    f
}

/// The nickname a client reports when nothing more specific is known: the
/// synthesized stand-in's own default. Each console's stand-in reports the
/// player it is (`player_name`), for the same reason the firmware nickname is
/// per-console (see `apply_console_mac`) - a room full of identical names is
/// not a room this fork can tell apart when a game lists its players.
pub(crate) const CLIENT_NAME: &str = "Player 1";

/// The name console `slot` advertises: `Player 1`..`Player 4`, which is what
/// the firmware nickname is set to as well, so the two halves of a console's
/// identity agree (the Download Play stand-in and the booted console it becomes
/// must not rename themselves across the hand-over).
pub(crate) fn player_name(slot: Option<u32>) -> String {
    match slot {
        Some(s) if s < 4 => format!("Player {}", s + 1),
        _ => CLIENT_NAME.to_string(),
    }
}

/// The 32-byte SSID element a DS client puts in its association request.
///
/// Captured from real clients' own frames (a 70-byte association request is
/// `24 header + 42 body + 4 FCS`):
///
/// ```text
/// 21 00                     capability
/// 01 00                     listen interval
/// 00 20                     SSID IE, 32 bytes
///   <4-byte game serial>    echoed from the beacon's DD tag (0040xxxx)
///   <4 bytes>               per-console
///   <UTF-16LE name, padded> 12 code units, zero-filled (24 bytes)
/// 01 02 82 84               the four rate bytes, raw (no IE header)
/// ```
///
/// Four separate joins in `logs/*_app.log.gz` (NSMB, MPH) all carry this shape;
/// the four bytes after the serial vary per console, but the serial itself is
/// the host's own and every captured client echoes it: MPH's clients send
/// `0A 00 40 00`, NSMB's `50 01 40 00` - the same word their respective hosts
/// advertise in the beacon's `DD` tag. A client that sends a *different* serial
/// (or zeros, as the first version of this joiner did) is telling the host it
/// is asking for another title.
pub(crate) fn client_ssid(serial: [u8; 4], token: [u8; 2], name: &str) -> Vec<u8> {
    let mut ssid = Vec::with_capacity(32);
    ssid.extend_from_slice(&serial);
    ssid.extend_from_slice(&token);
    ssid.extend_from_slice(&[0x00, 0x00]);
    for ch in name.encode_utf16().take(12) {
        ssid.extend_from_slice(&ch.to_le_bytes());
    }
    ssid.resize(32, 0);
    ssid
}

pub(crate) fn push_ie(f: &mut Vec<u8>, id: u8, data: &[u8]) {
    f.push(id);
    f.push(data.len() as u8);
    f.extend_from_slice(data);
}

/// An open-system authentication request (fc=0x00B0, algorithm 0, transaction
/// sequence 1, status 0) - byte for byte the frame a real DS client sends and a
/// host answers with status 0.
pub fn build_auth_request(joiner: &[u8; 6], host: &HostBeacon, seq: u16) -> Vec<u8> {
    let mut f = mgmt_header(0x00B0, &host.mac, joiner, &host.bssid, seq);
    f.extend_from_slice(&0u16.to_le_bytes()); // auth algorithm: open system
    f.extend_from_slice(&1u16.to_le_bytes()); // auth transaction sequence
    f.extend_from_slice(&0u16.to_le_bytes()); // status: success
    wrap(host.channel, &f)
}

/// An association request (fc=0x0000), body-for-body the frame a captured DS
/// client sends: the capability, a listen interval, the client's own 32-byte
/// SSID element and then the four supported-rate bytes *raw*, in Nintendo's own
/// layout (not a rates information element). That is `2 + 2 + 34 + 4 = 42` bytes
/// of body, so the frame is 70 on the air (`24 + 42 + 4`), the exact length the
/// captured association request traces (`body[0..41]`, `fc=0000 len=70` in
/// `logs/ab_ctl_app.log.gz`).
pub fn build_assoc_request(
    joiner: &[u8; 6],
    host: &HostBeacon,
    seq: u16,
    name: &str,
) -> Vec<u8> {
    let mut f = mgmt_header(0x0000, &host.mac, joiner, &host.bssid, seq);
    f.extend_from_slice(&host.capability.to_le_bytes());
    f.extend_from_slice(&0x0001u16.to_le_bytes()); // listen interval (see a real client's frame)
    push_ie(&mut f, 0x00, &client_ssid(host.serial, host.token, name));
    const DS_RATES: [u8; 4] = [0x01, 0x02, 0x82, 0x84];
    let rates: &[u8] = if host.rates.len() == 4 {
        &host.rates
    } else {
        &DS_RATES
    };
    f.extend_from_slice(rates);
    wrap(host.channel, &f)
}

/// The address a DS client's *replies* are third-addressed with. GBATEK,
/// "REPLY, Client to Host Replies (with DA = 03:09:BF:00:00:10)": a captured
/// client's own reply frame carries the host's MAC as receiver and this
/// multicast address as BSSID (`p2 tx reply len=42 fc=0118 rx=0009BF112233
/// tx=0009BF126643 bssid=0309BF000010` in `logs/mph4b_app_4con.log.gz`).
pub(crate) const REPLY_MAC: [u8; 6] = [0x03, 0x09, 0xBF, 0x00, 0x00, 0x10];

/// Build the *first* frame a real DS client sends after its association is
/// accepted: a short dummy reply (`fc=0118`, body `00 00`, a 30-byte frame).
///
/// This is reconstructed from a real client, not invented. GBATEK's Download
/// Play reply list names it - "Normal Dummy Reply ... (can occur before username
/// transfer)" - and a captured multi-card client sends exactly this as its first
/// transmission after associating (`logs/mph4b_app_4con.log.gz`): the client's
/// own `tx frame: slot 5 (REPLY) fc=0118 len=30 seq=0020`, its body
/// `00 00 5A 5A B8 B6` whose last four bytes are the wire FCS slot (the host
/// reads the same frame cropped as `26 byte frame, body[2]=00 00`), and the
/// host collecting it as `a reply from p2 for aid 1`. So the body is two bytes
/// (reply type 0 = dummy), and the frame is `24 header + 2 body + 4 FCS`.
pub fn build_dummy_reply(joiner: &[u8; 6], host: &HostBeacon, seq: u16) -> Vec<u8> {
    let mut f = mgmt_header(0x0118, &host.mac, joiner, &REPLY_MAC, seq);
    f.extend_from_slice(&[0x00, 0x00]); // size 0 (short), flags 0 - a dummy reply
    wrap(host.channel, &f)
}

/// The DS nickname this client reports, UTF-16LE: the player's own name (see
/// `player_name`), so a room lists four different players rather than four
/// copies of the firmware default.
pub(crate) fn username_snippet(serial: [u8; 4], snippet: u8, name: &str) -> [u8; 6] {
    let name: Vec<u16> = name.encode_utf16().take(10).collect();
    let mut out = [0u8; 6];
    match snippet {
        0 => {
            out[0..4].copy_from_slice(&serial);
            out[4] = 0x00; // favourite colour
            out[5] = name.len() as u8; // name length (0..10)
        }
        1..=3 => {
            let base = 3 * (snippet as usize - 1);
            for i in 0..3 {
                let c = name.get(base + i).copied().unwrap_or(0);
                out[i * 2] = c as u8;
                out[i * 2 + 1] = (c >> 8) as u8;
            }
        }
        _ => {
            let c = name.get(9).copied().unwrap_or(0);
            out[0] = c as u8;
            out[1] = (c >> 8) as u8;
            out[2] = 0x01; // 0001h
            out[4] = 0x00; // C500h, little-endian
            out[5] = 0xC5;
        }
    }
    out
}

/// A UsernameReply (`fc=0118`) answering the host's NameRequest command.
///
/// The body is GBATEK's REPLY layout: `size in halfwords` (`04` = normal),
/// `flags`, `reply type` (`07` = Username), `snippet number`, then the six-byte
/// snippet. That makes the frame `24 header + 10 body + 4 FCS` = 38 bytes on the
/// air - the length a captured real client's non-dummy reply carries
/// (`logs/mph4b_app_4con.log.gz`, `collected a reply ... 38 byte frame,
/// fc=0118`, whose own body is 10 bytes once the wire FCS is removed).
pub fn build_username_reply(
    joiner: &[u8; 6],
    host: &HostBeacon,
    seq: u16,
    snippet: u8,
    name: &str,
) -> Vec<u8> {
    let mut f = mgmt_header(0x0118, &host.mac, joiner, &REPLY_MAC, seq);
    f.extend_from_slice(&[0x04, 0x01, 0x07, snippet]); // normal, flags, Username, snippet #
    f.extend_from_slice(&username_snippet(host.serial, snippet, name));
    wrap(host.channel, &f)
}

/// An RsaReply (`fc=0118`) answering the host's RSA command.
///
/// GBATEK's REPLY list is the whole specification here: a *normal* reply (size
/// 4, flags 1) whose third body byte is the reply type `08h`, followed by seven
/// filler bytes ("Unused (garbage, usually same as Username Snippet #2)"). The
/// host's RSA frame carries the signature; the client contributes no key
/// material, only the reply type the host's own state machine waits for before
/// it will start sending data. That is why the host re-sends its `03` command
/// forever against a dummy reply: the dummy's type is `00`, not `08`.
pub fn build_rsa_reply(joiner: &[u8; 6], host: &HostBeacon, seq: u16) -> Vec<u8> {
    let mut f = mgmt_header(0x0118, &host.mac, joiner, &REPLY_MAC, seq);
    f.extend_from_slice(&[0x04, 0x01, 0x08]); // normal, flags, RsaReply
    let filler = username_snippet(host.serial, 2, CLIENT_NAME);
    f.extend_from_slice(&filler); // six of the seven filler bytes
    f.push(0x00); // ... and the seventh
    wrap(host.channel, &f)
}

/// A DataReply (`fc=0118`, type `09`), the frame that advances the download.
///
/// GBATEK's client reply layout is size in halfwords at `00h` (`04h` = normal),
/// flags at `01h`, reply type at `02h`, then for a Data Reply **the smallest
/// missing packet number at `03h`** and the number of distinct packets received
/// at `05h`. Those two fields are the only input the host's per-client machine
/// needs: measured, a reply whose `03h` halfword carried username-snippet bytes
/// left the host re-sending one 510-byte packet for the whole run, and a reply
/// carrying the pointer actually held moves the transfer on.
pub fn build_data_reply(
    joiner: &[u8; 6],
    host: &HostBeacon,
    seq: u16,
    next: u16,
    count: u16,
) -> Vec<u8> {
    let mut f = mgmt_header(0x0118, &host.mac, joiner, &REPLY_MAC, seq);
    f.extend_from_slice(&[0x04, 0x01, 0x09]); // normal, flags, DataReply
    f.extend_from_slice(&next.to_le_bytes());
    f.extend_from_slice(&count.to_le_bytes());
    f.extend_from_slice(&[0x00, 0x00, 0x00]); // 07h..09h unused (zerofilled)
    wrap(host.channel, &f)
}

/// A GotAllReply (`fc=0118`, type `0Ah`): no further packets are needed. GBATEK
/// puts it in the same ten-byte body as a DataReply, whose `03h..09h` bytes it
/// calls garbage - "old values from last Data Reply" - so the caller hands the
/// same two fields straight through rather than zeroing them.
pub fn build_got_all_reply(
    joiner: &[u8; 6],
    host: &HostBeacon,
    seq: u16,
    next: u16,
    count: u16,
) -> Vec<u8> {
    let mut f = mgmt_header(0x0118, &host.mac, joiner, &REPLY_MAC, seq);
    f.extend_from_slice(&[0x04, 0x01, 0x0A]); // normal, flags, GotAllReply
    f.extend_from_slice(&next.to_le_bytes());
    f.extend_from_slice(&count.to_le_bytes());
    f.extend_from_slice(&[0x00, 0x00, 0x00]); // old DataReply tail
    wrap(host.channel, &f)
}

/// A FinalReply (`fc=0118`, type `0Bh`): GBATEK's confirmation of the host's
/// `05` Final command, in the same ten-byte body with the same reused tail.
pub fn build_final_reply(
    joiner: &[u8; 6],
    host: &HostBeacon,
    seq: u16,
    next: u16,
    count: u16,
) -> Vec<u8> {
    let mut f = mgmt_header(0x0118, &host.mac, joiner, &REPLY_MAC, seq);
    f.extend_from_slice(&[0x04, 0x01, 0x0B]); // normal, flags, FinalReply
    f.extend_from_slice(&next.to_le_bytes());
    f.extend_from_slice(&count.to_le_bytes());
    f.extend_from_slice(&[0x00, 0x00, 0x00]); // old DataReply tail
    wrap(host.channel, &f)
}

/// A *normal* reply (`fc=0118`, size 4, flags 1) carrying an arbitrary third
/// body byte as its reply type, then the same seven filler bytes `RsaReply`
/// uses. This exists for the `MD_DP_REPLY_FORCE` diagnostic below, so one build
/// can be swept across reply types instead of one rebuild per type.
pub(crate) fn build_typed_reply(joiner: &[u8; 6], host: &HostBeacon, seq: u16, reply_type: u8) -> Vec<u8> {
    let mut f = mgmt_header(0x0118, &host.mac, joiner, &REPLY_MAC, seq);
    f.extend_from_slice(&[0x04, 0x01, reply_type]); // normal, flags, type
    let filler = username_snippet(host.serial, 2, CLIENT_NAME);
    f.extend_from_slice(&filler); // six of the seven filler bytes
    f.push(0x00); // ... and the seventh
    wrap(host.channel, &f)
}
