// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_MODEL_RENDERER_H_
#define LIB_ESCHER_IMPL_MODEL_RENDERER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_display_list_flags.h"
#include "lib/escher/impl/model_pipeline_cache.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/vk/texture.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {
namespace impl {

class ModelData;

// ModelRenderer is a subcomponent used by PaperRenderer.
class ModelRenderer final : public fxl::RefCountedThreadSafe<ModelRenderer> {
 public:
  static ModelRendererPtr New(EscherWeakPtr escher, ModelDataPtr model_data);

  void Draw(const Stage& stage, const ModelDisplayListPtr& display_list,
            CommandBuffer* command_buffer, const Camera::Viewport& viewport);

  // Returns a single-pixel white texture.  Do with it what you will.
  const TexturePtr& white_texture() const { return white_texture_; }

  ResourceRecycler* resource_recycler() const { return resource_recycler_; }

  ModelDisplayListPtr CreateDisplayList(
      const Stage& stage, const Model& model, const Camera& camera,
      const ModelRenderPassPtr& render_pass, ModelDisplayListFlags flags,
      float scale, const TexturePtr& shadow_texture, const mat4& shadow_matrix,
      vec3 ambient_light_color, vec3 direct_light_color,
      CommandBuffer* command_buffer);

  const MeshPtr& GetMeshForShape(const Shape& shape) const;

 private:
  ModelRenderer(EscherWeakPtr escher, ModelDataPtr model_data);
  ~ModelRenderer();

  const EscherWeakPtr escher_;
  vk::Device device_;

  ResourceRecycler* const resource_recycler_;
  ModelDataPtr model_data_;

  MeshPtr CreateRectangle();
  MeshPtr CreateCircle();

  static TexturePtr CreateWhiteTexture(Escher* escher);

  MeshPtr rectangle_;
  MeshPtr circle_;

  TexturePtr white_texture_;

  // Used to accumulate indices of objects in render order. Kept as an instance
  // field to reuse memory.
  // TODO(rosswang): maybe shrink to fit if capacity ≫ size after drawing.
  std::vector<uint32_t> opaque_objects_;
  // Used for semitransparent objects, sorted back-to-front.
  // TODO(jjosh): relax this ordering requirement in cases where we can prove
  //  that the semitransparent objects don't overlap.
  // TODO(rosswang): take advantage of relatively stable ordering in retained
  //  mode (bubble sort).
  // TODO(rosswang): This needs to be better factored with
  //  |ModelDisplayListBuilder|, as the latter handles all clip children. Having
  //  them separate allows for edge cases where clip groups with semitransparent
  //  geometry are not sorted against one another.
  std::vector<uint32_t> alpha_objects_;

  FRIEND_REF_COUNTED_THREAD_SAFE(ModelRenderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(ModelRenderer);
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_MODEL_RENDERER_H_
