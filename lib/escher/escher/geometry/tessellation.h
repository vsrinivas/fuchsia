// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "escher/forward_declarations.h"
#include "escher/geometry/types.h"

namespace escher {

// Represents an indexed triangle mesh.  Holds the output from the tessellation
// functions below.  'normals' and 'uvs' may be empty, but otherwise they must
// must contain the same number of elements as 'positions'.
struct Tessellation {
  Tessellation();
  ~Tessellation();

  typedef vec3 position_type;
  typedef vec3 normal_type;
  typedef vec2 uv_type;

  std::vector<position_type> positions;
  std::vector<normal_type> normals;
  std::vector<uv_type> uvs;
  std::vector<uint16_t> indices;

  // Perform basic consistency checks.
  void SanityCheck() const;
};

// Tessellate a circle.  The coarsest circle (i.e. subdivisions == 0) is a
// square.
MeshPtr TessellateCircle(MeshBuilderFactory* factory,
                         const MeshSpec& spec,
                         int subdivisions,
                         vec2 center,
                         float radius);

// Tessellate a full-screen mesh.  The returned mesh has only position and UV
// coordinates.
MeshPtr NewFullScreenMesh(MeshBuilderFactory* factory);

}  // namespace escher
