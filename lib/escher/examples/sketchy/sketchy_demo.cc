// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/sketchy/sketchy_demo.h"

#include "escher/renderer/paper_renderer.h"
#include "escher/scene/camera.h"

// Material design places objects from 0.0f to 24.0f.
static constexpr float kNear = 24.f;
static constexpr float kFar = 0.f;

SketchyDemo::SketchyDemo(DemoHarness* harness, int argc, char** argv)
    : Demo(harness),
      page_(escher()),
      renderer_(escher()->NewPaperRenderer()),
      swapchain_helper_(harness->GetVulkanSwapchain(),
                        escher()->vulkan_context().device,
                        escher()->vulkan_context().queue) {
  InitializeEscherStage();
}

SketchyDemo::~SketchyDemo() {}

void SketchyDemo::InitializeEscherStage() {
  stage_.set_viewing_volume(
      escher::ViewingVolume(kDemoWidth, kDemoHeight, kNear, kFar));
  // TODO: perhaps lights should be initialized by the various demo scenes.
  stage_.set_key_light(escher::DirectionalLight(
      escher::vec2(1.5f * M_PI, 1.5f * M_PI), 0.15f * M_PI, 0.7f));
  stage_.set_fill_light(escher::AmbientLight(0.3f));
}

void SketchyDemo::DrawFrame() {
  escher::Model* model = page_.GetModel(stopwatch_, &stage_);
  escher::Camera camera = escher::Camera::NewOrtho(stage_.viewing_volume());
  swapchain_helper_.DrawFrame(renderer_.get(), stage_, *model, camera);
}

bool SketchyDemo::HandleKeyPress(std::string key) {
  if (key == "c" || key == "C") {
    page_.Clear();
    return true;
  } else {
    return Demo::HandleKeyPress(key);
  }
}

void SketchyDemo::BeginTouch(uint64_t touch_id,
                             double x_position,
                             double y_position) {
  FTL_DCHECK(stroke_fitters_.find(touch_id) == stroke_fitters_.end());
  auto& fitter =
      (stroke_fitters_[touch_id] =
           std::make_unique<sketchy::StrokeFitter>(&page_, next_stroke_id_++));
  fitter->StartStroke(sketchy::vec2(static_cast<float>(x_position),
                                    static_cast<float>(y_position)));
}

void SketchyDemo::ContinueTouch(uint64_t touch_id,
                                const double* x_positions,
                                const double* y_positions,
                                size_t position_count) {
  auto it = stroke_fitters_.find(touch_id);
  FTL_CHECK(it != stroke_fitters_.end());
  auto& fitter = it->second;
  std::vector<sketchy::vec2> positions;
  positions.reserve(position_count);
  for (size_t i = 0; i < position_count; ++i) {
    positions.emplace_back(static_cast<float>(x_positions[i]),
                           static_cast<float>(y_positions[i]));
  }
  fitter->ContinueStroke(std::move(positions), {});
}

void SketchyDemo::EndTouch(uint64_t touch_id,
                           double x_position,
                           double y_position) {
  auto it = stroke_fitters_.find(touch_id);
  FTL_CHECK(it != stroke_fitters_.end());
  auto& fitter = it->second;
  fitter->ContinueStroke({sketchy::vec2(static_cast<float>(x_position),
                                        static_cast<float>(y_position))},
                         {});
  fitter->FinishStroke();
  stroke_fitters_.erase(it);
}
