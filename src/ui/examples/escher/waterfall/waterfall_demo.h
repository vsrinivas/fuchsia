// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_ESCHER_WATERFALL_WATERFALL_DEMO_H_
#define SRC_UI_EXAMPLES_ESCHER_WATERFALL_WATERFALL_DEMO_H_

#include <stdlib.h>

#include <cmath>
#include <iostream>

#include "src/lib/fxl/logging.h"
#include "src/ui/examples/escher/common/demo.h"
#include "src/ui/examples/escher/waterfall/scenes/scene.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/fs/hack_filesystem.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/util/stopwatch.h"

class WaterfallDemo : public Demo {
 public:
  static constexpr uint32_t kDemoWidth = 2160;
  static constexpr uint32_t kDemoHeight = 1440;

  enum ShadowMode {
    kNone,
    kShadowMap,
    kMomentShadowMap,
    kNumShadowModes,
  };

  WaterfallDemo(escher::EscherWeakPtr escher, int argc, char** argv);
  virtual ~WaterfallDemo();

  bool HandleKeyPress(std::string key) override;

  void DrawFrame(const escher::FramePtr& frame, const escher::ImagePtr& output_image,
                 const escher::SemaphorePtr& framebuffer_acquired) override;

  escher::PaperRenderer* renderer() const { return renderer_.get(); }

  // Return the list of MSAA sample counts which can be cycled through; these are known to be
  // supported by the current Vulkan device.
  const std::vector<uint8_t>& allowed_sample_counts() const { return allowed_sample_counts_; }

 private:
  void ProcessCommandLineArgs(int argc, char** argv);

  void SetWindowSize(vk::Extent2D window_size);

  void InitializeDemoScenes();
  void CycleNumLights();

  // See |animation_state_| comment below.
  void CycleAnimationState();

  escher::PaperRendererConfig renderer_config_;
  escher::PaperRendererPtr renderer_;

  vk::Extent2D window_size_ = {0, 0};
  escher::PaperScenePtr paper_scene_;

  // 4 camera projection modes:
  // - orthographic full-screen
  // - perspective where floor plane is full-screen, and parallel to screen
  // - perspective from tilted viewpoint (from x-center of stage).
  // - perspective from tilted viewpoint (from corner).
  int camera_projection_mode_ = 0;

  int current_scene_ = 0;
  std::vector<std::unique_ptr<Scene>> demo_scenes_;

  // 0 - both lights and objects animating.
  // 1 - only lights animating.
  // 2 - neither lights nor objects animating.
  uint32_t animation_state_ = 0;
  escher::Stopwatch object_stopwatch_;
  escher::Stopwatch lighting_stopwatch_;

  std::vector<uint8_t> allowed_sample_counts_;
  size_t current_sample_count_index_ = 0;

  // Toggle debug overlays.
  bool show_debug_info_ = false;
};

#endif  // SRC_UI_EXAMPLES_ESCHER_WATERFALL_WATERFALL_DEMO_H_
