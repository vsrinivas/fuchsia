// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_SCENE_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_SCENE_H_

#include "garnet/examples/escher/common/demo.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"

namespace escher {
class Stopwatch;
}

class Scene {
 public:
  Scene(Demo* demo);
  virtual ~Scene();

  // Convenience method for initializing scene. Use this to create meshes,
  // materials, and other long-lived objects.
  // TODO(ES-155): deprecated, use PaperScene instead.
  virtual void Init(escher::Stage* stage) = 0;

  // Convenience method for initializing scene. Use this to create meshes,
  // materials, and other long-lived objects.
  // Default implementation invokes Init(Stage*);
  // TODO(ES-155): make this pure virtual when Init(Stage*) dies.
  virtual void Init(escher::PaperScene* scene);

  // Returns a |Model| for the specified time and frame_count, and gives
  // subclasses a chance to update properties on |stage| (mainly brightness).
  // The returned Model only needs to be valid for the duration of the
  // frame.
  // NOTE: this method signature allows the Scene to be used with both the
  // Waterfall and Waterfall2 demos, the former by iterating over the returned
  // Model, and the latter by pushing objects into |render_queue|.  In the
  // near-ish future, Waterfall will be deleted, and the |render_queue| argument
  // to this method will become non-optional.
  // TODO(ES-155): deprecated, use PaperScene instead.
  virtual escher::Model* Update(const escher::Stopwatch& stopwatch,
                                uint64_t frame_count, escher::Stage* stage,
                                escher::PaperRenderer* renderer = nullptr) = 0;

  // Default implementation delegates to the escher::Stage version.
  // TODO(ES-155): make this pure virtual when Init(Stage*) dies.
  virtual void Update(const escher::Stopwatch& stopwatch, uint64_t frame_count,
                      escher::PaperScene* scene,
                      escher::PaperRenderer* renderer);

  // Optionally returns a |Model| for the specified time, frame_count, and
  // screen dimensions.  The returned Model only needs to be valid for the
  // duration of the frame.
  virtual escher::Model* UpdateOverlay(const escher::Stopwatch& stopwatch,
                                       uint64_t frame_count, uint32_t width,
                                       uint32_t height) {
    return nullptr;
  }

 protected:
  const escher::VulkanContext& vulkan_context() const {
    return demo_->vulkan_context();
  }
  escher::Escher* escher() { return demo_->escher(); }

 private:
  Demo* demo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scene);
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL_SCENES_SCENE_H_
