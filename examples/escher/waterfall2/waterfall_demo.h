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
#include "garnet/examples/escher/waterfall2/waterfall_renderer.h"

#include "lib/escher/escher.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/fs/hack_filesystem.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/fxl/logging.h"

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

  void InitializeEscherStage(const DemoHarness::WindowParams& window_params);
  void InitializeDemoScene();

  WaterfallRendererPtr renderer_;

  escher::Stage stage_;

  escher::MeshPtr ring_;
  escher::TexturePtr texture_;
  escher::MaterialPtr material_;
  escher::MaterialPtr material2_;

  escher::Stopwatch stopwatch_;
  uint64_t frame_count_ = 0;
  uint64_t first_frame_microseconds_;

  // Toggle debug overlays.
  bool show_debug_info_ = false;
};

#endif  // GARNET_EXAMPLES_ESCHER_WATERFALL2_WATERFALL_DEMO_H_
