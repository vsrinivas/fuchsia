// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_display_list_flags.h"
#include "lib/escher/impl/model_pipeline_cache.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/vk/texture.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {
namespace impl {

class ModelData;

// ModelRenderer is a subcomponent used by PaperRenderer.
class ModelRenderer final : public fxl::RefCountedThreadSafe<ModelRenderer> {
 public:
  static ModelRendererPtr New(Escher* escher, ModelDataPtr model_data);

  void Draw(const Stage& stage,
            const ModelDisplayListPtr& display_list,
            CommandBuffer* command_buffer);

  // Returns a single-pixel white texture.  Do with it what you will.
  const TexturePtr& white_texture() const { return white_texture_; }

  ResourceRecycler* resource_recycler() const { return resource_recycler_; }

  ModelDisplayListPtr CreateDisplayList(const Stage& stage,
                                        const Model& model,
                                        const Camera& camera,
                                        const ModelRenderPassPtr& render_pass,
                                        ModelDisplayListFlags flags,
                                        float scale,
                                        const TexturePtr& illumination_texture,
                                        CommandBuffer* command_buffer);

  const MeshPtr& GetMeshForShape(const Shape& shape) const;

 private:
  ModelRenderer(Escher* escher, ModelDataPtr model_data);
  ~ModelRenderer();

  Escher* const escher_;
  vk::Device device_;

  ResourceRecycler* const resource_recycler_;
  ModelDataPtr model_data_;

  MeshPtr CreateRectangle();
  MeshPtr CreateCircle();

  static TexturePtr CreateWhiteTexture(Escher* escher);

  MeshPtr rectangle_;
  MeshPtr circle_;

  TexturePtr white_texture_;

  FRIEND_REF_COUNTED_THREAD_SAFE(ModelRenderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(ModelRenderer);
};

}  // namespace impl
}  // namespace escher
