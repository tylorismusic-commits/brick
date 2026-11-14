// Brick — lv2 wrapper
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include <lv2/core/lv2.h>

#include "brick_strip.hpp"
#include "inline_display.hpp"
#include "param_info.hpp"
#include "params.hpp"

namespace brick {

class BrickInstance {
public:
  float* ports[PORT_COUNT] = {};
  float sampleRate = 48000.0f;
  BrickStrip strip;
  Params params;
  InlineDisplay display;
  const InlineDisplayQueueDraw* queueDraw = nullptr;
  uint32_t drawCounter = 0;
};

static const ParamInfo& infoFor(uint32_t index) noexcept {
  for (int i = 0; i < kParamInfoCount; ++i) {
    if (kParams[i].index == index) {
      return kParams[i];
    }
  }
  static ParamInfo fallback;
  fallback.index = index;
  return fallback;
}

static float ctrlValue(float* const* ports, uint32_t index,
                       float fallback) noexcept {
  const ParamInfo& info = infoFor(index);
  float v = fallback;
  if (index < PORT_COUNT && ports[index]) {
    v = ports[index][0];
  }
  if (!std::isfinite(v)) {
    v = fallback;
  }
  const float lo = std::min(info.min, info.max);
  const float hi = std::max(info.min, info.max);
  if (hi > lo) {
    v = std::min(std::max(v, lo), hi);
  }
  return v;
}

static void readParams(BrickInstance* self) noexcept {
  self->params.inputGainDb = ctrlValue(self->ports, PORT_INPUT_GAIN, 0.0f);
  self->params.ceilingDb = ctrlValue(self->ports, PORT_CEILING, -1.0f);
  self->params.releaseMs = ctrlValue(self->ports, PORT_RELEASE, 100.0f);
  self->params.lookaheadMs = ctrlValue(self->ports, PORT_LOOKAHEAD, 3.0f);
  self->params.link = ctrlValue(self->ports, PORT_LINK, 100.0f) / 100.0f;
  self->params.truePeak =
      ctrlValue(self->ports, PORT_TRUE_PEAK, 1.0f) > 0.5f;
  self->params.rmsMode =
      ctrlValue(self->ports, PORT_RMS_MODE, 0.0f) > 0.5f;
  self->params.enabled = ctrlValue(self->ports, PORT_ENABLE, 1.0f) > 0.5f;
}

static void reportLatency(BrickInstance* self) noexcept {
  if (PORT_LATENCY < PORT_COUNT && self->ports[PORT_LATENCY]) {
    self->ports[PORT_LATENCY][0] =
        static_cast<float>(self->strip.latencySamples());
  }
}

} // namespace brick

using namespace brick;

extern "C" LV2_Handle lv2_brick_instantiate(
    const LV2_Descriptor* descriptor, double rate, const char* bundle_path,
    const LV2_Feature* const* features) {
  (void)descriptor;
  (void)bundle_path;

  brick::BrickInstance* self = new (std::nothrow) brick::BrickInstance();
  if (!self) {
    return nullptr;
  }
  for (const LV2_Feature* const* f = features; f && *f; ++f) {
    if (!std::strcmp((*f)->URI, brick::kInlineDisplayQueueDrawURI) &&
        (*f)->data) {
      self->queueDraw = static_cast<const brick::InlineDisplayQueueDraw*>(
          static_cast<const void*>((*f)->data));
    }
  }
  self->sampleRate = std::max(8000.0f, static_cast<float>(rate));
  self->strip.prepare(self->sampleRate);

  brick::readParams(self);
  self->strip.setTargets(self->params);
  self->strip.reset();
  self->display.setGainReductionDb(0.0f);
  return self;
}

extern "C" void lv2_brick_connect_port(LV2_Handle handle, uint32_t port,
                                       void* data_location) {
  brick::BrickInstance* self = static_cast<brick::BrickInstance*>(handle);
  if (!self) {
    return;
  }
  if (port < brick::PORT_COUNT) {
    self->ports[port] = static_cast<float*>(data_location);
  }
  if (port == brick::PORT_LATENCY && self->ports[port]) {
    self->ports[port][0] = static_cast<float>(self->strip.latencySamples());
  }
}

