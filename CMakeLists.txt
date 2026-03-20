cmake_minimum_required(VERSION 3.20)

project(QuickTile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(QUICKTILE_BUILD_VERSION "unknown")
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short HEAD
        RESULT_VARIABLE QUICKTILE_GIT_HASH_RESULT
        OUTPUT_VARIABLE QUICKTILE_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    if(QUICKTILE_GIT_HASH_RESULT EQUAL 0 AND NOT QUICKTILE_GIT_HASH STREQUAL "")
        set(QUICKTILE_BUILD_VERSION "${QUICKTILE_GIT_HASH}")

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" diff --quiet --ignore-submodules HEAD --
            RESULT_VARIABLE QUICKTILE_GIT_DIFF_RESULT
            ERROR_QUIET)

        if(QUICKTILE_GIT_DIFF_RESULT EQUAL 1)
            string(APPEND QUICKTILE_BUILD_VERSION "-dirty")
        endif()
    endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/generated_version.h.in"
    "${CMAKE_BINARY_DIR}/generated_version.h"
    @ONLY)

option(QUICKTILE_ENABLE_MSVC_ANALYZE "Enable MSVC /analyze static analysis" OFF)

function(quicktile_configure_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX)
        if(QUICKTILE_ENABLE_MSVC_ANALYZE)
            target_compile_options(${target_name} PRIVATE /analyze)
        endif()
    endif()
endfunction()

function(quicktile_configure_precompiled_headers target_name)
    target_precompile_headers(${target_name} PRIVATE src/pch.h)
endfunction()

include(CTest)

set(QUICKTILE_INCLUDE_DIRS
    src
)

set(QUICKTILE_APP_SOURCES
    src/app/app_commands.h
    src/app/main.cpp
    src/app/shortcuts.cpp
    src/app/shortcuts.h
)

set(QUICKTILE_CONFIG_SOURCES
    src/config/settings.cpp
    src/config/settings.h
)

set(QUICKTILE_LAYOUT_SOURCES
    src/layout/direction_resize.cpp
    src/layout/direction_resize.h
    src/layout/layout_engine.cpp
    src/layout/layout_engine.h
    src/layout/layout_helpers.h
    src/layout/layout_managers.cpp
    src/layout/layout_managers.h
    src/layout/layout_metadata.h
    src/layout/layout_model.h
    src/layout/layout_policy.cpp
    src/layout/layout_policy.h
    src/layout/layout_types.h
    src/layout/length_allocator.cpp
    src/layout/length_allocator.h
)

set(QUICKTILE_PLATFORM_SOURCES
    src/platform/file_watcher.cpp
    src/platform/file_watcher.h
    src/platform/logger.cpp
    src/platform/logger.h
    src/platform/shell_integration.cpp
    src/platform/shell_integration.h
    src/platform/single_instance.cpp
    src/platform/single_instance.h
)

set(QUICKTILE_UI_SOURCES
    src/ui/status_overlay.cpp
    src/ui/status_overlay.h
    src/ui/status_overlay_internal.h
    src/ui/systray.cpp
    src/ui/systray.h
    src/ui/top_bar.cpp
    src/ui/top_bar.h
)

set(QUICKTILE_WINDOWS_SOURCES
    src/windows/event_router.cpp
    src/windows/event_router.h
    src/windows/focus_tracker.cpp
    src/windows/focus_tracker.h
    src/windows/inspect_window.cpp
    src/windows/inspect_window.h
    src/windows/window_classifier.cpp
    src/windows/window_classifier.h
    src/windows/window_geometry.cpp
    src/windows/window_geometry.h
    src/windows/window_manager.cpp
    src/windows/window_manager.h
    src/windows/window_manager_tiling.cpp
)

set(QUICKTILE_WORKSPACE_SOURCES
    src/workspace/virtual_desktop.cpp
    src/workspace/virtual_desktop.h
    src/workspace/workspace_manager.cpp
    src/workspace/workspace_manager.h
    src/workspace/workspace_model.cpp
    src/workspace/workspace_model.h
)

