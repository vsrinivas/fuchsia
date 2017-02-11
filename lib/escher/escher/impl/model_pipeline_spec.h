// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/shape/mesh_spec.h"
#include "escher/scene/shape.h"

namespace escher {
namespace impl {

// Used to look up cached Vulkan pipelines that are compatible with the params.
#pragma pack(push, 1)  // As required by escher::Hash<ModelPipelineSpec>
struct ModelPipelineSpec {
  MeshSpec mesh_spec;
  ShapeModifiers shape_modifiers;
  // TODO: For now, there is only 1 material, so the ModelPipelineSpec doesn't
  // bother to mention anything about it.
  uint32_t sample_count = 1;
  // TODO: this is a hack.
  bool use_depth_prepass = true;
};
#pragma pack(pop)

// Inline function definitions.

inline bool operator==(const ModelPipelineSpec& spec1,
                       const ModelPipelineSpec& spec2) {
  return spec1.mesh_spec == spec2.mesh_spec &&
         spec1.shape_modifiers == spec2.shape_modifiers &&
         spec1.sample_count == spec2.sample_count &&
         spec1.use_depth_prepass == spec2.use_depth_prepass;
}

inline bool operator!=(const ModelPipelineSpec& spec1,
                       const ModelPipelineSpec& spec2) {
  return !(spec1 == spec2);
}

}  // namespace impl

// Debugging.
ESCHER_DEBUG_PRINTABLE(impl::ModelPipelineSpec);

}  // namespace escher
