// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/waterfall/scenes/demo_scene.h"

#include "escher/geometry/tessellation.h"
#include "escher/geometry/types.h"
#include "escher/material/material.h"
#include "escher/renderer/image.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "escher/shape/modifier_wobble.h"
#include "escher/util/stopwatch.h"
#include "escher/vk/vulkan_context.h"

using escher::vec2;
using escher::vec3;
using escher::MeshAttribute;
using escher::MeshSpec;
using escher::Object;
using escher::ShapeModifier;

DemoScene::DemoScene(escher::VulkanContext* vulkan_context,
                     escher::Escher* escher)
    : Scene(vulkan_context, escher) {}

void DemoScene::Init(escher::Stage* stage) {
  auto checkerboard = ftl::MakeRefCounted<escher::Texture>(
      escher()->NewCheckerboardImage(16, 16), vulkan_context()->device,
      vk::Filter::eNearest);

  purple_ = ftl::MakeRefCounted<escher::Material>(checkerboard);
  purple_->set_color(vec3(0.588f, 0.239f, 0.729f));
}

DemoScene::~DemoScene() {}

escher::Model* DemoScene::Update(const escher::Stopwatch& stopwatch,
                                 uint64_t frame_count,
                                 escher::Stage* stage) {
  stage->set_clear_color(vec3(0.f, 0.f, 0.f));
  float current_time_sec = stopwatch.GetElapsedSeconds();
  float t = sin(current_time_sec);
  vec2 rect_size = vec2(abs(800.f * t), abs(800.f * t));
  Object rectangle(
      Object::NewRect(vec2(112.f + 100 * t, 112.f), rect_size, 8.f, purple_));
  rectangle.set_rotation(current_time_sec * 0.5);
  rectangle.set_rotation_point(vec2(0.5f, 0.5f));
  std::vector<Object> objects{rectangle};
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_blur_plane_height(12.0f);
  model_->set_time(stopwatch.GetElapsedSeconds());

  return model_.get();
}
