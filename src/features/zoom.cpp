#include "zoom.h"

#include <windows.h>

#include <atomic>
#include <cmath>

#include "../config.h"
#include "../game.h"
#include "../log.h"

namespace zoom {
namespace {

// Bedrock renders the first-person hand with a fixed 70 degree FOV through the same getFov call.
constexpr float kHandFov = 70.0f;

std::atomic<bool> g_active{false};
std::atomic<float> g_factor{0.0f};  // 0 = not initialised yet

// Render-thread state.
float g_multiplier = 1.0f;
ULONGLONG g_lastNonHandFovTick = 0;
LARGE_INTEGER g_lastFrame{};
LARGE_INTEGER g_frequency{};

float Factor() {
    float f = g_factor.load(std::memory_order_relaxed);
    return f > 0.0f ? f : g_config.zoomFactor;
}

void Start() {
    if (!g_config.zoomRememberScroll || g_factor.load() <= 0.0f) g_factor.store(g_config.zoomFactor);
    g_active.store(true);
}

float StepMultiplier(float target) {
    if (!g_config.zoomSmooth) return target;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);

    float dt = 0.0f;
    if (g_lastFrame.QuadPart != 0) {
        dt = static_cast<float>(now.QuadPart - g_lastFrame.QuadPart) / static_cast<float>(g_frequency.QuadPart);
    }
    g_lastFrame = now;
    if (dt > 0.1f) dt = 0.1f;  // after a hitch / alt-tab, don't jump

    const float t = 1.0f - std::exp(-g_config.zoomSmoothSpeed * dt);
    float m = g_multiplier + (target - g_multiplier) * t;
    if (std::fabs(m - target) < 0.0005f) m = target;
    return m;
}

}  // namespace

void OnKey(bool down, bool inWorld) {
    if (!g_config.zoomEnabled) return;

    if (g_config.zoomToggle) {
        if (!down || !inWorld) return;
        if (g_active.load()) {
            g_active.store(false);
        } else {
            Start();
        }
        return;
    }

    if (down && inWorld) {
        if (!g_active.load()) Start();
    } else if (!down) {
        g_active.store(false);
    }
}

bool OnScroll(int delta, bool inWorld) {
    if (!g_config.zoomEnabled || !g_config.zoomScrollAdjust || !g_active.load() || !inWorld || delta == 0) {
        return false;
    }

    float f = Factor();
    f = delta > 0 ? f * g_config.zoomScrollStep : f / g_config.zoomScrollStep;
    if (f < g_config.zoomMinFactor) f = g_config.zoomMinFactor;
    if (f > g_config.zoomMaxFactor) f = g_config.zoomMaxFactor;
    g_factor.store(f);
    return true;
}

float OnFov(float fov) {
    if (!g_config.zoomEnabled || !std::isfinite(fov) || fov <= 0.0f) return fov;

    // The hand is drawn with 70; only treat 70 as the hand if the world FOV is something else
    // (otherwise a player with FOV=70 would never zoom).
    const ULONGLONG now = GetTickCount64();
    const bool isHand = fov == kHandFov && now - g_lastNonHandFovTick < 1000;
    if (fov != kHandFov) g_lastNonHandFovTick = now;

    if (!isHand) {
        const bool zooming = g_active.load(std::memory_order_relaxed) && game::InWorldCached();
        g_multiplier = StepMultiplier(zooming ? 1.0f / Factor() : 1.0f);
    }

    if (isHand && !g_config.zoomHand) return fov;
    return fov * g_multiplier;
}

void Reset() {
    g_active.store(false);
    g_multiplier = 1.0f;
}

}  // namespace zoom
