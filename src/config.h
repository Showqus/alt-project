#pragma once

#include <string>

struct Config {
    // [General]
    int unloadKey = 0x23;  // END
    bool requireHiddenCursor = true;

    // [AutoSprint]
    bool sprintEnabled = true;
    int sprintToggleKey = 0x77;  // F8
    int forwardKey = 'W';
    int sprintKey = 0x11;  // CTRL
    bool sprintFallbackSendInput = true;

    // [Zoom]
    bool zoomEnabled = true;
    int zoomKey = 'C';
    bool zoomToggle = false;
    float zoomFactor = 4.0f;
    float zoomMinFactor = 1.5f;
    float zoomMaxFactor = 50.0f;
    bool zoomScrollAdjust = true;
    float zoomScrollStep = 1.25f;
    bool zoomRememberScroll = false;
    bool zoomSmooth = true;
    float zoomSmoothSpeed = 12.0f;
    bool zoomHand = false;

    // [Signatures] - empty means "use the built-in ones".
    std::string sigGetFov;
    std::string sigKeyboardFeed;
    std::string sigMouseFeed;
};

// Global configuration, loaded once at startup.
extern Config g_config;

namespace config {

// Loads the INI file; writes a commented default file first if it does not exist.
void Load(const std::wstring& path);

}  // namespace config
