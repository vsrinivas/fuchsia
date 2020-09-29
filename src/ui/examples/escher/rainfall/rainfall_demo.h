// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_RAINFALL_RAINFALL_DEMO_H_
#define SRC_UI_EXAMPLES_ESCHER_RAINFALL_RAINFALL_DEMO_H_

#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>

#include <cmath>
#include <iostream>

#include "src/ui/examples/escher/common/demo.h"
#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/vk/shader_program.h"

class Scene;

class RainfallDemo : public Demo {
 public:
  static constexpr uint32_t kDemoWidth = 2160;
  static constexpr uint32_t kDemoHeight = 1140;

  RainfallDemo(escher::EscherWeakPtr escher, int argc, char** argv);
  virtual ~RainfallDemo();

  // |Demo|.
  bool HandleKeyPress(std::string key) override;

  // |Demo|.
  void DrawFrame(const escher::FramePtr& frame, const escher::ImagePtr& output_image,
                 const escher::SemaphorePtr& framebuffer_acquired) override;

  escher::Texture* default_texture() { return default_texture_.get(); }

 private:
  void SetWindowSize(vk::Extent2D window_size);
  void InitializeDemoScenes();

  std::unique_ptr<escher::RectangleCompositor> renderer_;
  vk::Extent2D window_size_ = {0, 0};

  escher::TexturePtr default_texture_;
  escher::TexturePtr depth_buffer_;

  uint32_t current_scene_ = 0;
  std::vector<Scene*> demo_scenes_;
  escher::Stopwatch stopwatch_;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_RAINFALL_RAINFALL_DEMO_H_
