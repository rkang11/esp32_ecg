#pragma once
#include <math.h>
#include <stdint.h>

// All coefficients assume 250 Hz. Display filtering changes waveform shape;
// this is an educational trace, not a diagnostic ECG bandwidth.
struct EcgBiquad {
  EcgBiquad(float b0_, float b1_, float b2_, float a1_, float a2_)
      : b0(b0_), b1(b1_), b2(b2_), a1(a1_), a2(a2_) {}
  float b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
  float step(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void reset() { z1 = z2 = 0; }
};

class EcgProcessing {
 public:
  EcgProcessing(float mainsHz = 60) {
    const float w = 6.283185307f * mainsHz / 250;
    const float alpha = sinf(w) / 30; // notch Q=15
    notch = {1 / (1 + alpha), -2 * cosf(w) / (1 + alpha),
             1 / (1 + alpha), -2 * cosf(w) / (1 + alpha),
             (1 - alpha) / (1 + alpha)};
  }
  void reset(float raw, uint32_t now) {
    baseline = raw; clean = previous = detectorBaseline = 0;
    sum = oldEnergy = olderEnergy = 0; index = 0;
    for (float &v : energyWindow) v = 0;
    signal = 100; noise = 1; bpm = 0; intervals = 0;
    lastBeat = 0; started = now;
    notch.reset(); lowpass.reset();
  }
  bool step(float raw, uint32_t now) {
    baseline += 0.0124f * (raw - baseline);
    clean = lowpass.step(notch.step(raw - baseline));
    // Suppress slow P/T waves for detection, then integrate squared slope.
    detectorBaseline += 0.167f * (clean - detectorBaseline);
    float qrs = clean - detectorBaseline;
    float slope = qrs - previous; previous = qrs;
    sum += slope * slope - energyWindow[index];
    energyWindow[index] = slope * slope;
    index = (index + 1) % 25; // 100 ms integration merges Q/R/S lobes
    float energy = fmaxf(0, sum / 25);
    bool beat = false;
    if (oldEnergy > olderEnergy && oldEnergy >= energy) {
      float threshold = fmaxf(4, noise + 0.35f * (signal - noise));
      if (now - started < 2000) {
        signal = fmaxf(signal, oldEnergy);
      } else if (oldEnergy > threshold && (!lastBeat || now - lastBeat >= 300)) {
        signal = 0.875f * signal + 0.125f * oldEnergy;
        if (lastBeat) {
          uint32_t rr = now - lastBeat;
          if (rr <= 1500) {
            float instant = 60000.0f / rr;
            bpm = intervals == 0 ? instant : 0.75f * bpm + 0.25f * instant;
            ++intervals;
          } else { bpm = 0; intervals = 0; }
        }
        lastBeat = now; beat = true;
      } else {
        noise = 0.875f * noise + 0.125f * oldEnergy;
      }
    }
    olderEnergy = oldEnergy; oldEnergy = energy;
    if (lastBeat && now - lastBeat > 2000) {
      lastBeat = 0; bpm = 0; intervals = 0;
    }
    return beat;
  }
  float clean = 0, bpm = 0;
 private:
  EcgBiquad notch = {1, 0, 0, 0, 0};
  // Butterworth low-pass, 25 Hz at 250 samples/s.
  EcgBiquad lowpass = {0.06745527f, 0.13491055f, 0.06745527f,
                         -1.14298050f, 0.41280160f};
  float baseline = 2048, previous = 0, detectorBaseline = 0;
  float energyWindow[25] = {}, sum = 0, oldEnergy = 0, olderEnergy = 0;
  float signal = 100, noise = 1;
  unsigned index = 0, intervals = 0;
  uint32_t started = 0, lastBeat = 0;
};
