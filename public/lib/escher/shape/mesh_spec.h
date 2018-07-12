// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SHAPE_MESH_SPEC_H_
#define LIB_ESCHER_SHAPE_MESH_SPEC_H_

#include <string>
#include <vector>
// TODO: Consider defining MeshSpec without using Vulkan types.
#include <vulkan/vulkan.hpp>

namespace escher {

enum class MeshAttribute {
  // vec2.  Position of the vertex, to be transformed by model-view-projection
  // (MVP) matrix.
  kPosition2D = 1,
  // vec3.  Position of the vertex, to be transformed by model-view-projection
  // (MVP) matrix.
  kPosition3D = 1 << 1,
  // vec2.  Scalable position offset.  If this is present, add (some scaled
  // version of) this to the position attribute before multiplying by the
  // MVP matrix.
  kPositionOffset = 1 << 2,
  // vec2.  UV surface parameterization, often used as texture coordinates.
  kUV = 1 << 3,
  // float. Parameterization around the perimeter of an shape, which varies from
  // 0 - 1, and allows the vertex shader to know "where it is" on the shape.
  kPerimeterPos = 1 << 4,
  // Pseudo-attribute, used to obtain the vertex stride for the mesh.
  kStride = 1 << 5,
};

using MeshAttributes = vk::Flags<MeshAttribute, uint32_t>;

inline MeshAttributes operator|(MeshAttribute bit0, MeshAttribute bit1) {
  return MeshAttributes(bit0) | bit1;
}

// Return the per-vertex size of the specified attribute, as documented above
// (e.g. kPosition2D == sizeof(vec2)).
size_t GetMeshAttributeSize(MeshAttribute attr);

struct MeshSpec {
  MeshAttributes flags;

  struct HashMapHasher {
    std::size_t operator()(const MeshSpec& spec) const {
      return static_cast<std::uint32_t>(spec.flags);
    }
  };

  size_t GetNumAttributes() const;
  size_t GetAttributeOffset(MeshAttribute flag) const;
  size_t GetStride() const {
    return GetAttributeOffset(MeshAttribute::kStride);
  }

  bool IsValid() const;

  static constexpr size_t kIndexSize = sizeof(uint32_t);
};

// Inline function definitions.

inline bool operator==(const MeshSpec& spec1, const MeshSpec& spec2) {
  return spec1.flags == spec2.flags;
}

// Debugging.
std::ostream& operator<<(std::ostream& str, const MeshAttribute& attr);
std::ostream& operator<<(std::ostream& str, const MeshSpec& spec);

}  // namespace escher

#endif  // LIB_ESCHER_SHAPE_MESH_SPEC_H_
