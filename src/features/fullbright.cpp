#include "fullbright.h"

#include "../config.h"

namespace fullbright {

float OnGamma(float gamma) {
    if (!g_config.fullbrightEnabled.load(std::memory_order_relaxed)) return gamma;
    return g_config.fullbrightGamma.load(std::memory_order_relaxed);
}

}  // namespace fullbright
