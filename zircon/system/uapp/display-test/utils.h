// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

static inline uint32_t interpolate(uint32_t max, int32_t cur_frame, int32_t period) {
  float fraction = ((float)(cur_frame % period)) / ((float)period - 1);
  fraction = (cur_frame / period) % 2 ? 1.0f - fraction : fraction;
  return (uint32_t)((float)max * fraction);
}
