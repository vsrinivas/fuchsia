// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_DEMO_H_
#define GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_DEMO_H_

#include <stdlib.h>
#include <cmath>
#include <iostream>

#include "garnet/examples/escher/common/demo.h"
#include "garnet/examples/escher/common/demo_harness.h"
#include "garnet/examples/escher/waterfall/scenes/scene.h"

#include "lib/escher/escher.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/fs/hack_filesystem.h"
#include "lib/escher/paper/paper_renderer2.h"
#include "lib/escher/util/stopwatch.h"
#include "src/lib/fxl/logging.h"

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

  WaterfallDemo(DemoHarness* harness, int argc, char** argv);
  virtual ~WaterfallDemo();

  bool HandleKeyPress(std::string key) override;

  void DrawFrame(const escher::FramePtr& frame,
                 const escher::ImagePtr& output_image) override;

 private:
  void ProcessCommandLineArgs(int argc, char** argv);

  void InitializePaperScene(const DemoHarness::WindowParams& window_params);
  void InitializeDemoScenes();

  escher::PaperRendererConfig renderer_config_;
  escher::PaperRenderer2Ptr renderer_;

  escher::PaperScenePtr paper_scene_;

  // 4 camera projection modes:
  // - orthographic full-screen
  // - perspective where floor plane is full-screen, and parallel to screen
  // - perspective from tilted viewpoint (from x-center of stage).
  // - perspective from tilted viewpoint (from corner).
  int camera_projection_mode_ = 0;

  int current_scene_ = 0;
  std::vector<std::unique_ptr<Scene>> demo_scenes_;

  // Used for FPS calculations and animating lighting params.
  escher::Stopwatch stopwatch_;
  // Used for animating object shapes and positions.
  escher::Stopwatch animation_stopwatch_;

  uint64_t frame_count_ = 0;
  uint64_t first_frame_microseconds_;

  // Toggle debug overlays.
  bool show_debug_info_ = false;
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_DEMO_H_
