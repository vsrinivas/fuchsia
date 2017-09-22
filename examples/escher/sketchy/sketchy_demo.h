// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/examples/escher/common/demo.h"
#include "escher/scene/stage.h"
#include "escher/util/stopwatch.h"
#include "escher/vk/vulkan_swapchain_helper.h"
#include "sketchy/page.h"
#include "sketchy/stroke_fitter.h"

class SketchyDemo : public Demo {
 public:
  static constexpr uint32_t kDemoWidth = 2160;
  static constexpr uint32_t kDemoHeight = 1440;

  SketchyDemo(DemoHarness* harness, int argc, char** argv);
  virtual ~SketchyDemo();

  void DrawFrame() override;

  bool HandleKeyPress(std::string key) override;

  void BeginTouch(uint64_t touch_id,
                  double x_position,
                  double y_position) override;
  void ContinueTouch(uint64_t touch_id,
                     const double* x_positions,
                     const double* y_positions,
                     size_t position_count) override;
  void EndTouch(uint64_t touch_id,
                double x_position,
                double y_position) override;

 private:
  void InitializeEscherStage();

  sketchy::Page page_;
  sketchy::StrokeId next_stroke_id_ = 1;
  std::map<uint64_t, std::unique_ptr<sketchy::StrokeFitter>> stroke_fitters_;
  escher::PaperRendererPtr renderer_;
  escher::VulkanSwapchainHelper swapchain_helper_;
  escher::Stage stage_;
  escher::Stopwatch stopwatch_;
};
