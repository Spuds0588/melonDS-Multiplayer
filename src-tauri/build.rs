// melonDS Multiplayer - links the Tauri host against the native bridge.
//
// The bridge (src/frontend/tauri) is built by CMake into build-tauri/ before
// this crate is compiled. These are LTO-built static libs, so the final link
// must run through the same GCC driver CMake used; see history.md v0.1.6.

use std::path::Path;

/// The C++ archives a CMake build produces, in link order: bridge, then core,
/// then teakra. Named per platform because a CMake build is not the same shape
/// everywhere - `.a` on ELF/Mach-O, `.lib` on MSVC, and the MSVC generator puts
/// them under a per-configuration subdirectory (`src/Release/core.lib`) because
/// it is multi-config.
fn archive(build_dir: &Path, rel: &str, stem: &str, msvc: bool) -> Option<std::path::PathBuf> {
    let file = if msvc { format!("{stem}.lib") } else { format!("lib{stem}.a") };
    // MSVC's Release subdirectory first, then the flat layout single-config
    // generators use. Whichever exists is the one CMake wrote.
    for sub in [Some("Release"), Some("RelWithDebInfo"), None] {
        let dir = match sub {
            Some(s) => build_dir.join(rel).join(s),
            None => build_dir.join(rel),
        };
        let path = dir.join(&file);
        if path.exists() {
            return Some(path);
        }
    }
    None
}

