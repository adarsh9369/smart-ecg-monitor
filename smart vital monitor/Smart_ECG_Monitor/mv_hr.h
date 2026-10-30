#ifndef MV_HR_H_
#define MV_HR_H_

#include <Arduino.h>

// =====================================================
// HEART RATE SMOOTHING
// heart_rate is already accurately averaged across recent
// beats in pulse_cal.h. hr_avg provides smooth transition.
// =====================================================
inline float hr_avg(float hr) {
  static float smoothed_bpm = 0.0f;

  if (hr <= 0.0f) {
    smoothed_bpm = 0.0f;
    return 0.0f;
  }

  if (smoothed_bpm <= 0.0f) {
    smoothed_bpm = hr; // Snap immediately to first valid reading
  } else {
    // 75% old, 25% new for quick, responsive display
    smoothed_bpm = 0.75f * smoothed_bpm + 0.25f * hr;
  }

  return smoothed_bpm;
}

inline void resetHrAvg() {
  hr_avg(0.0f);
}

#endif // MV_HR_H_
