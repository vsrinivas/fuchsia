// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/yuv/yuv.h"

#include <algorithm>
#include <cmath>

namespace {

// Convert linear RGB values to sRGB. |in| is a float
// value between 0 and 1.
float LinearRgbToSrgb(float in) {
  if (in <= 0.0031308f) {
    return in * 12.92f;
  } else {
    return 1.055f * pow(in, 1.f / 2.4f) - 0.055f;
  }
}

uint8_t NormalizedFloatToUnsignedByte(float in) {
  int32_t out = static_cast<int32_t>(std::lround(in * 255.0f));
  return static_cast<uint8_t>(std::clamp(out, 0, 255));
}

}  // namespace

namespace yuv {

// Letting compiler decide whether to inline, for now.
void YuvToBgra(uint8_t y_raw, uint8_t u_raw, uint8_t v_raw, uint8_t* bgra) {
  // Convert from encoded space to normalized space assuming eItuNarrow.
  int32_t y = static_cast<int32_t>(y_raw) - 16;
  int32_t u = static_cast<int32_t>(u_raw) - 128;
  int32_t v = static_cast<int32_t>(v_raw) - 128;

  // Note: Normally, we would clamp here. But some drivers do not clamp in the
  // middle of their implementation, and this function is used for pixel tests.
  float fy = static_cast<float>(y) / 219.0f;
  float fu = static_cast<float>(u) / 224.0f;
  float fv = static_cast<float>(v) / 224.0f;

  // Convert from YUV to RGB using the coefficients for eYcbcr709.
  float r = fy + 1.5748f * fv;
  float g = fy - (0.13397432f / 0.7152f) * fu - (0.33480248f / 0.7152f) * fv;
  float b = fy + 1.8556f * fu;

  // Convert to sRGB, then store the value as unsigned bytes.
  bgra[0] = NormalizedFloatToUnsignedByte(LinearRgbToSrgb(b));  // blue
  bgra[1] = NormalizedFloatToUnsignedByte(LinearRgbToSrgb(g));  // green
  bgra[2] = NormalizedFloatToUnsignedByte(LinearRgbToSrgb(r));  // red
  bgra[3] = 0xff;                                               // alpha
}

}  // namespace yuv