fn main() {
    // Paths are absolute so builds work from any working directory. CMake
    // writes CMAKE_CACHEFILE_DIR into the cache; we read the cache file itself
    // rather than guessing, so a renamed build dir stays supported.
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
    let project_root = manifest
        .parent()
        .expect("src-tauri always sits at the project root");

    let build_dir = project_root.join("build-tauri");
    let cache = build_dir.join("CMakeCache.txt");
    if !cache.exists() {
        panic!(
            "build-tauri/CMakeCache.txt not found - build the C++ core first:\n\
             \x20 cmake -B build-tauri -DCMAKE_BUILD_TYPE=Release && cmake --build build-tauri"
        );
    }

    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let msvc = target_os == "windows";

    // Pull CMAKE_BUILD_TYPE out of the cache: the debug core has assertion
    // hooks the release core does not, and mixing them is a subtle way to lose
    // a weekend. MSVC's generator is multi-config, so CMAKE_BUILD_TYPE is
    // legitimately empty there and `--config Release` picks the archive dir.
    let cmake_build_type = std::fs::read_to_string(&cache)
        .ok()
        .and_then(|text| {
            text.lines()
                .find(|l| l.starts_with("CMAKE_BUILD_TYPE:STRING="))
                .map(|l| l["CMAKE_BUILD_TYPE:STRING=".len()..].trim().to_string())
        })
        .unwrap_or_default();
    assert!(
        cmake_build_type == "Release" || cmake_build_type.is_empty(),
        "build-tauri was configured in '{cmake_build_type}', not Release. \
         The Tauri host needs the optimised core: \
         cmake -B build-tauri -DCMAKE_BUILD_TYPE=Release && cmake --build build-tauri"
    );

    let libs = [
        ("src/frontend/tauri", "melonds_bridge"),
        ("src", "core"),
        ("src/teakra/src", "teakra"),
    ];
    let mut paths = Vec::new();
    for (rel, stem) in libs {
        let path = archive(&build_dir, rel, stem, msvc).unwrap_or_else(|| {
            panic!(
                "{stem} archive missing under build-tauri/{rel} - build the C++ core first \
                 (cmake --build build-tauri --config Release)"
            )
        });
        if let Some(dir) = path.parent() {
            println!("cargo:rustc-link-search=native={}", dir.display());
        }
        paths.push(path);
    }

    // Order matters to a static linker: bridge, then core, then teakra.
    println!("cargo:rustc-link-lib=static=melonds_bridge");
    println!("cargo:rustc-link-lib=static=core");
    println!("cargo:rustc-link-lib=static=teakra");

    // cargo 1.95 quirk, verified by comparing the two rustc invocations: the
    // package's *library* target receives the `-l static=...` flags above, but
    // the *binary's* final link only carries the -L search dirs and the dylibs
    // re-emitted from rlib metadata - the static archives never make it onto
    // the line, leaving every md_* symbol undefined. Forcing them through
    // rustc-link-arg-bins puts them verbatim on the bin's link command, after
    // the rlibs that reference them, which is the correct extraction order.
    for path in &paths {
        println!("cargo:rustc-link-arg-bins={}", path.display());
    }

    // Everything below is per-platform, and the rest of this file is the story
    // of why each line is there:
    //
    //   linux  - GNU ld with GCC LTO, libstdc++, dl/rt/m as separate DSOs.
    //   macos  - the same C++ symbols, but libc++ and no separate libdl/librt
    //            (libSystem provides them); no -fuse-ld or -rdynamic, which are
    //            GNU-ld flags Apple's linker does not take.
    //   windows- MSVC: the archives carry their own /DEFAULTLIB directives, so
    //            the C++ runtime and the system libs come in from the .libs
    //            themselves and nothing extra is needed here.
    match target_os.as_str() {
        "linux" | "freebsd" | "openbsd" | "netbsd" => {
            // Second half of the same problem: the core is built with GCC -flto,
            // whose object code lives in GCC IR sections that only GNU ld (via
            // the gcc driver's LTO plugin) can materialize - rust-lld, rustc
            // 1.95's default, has no GCC LTO plugin and reports every md_*
            // symbol as undefined even with the archives right there on the
            // line (proven standalone with gcc vs gcc -fuse-ld=lld). Point the
            // driver at GNU ld; the later -fuse-ld wins over rustc's own.
            println!("cargo:rustc-link-arg=-fuse-ld=bfd");

            // Dynamic symbols for the crash handler's backtrace: without
            // -rdynamic a frame from the C++ core prints as a bare address,
            // which is not enough to find a segfault in a 100k-line emulator.
            println!("cargo:rustc-link-arg-bins=-rdynamic");

            // Third piece: with GNU ld's --as-needed, a DSO must be listed
            // AFTER the objects that reference it. The LTO-expanded core
            // archives reference libstdc++/libm, but cargo emits those dylibs
            // from rlib metadata before the archives, so bfd refuses with
            // "DSO missing from command line". Re-listing them here puts them
            // after the archives; a duplicate DT_NEEDED is harmless.
            println!("cargo:rustc-link-arg=-lstdc++");
            println!("cargo:rustc-link-arg=-lm");

            println!("cargo:rustc-link-lib=dylib=stdc++");
            println!("cargo:rustc-link-lib=dylib=m");
            println!("cargo:rustc-link-lib=dylib=dl");
            println!("cargo:rustc-link-lib=dylib=rt");
        }
        "macos" | "ios" => {
            // Clang's LTO objects are bitcode, which Apple's ld materializes on
            // its own, so no driver override is needed. libc++ replaces
            // libstdc++ (and must come after the archives, like above).
            println!("cargo:rustc-link-arg=-lc++");
            println!("cargo:rustc-link-lib=dylib=c++");
        }
        "windows" => {
            // MSVC's linker takes the archives as-is; the C++ runtime arrives
            // through their /DEFAULTLIB records. The core's sockets code uses
            // winsock and the bridge uses ole32/comctl32 (see
            // src/frontend/tauri/CMakeLists.txt), which CMake already puts on
            // melonds_bridge's link interface.
        }
        other => panic!(
            "no link flags for target os '{other}': add a branch here, and a CMake \
             recipe that produces the three archives"
        ),
    }

    // Rebuild the Rust side whenever the C++ sources change, so an edited
    // bridge cannot end up running inside a stale binary.
    // The whole C++ core, not one file: the archives above are inputs to this
    // link, and cargo has no way to know when they changed. Watching only the
    // bridge left the host linked against a stale core every time an emulator
    // file changed - `src/Wifi.cpp`, which owns the link's timing, included.
    for watch in [
        "src/frontend/tauri/Bridge.cpp",
        "src/frontend/tauri/PlatformBridge.cpp",
        "src/frontend/tauri/melonds_bridge.h",
        "src/net/LocalMP.cpp",
        "src/net/LocalMP.h",
        "src/Wifi.cpp",
        "src/Wifi.h",
        "src/NDS.cpp",
        "src/NDS.h",
        "src/GPU.cpp",
        "src/GPU.h",
        // The CPU and its CP15: an edit here changes what the emulator does
        // with every access, and the archives are still the same files cargo
        // already saw, so without these two names the host links stale.
        "src/ARM.cpp",
        "src/ARM.h",
        "src/CP15.cpp",
    ] {
        println!("cargo:rerun-if-changed={}", project_root.join(watch).display());
    }

    tauri_build::build()
}
