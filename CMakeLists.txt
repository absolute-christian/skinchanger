cmake_minimum_required(VERSION 3.24)
project(cs2_skinchanger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- portable core tests: build anywhere ----------------------------------
add_executable(core_tests tests/core_tests.cpp)
target_include_directories(core_tests PRIVATE src)

if(NOT WIN32)
    message(STATUS "Not Windows: only core_tests is configured.")
    return()
endif()

include(FetchContent)

# CMake 4.x refuses dependencies that declare a minimum below 3.5; this keeps them configurable.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Versions are pinned tags. If a tag fails to fetch, check the repo's tag list and adjust.
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.5)
FetchContent_Declare(minhook GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git GIT_TAG v1.3.4)
FetchContent_MakeAvailable(imgui minhook)

add_library(imgui_static STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui_static PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

add_library(skinchanger SHARED
    src/main.cpp
    src/game/memory.cpp
    src/game/skins.cpp
    src/hooks/hooks.cpp
    src/ui/ui.cpp)
target_include_directories(skinchanger PRIVATE src)
target_compile_definitions(skinchanger PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(skinchanger PRIVATE imgui_static minhook d3d11 dxgi dwmapi windowscodecs ole32 uuid)
set_target_properties(skinchanger PROPERTIES OUTPUT_NAME "skinchanger")
if(MSVC)
    target_compile_options(skinchanger PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(imgui_static PRIVATE /W3)
endif()

# Data files next to the DLL.
add_custom_command(TARGET skinchanger POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/offsets.ini.example $<TARGET_FILE_DIR:skinchanger>/offsets.ini.example)

# ---- standalone launcher: just starts cs2.exe, no injection ---------------
add_executable(launcher tools/launcher/launcher.cpp)
target_compile_definitions(launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
if(MSVC)
    target_compile_options(launcher PRIVATE /W4 /permissive- /EHsc)
endif()
