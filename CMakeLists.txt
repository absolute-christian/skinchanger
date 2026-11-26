# ---- standalone launcher: starts cs2.exe AND loads the DLL -----------------
add_executable(launcher WIN32 tools/launcher/launcher.cpp)
target_compile_definitions(launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(launcher PRIVATE comdlg32 shell32)
if(MSVC)
    target_compile_options(launcher PRIVATE /W4 /permissive- /EHsc)
else()
    target_link_options(launcher PRIVATE -municode)
endif()
add_dependencies(launcher skinchanger)

# DLL next to the launcher, so the pair is self-sufficient.
add_custom_command(TARGET launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:skinchanger> $<TARGET_FILE_DIR:launcher>/skinchanger.dll)
