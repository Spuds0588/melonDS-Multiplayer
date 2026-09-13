# Headless core tests: boot the diagnostic ROM through the bridge.
# Build with -DBUILD_TAURI_BRIDGE=ON, then:
#
#   build-tauri/md_smoke_test tools/mkdiagrom/diag.nds          # assertions
#   build-tauri/md_capture <rom> captures                        # real frames
#   python3 tools/coretest/frames_to_html.py captures            # contact sheet

add_executable(md_smoke_test smoke_test.cpp)
target_link_libraries(md_smoke_test PRIVATE melonds_bridge)

# Writes raw frames plus a manifest so the rendering can be inspected by eye,
# not just asserted on.
add_executable(md_capture capture_frames.cpp)
target_link_libraries(md_capture PRIVATE melonds_bridge)

# Runs a real game ROM in one or more consoles: stability, link activity, and how
# much of realtime the machine can keep up with.
add_executable(md_game_session game_session.cpp)
target_link_libraries(md_game_session PRIVATE melonds_bridge)
