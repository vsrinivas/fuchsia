// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>
// TODO: Consider defining MeshSpec without using Vulkan types.
#include <vulkan/vulkan.hpp>

namespace escher {

enum class MeshAttributeFlagBits {
  // vec2.  Position of the vertex, to be transformed by model-view-projection
  // (MVP) matrix.
  kPosition = 1,
  // vec2.  Per-vertex color.
  kColor = 1 << 1,
  // vec2.  UV surface parameterization, often used as texture coordinates.
  kUV = 1 << 2,
  // vec2.  Scalable position offset.  If this is present, add (some scaled
  // version of) this to the position attribute before multiplying by the
  // MVP matrix.
  kPositionOffset = 1 << 3,
};

using MeshAttributeFlags = vk::Flags<MeshAttributeFlagBits, uint32_t>;

struct MeshSpec {
  MeshAttributeFlags flags;

  struct Hash {
    std::size_t operator()(const MeshSpec& spec) const {
      return static_cast<std::uint32_t>(spec.flags);
    }
  };
};

// Inline function definitions.

inline bool operator==(const MeshSpec& spec1, const MeshSpec& spec2) {
  return spec1.flags == spec2.flags;
}

}  // namespace escher
