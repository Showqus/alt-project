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

# Dear ImGui (vendored) with the DirectX 11 / 12 renderer backends. src/gui/imgui_config.h routes
# assertions to the mod's log and enables the item hooks used by the tests.
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    third_party/imgui/backends/imgui_impl_dx12.cpp
)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends src/gui)
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="imgui_config.h" NOMINMAX)

# The menu's built-in font (Roboto Medium, Latin + Cyrillic subset), embedded into the DLL.
include(cmake/embed.cmake)
embed_file(${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/Roboto-Medium.ttf
           ${CMAKE_CURRENT_BINARY_DIR}/generated/font_roboto.cpp kFontRoboto)

set(BEDROCKQOL_VERSION "dev" CACHE STRING "Version string compiled into the DLL (CI sets build number + commit)")

# The mod itself
add_library(BedrockQoL SHARED
    src/dllmain.cpp
    src/chat.cpp
    src/commands.cpp
    src/config.cpp
    src/crashlog.cpp
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
    src/gui/assets.cpp
    src/gui/automation.cpp
    src/gui/input.cpp
    src/gui/menu.cpp
    src/gui/overlay.cpp
    src/gui/renderer.cpp
    src/gui/state.cpp
    src/gui/theme.cpp
    src/gui/widgets.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/font_roboto.cpp
)
target_include_directories(BedrockQoL PRIVATE src third_party/nlohmann third_party/stb)
# d3d12.dll and d3dcompiler_47.dll are loaded at run time (see src/gui/renderer.cpp), so the DLL
# still loads into a game that only uses Direct3D 11.
target_link_libraries(BedrockQoL PRIVATE imgui minhook d3d11 dxgi user32 shell32 ole32)
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
    set_property(TARGET minhook imgui BedrockQoL BedrockQoLLauncher PROPERTY
                 MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(BedrockQoL PRIVATE /W4 /utf-8)
    target_compile_options(BedrockQoLLauncher PRIVATE /W4 /utf-8)
else()
    # MinGW: link libgcc/libstdc++/winpthread statically for the same reason.
    target_compile_options(BedrockQoL PRIVATE -Wall -Wextra)
    target_compile_options(BedrockQoLLauncher PRIVATE -Wall -Wextra)
    # The map file turns "CRASH: ... at BedrockQoL.dll+0x..." lines of the log into function names.
    target_link_options(BedrockQoL PRIVATE -static -static-libgcc -static-libstdc++ -s
                        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/BedrockQoL.map)
    target_link_options(BedrockQoLLauncher PRIVATE -static -static-libgcc -static-libstdc++ -s)
endif()

# Smoke test: a fake "game" whose code matches the signatures (MinGW only, uses GAS assembly).
# Run under Windows or Wine next to BedrockQoL.dll: fake_game.exe BedrockQoL.dll
if(MINGW)
    add_executable(fake_game tests/fake_game.cpp)
    target_compile_definitions(fake_game PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(fake_game PRIVATE d3d11 d3d12 dxgi)
    target_link_options(fake_game PRIVATE -static -static-libgcc -static-libstdc++)
    # Font for the menu font test (copied into the mod's fonts folder by the test).
    configure_file(third_party/fonts/Roboto-Medium.ttf ${CMAKE_CURRENT_BINARY_DIR}/test-font.ttf COPYONLY)
endif()
