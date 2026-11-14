// Brick — param_info.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace brick {

enum class Unit {
  Unitless = 0,
  Db = 1,
  DbTp = 2,
  Hz = 3,
  Ms = 4,
  Percent = 5,
};

// Control ports, in ascending port index order, interleaved with the stereo
// audio ports as defined in the plugin RDF.
enum PortIndex : uint32_t {
  PORT_AUDIO_IN_L = 0,
  PORT_AUDIO_IN_R = 1,
  PORT_AUDIO_OUT_L = 2,
  PORT_AUDIO_OUT_R = 3,
  PORT_INPUT_GAIN = 4,
  PORT_CEILING = 5,
  PORT_RELEASE = 6,
  PORT_LOOKAHEAD = 7,
  PORT_LINK = 8,
  PORT_TRUE_PEAK = 9,
  PORT_RMS_MODE = 10,
  PORT_ENABLE = 11,
  PORT_METER_GR = 12,
  PORT_METER_OUT = 13,
  PORT_METER_IN = 14,
  PORT_METER_TRUE_PEAK = 15,
  PORT_METER_RMS = 16,
  PORT_LATENCY = 17,
  PORT_COUNT = 18
};

enum ParamGroup : uint32_t {
  GROUP_LIMITER = 0,
  GROUP_MODE = 1,
  GROUP_GLOBAL = 2,
};

struct ScalePoint {
  float value = 0.0f;
  const char* label = "";
};

struct ParamInfo {
  uint32_t index = 0;
  const char* symbol = "";
  const char* name = "";
  float min = 0.0f;
  float max = 1.0f;
  float def = 0.0f;
  Unit unit = Unit::Unitless;
  bool logarithmic = false;
  bool toggle = false;
  bool enumeration = false;
  int group = GROUP_LIMITER;
  const ScalePoint* points = nullptr;
  int pointCount = 0;
};

inline constexpr ScalePoint kScaleLink[] = {
    {0.0f, "0"}, {50.0f, "50"}, {100.0f, "100"}};

inline constexpr ParamInfo kParams[] = {
    {4, "input_gain", "Input", -20.0f, 20.0f, 0.0f, Unit::Db, false, false,
     false, GROUP_LIMITER, nullptr, 0},
    {5, "ceiling", "Ceiling", -20.0f, 0.0f, -1.0f, Unit::Db, false, false,
     false, GROUP_LIMITER, nullptr, 0},
    {6, "release", "Release", 10.0f, 1000.0f, 100.0f, Unit::Ms, true, false,
     false, GROUP_LIMITER, nullptr, 0},
    {7, "lookahead", "Lookahead", 0.0f, 5.0f, 3.0f, Unit::Ms, false, false,
     false, GROUP_LIMITER, nullptr, 0},
    {8, "link", "Link", 0.0f, 100.0f, 100.0f, Unit::Percent, false, false,
     false, GROUP_LIMITER, kScaleLink, 3},
    {9, "true_peak", "True Peak", 0.0f, 1.0f, 1.0f, Unit::Unitless, false,
     true, false, GROUP_MODE, nullptr, 0},
    {10, "rms_mode", "RMS Mode", 0.0f, 1.0f, 0.0f, Unit::Unitless, false, true,
     false, GROUP_MODE, nullptr, 0},
    {11, "enable", "Enabled", 0.0f, 1.0f, 1.0f, Unit::Unitless, false, true,
     false, GROUP_GLOBAL, nullptr, 0},
};

inline constexpr int kParamInfoCount =
    static_cast<int>(sizeof(kParams) / sizeof(kParams[0]));

inline constexpr uint32_t kMeterGrPort = PORT_METER_GR;
inline constexpr uint32_t kMeterOutPort = PORT_METER_OUT;
inline constexpr uint32_t kMeterInPort = PORT_METER_IN;
inline constexpr uint32_t kMeterTruePeakPort = PORT_METER_TRUE_PEAK;
inline constexpr uint32_t kMeterRmsPort = PORT_METER_RMS;
inline constexpr uint32_t kLatencyPort = PORT_LATENCY;

} // namespace brick
