// Brick — brick_strip.hpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "filters.hpp"
#include "params.hpp"
#include "smoother.hpp"
#include "util.hpp"

namespace brick {

// Monotonic sliding maximum.  Used for the lookahead peak detector.
class SlidingMax {
public:
  void prepare(int maxWindow) noexcept {
    maxWindow_ = std::max(2, maxWindow);
    values_.assign(static_cast<size_t>(maxWindow_ + 2), 0.0f);
    indices_.assign(static_cast<size_t>(maxWindow_ + 2), 0);
    capacity_ = maxWindow_ + 2;
    head_ = tail_ = count_ = 0;
    counter_ = 0;
    window_ = 1;
  }

  void reset() noexcept {
    head_ = tail_ = count_ = 0;
    counter_ = 0;
    std::fill(values_.begin(), values_.end(), 0.0f);
  }

  void setWindow(int window) noexcept {
    window_ = std::min(std::max(window, 1), maxWindow_);
  }

  inline void push(float value) noexcept {
    while (count_ > 0) {
      const int last = (tail_ + capacity_ - 1) % capacity_;
      if (values_[last] <= value) {
        tail_ = last;
        --count_;
      } else {
        break;
      }
    }
    values_[tail_] = value;
    indices_[tail_] = counter_;
    tail_ = (tail_ + 1) % capacity_;
    ++count_;

    while (count_ > 0 && indices_[head_] + window_ <= counter_) {
      head_ = (head_ + 1) % capacity_;
      --count_;
    }
    ++counter_;
  }

  float max() const noexcept {
    return count_ > 0 ? values_[head_] : 0.0f;
  }

private:
  std::vector<float> values_;
  std::vector<uint64_t> indices_;
  int maxWindow_ = 2;
  int capacity_ = 4;
  int head_ = 0;
  int tail_ = 0;
  int count_ = 0;
  int window_ = 1;
  uint64_t counter_ = 0;
};

// Four-times oversampled peak detector using Catmull-Rom interpolation.  It is
// deliberately detector-only: the audio path stays at the host sample rate and
// the resulting gain is applied through the lookahead delay.
class TruePeakDetector {
public:
  void reset() noexcept {
    for (float& x : history_) {
      x = 0.0f;
    }
    index_ = 0;
  }

