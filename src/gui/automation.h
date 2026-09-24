#pragma once

// Widget lookup for automated tests (tests/fake_game.cpp): while a test asks for widgets through
// the exported BedrockQoL_FindItem(), ImGui's item hooks record the label and rectangle of every
// visible widget of the last frame. Costs nothing until the first query.
namespace gui::automation {

// Render thread, around each ImGui frame.
void BeginFrame();
void EndFrame();

// Render thread: nothing is drawn this frame, so no widget is visible.
void NoFrame();

}  // namespace gui::automation
