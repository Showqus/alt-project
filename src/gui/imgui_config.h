#pragma once

// Dear ImGui build configuration for BedrockQoL (passed to every ImGui file as IMGUI_USER_CONFIG).

// A failed ImGui assertion is logged instead of aborting the game.
void BqolImGuiAssert(const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) ((_EXPR) ? (void)0 : BqolImGuiAssert(#_EXPR, __FILE__, __LINE__))

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS

// Item hooks (ImGuiTestEngineHook_ItemAdd / ItemInfo) let tests find widgets by label, see automation.cpp.
#define IMGUI_ENABLE_TEST_ENGINE