extern "C" void lv2_brick_activate(LV2_Handle handle) {
  brick::BrickInstance* self = static_cast<brick::BrickInstance*>(handle);
  if (!self) {
    return;
  }
  brick::readParams(self);
  self->strip.setTargets(self->params);
  self->strip.reset();
  self->display.setGainReductionDb(0.0f);
  brick::reportLatency(self);
}

extern "C" void lv2_brick_run(LV2_Handle handle, uint32_t sampleCount) {
  brick::BrickInstance* self = static_cast<brick::BrickInstance*>(handle);
  if (!self) {
    return;
  }

  const bool audioValid =
      self->ports[PORT_AUDIO_IN_L] && self->ports[PORT_AUDIO_IN_R] &&
      self->ports[PORT_AUDIO_OUT_L] && self->ports[PORT_AUDIO_OUT_R];

  float grDb = 0.0f;
  float outDb = -60.0f;
  float inDb = -60.0f;
  float truePeakDb = -60.0f;
  float rmsDb = -60.0f;
  if (audioValid) {
    brick::readParams(self);
    self->strip.setTargets(self->params);
    self->strip.process(
        self->ports[PORT_AUDIO_IN_L], self->ports[PORT_AUDIO_IN_R],
        self->ports[PORT_AUDIO_OUT_L], self->ports[PORT_AUDIO_OUT_R],
        sampleCount, grDb, outDb, inDb, truePeakDb, rmsDb);
  }

  if (self->ports[PORT_METER_GR]) {
    self->ports[PORT_METER_GR][0] = grDb;
  }
  if (self->ports[PORT_METER_OUT]) {
    self->ports[PORT_METER_OUT][0] = outDb;
  }
  if (self->ports[PORT_METER_IN]) {
    self->ports[PORT_METER_IN][0] = inDb;
  }
  if (self->ports[PORT_METER_TRUE_PEAK]) {
    self->ports[PORT_METER_TRUE_PEAK][0] = truePeakDb;
  }
  if (self->ports[PORT_METER_RMS]) {
    self->ports[PORT_METER_RMS][0] = rmsDb;
  }
  brick::reportLatency(self);

  self->display.setGainReductionDb(std::max(0.0f, -grDb));
  if (self->queueDraw && self->queueDraw->queue_draw &&
      ((self->drawCounter++ & 31u) == 0u)) {
    self->queueDraw->queue_draw(self->queueDraw->handle);
  }
}

extern "C" void lv2_brick_deactivate(LV2_Handle handle) {
  brick::BrickInstance* self = static_cast<brick::BrickInstance*>(handle);
  if (!self) {
    return;
  }
  self->strip.reset();
  self->display.setGainReductionDb(0.0f);
}

extern "C" void lv2_brick_cleanup(LV2_Handle handle) {
  delete static_cast<brick::BrickInstance*>(handle);
}

static brick::InlineDisplaySurface* renderInlineDisplay(void* instance,
                                                        uint32_t width,
                                                        uint32_t height) {
  brick::BrickInstance* self = static_cast<brick::BrickInstance*>(instance);
  return self ? self->display.render(width, height) : nullptr;
}

static const brick::InlineDisplayInterface brickDisplayInterface = {
    &renderInlineDisplay};

extern "C" const void* lv2_brick_extension_data(const char* uri) {
  if (!std::strcmp(uri, brick::kInlineDisplayInterfaceURI)) {
    return &brickDisplayInterface;
  }
  return nullptr;
}

static const LV2_Descriptor brick_descriptor = {
    "https://github.com/tylorismusic-commits/brick",
    &lv2_brick_instantiate,
    &lv2_brick_connect_port,
    &lv2_brick_activate,
    &lv2_brick_run,
    &lv2_brick_deactivate,
    &lv2_brick_cleanup,
    &lv2_brick_extension_data,
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(
    uint32_t index) {
  if (index == 0) {
    return &brick_descriptor;
  }
  return nullptr;
}
