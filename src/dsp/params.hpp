// Brick — params.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace brick {

// Control values in their natural DSP units.  The LV2/CLAP wrappers convert
// from port units (percent, etc.) before filling this in.
struct Params {
  float inputGainDb = 0.0f;
  float ceilingDb = -1.0f;
  float releaseMs = 100.0f;
  float lookaheadMs = 3.0f;
  float link = 1.0f; // 0..1 stereo link
  bool truePeak = true;
  bool rmsMode = false;
  bool enabled = true;
};

} // namespace brick
