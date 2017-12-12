// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/geometry/types.h"

namespace escher {

// Returns a homogenized representation of the provided vector.
inline vec4 homogenize(const vec4& vector) {
  if (vector.w == 0.f) {
    return vector;
  }
  return vector / vector.w;
}

}  // namespace escher