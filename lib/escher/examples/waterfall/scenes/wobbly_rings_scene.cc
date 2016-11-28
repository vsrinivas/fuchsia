// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/waterfall/scenes/wobbly_rings_scene.h"

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

const float kRectYPos = 40.f;

WobblyRingsScene::WobblyRingsScene(escher::VulkanContext* vulkan_context,
                                   escher::Escher* escher)
    : Scene(vulkan_context, escher) {}

void WobblyRingsScene::Init(escher::Stage* stage) {
  // Create meshes for fancy wobble effect.
  MeshSpec spec{MeshAttribute::kPosition | MeshAttribute::kPositionOffset |
                MeshAttribute::kPerimeterPos | MeshAttribute::kUV};
  ring_mesh1_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 300.f,
                                    250.f, 18.f, -15.f);
  ring_mesh2_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 200.f,
                                    150.f, 11.f, -8.f);
  ring_mesh3_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 100.f,
                                    50.f, 5.f, -2.f);

  // Make this mesh the size of the stage
  float screenWidth = stage->viewing_volume().width();
  float screenHeight = stage->viewing_volume().height();
  wobbly_rect_mesh_ = escher::NewRectangleMesh(
      escher(), spec, 8, vec2(screenWidth, screenHeight - kRectYPos),
      vec2(0.f, 0.f), 18.f, 0.f);

  // Create materials.
  auto checkerboard = ftl::MakeRefCounted<escher::Texture>(
      escher()->NewCheckerboardImage(16, 16), vulkan_context()->device,
      vk::Filter::eNearest);

  blue_ = ftl::MakeRefCounted<escher::Material>();
  pink_ = ftl::MakeRefCounted<escher::Material>();
  green_ = ftl::MakeRefCounted<escher::Material>();
  blue_green_ = ftl::MakeRefCounted<escher::Material>();
  purple_ = ftl::MakeRefCounted<escher::Material>(checkerboard);
  blue_->set_color(vec3(0.188f, 0.188f, 0.788f));
  pink_->set_color(vec3(0.929f, 0.678f, 0.925f));
  green_->set_color(vec3(0.259f, 0.956f, 0.667));
  blue_green_->set_color(vec3(0.039f, 0.788f, 0.788f));
  purple_->set_color(vec3(0.588f, 0.239f, 0.729f));
}

WobblyRingsScene::~WobblyRingsScene() {}

escher::Model* WobblyRingsScene::Update(const escher::Stopwatch& stopwatch,
                                        uint64_t frame_count,
                                        escher::Stage* stage) {
  float current_time_sec = stopwatch.GetElapsedSeconds();

  Object circle1(Object::NewCircle(vec2(612.f, 212.f), 200.f, 8.f, blue_));
  Object circle2(Object::NewCircle(vec2(412.f, 800.f), 200.f, 8.f, blue_));
  Object circle3(Object::NewCircle(vec2(162.f, 412.f), 120.f, 15.f, blue_));
  Object circle4(Object::NewCircle(vec2(850.f, 600.f), 120.f, 15.f, blue_));

  // Animate the position of the rings, using a cos and sin function applied
  // to time
  float x_pos_offset = cos(current_time_sec * 0.4f) * 200.;
  float y_pos_offset = sin(current_time_sec) * 100.;
  vec3 ring_pos(512.f + x_pos_offset, 512.f + y_pos_offset, 0);

  Object ring1(ring_mesh1_, ring_pos + vec3(0, 0, 4.f), pink_);
  ring1.set_shape_modifiers(ShapeModifier::kWobble);
  Object ring2(ring_mesh2_, ring_pos + vec3(0, 0, 12.f), green_);
  ring2.set_shape_modifiers(ShapeModifier::kWobble);
  Object ring3(ring_mesh3_, ring_pos + vec3(0, 0, 24.f), blue_green_);
  ring3.set_shape_modifiers(ShapeModifier::kWobble);

  constexpr float TWO_PI = 6.28318530718f;
  escher::ModifierWobble wobble_data{{{-0.3f * TWO_PI, 0.4f, 7.f * TWO_PI},
                                      {-0.2f * TWO_PI, 0.2f, 23.f * TWO_PI},
                                      {1.f * TWO_PI, 0.6f, 5.f * TWO_PI}}};
  ring1.set_shape_modifier_data(wobble_data);
  ring2.set_shape_modifier_data(wobble_data);
  ring3.set_shape_modifier_data(wobble_data);

  // Create a wobbly rectangle
  Object rectangle(wobbly_rect_mesh_, vec3(0.f, kRectYPos, 2.f), purple_);
  rectangle.set_shape_modifiers(ShapeModifier::kWobble);
  rectangle.set_shape_modifier_data(wobble_data);

  std::vector<Object> objects{circle1,   circle2, circle3, circle4,
                              rectangle, ring1,   ring2,   ring3};

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_blur_plane_height(12.0f);
  model_->set_time(current_time_sec);

  return model_.get();
}
