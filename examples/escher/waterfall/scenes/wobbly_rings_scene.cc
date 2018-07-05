// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/wobbly_rings_scene.h"

#include "lib/escher/geometry/tessellation.h"
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
using escher::vec2;
using escher::vec3;

const float kRectYPos = 40.f;

WobblyRingsScene::WobblyRingsScene(Demo* demo, vec3 clear_color,
                                   vec3 ring1_color, vec3 ring2_color,
                                   vec3 ring3_color, vec3 circle_color,
                                   vec3 checkerboard_color)
    : Scene(demo), clear_color_(clear_color) {
  ring1_color_ = fxl::MakeRefCounted<escher::Material>();
  ring2_color_ = fxl::MakeRefCounted<escher::Material>();
  ring3_color_ = fxl::MakeRefCounted<escher::Material>();
  circle_color_ = fxl::MakeRefCounted<escher::Material>();
  clip_color_ = fxl::MakeRefCounted<escher::Material>();
  checkerboard_material_ = fxl::MakeRefCounted<escher::Material>();

  ring1_color_->set_color(ring1_color);
  ring2_color_->set_color(ring2_color);
  ring3_color_->set_color(ring3_color);
  circle_color_->set_color(circle_color);
  vec3 clip_color = circle_color * 0.8f;
  clip_color_->set_color(clip_color);
  checkerboard_material_->set_color(checkerboard_color);
}

void WobblyRingsScene::Init(escher::Stage* stage) {
  // Create meshes for fancy wobble effect.
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
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
      escher(), spec, 2, vec2(screenWidth, screenHeight - kRectYPos),
      vec2(0.f, 0.f), 18.f, 0.f);

  // Create materials.
  TexturePtr checkerboard = escher()->NewTexture(
      escher()->NewCheckerboardImage(16, 16), vk::Filter::eNearest);
  auto checkerboard_color = checkerboard_material_->color();
  checkerboard_material_ = fxl::MakeRefCounted<escher::Material>();
  checkerboard_material_->SetTexture(checkerboard);
  checkerboard_material_->set_color(checkerboard_color);
}

WobblyRingsScene::~WobblyRingsScene() {}

escher::Model* WobblyRingsScene::Update(const escher::Stopwatch& stopwatch,
                                        uint64_t frame_count,
                                        escher::Stage* stage) {
  stage->set_clear_color(clear_color_);
  float current_time_sec = stopwatch.GetElapsedSeconds();

  vec2 center(stage->viewing_volume().width() / 2.f,
              stage->viewing_volume().height() / 2.f);

  Object circle1(Object::NewCircle(center + vec2(100.f, -300.f), 200.f, 8.f,
                                   circle_color_));
  Object circle2(Object::NewCircle(center + vec2(-100.f, 268.f), 200.f, 8.f,
                                   circle_color_));
  Object circle3(Object::NewCircle(center + vec2(-350.f, -100.f), 120.f, 15.f,
                                   circle_color_));
  Object circle4(Object::NewCircle(center + vec2(338.f, 88.f), 120.f, 15.f,
                                   circle_color_));

  // Animate the position of the rings, using a cos and sin function applied
  // to time
  float x_pos_offset = cos(current_time_sec * 0.4f) * 200.;
  float y_pos_offset = sin(current_time_sec) * 100.;
  vec3 ring_pos(center + vec2(x_pos_offset, y_pos_offset), 0);

  Object ring1(ring_pos + vec3(0, 0, 4.f), ring_mesh1_, ring1_color_);
  ring1.set_shape_modifiers(ShapeModifier::kWobble);
  Object ring2(ring_pos + vec3(0, 0, 12.f), ring_mesh2_, ring2_color_);
  ring2.set_shape_modifiers(ShapeModifier::kWobble);
  Object ring3(ring_pos + vec3(0, 0, 24.f), ring_mesh3_, ring3_color_);
  ring3.set_shape_modifiers(ShapeModifier::kWobble);

  constexpr float TWO_PI = 6.28318530718f;
  escher::ModifierWobble wobble_data{{{-0.3f * TWO_PI, 0.4f, 7.f * TWO_PI},
                                      {-0.2f * TWO_PI, 0.2f, 23.f * TWO_PI},
                                      {1.f * TWO_PI, 0.6f, 5.f * TWO_PI}}};
  ring1.set_shape_modifier_data(wobble_data);
  ring2.set_shape_modifier_data(wobble_data);
  ring3.set_shape_modifier_data(wobble_data);

  // Create two circles that will be part of a clip group.  One draws a
  // background, and is orbited by a smaller circle that doesn't draw a
  // background.
  Object clip_circle1(Object::NewCircle(
      center - vec2(x_pos_offset, y_pos_offset), 400.f, 2.f, clip_color_));
  x_pos_offset += cos(current_time_sec * 2.f) * 420.f;
  y_pos_offset += sin(current_time_sec * 2.f) * 420.f;
  Object clip_circle2(Object::NewCircle(
      center - vec2(x_pos_offset, y_pos_offset), 180.f, 2.f, nullptr));

  // Create a clip group where the two clip-circles are used to clip some of
  // the other objects defined above.
  Object clip_group({clip_circle1, clip_circle2},
                    {ring1, ring2, ring3, circle1, circle2});

// Create a wobbly rectangle
#if 0
  Object rectangle(wobbly_rect_mesh_, vec3(0.f, kRectYPos, 0.f),
                   checkerboard_material_);
  rectangle.set_shape_modifiers(ShapeModifier::kWobble);
  rectangle.set_shape_modifier_data(wobble_data);
#else
  Object rectangle(Object::NewRect(
      vec2(0.f, 0.f),
      vec2(stage->viewing_volume().width(), stage->viewing_volume().height()),
      0.f, checkerboard_material_));
#endif

  std::vector<Object> objects{std::move(clip_group), circle3, circle4,
                              rectangle};

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_time(current_time_sec);

  return model_.get();
}
