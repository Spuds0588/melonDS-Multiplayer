# Headless smoke test: boots the diagnostic ROM through the bridge.
# Build with -DBUILD_TAURI_BRIDGE=ON, then:
#   build-tauri/tools/coretest/md_smoke_test tools/mkdiagrom/diag.nds

add_executable(md_smoke_test smoke_test.cpp)
target_link_libraries(md_smoke_test PRIVATE melonds_bridge)
