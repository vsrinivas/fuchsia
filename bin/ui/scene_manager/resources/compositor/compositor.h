// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene_manager/engine/swapchain.h"
#include "apps/mozart/src/scene_manager/resources/resource.h"

namespace escher {
class Escher;
class Image;
class Model;
class PaperRenderer;
class Semaphore;
class Stage;
using ImagePtr = ftl::RefPtr<Image>;
using SemaphorePtr = ftl::RefPtr<Semaphore>;
}  // namespace escher

namespace scene_manager {

class FrameTimings;
class Layer;
class LayerStack;
class Scene;
class Swapchain;
using FrameTimingsPtr = ftl::RefPtr<FrameTimings>;
using LayerStackPtr = ftl::RefPtr<LayerStack>;

// A Compositor composes multiple layers into a single image.  This is intended
// to provide an abstraction that can make use of hardware overlay layers.
class Compositor : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ~Compositor() override;

  // SetLayerStackOp.
  bool SetLayerStack(LayerStackPtr layer_stack);
  const LayerStackPtr& layer_stack() const { return layer_stack_; }

  // Add scenes in all layers to |scenes_out|.
  void CollectScenes(std::set<Scene*>* scenes_out);

  // Determine the appropriate order to render all layers, and then combine them
  // into a single output image.  Subclasses determine how to obtain and present
  // the output image.
  void DrawFrame(const FrameTimingsPtr& frame_timings,
                 escher::PaperRenderer* renderer);

 protected:
  escher::Escher* escher() const { return escher_; }

  Compositor(Session* session,
             scenic::ResourceId id,
             const ResourceTypeInfo& type_info,
             std::unique_ptr<Swapchain> swapchain);

 private:
  escher::ImagePtr GetLayerFramebufferImage(uint32_t width, uint32_t height);

  void InitStage(escher::Stage* stage, uint32_t width, uint32_t height);
  void DrawLayer(escher::PaperRenderer* escher_renderer,
                 Layer* layer,
                 const escher::ImagePtr& output_image,
                 const escher::SemaphorePtr& frame_done_semaphore,
                 const escher::Model* overlay_model);

  escher::Escher* const escher_;
  std::unique_ptr<Swapchain> swapchain_;
  LayerStackPtr layer_stack_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Compositor);
};

}  // namespace scene_manager
