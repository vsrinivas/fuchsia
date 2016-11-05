// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/shape/mesh_spec.h"

namespace escher {
namespace impl {

// TODO: For now, there is only 1 material, so the PipelineSpec doesn't
// bother to mention anything about it.
// TODO: Rename (ModelPipelineSpec?).  This is the spec for a particular kind of
// pipeline; there  will be other pipelines that are not created this way.
struct PipelineSpec {
  MeshSpec mesh_spec;

  struct Hash {
    std::size_t operator()(const PipelineSpec& spec) const {
      return static_cast<std::uint32_t>(spec.mesh_spec.flags);
    }
  };
};

// Inline function definitions.

inline bool operator==(const PipelineSpec& spec1, const PipelineSpec& spec2) {
  return spec1.mesh_spec == spec2.mesh_spec;
}

inline bool operator!=(const PipelineSpec& spec1, const PipelineSpec& spec2) {
  return !(spec1 == spec2);
}

}  // namespace impl
}  // namespace escher
