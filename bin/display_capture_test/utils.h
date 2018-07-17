// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DISPLAY_CAPTURE_TEST_UTILS_H_
#define GARNET_BIN_DISPLAY_CAPTURE_TEST_UTILS_H_

#include <fbl/algorithm.h>
#include <inttypes.h>

namespace display_test {
namespace internal {

// Premultiplies the color channels of |val| with |alpha|. Does not touch
// the alpha channel of |val|.
static uint32_t premultiply_color_channels(uint32_t val, uint8_t alpha) {
  uint32_t ret = val & 0xff000000;

  constexpr uint32_t offsets[3] = {0, 8, 16};
  for (unsigned i = 0; i < fbl::count_of(offsets); i++) {
    uint32_t comp_value = (val >> offsets[i]) & 0xff;
    comp_value = ((comp_value * alpha) + 254) >> 8;
    ret |= (comp_value << offsets[i]);
  }

  return ret;
}

}  // namespace internal
}  // namespace display_test

#endif  // GARNET_BIN_DISPLAY_CAPTURE_TEST_UTILS_H_
