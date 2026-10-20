// The link watchdog's policy: when a stalled console is resynced, and when the
// stall is really a join (or a departure) that a kick would break.
//
// Kept apart from the monitor that calls it because this is the decision that
// cost joiners their sessions, and it is decidable from five numbers. The
// monitor's own state - the per-console shape carried window to window - stays
// in `start_session_inner` where the windows are.

/// Consecutive stalled windows after which the health watchdog resyncs a
/// console's bus state (its FIFO read offset and receive semaphores).
///
/// The cure is real - the historic freeze it was built for came after ~10s of
/// continuous STALL - but a console that is *waiting for a peer to finish
/// associating* looks identical window for window, and kicking it then discards
/// the very replies it goes on to miss: the 2026-09-25 MKDS join was kicked
/// 1.45s after the peer's radio came up, the host's next six acks said
/// `clientfail=0002`, and the client deauthenticated. **Metroid Prime - Hunters
/// four-console multi-card reproduces it exactly** (2026-09-29): kick #1 fires
/// 265ms after the host forms its group (`+161.244s`, frame 8655, right on the
/// host's `wireless ON (begin #3)`), and both guests that were mid-join
/// deauthenticate (reason `0003`) 7.8s later at frame 9068.
///
/// Override with `MD_KICK_AFTER_WINDOWS` to sweep without a rebuild; `0`
/// disables the kick entirely, which is what a route that has to bring three
/// guests in at once needs while the policy is being decided.
pub(crate) fn kick_after_windows() -> u32 {
    static KICK_AFTER_WINDOWS: std::sync::OnceLock<u32> = std::sync::OnceLock::new();
    *KICK_AFTER_WINDOWS.get_or_init(|| {
        std::env::var("MD_KICK_AFTER_WINDOWS")
            .ok()
            .and_then(|v| v.trim().parse::<u32>().ok())
            .unwrap_or(2)
    })
}

/// Should the watchdog resync this console's bus state in this window?
///
/// The kick exists for one failure: a console polling hard while accepting
/// nothing, with its peers sitting on the bus, which a resync cures. Two ways
/// of mistaking something else for that were measured, and both are excluded
/// here rather than left to a route's environment variable:
///
/// * **A join in progress looks identical, window for window.** A console
///   waiting for a peer to associate polls hard and accepts nothing, and the
///   kick then discards the very replies it was about to deliver: the MPH
///   four-console record has kick #1 firing 265 ms after the host's radio came
///   on, both mid-join guests deauthenticating 7.8 s later. A window in which
///   the group's shape changed - a radio began or ended, the connected mask
///   moved - is therefore not evidence of a wedge, and does not count.
/// * **One kick, not three.** The old range test fired the same kick on three
///   consecutive windows; a four-console MKDS room (2026-10-20) took three
///   kicks per console five seconds apart and none of them cured it.
///
/// `after == 0` still disables the kick entirely (`MD_KICK_AFTER_WINDOWS=0`).
pub(crate) fn kick_allowed(
    after: u32,
    stalled_windows: u32,
    connected: u32,
    shape_changed: bool,
    already_kicked: bool,
) -> bool {
    after > 0
        && connected.count_ones() > 1
        && !shape_changed
        && !already_kicked
        && stalled_windows >= after
}


#[cfg(test)]
mod tests {
    use super::*;

    /// The kick's policy, which is the part that cost joiners their session:
    /// a window in which the group's shape moved is a join in progress, not a
    /// wedge, and one stall gets one kick.
    #[test]
    fn a_join_in_progress_is_not_a_stall_to_kick()
    {
        // Two peers on the bus, stalled well past the threshold, never kicked -
        // and a radio came on (or went off) this window. This is the MPH
        // four-console window that lost both joining guests.
        assert!(!kick_allowed(2, 9, 0b11, true, false));
        // The same stall with a stable shape is the failure the kick is for.
        assert!(kick_allowed(2, 2, 0b11, false, false));
    }

    #[test]
    fn one_kick_per_stall_and_none_alone_or_disabled()
    {
        assert!(!kick_allowed(2, 5, 0b11, false, true), "a second kick in one stall");
        assert!(!kick_allowed(2, 5, 0b01, false, false), "no peer to resync against");
        assert!(!kick_allowed(0, 50, 0b11, false, false), "MD_KICK_AFTER_WINDOWS=0 means never");
        assert!(!kick_allowed(2, 1, 0b11, false, false), "short of the threshold");
    }
}
