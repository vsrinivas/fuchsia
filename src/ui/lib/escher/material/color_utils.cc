// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/material/color_utils.h"

namespace escher {

namespace {
float LinearToSrgb(float linear_color_channel) {
  if (linear_color_channel <= 0.00313066844250063f) {
    return linear_color_channel * 12.92f;
  } else {
    return 1.055f * pow(linear_color_channel, 1 / 2.4f) - 0.055f;
  }
}

float SrgbToLinear(float srgb_color_channel) {
  if (srgb_color_channel <= 0.0404482362771082f) {
    return srgb_color_channel / 12.92f;
  } else {
    return pow((srgb_color_channel + 0.055f) / 1.055f, 2.4f);
  }
}
}  // namespace

vec3 LinearToSrgb(vec3 linear_color) {
  return vec3(LinearToSrgb(linear_color.x), LinearToSrgb(linear_color.y),
              LinearToSrgb(linear_color.z));
}

vec3 SrgbToLinear(vec3 srgb_color) {
  return vec3(SrgbToLinear(srgb_color.x), SrgbToLinear(srgb_color.y), SrgbToLinear(srgb_color.z));
}

vec3 HsvToLinear(vec3 hsv_color) {
  float h = hsv_color.x;
  float s = hsv_color.y;
  float v = hsv_color.z;
  float r, g, b;

  float chroma = s * v;
  float h_prime = fmodf(h / 60.0f, 6);
  float x = chroma * (1.f - fabsf(fmodf(h_prime, 2) - 1.f));
  float m = v - chroma;

  if (h_prime < 1) {
    r = chroma;
    g = x;
    b = 0;
  } else if (1 <= h_prime && h_prime < 2) {
    r = x;
    g = chroma;
    b = 0;
  } else if (2 <= h_prime && h_prime < 3) {
    r = 0;
    g = chroma;
    b = x;
  } else if (3 <= h_prime && h_prime < 4) {
    r = 0;
    g = x;
    b = chroma;
  } else if (4 <= h_prime && h_prime < 5) {
    r = x;
    g = 0;
    b = chroma;
  } else if (5 <= h_prime && h_prime < 6) {
    r = chroma;
    g = 0;
    b = x;
  } else {
    r = 0;
    g = 0;
    b = 0;
  }

  r += m;
  g += m;
  b += m;

  return vec3(r, g, b);
}

}  // namespace escher
