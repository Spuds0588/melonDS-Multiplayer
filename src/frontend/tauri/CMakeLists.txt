# melonDS Multiplayer - native bridge for the Tauri host.
#
# Deliberately dependency free: it only needs the core library plus melonDS's
# in-tree LocalMP implementation, which is what gives us the "virtual link
# cable" between splitscreen instances. No Qt, no SDL, no slirp/enet.

add_library(melonds_bridge STATIC
    Bridge.cpp
    PlatformBridge.cpp
    "${CMAKE_SOURCE_DIR}/src/net/LocalMP.cpp")

target_include_directories(melonds_bridge
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/src/net")

# LocalMP is core code: it needs the same warning suppressions the core target uses.
if (NOT MSVC)
    target_compile_options(melonds_bridge PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>")
endif()

target_link_libraries(melonds_bridge PUBLIC core)

if (NOT MSVC)
    target_compile_options(melonds_bridge PUBLIC -fwrapv)
endif()

if (WIN32)
    target_link_libraries(melonds_bridge PRIVATE ole32 comctl32 wsock32 ws2_32)
elseif (NOT APPLE)
    find_library(MD_BRIDGE_MATH_LIBRARY m)
    if (MD_BRIDGE_MATH_LIBRARY)
        target_link_libraries(melonds_bridge PRIVATE ${MD_BRIDGE_MATH_LIBRARY})
    endif()
    target_link_libraries(melonds_bridge PRIVATE ${CMAKE_DL_LIBS})
endif()
