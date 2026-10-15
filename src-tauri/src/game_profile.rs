//! Per-game profiles: the one place that knows what a cartridge's game code is
//! and what (if anything) this app does differently for that title.
//!
//! The seam the mission asks for lives here. A session reads the loaded ROM's
//! 4-byte game code (`md_rom_game_code`), resolves it through [`resolve`], and
//! carries the result on `SessionInfo`. When a title-specific patch is written
//! it attaches to the matching [`GameFixSpec`] and is gated by the one
//! `enabled` switch, so no `MD_*` environment variable is involved and no
//! behaviour is keyed off a file path.
//!
//! Deliberately small: this module owns *identity and policy only*. It performs
//! no emulator work, so a profile that says `implemented: false` changes
//! nothing - the default path for a title with no fix, and for every ROM whose
//! code the registry does not know, is exactly what it was before.

/// One title this app knows something about, keyed by its cartridge game code
/// (the four ASCII bytes at header offset 0x0C).
struct GameFixSpec
{
    /// The header game code, verbatim ("AMCE", "A2DE", ...).
    code: &'static str,
    /// Human-readable title, for the Settings panel and the log.
    name: &'static str,
    /// What a per-game fix for this title would change, or the reason none is
    /// needed. Written from the project's own measured record; never a guess.
    fix: &'static str,
    /// True once the `fix` is actual code that runs. False means this is a
    /// recognised attach point and nothing more, so the link path is untouched.
    implemented: bool,
    /// True when this title's multiplayer is *single-card Download Play* - one
    /// cartridge serving several consoles that run a downloaded image. That is
    /// a different problem from multi-card linking: the downloading consoles do
    /// not run the game yet, so there is no game code on them to be the client.
    /// The app supplies the joiner half instead (see `src-tauri/src/dp_joiner`),
    /// and this flag is the identity half of the gate that keeps it off every
    /// other title.
    download_play: bool,
    /// Where the claim in `fix` comes from (a doc section or a log), so a reader
    /// can check it rather than trust it.
    source: &'static str,
}