  inline float process(float x) noexcept {
    history_[index_] = x;
    const int i0 = (index_ + 3) & 3;
    const int i1 = (index_ + 2) & 3;
    const int i2 = (index_ + 1) & 3;
    const int i3 = index_;
    index_ = (index_ + 1) & 3;

    const float p0 = history_[i0];
    const float p1 = history_[i1];
    const float p2 = history_[i2];
    const float p3 = history_[i3];

    float peak = std::max(std::fabs(p2), std::fabs(p3));
    for (int step = 1; step <= 4; ++step) {
      const float t = 0.25f * static_cast<float>(step);
      const float t2 = t * t;
      const float t3 = t2 * t;
      const float v = 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                              (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                              (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
      peak = std::max(peak, std::fabs(v));
    }
    return peak;
  }

private:
  float history_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  int index_ = 0;
};

class BrickStrip {
public:
  static constexpr float kMaxLookaheadMs = 5.0f;
  static constexpr float kRmsTimeMs = 80.0f;

  void prepare(float sampleRate) noexcept {
    sampleRate_ = sampleRate;
    maxDelay_ = std::max(1, static_cast<int>(std::lround(
                                  kMaxLookaheadMs * 0.001f * sampleRate)));
    delaySize_ = maxDelay_ + 4;
    delayL_.assign(static_cast<size_t>(delaySize_), 0.0f);
    delayR_.assign(static_cast<size_t>(delaySize_), 0.0f);
    dryL_.assign(static_cast<size_t>(delaySize_), 0.0f);
    dryR_.assign(static_cast<size_t>(delaySize_), 0.0f);
    maxLineL_.assign(static_cast<size_t>(delaySize_), 0.0f);
    maxLineR_.assign(static_cast<size_t>(delaySize_), 0.0f);
    write_ = 0;
    maxWrite_ = 0;

    inputGain_.setTime(20.0f, sampleRate);
    ceiling_.setTime(20.0f, sampleRate);
    release_.setTime(20.0f, sampleRate);
    lookahead_.setTime(20.0f, sampleRate);
    link_.setTime(20.0f, sampleRate);
    truePeak_.setTime(30.0f, sampleRate);
    rmsMode_.setTime(30.0f, sampleRate);
    enable_.setTime(10.0f, sampleRate);

    inputGain_.reset(1.0f);
    ceiling_.reset(dbToGain(-1.0f));
    release_.reset(100.0f);
    lookahead_.reset(3.0f);
    link_.reset(1.0f);
    truePeak_.reset(1.0f);
    rmsMode_.reset(0.0f);
    enable_.reset(1.0f);

    maxL_.prepare(maxDelay_ + 2);
    maxR_.prepare(maxDelay_ + 2);
    truePeakL_.reset();
    truePeakR_.reset();
    truePeakOutL_.reset();
    truePeakOutR_.reset();

    rmsCoeff_ = std::exp(-1.0f / (kRmsTimeMs * 0.001f * sampleRate));
    rmsInL_ = 0.0f;
    rmsInR_ = 0.0f;
    rmsOutL_ = 0.0f;
    rmsOutR_ = 0.0f;
    gainL_ = 1.0f;
    gainR_ = 1.0f;
    reset();
  }

  void setTargets(const Params& p) noexcept {
    params_ = p;
    inputGain_.setTarget(dbToGain(p.inputGainDb));
    ceiling_.setTarget(dbToGain(p.ceilingDb));
    release_.setTarget(p.releaseMs);
    lookahead_.setTarget(p.lookaheadMs);
    link_.setTarget(clamp(p.link, 0.0f, 1.0f));
    truePeak_.setTarget(p.truePeak ? 1.0f : 0.0f);
    rmsMode_.setTarget(p.rmsMode ? 1.0f : 0.0f);
    enable_.setTarget(p.enabled ? 1.0f : 0.0f);
  }

  void reset() noexcept {
    std::fill(delayL_.begin(), delayL_.end(), 0.0f);
    std::fill(delayR_.begin(), delayR_.end(), 0.0f);
    std::fill(dryL_.begin(), dryL_.end(), 0.0f);
    std::fill(dryR_.begin(), dryR_.end(), 0.0f);
    std::fill(maxLineL_.begin(), maxLineL_.end(), 0.0f);
    std::fill(maxLineR_.begin(), maxLineR_.end(), 0.0f);
    write_ = 0;
    maxWrite_ = 0;
    maxL_.reset();
    maxR_.reset();
    truePeakL_.reset();
    truePeakR_.reset();
    truePeakOutL_.reset();
    truePeakOutR_.reset();
    rmsInL_ = rmsInR_ = rmsOutL_ = rmsOutR_ = 0.0f;
    gainL_ = gainR_ = 1.0f;
    inputGain_.snap();
    ceiling_.snap();
    release_.snap();
    lookahead_.snap();
    link_.snap();
    truePeak_.snap();
    rmsMode_.snap();
    enable_.snap();
  }

  int latencySamples() const noexcept { return maxDelay_; }

  void process(const float* inLeft, const float* inRight, float* outLeft,
               float* outRight, uint32_t frames, float& grDb, float& outDb,
               float& inDb, float& truePeakDb, float& rmsDb) noexcept {
    float lastGr = 0.0f;
    float peakIn = 0.0f;
    float peakOut = 0.0f;
    float truePeakOut = 0.0f;
    float rmsOut = 0.0f;

    for (uint32_t i = 0; i < frames; ++i) {
      const float inGain = inputGain_.next();
      const float ceiling = ceiling_.next();
      const float releaseMs = release_.next();
      const float lookMs = lookahead_.next();
      const float link = link_.next();
      const float tp = truePeak_.next();
      const float rmsMode = rmsMode_.next();
      const float enable = enable_.next();

      const float rawL = inLeft[i];
      const float rawR = inRight[i];
      peakIn = std::max(peakIn, std::max(std::fabs(rawL), std::fabs(rawR)));

      const float xL = rawL * inGain;
      const float xR = rawR * inGain;

      const float samplePeakL = std::fabs(xL);
      const float samplePeakR = std::fabs(xR);
      const float tpL = truePeakL_.process(xL);
      const float tpR = truePeakR_.process(xR);
      const float detL = samplePeakL + tp * (tpL - samplePeakL);
      const float detR = samplePeakR + tp * (tpR - samplePeakR);

      const int lookSamples = std::min(
          maxDelay_, std::max(0, static_cast<int>(std::lround(
                                     lookMs * 0.001f * sampleRate_))));
      maxL_.setWindow(lookSamples + 1);
      maxR_.setWindow(lookSamples + 1);
      maxL_.push(detL);
      maxR_.push(detR);

      const float windowMaxL = maxL_.max();
      const float windowMaxR = maxR_.max();
      const int maxLineDelay = maxDelay_ - lookSamples;
      maxLineL_[maxWrite_] = windowMaxL;
      maxLineR_[maxWrite_] = windowMaxR;
      int maxRead = maxWrite_ - maxLineDelay;
      if (maxRead < 0) {
        maxRead += delaySize_;
      }
      const float peakL = maxLineL_[maxRead];
      const float peakR = maxLineR_[maxRead];
      maxWrite_ = (maxWrite_ + 1) % delaySize_;

      rmsInL_ = rmsCoeff_ * rmsInL_ + (1.0f - rmsCoeff_) * xL * xL;
      rmsInR_ = rmsCoeff_ * rmsInR_ + (1.0f - rmsCoeff_) * xR * xR;
      const float rmsL = std::sqrt(std::max(rmsInL_, 1.0e-20f));
      const float rmsR = std::sqrt(std::max(rmsInR_, 1.0e-20f));

      const float ceilingSafe = std::max(ceiling, 1.0e-6f);
      const float peakTargetL = std::min(1.0f, ceilingSafe / std::max(peakL, ceilingSafe));
      const float peakTargetR = std::min(1.0f, ceilingSafe / std::max(peakR, ceilingSafe));
      const float rmsTargetL = std::min(1.0f, ceilingSafe / std::max(rmsL, ceilingSafe));
      const float rmsTargetR = std::min(1.0f, ceilingSafe / std::max(rmsR, ceilingSafe));

      const float envTargetL = peakTargetL + rmsMode * (rmsTargetL - peakTargetL);
      const float envTargetR = peakTargetR + rmsMode * (rmsTargetR - peakTargetR);
      const float attackMs = 0.05f + 5.0f * rmsMode;
      const float attackCoeff = std::exp(-1.0f / std::max(1.0f, attackMs * 0.001f * sampleRate_));
      const float releaseCoeff = std::exp(-1.0f / std::max(1.0f, releaseMs * 0.001f * sampleRate_));
      if (envTargetL < gainL_) {
        gainL_ = attackCoeff * gainL_ + (1.0f - attackCoeff) * envTargetL;
      } else {
        gainL_ = releaseCoeff * gainL_ + (1.0f - releaseCoeff) * envTargetL;
      }
      if (envTargetR < gainR_) {
        gainR_ = attackCoeff * gainR_ + (1.0f - attackCoeff) * envTargetR;
      } else {
        gainR_ = releaseCoeff * gainR_ + (1.0f - releaseCoeff) * envTargetR;
      }
      // In peak mode the gain is clamped to the peak target.  In RMS mode the
      // smoother RMS envelope is allowed to breathe and the final ceiling clip
      // catches the remaining short peaks.
      const float peakClampL = peakTargetL + rmsMode * (1.0f - peakTargetL);
      const float peakClampR = peakTargetR + rmsMode * (1.0f - peakTargetR);
      gainL_ = std::min(gainL_, peakClampL);
      gainR_ = std::min(gainR_, peakClampR);

      const float linkedGain = std::min(gainL_, gainR_);
      const float outGainL = gainL_ + link * (linkedGain - gainL_);
      const float outGainR = gainR_ + link * (linkedGain - gainR_);

      delayL_[write_] = xL;
      delayR_[write_] = xR;
      dryL_[write_] = rawL;
      dryR_[write_] = rawR;
      int read = write_ - maxDelay_;
      if (read < 0) {
        read += delaySize_;
      }
      float finalL = delayL_[read] * outGainL;
      float finalR = delayR_[read] * outGainR;
      const float dryReadL = dryL_[read];
      const float dryReadR = dryR_[read];
      write_ = (write_ + 1) % delaySize_;

      // Safety ceiling.  The lookahead path should make this almost never
      // engage, but it guarantees the selected ceiling.
      finalL = std::min(std::max(finalL, -ceilingSafe), ceilingSafe);
      finalR = std::min(std::max(finalR, -ceilingSafe), ceilingSafe);

      const float actualL = dryReadL + enable * (finalL - dryReadL);
      const float actualR = dryReadR + enable * (finalR - dryReadR);
      outLeft[i] = actualL;
      outRight[i] = actualR;

      const float outTrueL = truePeakOutL_.process(actualL);
      const float outTrueR = truePeakOutR_.process(actualR);
      rmsOutL_ = rmsCoeff_ * rmsOutL_ + (1.0f - rmsCoeff_) * actualL * actualL;
      rmsOutR_ = rmsCoeff_ * rmsOutR_ + (1.0f - rmsCoeff_) * actualR * actualR;

      peakOut = std::max(peakOut, std::max(std::fabs(actualL), std::fabs(actualR)));
      truePeakOut = std::max(truePeakOut, std::max(outTrueL, outTrueR));
      rmsOut = std::max(rmsOut, std::max(std::sqrt(std::max(rmsOutL_, 1.0e-20f)),
                                         std::sqrt(std::max(rmsOutR_, 1.0e-20f))));

      lastGr = enable > 0.5f ? gainToDb(std::min(outGainL, outGainR)) : 0.0f;
    }

    grDb = lastGr;
    outDb = clamp(gainToDb(peakOut), -60.0f, 6.0f);
    inDb = clamp(gainToDb(peakIn), -60.0f, 6.0f);
    truePeakDb = clamp(gainToDb(truePeakOut), -60.0f, 6.0f);
    rmsDb = clamp(gainToDb(rmsOut), -60.0f, 6.0f);
  }

private:
  float sampleRate_ = 48000.0f;
  Params params_;
  int maxDelay_ = 1;
  int delaySize_ = 8;
  int write_ = 0;
  int maxWrite_ = 0;
  std::vector<float> delayL_;
  std::vector<float> delayR_;
  std::vector<float> dryL_;
  std::vector<float> dryR_;
  std::vector<float> maxLineL_;
  std::vector<float> maxLineR_;

  SlidingMax maxL_;
  SlidingMax maxR_;
  TruePeakDetector truePeakL_;
  TruePeakDetector truePeakR_;
  TruePeakDetector truePeakOutL_;
  TruePeakDetector truePeakOutR_;

  Smoothed inputGain_;
  Smoothed ceiling_;
  Smoothed release_;
  Smoothed lookahead_;
  Smoothed link_;
  Smoothed truePeak_;
  Smoothed rmsMode_;
  Smoothed enable_;

  float rmsCoeff_ = 0.0f;
  float rmsInL_ = 0.0f;
  float rmsInR_ = 0.0f;
  float rmsOutL_ = 0.0f;
  float rmsOutR_ = 0.0f;
  float gainL_ = 1.0f;
  float gainR_ = 1.0f;
};

} // namespace brick
