// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_PAPER_RENDERER_H_
#define LIB_ESCHER_RENDERER_PAPER_RENDERER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/renderer/renderer.h"

namespace escher {

class DepthToColor;

enum class PaperRendererShadowType {
  kNone,
  kSsdo,
  kShadowMap,
  kMomentShadowMap,
};

class PaperRenderer : public Renderer {
 public:
  static fxl::RefPtr<PaperRenderer> New(EscherWeakPtr escher);

  void DrawFrame(const FramePtr& frame, const Stage& stage, const Model& model,
                 const Camera& camera, const ImagePtr& color_image_out,
                 const ShadowMapPtr& shadow_map, const Model* overlay_model);

  // Set whether one or more debug-overlays is to be show.
  void set_show_debug_info(bool b) { show_debug_info_ = b; }

  void set_shadow_type(PaperRendererShadowType shadow_type) {
    shadow_type_ = shadow_type;
  }

  // Set whether SSDO computation should be accelerated by generating a lookup
  // table each frame.
  void set_enable_ssdo_acceleration(bool b);

  // Set whether objects should be sorted by their pipeline, or rendered in the
  // order that they are provided by the caller.
  void set_sort_by_pipeline(bool b) { sort_by_pipeline_ = b; }

  // Set the color of the ambient light.
  void set_ambient_light_color(vec3 color) { ambient_light_color_ = color; }

  // Cycle through the available SSDO acceleration modes.  This is a temporary
  // API: eventually there will only be one mode (the best one!), but this is
  // useful during development.
  void CycleSsdoAccelerationMode();

  const impl::ModelDataPtr& model_data() const { return model_data_; }
  const impl::ModelRendererPtr& model_renderer() const {
    return model_renderer_;
  }

 private:
  PaperRenderer(EscherWeakPtr escher, impl::ModelDataPtr model_data);
  ~PaperRenderer() override;

  static constexpr uint32_t kFramebufferColorAttachmentIndex = 0;
  static constexpr uint32_t kFramebufferDepthAttachmentIndex = 1;

  void DrawFrameWithNoShadows(const FramePtr& frame, const Stage& stage,
                              const Model& model, const Camera& camera,
                              const ImagePtr& color_image_out,
                              const Model* overlay_model);

  void DrawFrameWithSsdoShadows(const FramePtr& frame, const Stage& stage,
                                const Model& model, const Camera& camera,
                                const ImagePtr& color_image_out,
                                const Model* overlay_model);

  void DrawFrameWithShadowMapShadows(const FramePtr& frame, const Stage& stage,
                                     const Model& model, const Camera& camera,
                                     const ImagePtr& color_image_out,
                                     const ShadowMapPtr& shadow_map,
                                     const Model* overlay_model);

  void DrawFrameWithMomentShadowMapShadows(
      const FramePtr& frame, const Stage& stage, const Model& model,
      const Camera& camera, const ImagePtr& color_image_out,
      const ShadowMapPtr& shadow_map, const Model* overlay_model);

  // Render pass that generates a depth buffer, but no color fragments.  The
  // resulting depth buffer is used by DrawSsdoPasses() in order to compute
  // per-pixel occlusion, and by DrawLightingPass().
  void DrawDepthPrePass(const FramePtr& frame, const ImagePtr& depth_image,
                        const ImagePtr& dummy_color_image, float scale,
                        const Stage& stage, const Model& model,
                        const Camera& camera);

  // Multiple render passes.  The first samples the depth buffer to generate
  // per-pixel occlusion information, and subsequent passes filter this noisy
  // data.
  void DrawSsdoPasses(const FramePtr& frame, const ImagePtr& depth_in,
                      const ImagePtr& color_out, const ImagePtr& color_aux,
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
  void DrawLightingPass(const FramePtr& frame, uint32_t sample_count,
                        const FramebufferPtr& framebuffer,
                        const TexturePtr& shadow_texture,
                        const mat4& shadow_matrix,
                        const vec3& ambient_light_color,
                        const vec3& direct_light_color,
                        const impl::ModelRenderPassPtr& render_pass,
                        const Stage& stage, const Model& model,
                        const Camera& camera, const Model* overlay_model);

  void DrawDebugOverlays(const FramePtr& frame, const ImagePtr& output,
                         const ImagePtr& depth, const ImagePtr& illumination,
                         const TexturePtr& ssdo_accel,
                         const TexturePtr& ssdo_accel_depth);

  // Configure the renderer to use the specified output formats.
  void UpdateRenderPasses(vk::Format pre_pass_color_format,
                          vk::Format lighting_pass_color_format);

  MeshPtr full_screen_;
  impl::ImageCache* image_cache_;
  impl::ModelRenderPassPtr depth_pass_;
  impl::ModelRenderPassPtr no_shadow_lighting_pass_;
  impl::ModelRenderPassPtr ssdo_lighting_pass_;
  impl::ModelRenderPassPtr shadow_map_lighting_pass_;
  impl::ModelRenderPassPtr moment_shadow_map_lighting_pass_;
  vk::Format depth_format_;
  impl::ModelDataPtr model_data_;
  impl::ModelRendererPtr model_renderer_;
  std::unique_ptr<impl::SsdoSampler> ssdo_;
  std::unique_ptr<impl::SsdoAccelerator> ssdo_accelerator_;
  std::unique_ptr<DepthToColor> depth_to_color_;
  std::vector<vk::ClearValue> clear_values_;
  vec3 ambient_light_color_;
  bool show_debug_info_ = false;
  bool sort_by_pipeline_ = true;
  PaperRendererShadowType shadow_type_ = PaperRendererShadowType::kSsdo;

  FRIEND_MAKE_REF_COUNTED(PaperRenderer);
  FRIEND_REF_COUNTED_THREAD_SAFE(PaperRenderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(PaperRenderer);
};

typedef fxl::RefPtr<PaperRenderer> PaperRendererPtr;

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_PAPER_RENDERER_H_
