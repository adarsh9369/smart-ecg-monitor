#ifndef MV_HR_H_
#define MV_HR_H_

#include <Arduino.h>

inline float hr_avg(float hr) {
  static float smoothed_bpm = 0.0f;

  if (hr <= 0.0f) {
    smoothed_bpm = 0.0f;
    return 0.0f;
  }

  if (smoothed_bpm <= 0.0f) {
    smoothed_bpm = hr;
  } else {
    smoothed_bpm = 0.75f * smoothed_bpm + 0.25f * hr;
  }

  return smoothed_bpm;
}

inline void resetHrAvg() {
  hr_avg(0.0f);
}

#endif // MV_HR_H_
