// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_
#define GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_

#include <set>
#include <utility>

#include "garnet/lib/ui/gfx/resources/resource.h"
#include "garnet/lib/ui/gfx/swapchain/swapchain.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace escher {
class Escher;
class Frame;
class Image;
class Model;
class PaperRenderer;
class Semaphore;
class ShadowMapRenderer;
class Stage;
using EscherWeakPtr = fxl::WeakPtr<Escher>;
using FramePtr = fxl::RefPtr<Frame>;
using ImagePtr = fxl::RefPtr<Image>;
using SemaphorePtr = fxl::RefPtr<Semaphore>;
namespace hmd {
class PoseBufferLatchingShader;
}
}  // namespace escher

namespace scenic {
namespace gfx {

class FrameTimings;
class Layer;
class LayerStack;
class Scene;
class Swapchain;
using FrameTimingsPtr = fxl::RefPtr<FrameTimings>;
using LayerStackPtr = fxl::RefPtr<LayerStack>;

// A Compositor composes multiple layers into a single image.  This is intended
// to provide an abstraction that can make use of hardware overlay layers.
class Compositor : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ~Compositor() override;

  // SetLayerStackCmd.
  bool SetLayerStack(LayerStackPtr layer_stack);
  const LayerStackPtr& layer_stack() const { return layer_stack_; }

  // Add scenes in all layers to |scenes_out|.
  void CollectScenes(std::set<Scene*>* scenes_out);

  // Determine the appropriate order to render all layers, and then combine them
  // into a single output image.  Subclasses determine how to obtain and present
  // the output image.
  //
  // Returns true if at least one layer is drawn.
  bool DrawFrame(const FrameTimingsPtr& frame_timings,
                 escher::PaperRenderer* renderer,
                 escher::ShadowMapRenderer* shadow_renderer);

  // Determine the appropriate order to render all layers, and then combine them
  // into a single output image, which is drawn into |output_image|. When done,
  // signals |frame_done_semaphore|.
  void DrawToImage(escher::PaperRenderer* escher_renderer,
                   escher::ShadowMapRenderer* shadow_renderer,
                   const escher::ImagePtr& output_image,
                   const escher::SemaphorePtr& frame_done_semaphore);

  std::pair<uint32_t, uint32_t> GetBottomLayerSize() const;
  int GetNumDrawableLayers() const;

 protected:
  // Returns the list of drawable layers from the layer stack.
  std::vector<Layer*> GetDrawableLayers() const;

  escher::Escher* escher() const { return escher_.get(); }

  Compositor(Session* session, scenic::ResourceId id,
             const ResourceTypeInfo& type_info,
             std::unique_ptr<Swapchain> swapchain);

 private:
  // Draws all the overlays to textures, which are then drawn using the
  // returned model. "Overlays" are all the layers except the bottom one.
  std::unique_ptr<escher::Model> DrawOverlaysToModel(
      const std::vector<Layer*>& drawable_layers, const escher::FramePtr& frame,
      const FrameTimingsPtr& frame_timings,
      escher::PaperRenderer* escher_renderer,
      escher::ShadowMapRenderer* shadow_renderer);
  escher::ImagePtr GetLayerFramebufferImage(uint32_t width, uint32_t height);

  void DrawLayer(const escher::FramePtr& frame,
                 const FrameTimingsPtr& frame_timings,
                 escher::PaperRenderer* escher_renderer,
                 escher::ShadowMapRenderer* shadow_renderer, Layer* layer,
                 const escher::ImagePtr& output_image,
                 const escher::Model* overlay_model);

  const escher::EscherWeakPtr escher_;
  std::unique_ptr<Swapchain> swapchain_;
  LayerStackPtr layer_stack_;
  std::unique_ptr<escher::hmd::PoseBufferLatchingShader>
      pose_buffer_latching_shader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Compositor);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_COMPOSITOR_H_
