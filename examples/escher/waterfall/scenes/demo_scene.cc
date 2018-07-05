// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/demo_scene.h"

#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/geometry/transform.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/material/material.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/shape/modifier_wobble.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/vulkan_context.h"

using escher::MeshAttribute;
using escher::MeshSpec;
using escher::Object;
using escher::ShapeModifier;
using escher::TexturePtr;
using escher::Transform;
using escher::vec2;
using escher::vec3;

DemoScene::DemoScene(Demo* demo) : Scene(demo) {}

void DemoScene::Init(escher::Stage* stage) {
  TexturePtr checkerboard = escher()->NewTexture(
      escher()->NewCheckerboardImage(16, 16), vk::Filter::eNearest);

  purple_ = fxl::MakeRefCounted<escher::Material>();
  purple_->SetTexture(checkerboard);
  purple_->set_color(vec3(0.588f, 0.239f, 0.729f));
}

DemoScene::~DemoScene() {}

escher::Model* DemoScene::Update(const escher::Stopwatch& stopwatch,
                                 uint64_t frame_count, escher::Stage* stage) {
  stage->set_clear_color(vec3(0.f, 0.f, 0.f));
  float current_time_sec = stopwatch.GetElapsedSeconds();
  float t = sin(current_time_sec);
  vec3 rect_scale(abs(800.f * t), abs(800.f * t), 1);
  Transform transform(vec3(112.f + 100 * t, 112.f, 8.f), rect_scale,
                      current_time_sec * 0.5, vec3(0, 0, 1), vec3(0.5, 0.5, 0));
  Object rectangle(Object::NewRect(transform, purple_));
  std::vector<Object> objects{rectangle};
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_time(stopwatch.GetElapsedSeconds());

  return model_.get();
}
