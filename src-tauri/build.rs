// melonDS Multiplayer - links the Tauri host against the native bridge.
//
// The bridge (src/frontend/tauri) is built by CMake into build-tauri/ before
// this crate is compiled. These are LTO-built static libs, so the final link
// must run through the same GCC driver CMake used; see history.md v0.1.6.

use std::path::Path;

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

    // Pull CMAKE_BUILD_TYPE out of the cache: the debug core has assertion
    // hooks the release core does not, and mixing them is a subtle way to lose
    // a weekend.
    let cmake_build_type = std::fs::read_to_string(&cache)
        .ok()
        .and_then(|text| {
            text.lines()
                .find(|l| l.starts_with("CMAKE_BUILD_TYPE:STRING="))
                .map(|l| l["CMAKE_BUILD_TYPE:STRING=".len()..].trim().to_string())
        })
        .unwrap_or_default();
    assert!(
        cmake_build_type == "Release",
        "build-tauri was configured in '{cmake_build_type}', not Release. \
         The Tauri host needs the optimised core: \
         cmake -B build-tauri -DCMAKE_BUILD_TYPE=Release && cmake --build build-tauri"
    );

    let bridge_dir = build_dir.join("src/frontend/tauri");
    let core_dir = build_dir.join("src");
    let teakra_dir = build_dir.join("src/teakra/src");

    let libs = [
        (bridge_dir.join("libmelonds_bridge.a"), "libmelonds_bridge.a"),
        (core_dir.join("libcore.a"), "libcore.a"),
        (teakra_dir.join("libteakra.a"), "libteakra.a"),
    ];
    for (path, name) in libs {
        assert!(
            path.exists(),
            "{} missing - build the C++ core first (cmake --build build-tauri)",
            name
        );
    }

    // Order matters to a static linker: bridge, then core, then teakra.
    println!("cargo:rustc-link-search=native={}", bridge_dir.display());
    println!("cargo:rustc-link-search=native={}", core_dir.display());
    println!("cargo:rustc-link-search=native={}", teakra_dir.display());
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
    for path in [
        bridge_dir.join("libmelonds_bridge.a"),
        core_dir.join("libcore.a"),
        teakra_dir.join("libteakra.a"),
    ] {
        println!("cargo:rustc-link-arg-bins={}", path.display());
    }

    // Second half of the same problem: the core is built with GCC -flto, whose
    // object code lives in GCC IR sections that only GNU ld (via the gcc
    // driver's LTO plugin) can materialize - rust-lld, rustc 1.95's default,
    // has no GCC LTO plugin and reports every md_* symbol as undefined even
    // with the archives right there on the line (proven standalone with gcc
    // vs gcc -fuse-ld=lld). Point the driver at GNU ld; the later -fuse-ld
    // wins over rustc's own -fuse-ld=lld.
    println!("cargo:rustc-link-arg=-fuse-ld=bfd");

    // Dynamic symbols for the crash handler's backtrace: without -rdynamic a
    // frame from the C++ core prints as a bare address, which is not enough to
    // find a segfault in a 100k-line emulator. With it, backtrace_symbols_fd
    // resolves function names for anything in the final link.
    println!("cargo:rustc-link-arg-bins=-rdynamic");

    // Third piece: with GNU ld's --as-needed (set earlier on the line), a DSO
    // must be listed AFTER the objects that reference it. The LTO-expanded
    // core archives reference libstdc++/libm, but cargo emits those dylibs
    // from rlib metadata before the archives, so bfd refuses with
    // "DSO missing from command line". Re-listing them here puts them after
    // the archives; a duplicate DT_NEEDED is harmless.
    println!("cargo:rustc-link-arg=-lstdc++");
    println!("cargo:rustc-link-arg=-lm");

    // System libraries the core needs on Linux (mirrors the CMake smoke-test
    // link line plus pthread, which this host brings itself).
    println!("cargo:rustc-link-lib=dylib=stdc++");
    println!("cargo:rustc-link-lib=dylib=m");
    println!("cargo:rustc-link-lib=dylib=dl");
    println!("cargo:rustc-link-lib=dylib=rt");

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
    ] {
        println!("cargo:rerun-if-changed={}", project_root.join(watch).display());
    }

    tauri_build::build()
}
