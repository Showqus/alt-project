cmake_minimum_required(VERSION 3.16)
project(BedrockQoL LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "BedrockQoL targets Windows. Use MSVC, or cmake/mingw-w64-x86_64.cmake to cross-compile.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Minecraft Bedrock is 64-bit: build for x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# MinHook (vendored)
add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC third_party/minhook/include)

set(BEDROCKQOL_VERSION "dev" CACHE STRING "Version string compiled into the DLL (CI sets build number + commit)")

# The mod itself
add_library(BedrockQoL SHARED
    src/dllmain.cpp
    src/chat.cpp
    src/commands.cpp
    src/config.cpp
    src/events.cpp
    src/game.cpp
    src/hooks.cpp
    src/keys.cpp
    src/log.cpp
    src/notify.cpp
    src/scanner.cpp
    src/text.cpp
    src/features/autosprint.cpp
    src/features/fullbright.cpp
    src/features/texthotkey.cpp
    src/features/zoom.cpp
)
target_link_libraries(BedrockQoL PRIVATE minhook user32 shell32 ole32)
target_compile_definitions(BedrockQoL PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
                           BEDROCKQOL_VERSION="${BEDROCKQOL_VERSION}")
set_target_properties(BedrockQoL PROPERTIES PREFIX "" OUTPUT_NAME "BedrockQoL")

# Launcher: headless background app (tray icon optional) with auto-inject and auto-update.
add_executable(BedrockQoLLauncher WIN32
    launcher/main.cpp
    launcher/common.cpp
    launcher/inject.cpp
    launcher/service.cpp
    launcher/update.cpp
)
target_link_libraries(BedrockQoLLauncher PRIVATE advapi32 shell32 ole32 user32 comdlg32 winhttp bcrypt)
target_compile_definitions(BedrockQoLLauncher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

if(MSVC)
    # Static CRT so the DLL has no extra runtime dependencies inside the game.
    set_property(TARGET minhook BedrockQoL BedrockQoLLauncher PROPERTY
                 MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(BedrockQoL PRIVATE /W4 /utf-8)
    target_compile_options(BedrockQoLLauncher PRIVATE /W4 /utf-8)
else()
    # MinGW: link libgcc/libstdc++/winpthread statically for the same reason.
    target_compile_options(BedrockQoL PRIVATE -Wall -Wextra)
    target_compile_options(BedrockQoLLauncher PRIVATE -Wall -Wextra)
    target_link_options(BedrockQoL PRIVATE -static -static-libgcc -static-libstdc++ -s)
    target_link_options(BedrockQoLLauncher PRIVATE -static -static-libgcc -static-libstdc++ -s)
endif()

# Smoke test: a fake "game" whose code matches the signatures (MinGW only, uses GAS assembly).
# Run under Windows or Wine next to BedrockQoL.dll: fake_game.exe BedrockQoL.dll
if(MINGW)
    add_executable(fake_game tests/fake_game.cpp)
    target_compile_definitions(fake_game PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_options(fake_game PRIVATE -static -static-libgcc -static-libstdc++)
endif()
