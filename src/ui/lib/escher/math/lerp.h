// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_MATH_LERP_H_
#define SRC_UI_LIB_ESCHER_MATH_LERP_H_

namespace escher {

// Linearly interpolate between the two values.  ValT can be any type that
// supports addition, as well as multiplication by a scalar.
template <typename ValT>
ValT Lerp(const ValT& a, const ValT& b, float t) {
  return b * t + a * (1.f - t);
}

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_MATH_LERP_H_
