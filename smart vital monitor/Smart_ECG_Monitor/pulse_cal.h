#ifndef PULSE_CAL_H_
#define PULSE_CAL_H_

#include <Arduino.h>

// =====================================================
// ADAPTIVE R-PEAK & HEART RATE DETECTOR
// =====================================================

// Adaptive detection thresholds
static float adaptive_threshold = 25.0f;
static float peak_max = 0.0f;
static bool peak_detecting = false;
static unsigned long last_beat_time = 0;
static const unsigned long REFRACTORY_PERIOD_MS = 250; // Max human HR = 240 BPM (250ms)

// Circular buffer of the last 5 heartbeats for immediate, stable averaging
static const int BEAT_HISTORY_SIZE = 5;
static int beat_history[BEAT_HISTORY_SIZE];
static int beat_count = 0;
static int beat_index = 0;

int heart_rate = 0;

inline void hr_calculation(float filter_pulse_e) {
  unsigned long now = millis();

  // If no beat detected for > 3.5 seconds (flatline or electrode removed), reset HR to 0
  if (last_beat_time > 0 && (now - last_beat_time > 3500)) {
    heart_rate = 0;
    beat_count = 0;
    adaptive_threshold = 25.0f;
  }

  // Refractory lockout: ignore T-waves or noise within 250ms of the last R-peak
  if (now - last_beat_time < REFRACTORY_PERIOD_MS) {
    return;
  }

  // Detect rising edge crossing adaptive threshold
  if (!peak_detecting && (filter_pulse_e > adaptive_threshold)) {
    peak_detecting = true;
    peak_max = filter_pulse_e;
  }
  // While above threshold, track the maximum crest of the R-wave
  else if (peak_detecting) {
    if (filter_pulse_e > peak_max) {
      peak_max = filter_pulse_e;
    }
    // When signal drops below 60% of crest or 80% of threshold, R-peak is confirmed!
    else if (filter_pulse_e < (peak_max * 0.60f) || filter_pulse_e < (adaptive_threshold * 0.80f)) {
      peak_detecting = false;

      if (last_beat_time > 0) {
        unsigned long rr_interval = now - last_beat_time;

        // Valid physiological human RR interval: 300ms (200 BPM) to 1500ms (40 BPM)
        if (rr_interval >= 300 && rr_interval <= 1500) {
          int instant_bpm = (int)(60000UL / rr_interval);

          // Store in circular buffer of actual heartbeats
          beat_history[beat_index] = instant_bpm;
          beat_index = (beat_index + 1) % BEAT_HISTORY_SIZE;
          if (beat_count < BEAT_HISTORY_SIZE) {
            beat_count++;
          }

          // Calculate moving average across recent heartbeats
          long sum_beats = 0;
          for (int i = 0; i < beat_count; i++) {
            sum_beats += beat_history[i];
          }
          heart_rate = (int)(sum_beats / beat_count);

          // Dynamically adjust threshold to 55% of this confirmed peak
          adaptive_threshold = peak_max * 0.55f;
          if (adaptive_threshold < 15.0f) adaptive_threshold = 15.0f;
          if (adaptive_threshold > 250.0f) adaptive_threshold = 250.0f;
        }
      }

      last_beat_time = now;
    }
  }

  // Gentle decay of threshold if ECG amplitude decreases
  static unsigned long last_decay_time = 0;
  if (now - last_decay_time >= 100) {
    last_decay_time = now;
    if (!peak_detecting && adaptive_threshold > 18.0f) {
      adaptive_threshold *= 0.99f;
    }
  }
}

inline void resetPulseCal() {
  adaptive_threshold = 25.0f;
  peak_max = 0.0f;
  peak_detecting = false;
  last_beat_time = 0;
  beat_count = 0;
  beat_index = 0;
  heart_rate = 0;
}

#endif // PULSE_CAL_H_
