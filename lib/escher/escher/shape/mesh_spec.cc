// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shape/mesh_spec.h"

#include "escher/geometry/types.h"
#include "lib/ftl/logging.h"

namespace escher {

size_t GetMeshAttributeSize(MeshAttribute attr) {
  switch (attr) {
    case MeshAttribute::kPosition:
      return sizeof(vec2);
    case MeshAttribute::kPositionOffset:
      return sizeof(vec2);
    case MeshAttribute::kUV:
      return sizeof(vec2);
    case MeshAttribute::kPerimeterPos:
      return sizeof(float);
    case MeshAttribute::kStride:
      FTL_CHECK(false);
      return 0;
  }
}

size_t MeshSpec::GetAttributeOffset(MeshAttribute flag) const {
  FTL_DCHECK(flags & flag || flag == MeshAttribute::kStride);
  size_t offset = 0;

  if (flag == MeshAttribute::kPosition) {
    return offset;
  } else if (flags & MeshAttribute::kPosition) {
    offset += sizeof(vec2);
  }

  if (flag == MeshAttribute::kPositionOffset) {
    return offset;
  } else if (flags & MeshAttribute::kPositionOffset) {
    offset += sizeof(vec2);
  }

  if (flag == MeshAttribute::kUV) {
    return offset;
  } else if (flags & MeshAttribute::kUV) {
    offset += sizeof(vec2);
  }

  if (flag == MeshAttribute::kPerimeterPos) {
    return offset;
  } else if (flags & MeshAttribute::kPerimeterPos) {
    offset += sizeof(float);
  }

  FTL_DCHECK(flag == MeshAttribute::kStride);
  return offset;
}

}  // namespace escher
