// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/shape/mesh_spec.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/util/hasher.h"

namespace escher {

// If these assertions fail, code throughout this file will need to be updated
// to match the new invariants.
static_assert(sizeof(MeshAttributes) == sizeof(uint32_t), "sizeof mismatch");
static_assert(VulkanLimits::kNumVertexBuffers >= 2, "too few vertex buffers");

uint32_t GetMeshAttributeSize(MeshAttribute attr) {
  switch (attr) {
    case MeshAttribute::kPosition2D:
      return sizeof(vec2);
    case MeshAttribute::kPosition3D:
      return sizeof(vec3);
    case MeshAttribute::kPositionOffset:
      return sizeof(vec2);
    case MeshAttribute::kUV:
      return sizeof(vec2);
    case MeshAttribute::kPerimeterPos:
      return sizeof(float);
    case MeshAttribute::kBlendWeight1:
      return sizeof(float);
    case MeshAttribute::kStride:
      FXL_CHECK(false);
      return 0;
  }
}

uint32_t MeshSpec::attribute_count(uint32_t vertex_buffer_index) const {
  FXL_DCHECK(vertex_buffer_index < VulkanLimits::kNumVertexBuffers);
  return CountOnes(uint32_t(attributes[vertex_buffer_index]));
}

uint32_t MeshSpec::total_attribute_count() const {
  return CountOnes(uint32_t(all_attributes()));
}

uint32_t MeshSpec::attribute_offset(uint32_t vertex_buffer_index,
                                    MeshAttribute flag) const {
  FXL_DCHECK(vertex_buffer_index < VulkanLimits::kNumVertexBuffers);
  return GetMeshAttributeOffset(attributes[vertex_buffer_index], flag);
}

uint32_t GetMeshAttributeOffset(const MeshAttributes& attrs,
                                MeshAttribute attr) {
  FXL_DCHECK(attrs & attr || attr == MeshAttribute::kStride);
  uint32_t offset = 0;

  if (attr == MeshAttribute::kPosition2D) {
    return offset;
  } else if (attrs & MeshAttribute::kPosition2D) {
    offset += sizeof(vec2);
  }

  if (attr == MeshAttribute::kPosition3D) {
    return offset;
  } else if (attrs & MeshAttribute::kPosition3D) {
    offset += sizeof(vec3);
  }

  if (attr == MeshAttribute::kPositionOffset) {
    return offset;
  } else if (attrs & MeshAttribute::kPositionOffset) {
    offset += sizeof(vec2);
  }

  if (attr == MeshAttribute::kUV) {
    return offset;
  } else if (attrs & MeshAttribute::kUV) {
    offset += sizeof(vec2);
  }

  if (attr == MeshAttribute::kPerimeterPos) {
    return offset;
  } else if (attrs & MeshAttribute::kPerimeterPos) {
    offset += sizeof(float);
  }

  if (attr == MeshAttribute::kBlendWeight1) {
    return offset;
  } else if (attrs & MeshAttribute::kBlendWeight1) {
    offset += sizeof(float);
  }

  FXL_DCHECK(attr == MeshAttribute::kStride);
  return offset;
}

bool MeshSpec::has_attribute(uint32_t vertex_buffer_index,
                             MeshAttribute attr) const {
  FXL_DCHECK(vertex_buffer_index < VulkanLimits::kNumVertexBuffers);
  return bool(attributes[vertex_buffer_index] & attr);
}

bool MeshSpec::has_attributes(uint32_t vertex_buffer_index,
                              MeshAttributes attrs) const {
  FXL_DCHECK(vertex_buffer_index < VulkanLimits::kNumVertexBuffers);
  return (attributes[vertex_buffer_index] & attrs) == attrs;
}

MeshAttributes MeshSpec::all_attributes() const {
  MeshAttributes all(attributes[0]);
  for (uint32_t i = 1; i < VulkanLimits::kNumVertexBuffers; ++i) {
    FXL_DCHECK((all & attributes[i]) == MeshAttributes());
    all |= attributes[i];
  }
  return all;
}

uint32_t MeshSpec::vertex_buffer_count() const {
  uint32_t count = 0;
  for (auto& attrs : attributes) {
    count += uint32_t(attrs) ? 1 : 0;
  }
  return count;
}

bool MeshSpec::IsValid() const {
  MeshAttributes all_attrs = all_attributes();
  auto position_attrs = MeshAttribute::kPosition2D | MeshAttribute::kPosition3D;
  if (!(all_attrs & position_attrs)) {
    // Mesh must have a position attribute, either 2D or 3D.
    return false;
  } else if ((all_attrs & position_attrs) == position_attrs) {
    return false;
  } else {
    // Position attribute must always be in the first vertex buffer.
    return bool(attributes[0] & position_attrs);
  }
}

bool MeshSpec::IsValidOneBufferMesh() const {
  if (!IsValid()) {
    return false;
  } else if (attribute_count(0) != total_attribute_count()) {
    // Only the first vertex buffer is allowed to have any attributes.
    return false;
  } else if (has_attribute(0, MeshAttribute::kPosition3D) &&
             (has_attribute(0, MeshAttribute::kPositionOffset) ||
              has_attribute(0, MeshAttribute::kPerimeterPos))) {
    // Position-offset and perimeter attributes are only allowed for 2D meshes.
    // The latter inherently only makes sense for 2D, whereas the former could
    // be modified to support both 2D and 3D variants.
    return false;
  }
  return true;
}

std::size_t MeshSpec::HashMapHasher::operator()(const MeshSpec& spec) const {
  Hasher h;
  for (auto& attr : spec.attributes) {
    h.u32(uint32_t(attr));
  }
  return h.value().val;
}

}  // namespace escher