/// The registry. Adding a title here is how a game-specific patch gets a home;
/// it is the only list that maps identity to per-game behaviour.
const GAME_FIXES: &[GameFixSpec] = &[
    GameFixSpec {
        code: "AMCE",
        name: "Mario Kart DS",
        fix: "Pre-race park: both consoles couple, CUT OFF moves both to the game's \
              own wait, and the group then dies without reaching character select - \
              the radios power off with no deauth. One attach point was tried live \
              (never narrow the host's client mask on a late reply) and measured as \
              not moving the run, so its code was removed rather than left gated \
              off. The remaining obligation is inside MKDS's own ARM7/ARM9 code: \
              the ARM7 wifi library posts nothing the host's blocking dequeue waits \
              for (see the MKDS handoff in compatibility.md).",
        implemented: false,
        download_play: false,
        source: "compatibility.md '0168 - Mario Kart DS'",
    },
    GameFixSpec {
        code: "A2DE",
        name: "New Super Mario Bros.",
        fix: "The working control: two consoles reach Mario Vs. Luigi and hold the \
              link for 234 emulated seconds. No per-game fix is needed on record.",
        implemented: false,
        download_play: false,
        source: "compatibility.md 'New Super Mario Bros.'",
    },
    GameFixSpec {
        code: "AMHE",
        name: "Metroid Prime - Hunters",
        fix: "A two-console match runs once the lobby route is driven; three and \
              four consoles couple but never start. No per-game fix implemented yet.",
        implemented: false,
        download_play: false,
        source: "compatibility.md 'Metroid Prime - Hunters'",
    },
    GameFixSpec {
        code: "ASME",
        name: "Super Mario 64 DS",
        fix: "Single-card Download Play only - GBATEK lists it among the titles \
              whose multiplayer is Download Play, and compatibility.md records no \
              multi-card mode in the cartridge, so nothing else in the app can be \
              disturbed by arming the shared joiner here. The joiner is the same \
              one Star Fox uses (src-tauri/src/dp_joiner.rs): it answers the \
              host's beacon, name phase, RSA frame and data stream. What this \
              title still needs is its own measured route into the host screen, \
              which no pass has driven yet - so it is a recognized attach point \
              and an opt-in, not a measured working download.",
        implemented: false,
        download_play: true,
        source: "GBATEK 'List of DS Download Play Games' + compatibility.md",
    },
    GameFixSpec {
        code: "ATRE",
        name: "Tetris DS",
        fix: "Single-card Download Play for up to 10 players alongside its \
              multi-card mode; the single-card host is the joiner's next target \
              after Star Fox and SM64DS. Same shared joiner \
              (src-tauri/src/dp_joiner.rs), armed by this entry: it answers the \
              host's beacon, name phase, RSA frame and data stream. No route \
              into the host screen has been driven yet - recognized attach \
              point and opt-in, not a measured working download.",
        implemented: false,
        download_play: true,
        source: "GBATEK 'List of DS Download Play Games' + compatibility.md",
    },
    GameFixSpec {
        code: "ABME",
        name: "Bomberman",
        fix: "Single-card Download Play up to 8 players alongside multi-card; \
              the Battle menu leads straight to a Download host lobby. Same \
              shared joiner (src-tauri/src/dp_joiner.rs), armed by this entry. \
              Host lobby reached live ('Starting a Battle Game! Waiting for \
              players to enter!'); the transfer itself is unmeasured - \
              recognized attach point and opt-in, not a working download.",
        implemented: false,
        download_play: true,
        source: "compatibility.md 'Bomberman' + logs/bomb_app.log",
    },
    GameFixSpec {
        code: "ASFE",
        name: "Star Fox Command",
        fix: "Single-card Download Play only: the game's Recruitment screen is the \
              host half and it beacons (fc=0080) with the room's SSID, but no \
              console can be the client because Download Play's client is the DS \
              firmware's own applet, which this checkout has no image of (see \
              to-do.md Milestone 4). The app therefore synthesizes the joiner: \
              when this profile is enabled, a background thread listens for the \
              host's beacon on one console and answers it with an open-system \
              authentication and an association request built from that beacon's \
              own IEs (src-tauri/src/dp_joiner.rs). Nothing else about the link \
              changes, and no other title runs that thread.",
        implemented: false,
        download_play: true,
        source: "compatibility.md 'Star Fox Command' + logs/sfc_dp_wire.log",
    },
];

/// A title resolved for the running session: its registry entry, plus the
/// session's per-game-fix switch folded in. A value, not a decision - whoever
/// needs to know "is there a per-game behaviour for this session, and is it on"
/// reads it here instead of re-deriving it.
#[derive(Clone, serde::Serialize)]
pub struct GameProfile
{
    pub code: String,
    pub name: &'static str,
    pub fix: &'static str,
    pub implemented: bool,
    /// This title's multiplayer is single-card Download Play (see GameFixSpec).
    /// The app's synthesized joiner attaches to exactly this, and to nothing
    /// else, so a title without it is untouched.
    pub download_play: bool,
    /// The session's per-game-fix switch. False suppresses any attached fix,
    /// leaving the default path even when one exists.
    pub enabled: bool,
    pub source: &'static str,
}

impl GameProfile
{
    /// Whether the synthesized single-card Download Play joiner belongs on this
    /// session: the title is a Download Play title and the Settings switch that
    /// gates every per-game behavior is on. One predicate, so the thread that
    /// runs it and the process that re-resolves it on a toggle cannot disagree.
    pub fn download_play_joiner(&self) -> bool
    {
        self.download_play && self.enabled
    }
}

