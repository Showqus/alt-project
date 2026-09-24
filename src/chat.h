#pragma once

// Tracks what the player types into the in-game chat, using only the key events that pass
// through Keyboard::feed, so that messages starting with the command prefix (".bind ...")
// can be run locally instead of being sent to the server.
namespace chat {

using KeyFeedFn = void (*)(int key, int state);

// Called for every key event on the game's input thread before it reaches the game.
// Returns true if the event must be swallowed (Enter of a command). `feed` is the original
// Keyboard::feed, used to close the chat with Escape.
bool OnKey(int vk, bool down, KeyFeedFn feed);

// Keeps Shift/Ctrl/Alt in sync for key events that OnKey does not see (the menu swallowed them).
void TrackModifiers(int vk, bool down);

// A mouse click inside the chat moves the text cursor to an unknown position.
void OnMouseClick();

// True while the chat is (believed to be) open.
bool IsOpen();

}  // namespace chat
