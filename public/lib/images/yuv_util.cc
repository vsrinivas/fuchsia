// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "yuv_util.h"

namespace {

uint8_t clip(int in) {
  uint32_t out = in < 0 ? 0 : (uint32_t)in;
  return out > 255 ? 255 : (out & 0xff);
}

}  // namespace

namespace yuv_util {

// Letting compiler decide whether to inline, for now.
void YuvToBgra(uint8_t y_raw, uint8_t u_raw, uint8_t v_raw, uint8_t* bgra) {
  int32_t y = 298 * (static_cast<int32_t>(y_raw) - 16);
  int32_t u = static_cast<int32_t>(u_raw) - 128;
  int32_t v = static_cast<int32_t>(v_raw) - 128;
  bgra[0] = clip(((y + 516 * u + 128) / 256));            // blue
  bgra[1] = clip(((y - 208 * v - 100 * u + 128) / 256));  // green
  bgra[2] = clip(((y + 409 * v + 128) / 256));            // red
  bgra[3] = 0xff;                                         // alpha
}

}  // namespace yuv_util
