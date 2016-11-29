// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/material/color_utils.h"

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
}

vec3 LinearToSrgb(vec3 linear_color) {
  return vec3(LinearToSrgb(linear_color.x), LinearToSrgb(linear_color.y),
              LinearToSrgb(linear_color.z));
}

vec3 SrgbToLinear(vec3 srgb_color) {
  return vec3(SrgbToLinear(srgb_color.x), SrgbToLinear(srgb_color.y),
              SrgbToLinear(srgb_color.z));
}

}  // namespace escher
