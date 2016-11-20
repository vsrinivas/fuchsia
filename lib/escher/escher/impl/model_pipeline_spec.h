// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/shape/mesh_spec.h"
#include "escher/scene/shape.h"

namespace escher {
namespace impl {

// TODO: For now, there is only 1 material, so the ModelPipelineSpec doesn't
// bother to mention anything about it.
struct ModelPipelineSpec {
  MeshSpec mesh_spec;
  ShapeModifiers shape_modifiers;

  // TODO: this is a hack.
  bool use_depth_prepass = true;

  struct Hash {
    // TODO: this hash should work fine for now, but feels cheezy.
    std::size_t operator()(const ModelPipelineSpec& spec) const {
      return 23 * (MeshSpec::Hash()(spec.mesh_spec) +
                   19 * static_cast<uint32_t>(spec.shape_modifiers)) +
             static_cast<std::uint32_t>(spec.use_depth_prepass);
    }
  };
};

// Inline function definitions.

inline bool operator==(const ModelPipelineSpec& spec1,
                       const ModelPipelineSpec& spec2) {
  return spec1.mesh_spec == spec2.mesh_spec &&
         spec1.shape_modifiers == spec2.shape_modifiers &&
         spec1.use_depth_prepass == spec2.use_depth_prepass;
}

inline bool operator!=(const ModelPipelineSpec& spec1,
                       const ModelPipelineSpec& spec2) {
  return !(spec1 == spec2);
}

}  // namespace impl
}  // namespace escher
