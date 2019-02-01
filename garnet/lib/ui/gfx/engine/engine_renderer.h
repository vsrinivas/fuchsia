// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_ENGINE_RENDERER_H_
#define GARNET_LIB_UI_GFX_ENGINE_ENGINE_RENDERER_H_

#include "lib/escher/escher.h"

namespace scenic_impl {
namespace gfx {

class Layer;

// EngineRender knows how to render Scenic layers using escher::PaperRenderer.
class EngineRenderer {
 public:
  explicit EngineRenderer(escher::EscherWeakPtr weak_escher);
  ~EngineRenderer();

  // Use GPU to render all layers into separate images, and compose them all
  // into |output_image|.
  void RenderLayers(const escher::FramePtr& frame,
                    zx_time_t target_presentation_time,
                    const escher::ImagePtr& output_image,
                    const std::vector<Layer*>& layers);

 private:
  // Draws all the overlays to textures, which are then drawn using the
  // returned model. "Overlays" are all the layers except the bottom one.
  // NOTE: moved "as-is" from Compositor to ease CL review; big changes soon.
  std::unique_ptr<escher::Model> DrawOverlaysToModel(
      const std::vector<Layer*>& drawable_layers, const escher::FramePtr& frame,
      zx_time_t target_presentation_time);

  // NOTE: moved "as-is" from Compositor to ease CL review; big changes soon.
  void DrawLayer(const escher::FramePtr& frame,
                 zx_time_t target_presentation_time, Layer* layer,
                 const escher::ImagePtr& output_image,
                 const escher::Model* overlay_model);

  escher::ImagePtr GetLayerFramebufferImage(uint32_t width, uint32_t height);

  const escher::EscherWeakPtr escher_;
  escher::PaperRendererPtr paper_renderer_;
  escher::ShadowMapRendererPtr shadow_renderer_;
  std::unique_ptr<escher::hmd::PoseBufferLatchingShader>
      pose_buffer_latching_shader_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_ENGINE_RENDERER_H_