set(QUICKTILE_SHARED_SOURCES
    ${QUICKTILE_APP_SOURCES}
    ${QUICKTILE_CONFIG_SOURCES}
    ${QUICKTILE_LAYOUT_SOURCES}
    ${QUICKTILE_PLATFORM_SOURCES}
    src/pch.h
    ${QUICKTILE_UI_SOURCES}
    ${QUICKTILE_WINDOWS_SOURCES}
    ${QUICKTILE_WORKSPACE_SOURCES}
)

set(QUICKTILE_TEST_SOURCES
    src/app/app_commands.h
    src/app/app_state.h
    src/app/shortcuts.cpp
    src/app/shortcuts.h
    src/config/config.h
    src/config/settings.cpp
    src/config/settings.h
    src/layout/direction_resize.cpp
    src/layout/direction_resize.h
    src/layout/layout_engine.cpp
    src/layout/layout_engine.h
    src/layout/layout_helpers.h
    src/layout/layout_managers.cpp
    src/layout/layout_managers.h
    src/layout/layout_metadata.h
    src/layout/layout_model.h
    src/layout/layout_policy.cpp
    src/layout/layout_policy.h
    src/layout/layout_types.h
    src/layout/length_allocator.cpp
    src/layout/length_allocator.h
    src/pch.h
    src/platform/file_watcher.cpp
    src/platform/file_watcher.h
    src/platform/logger.cpp
    src/platform/logger.h
    src/platform/shell_integration.cpp
    src/platform/shell_integration.h
    src/platform/single_instance.cpp
    src/platform/single_instance.h
    src/ui/status_overlay.cpp
    src/ui/status_overlay.h
    src/ui/status_overlay_internal.h
    src/ui/top_bar.cpp
    src/ui/top_bar.h
    src/windows/event_router.cpp
    src/windows/event_router.h
    src/windows/focus_tracker.cpp
    src/windows/focus_tracker.h
    src/windows/inspect_window.cpp
    src/windows/inspect_window.h
    src/windows/window_classifier.cpp
    src/windows/window_classifier.h
    src/windows/window_geometry.cpp
    src/windows/window_geometry.h
    src/windows/window_manager.cpp
    src/windows/window_manager.h
    src/windows/window_manager_tiling.cpp
    src/workspace/virtual_desktop.cpp
    src/workspace/virtual_desktop.h
    src/workspace/workspace_manager.cpp
    src/workspace/workspace_manager.h
    src/workspace/workspace_model.cpp
    src/workspace/workspace_model.h
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${QUICKTILE_SHARED_SOURCES})

add_executable(QuickTile WIN32
    ${QUICKTILE_SHARED_SOURCES}
)

set_property(TARGET QuickTile PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(QuickTile PRIVATE ${QUICKTILE_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
target_compile_definitions(QuickTile PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
quicktile_configure_warnings(QuickTile)
quicktile_configure_precompiled_headers(QuickTile)
target_link_libraries(QuickTile PRIVATE dwmapi gdi32 shell32 shcore user32 ole32)

if(BUILD_TESTING)
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/tests" PREFIX "tests" FILES tests/quicktile_tests.cpp)
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${QUICKTILE_TEST_SOURCES})

    add_executable(QuickTileTests
        tests/quicktile_tests.cpp
        ${QUICKTILE_TEST_SOURCES}
    )

    set_property(TARGET QuickTileTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_include_directories(QuickTileTests PRIVATE ${QUICKTILE_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
    target_compile_definitions(QuickTileTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    quicktile_configure_warnings(QuickTileTests)
    quicktile_configure_precompiled_headers(QuickTileTests)
    target_link_libraries(QuickTileTests PRIVATE dwmapi gdi32 shell32 shcore user32 ole32)

    add_test(NAME QuickTileTests COMMAND QuickTileTests)
endif()
