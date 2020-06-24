// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_TESTING_UTILS_H_
#define SRC_GRAPHICS_DISPLAY_TESTING_UTILS_H_

#include <inttypes.h>

#include <cmath>

static inline uint32_t interpolate(uint32_t max, int32_t cur_frame, int32_t period) {
  float fraction = ((float)(cur_frame % period)) / ((float)period - 1);
  fraction = (cur_frame / period) % 2 ? 1.0f - fraction : fraction;
  return (uint32_t)((float)max * fraction);
}

// This function generates gamma lookup table using input gamma value.
static inline void generate_gamma_table(float gamma, float* v) {
  if (!v) {
    return;
  }
  for (int i = 0; i < 256; i++) {
    float num = static_cast<float>(i) / 255.0f;
    v[i] = std::powf(num, gamma);
  }
}

#endif  // SRC_GRAPHICS_DISPLAY_TESTING_UTILS_H_
