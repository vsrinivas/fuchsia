// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_GFX_CPP_COMPARE_H_
#define LIB_UI_GFX_CPP_COMPARE_H_

#include <fuchsia/cpp/gfx.h>

namespace gfx {

inline bool operator==(const Metrics& lhs, const Metrics& rhs) {
  return lhs.scale_x == rhs.scale_x && lhs.scale_y == rhs.scale_y &&
         lhs.scale_z == rhs.scale_z;
}

inline bool operator!=(const Metrics& lhs, const Metrics& rhs) {
  return !(lhs == rhs);
}

}  // namespace gfx

#endif  // LIB_UI_GFX_CPP_COMPARE_H_
