// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/renderer.h"

namespace escher {

class DepthToColor;

class PaperRenderer : public Renderer {
 public:
  explicit PaperRenderer(Escher* escher);

  void DrawFrame(const Stage& stage,
                 const Model& model,
                 const Camera& camera,
                 const ImagePtr& color_image_out,
                 const Model* overlay_model,
                 const SemaphorePtr& frame_done,
                 FrameRetiredCallback frame_retired_callback);

  // Set whether one or more debug-overlays is to be show.
  void set_show_debug_info(bool b) { show_debug_info_ = b; }

  // Set whether SSDO lighting model is used.
  void set_enable_lighting(bool b) { enable_lighting_ = b; }

  // Set whether SSDO computation should be accelerated by generating a lookup
  // table each frame.
  void set_enable_ssdo_acceleration(bool b);

  // Set whether objects should be sorted by their pipeline, or rendered in the
  // order that they are provided by the caller.
  void set_sort_by_pipeline(bool b) { sort_by_pipeline_ = b; }

  // Cycle through the available SSDO acceleration modes.  This is a temporary
  // API: eventually there will only be one mode (the best one!), but this is
  // useful during development.
  void CycleSsdoAccelerationMode();

 private:
  ~PaperRenderer() override;

  static constexpr uint32_t kFramebufferColorAttachmentIndex = 0;
  static constexpr uint32_t kFramebufferDepthAttachmentIndex = 1;

  // Render pass that generates a depth buffer, but no color fragments.  The
  // resulting depth buffer is used by DrawSsdoPasses() in order to compute
  // per-pixel occlusion, and by DrawLightingPass().
  void DrawDepthPrePass(const ImagePtr& depth_image,
                        const ImagePtr& dummy_color_image,
                        const Stage& stage,
                        const Model& model,
                        const Camera& camera);

  // Multiple render passes.  The first samples the depth buffer to generate
  // per-pixel occlusion information, and subsequent passes filter this noisy
  // data.
  void DrawSsdoPasses(const ImagePtr& depth_in,
                      const ImagePtr& color_out,
                      const ImagePtr& color_aux,
                      const TexturePtr& accelerator_texture,
                      const Stage& stage);

  // Render pass that renders the fully-lit/shadowed scene.  Uses the depth
  // buffer from DrawDepthPrePass(), and the illumination texture from
  // DrawSsdoPasses().
  // TODO: on GPUs that use tiled rendering, it may be faster simply clear the
  // depth buffer instead of reusing the values from DrawDepthPrePass().  This
  // might save bandwidth at the cost of more per-fragment computation (but
  // the latter might be mitigated by sorting front-to-back, etc.).  Revisit
  // after doing performance profiling.
  void DrawLightingPass(uint32_t sample_count,
                        const FramebufferPtr& framebuffer,
                        const TexturePtr& illumination_texture,
                        const Stage& stage,
                        const Model& model,
                        const Camera& camera,
                        const Model* overlay_model);

  void DrawDebugOverlays(const ImagePtr& output,
                         const ImagePtr& depth,
                         const ImagePtr& illumination,
                         const TexturePtr& ssdo_accel,
                         const TexturePtr& ssdo_accel_depth);

  // Configure the renderer to use the specified output formats.
  void UpdateModelRenderer(vk::Format pre_pass_color_format,
                           vk::Format lighting_pass_color_format);

  MeshPtr full_screen_;
  impl::ImageCache* image_cache_;
  vk::Format depth_format_;
  std::unique_ptr<impl::ModelData> model_data_;
  std::unique_ptr<impl::ModelRenderer> model_renderer_;
  std::unique_ptr<impl::SsdoSampler> ssdo_;
  std::unique_ptr<impl::SsdoAccelerator> ssdo_accelerator_;
  std::unique_ptr<DepthToColor> depth_to_color_;
  std::vector<vk::ClearValue> clear_values_;
  bool show_debug_info_ = false;
  bool enable_lighting_ = true;
  bool sort_by_pipeline_ = true;

  FRIEND_REF_COUNTED_THREAD_SAFE(PaperRenderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(PaperRenderer);
};

}  // namespace escher
