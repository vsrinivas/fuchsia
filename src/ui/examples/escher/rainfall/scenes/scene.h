// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_SCENE_H_
#define SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_SCENE_H_

#include "src/ui/examples/escher/rainfall/rainfall_demo.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/flatland/rectangle_renderable.h"

namespace escher {
class Stopwatch;
}

// Base scene class for Rainfall Demo Scenes. New scenes
// should inherit from this class.
class Scene {
 public:
  Scene(RainfallDemo* demo) : demo_(demo) {}
  virtual ~Scene() {}

  // Convenience method for initializing scene.
  virtual void Init() = 0;

  // Implementors use this to update the renderables in the scene per frame.
  virtual void Update(const escher::Stopwatch& stopwatch) = 0;

  virtual std::vector<escher::RectangleRenderable>& renderables() = 0;

 protected:
  const escher::VulkanContext& vulkan_context() const { return demo_->vulkan_context(); }
  escher::Escher* escher() { return demo_->escher(); }

  RainfallDemo* demo_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Scene);
};

#endif  // SRC_UI_EXAMPLES_ESCHER_RAINFALL_SCENES_SCENE_H_