/// Resolve a cartridge game code to its profile. `enabled` is the session's
/// per-game-fix switch. Returns `None` when the code is not in the registry or
/// is empty - the "no entry applies" case, where the link path stays exactly as
/// it was.
pub fn resolve(code: &str, enabled: bool) -> Option<GameProfile>
{
    if code.len() != 4 {
        return None;
    }
    let spec = GAME_FIXES.iter().find(|s| s.code == code)?;
    Some(GameProfile {
        code: code.to_string(),
        name: spec.name,
        fix: spec.fix,
        implemented: spec.implemented,
        download_play: spec.download_play,
        enabled,
        source: spec.source,
    })
}

/// One-line description for the log and the control channel: what the loaded
/// title is, and whether anything per-game is attached to it right now.
pub fn describe(profile: Option<&GameProfile>) -> String
{
    match profile {
        None => "no per-game profile (default path)".to_string(),
        Some(p) if !p.enabled => {
            format!("{} ({}) - per-game fixes off", p.name, p.code)
        }
        Some(p) if p.download_play => {
            format!(
                "{} ({}) - single-card Download Play; synthesized joiner on",
                p.name, p.code
            )
        }
        Some(p) if !p.implemented => {
            format!("{} ({}) - recognised, no fix implemented yet", p.name, p.code)
        }
        Some(p) => format!("{} ({}) - per-game fix applied", p.name, p.code),
    }
}

#[cfg(test)]
mod tests
{
    use super::*;

    #[test]
    fn known_codes_resolve_and_unknown_is_none()
    {
        for code in ["AMCE", "A2DE", "AMHE", "ASFE"] {
            assert!(resolve(code, true).is_some(), "{code} should resolve");
        }
        assert!(resolve("MDSP", true).is_none()); // the diag ROM
        assert!(resolve("", true).is_none());
        assert!(resolve("AMC", true).is_none()); // wrong width
    }

    #[test]
    fn enabled_flag_rides_along_and_describe_says_so()
    {
        let on = resolve("AMCE", true).unwrap();
        assert!(on.enabled);
        let off = resolve("AMCE", false).unwrap();
        assert!(!off.enabled);
        assert!(describe(Some(&off)).contains("off"));
        assert!(describe(None).contains("default path"));
    }

    #[test]
    fn only_the_download_play_title_runs_the_joiner_and_only_with_fixes_on()
    {
        // The gate the whole behavior hangs from: only titles that are *single-card
        // Download Play* say download_play, and the single Settings switch turns
        // the joiner off for each of them. The multi-card titles (MKDS, NSMB, MPH)
        // are deliberately excluded: they also offer Download Play, but their own
        // beacons are indistinguishable from a Download Play host's to this
        // joiner, so arming it for them would inject authentication frames into a
        // multiplayer session that already works.
        for code in ["AMCE", "A2DE", "AMHE", "AL7E"] {
            let p = resolve(code, true);
            if let Some(p) = p {
                assert!(!p.download_play, "{code} must not run the Download Play joiner");
                assert!(!p.download_play_joiner(), "{code}");
            }
        }
        for code in ["ASFE", "ASME", "ATRE", "ABME"] {
            let on = resolve(code, true).unwrap();
            assert!(on.download_play, "{code} is a single-card Download Play title");
            assert!(on.download_play_joiner(), "{code}");
            let off = resolve(code, false).unwrap();
            assert!(!off.download_play_joiner(), "{code}: switch off must gate the joiner");
        }
        let sfc_off = resolve("ASFE", false).unwrap();
        let sfc_on = resolve("ASFE", true).unwrap();
        assert!(describe(Some(&sfc_off)).contains("off"));
        assert!(describe(Some(&sfc_on)).contains("joiner on"));
    }

    #[test]
    fn no_entry_claims_a_fix_is_implemented()
    {
        // The registry is an attach point, not a behaviour: every entry is
        // honest about having nothing implemented, so no title's link path is
        // touched and the default is what every ROM gets.
        for code in ["AMCE", "A2DE", "AMHE"] {
            assert!(!resolve(code, true).unwrap().implemented, "{code}");
        }
    }
}
