#pragma once

namespace fullbright {

// Called from the Options::getGamma hook with the game's gamma; returns the gamma to use.
float OnGamma(float gamma);

}  // namespace fullbright
