// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_COLOR_TRANSFORM_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_COLOR_TRANSFORM_H_

#include <array>

// This is a color transform in the format expected by the
// Fuchsia display controller API.  The equation is:
// matrix * (rgb + preoffsets) + postoffsets
struct ColorTransform {
  std::array<float, 3> preoffsets = {0, 0, 0};
  // Row-major 3x3 matrix.
  std::array<float, 9> matrix = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::array<float, 3> postoffsets = {0, 0, 0};
};

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_COLOR_TRANSFORM_H_
