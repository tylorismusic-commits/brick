// Brick — DSP tests
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <vector>

#include "brick_strip.hpp"
#include "params.hpp"

using namespace brick;

static int g_failures = 0;

static void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::printf("FAIL: %s\n", what);
  }
}

static void fillSine(std::vector<float>& l, std::vector<float>& r, float freq,
                     float amp, float sr) {
  const double w = 2.0 * 3.14159265358979323846 * freq / sr;
  for (size_t i = 0; i < l.size(); ++i) {
    const float x = static_cast<float>(amp * std::sin(w * i));
    l[i] = x;
    r[i] = x;
  }
}

static float peakOf(const std::vector<float>& l, uint32_t start) {
  float peak = 0.0f;
  for (uint32_t i = start; i < l.size(); ++i) {
    peak = std::max(peak, std::fabs(l[i]));
  }
  return peak;
}

int main() {
  std::printf("Brick DSP tests\n");

  check(std::fabs(dbToGain(0.0f) - 1.0f) < 1.0e-6f, "dbToGain(0) == 1");
  check(std::fabs(gainToDb(0.5f) + 6.0206f) < 0.05f, "gainToDb(0.5)");

  // --- transparent limiting at -1 dBFS with a hot sine ---
  {
    BrickStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.inputGainDb = 6.0f;
    p.ceilingDb = -1.0f;
    p.releaseMs = 100.0f;
    p.lookaheadMs = 3.0f;
    p.link = 1.0f;
    p.truePeak = true;
    p.rmsMode = false;
    p.enabled = true;
    strip.setTargets(p);
    strip.reset();

    const uint32_t n = 48000;
    std::vector<float> l(n), r(n);
    fillSine(l, r, 1000.0f, 0.75f, 48000.0f);
    float gr = 0.0f, out = -60.0f, in = -60.0f, tp = -60.0f, rms = -60.0f;
    strip.process(l.data(), r.data(), l.data(), r.data(), n, gr, out, in, tp,
                  rms);
    const float peak = peakOf(l, 0);
    check(peak <= dbToGain(-1.0f) + 0.002f, "output respects the ceiling");
    check(peak > 0.80f, "output stays loud");
    check(gr < -1.0f, "limiter reports gain reduction");
    check(std::fabs(strip.latencySamples() - 240) < 1.0f,
          "latency is the fixed 5 ms lookahead window");
    check(std::isfinite(out) && std::isfinite(in) && std::isfinite(tp) &&
              std::isfinite(rms),
          "meters are finite");
    check(tp <= out + 3.0f, "true-peak meter is in a sane range");
  }

  // --- bypass is delay-aligned and clean ---
  {
    BrickStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.inputGainDb = 6.0f;
    p.ceilingDb = -6.0f;
    p.enabled = false;
    strip.setTargets(p);
    strip.reset();
    const uint32_t n = 9600;
    std::vector<float> l(n), r(n);
    fillSine(l, r, 1000.0f, 0.25f, 48000.0f);
    float gr = 0.0f, out = -60.0f, in = -60.0f, tp = -60.0f, rms = -60.0f;
    strip.process(l.data(), r.data(), l.data(), r.data(), n, gr, out, in, tp,
                  rms);
    const float peak = peakOf(l, 1000);
    check(peak > 0.24f && peak < 0.26f, "bypass passes the dry signal");
    check(std::fabs(gr) < 1.0e-6f, "bypass reports no gain reduction");
  }

  // --- true-peak detector catches inter-sample peaks ---
  {
    BrickStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.inputGainDb = 0.0f;
    p.ceilingDb = -6.0f;
    p.lookaheadMs = 5.0f;
    p.truePeak = true;
    strip.setTargets(p);
    strip.reset();

    // A high-frequency signal near Nyquist produces inter-sample peaks.
    const uint32_t n = 48000;
    std::vector<float> l(n), r(n);
    fillSine(l, r, 11000.0f, 0.9f, 48000.0f);
    float gr = 0.0f, out = -60.0f, in = -60.0f, tp = -60.0f, rms = -60.0f;
    strip.process(l.data(), r.data(), l.data(), r.data(), n, gr, out, in, tp,
                  rms);
    check(gr < -1.0f, "true-peak mode reduces the hot HF signal");
    check(tp <= dbToGain(-6.0f) + 1.0f, "true-peak meter is bounded");
  }

  // --- RMS mode and lookahead changes stay finite ---
  {
    BrickStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.inputGainDb = 4.0f;
    p.ceilingDb = -0.5f;
    p.releaseMs = 400.0f;
    p.lookaheadMs = 4.0f;
    p.rmsMode = true;
    p.truePeak = false;
    strip.setTargets(p);
    strip.reset();
    const uint32_t n = 48000;
    std::vector<float> l(n), r(n);
    fillSine(l, r, 220.0f, 0.9f, 48000.0f);
    float gr = 0.0f, out = -60.0f, in = -60.0f, tp = -60.0f, rms = -60.0f;
    strip.process(l.data(), r.data(), l.data(), r.data(), n, gr, out, in, tp,
                  rms);
    check(peakOf(l, 0) <= dbToGain(-0.5f) + 0.002f,
          "RMS mode still respects the ceiling");
    check(std::isfinite(gr) && std::isfinite(rms), "RMS mode is finite");
  }

  // --- extreme settings ---
  {
    BrickStrip strip;
    strip.prepare(48000.0f);
    Params p;
    p.inputGainDb = 20.0f;
    p.ceilingDb = -20.0f;
    p.releaseMs = 10.0f;
    p.lookaheadMs = 5.0f;
    p.truePeak = true;
    p.rmsMode = true;
    strip.setTargets(p);
    strip.reset();
    const uint32_t n = 4800;
    std::vector<float> l(n), r(n);
    uint32_t seed = 12345;
    for (uint32_t i = 0; i < n; ++i) {
      seed = seed * 1103515245u + 12345u;
      const float noise =
          static_cast<float>(static_cast<int>((seed >> 16) & 0x7fffu) - 4096) /
          4096.0f;
      l[i] = noise;
      r[i] = noise;
    }
    float gr = 0.0f, out = -60.0f, in = -60.0f, tp = -60.0f, rms = -60.0f;
    strip.process(l.data(), r.data(), l.data(), r.data(), n, gr, out, in, tp,
                  rms);
    check(std::isfinite(gr) && std::isfinite(out) && std::isfinite(in) &&
              std::isfinite(tp) && std::isfinite(rms),
          "extreme settings stay finite");
    check(peakOf(l, 0) <= dbToGain(-20.0f) + 0.002f,
          "extreme settings respect the ceiling");
  }

  // --- sample rates ---
  for (const float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
    BrickStrip strip;
    strip.prepare(sr);
    Params p;
    p.enabled = true;
    p.ceilingDb = -1.0f;
    p.lookaheadMs = 5.0f;
    strip.setTargets(p);
    strip.reset();
    const uint32_t n = 2048;
    std::vector<float> l(n, 0.5f), r(n, 0.5f);
    float gr = 0.0f, out = -60.0f, in = -60.0f, tp = -60.0f, rms = -60.0f;
    strip.process(l.data(), r.data(), l.data(), r.data(), n, gr, out, in, tp,
                  rms);
    check(std::isfinite(l[n - 1]) && std::isfinite(out),
          "finite at sample rate");
    check(strip.latencySamples() ==
              static_cast<int>(std::lround(0.005f * sr)),
          "latency scales with sample rate");
  }

  if (g_failures == 0) {
    std::printf("ALL DSP TESTS PASSED\n");
  }
  return g_failures == 0 ? 0 : 1;
}
