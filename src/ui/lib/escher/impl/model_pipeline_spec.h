// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_MODEL_PIPELINE_SPEC_H_
#define SRC_UI_LIB_ESCHER_IMPL_MODEL_PIPELINE_SPEC_H_

#include "src/ui/lib/escher/scene/shape.h"
#include "src/ui/lib/escher/shape/mesh_spec.h"

namespace escher {
namespace impl {

// Used to look up cached Vulkan pipelines that are compatible with the params.
#pragma pack(push, 1)  // As required by HashMapHasher<ModelPipelineSpec>
struct ModelPipelineSpec {
  enum class ClipperState {
    // The current object clips subsequent objects to its bounds, until the
    // original object is rendered again with |kEndClipChildren|.
    kBeginClipChildren = 1,
    // Clean up the clip region established by |kBeginClipChildren.
    kEndClipChildren,
    // This object rendered by this pipeline has no children to clip.
    kNoClipChildren
  };

  MeshSpec mesh_spec;
  // TODO: For now, there is only 1 material, so the ModelPipelineSpec doesn't
  // bother to mention anything about it.
  ClipperState clipper_state = ClipperState::kNoClipChildren;
  bool is_clippee = false;
  // Set to true if an object has a material.
  bool has_material = false;
  // Set to true if the object has an opaque material, and false if it has no
  // material or the material is not fully opaque.
  bool is_opaque = false;
  // Entirely disable depth test and depth write.
  bool disable_depth_test = false;
};
#pragma pack(pop)

// Inline function definitions.

inline bool operator==(const ModelPipelineSpec& spec1, const ModelPipelineSpec& spec2) {
  return spec1.mesh_spec == spec2.mesh_spec && spec1.clipper_state == spec2.clipper_state &&
         spec1.is_clippee == spec2.is_clippee && spec1.has_material == spec2.has_material &&
         spec1.is_opaque == spec2.is_opaque;
}

inline bool operator!=(const ModelPipelineSpec& spec1, const ModelPipelineSpec& spec2) {
  return !(spec1 == spec2);
}

}  // namespace impl

// Debugging.
ESCHER_DEBUG_PRINTABLE(impl::ModelPipelineSpec);
ESCHER_DEBUG_PRINTABLE(impl::ModelPipelineSpec::ClipperState);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_MODEL_PIPELINE_SPEC_H_
