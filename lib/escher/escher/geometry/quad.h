// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "escher/geometry/types.h"

namespace escher {

class Quad {
 public:
  Quad();
  Quad(vec3 p0, vec3 p1, vec3 p2, vec3 p3);

  static Quad CreateFromRect(vec2 position, vec2 size, float z);
  static Quad CreateFillClipSpace(float z);

  const float* data() const { return reinterpret_cast<const float*>(this); }

  static const unsigned short* GetIndices();
  static int GetIndexCount();

 private:
  vec3 p[4] = {};
};

}  // namespace escher
