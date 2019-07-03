// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_SCENE_H_
#define SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_SCENE_H_

#include "src/ui/examples/escher/common/demo.h"
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
  virtual void Init(escher::PaperScene* scene) = 0;

  // Implementors draw the animated scene by issuing calls to |renderer|.  |BeginFrame()| has
  // already been invoked, and |EndFrame()| will be called after returning from this method.
  virtual void Update(const escher::Stopwatch& stopwatch, uint64_t frame_count,
                      escher::PaperScene* scene, escher::PaperRenderer* renderer) = 0;

  // Optionally returns a |Model| for the specified time, frame_count, and
  // screen dimensions.  The returned Model only needs to be valid for the
  // duration of the frame.
  virtual escher::Model* UpdateOverlay(const escher::Stopwatch& stopwatch, uint64_t frame_count,
                                       uint32_t width, uint32_t height) {
    return nullptr;
  }

 protected:
  const escher::VulkanContext& vulkan_context() const { return demo_->vulkan_context(); }
  escher::Escher* escher() { return demo_->escher(); }

 private:
  Demo* demo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scene);
};

#endif  // SRC_UI_EXAMPLES_ESCHER_WATERFALL_SCENES_SCENE_H_
