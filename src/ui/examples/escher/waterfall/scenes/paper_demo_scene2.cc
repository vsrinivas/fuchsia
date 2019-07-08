// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/waterfall/scenes/paper_demo_scene2.h"

#include "src/ui/lib/escher/geometry/plane_ops.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/material/material.h"
#include "src/ui/lib/escher/math/lerp.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/scene/model.h"
#include "src/ui/lib/escher/scene/stage.h"
#include "src/ui/lib/escher/shape/modifier_wobble.h"
#include "src/ui/lib/escher/util/alloca.h"
#include "src/ui/lib/escher/util/stopwatch.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/texture.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

using escher::MeshAttribute;
using escher::MeshSpec;
using escher::Object;
using escher::RoundedRectSpec;
using escher::ShapeModifier;

using escher::plane3;
using escher::vec2;
using escher::vec3;
using escher::vec4;

PaperDemoScene2::PaperDemoScene2(Demo* demo) : Scene(demo) {}

void PaperDemoScene2::Init(escher::PaperScene* scene) {
  red_ = fxl::MakeRefCounted<escher::Material>();
  bg_ = fxl::MakeRefCounted<escher::Material>();
  color1_ = fxl::MakeRefCounted<escher::Material>();
  color2_ = fxl::MakeRefCounted<escher::Material>();
  red_->set_color(vec3(0.15f, 0.3f, 0.15f));
  bg_->set_color(vec3(0.1f, 0.1f, 0.3f));
  color2_->set_color(vec3(63.f / 255.f, 138.f / 255.f, 153.f / 255.f));
  color1_->set_color(vec3(143.f / 255.f, 143.f / 255.f, 143.f / 255.f));
}

PaperDemoScene2::~PaperDemoScene2() {}

void PaperDemoScene2::Update(const escher::Stopwatch& stopwatch, uint64_t frame_count,
                             escher::PaperScene* scene, escher::PaperRenderer* renderer) {
  const float current_time_sec = stopwatch.GetElapsedSeconds();
  const float screen_width = scene->bounding_box.width();
  const float screen_height = scene->bounding_box.height();

  // Draws text to the screen
  renderer->DrawDebugText("ABC", {50, 50}, 20);
  renderer->DrawDebugText("CBZ", {800, 800}, 10);
}

float PaperDemoScene2::AnimatedState::Update(float current_time_sec) {
  float t = 0.f;

  const float time_in_state = current_time_sec - state_start_time;

  if (paused) {
    // Paused, see if it is time to resume action.
    if (time_in_state > inter_cycle_pause_duration) {
      paused = false;
      state_start_time = current_time_sec;
    }
  } else if (time_in_state > cycle_duration * cycle_count_before_pause) {
    // Was running, now paused.
    paused = true;
    state_start_time = current_time_sec;
  } else {
    t = 0.5f - 0.5f * cos(time_in_state / cycle_duration * 2.f * M_PI);
  }

  return t;
}

escher::plane3 PaperDemoScene2::ClipPlaneState::Update(float current_time_sec) {
  const float t = animation.Update(current_time_sec);
  const vec2 pos = escher::Lerp(pos1, pos2, t);
  const float radians = escher::Lerp(radians1, radians2, t);
  const vec2 dir(cos(radians), sin(radians));
  return plane3(vec3(pos, 0), vec3(dir, 0));
}
