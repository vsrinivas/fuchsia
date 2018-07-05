// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/geometry/quad.h"

#include "lib/fxl/arraysize.h"

namespace escher {
namespace {

constexpr unsigned short g_indices[] = {
    0, 1, 2, 0, 2, 3,
};

}  // namespace

Quad::Quad() {}

Quad::Quad(vec3 p0, vec3 p1, vec3 p2, vec3 p3) {
  p[0] = p0;
  p[1] = p1;
  p[2] = p2;
  p[3] = p3;
}

Quad Quad::CreateFromRect(vec2 position, vec2 size, float z) {
  return Quad(vec3(position.x, position.y + size.y, z),
              vec3(position.x + size.x, position.y + size.y, z),
              vec3(position.x + size.x, position.y, z),
              vec3(position.x, position.y, z));
}

Quad Quad::CreateFillClipSpace(float z) {
  return CreateFromRect(vec2(-1.0f, 1.0f), vec2(2.0f, -2.0f), z);
}

const unsigned short* Quad::GetIndices() { return g_indices; }

int Quad::GetIndexCount() { return arraysize(g_indices); }

}  // namespace escher
