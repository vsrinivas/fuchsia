// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/uber_scene2.h"

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
using escher::vec2;
using escher::vec3;
using escher::vec4;

UberScene2::UberScene2(Demo* demo) : Scene(demo) {}

void UberScene2::Init(escher::Stage* stage) {
  blue_ = fxl::MakeRefCounted<escher::Material>();
  red_ = fxl::MakeRefCounted<escher::Material>();
  purple_ = fxl::MakeRefCounted<escher::Material>();
  bg_ = fxl::MakeRefCounted<escher::Material>();
  gray1_ = fxl::MakeRefCounted<escher::Material>();
  gray2_ = fxl::MakeRefCounted<escher::Material>();
  blue_->set_color(vec3(0.188f, 0.188f, 0.788f));
  red_->set_color(vec3(0.98f, 0.15f, 0.15f));
  purple_->set_color(vec3(0.588f, 0.239f, 0.729f));
  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));
  gray1_->set_color(vec4(0.7f, 0.7f, 0.7f, 0.9f));
  gray1_->set_opaque(false);
  gray2_->set_color(vec4(0.4f, 0.4f, 0.4f, 0.4f));
  gray2_->set_opaque(false);

  // Create meshes for fancy wobble effect.
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                MeshAttribute::kPerimeterPos | MeshAttribute::kUV};
  ring_mesh1_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 150.f,
                                    100.f, 18.f, -15.f);

  ring_mesh2_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 300.f,
                                    250.f, 18.f, -15.f);

  ring_mesh3_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 500.f,
                                    350.f, 18.f, -15.f);

  ring_mesh4_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 700.f,
                                    600.f, 18.f, -15.f);

  ring_mesh5_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 1300.f,
                                    1150.f, 18.f, -15.f);
}

UberScene2::~UberScene2() {}

escher::Model* UberScene2::Update(const escher::Stopwatch& stopwatch,
                                  uint64_t frame_count, escher::Stage* stage,
                                  escher::PaperRenderQueue* render_queue) {
  float current_time_sec = stopwatch.GetElapsedSeconds();

  float screen_width = stage->viewing_volume().width();
  float screen_height = stage->viewing_volume().height();
  float min_height = 2.f;
  float max_height = 20.f;

  // animate the position along a figure-eight
  float circle1_time_offset = 0.f;
  float circle1_time = current_time_sec + circle1_time_offset;
  float circle1_path_scale = 2. / (3. - cos(2. * circle1_time)) * 800.;
  float circle1_x_pos =
      circle1_path_scale * cos(circle1_time) + (screen_width * 0.5);
  float circle1_y_pos =
      circle1_path_scale * sin(2. * circle1_time) / 2. + (screen_height * 0.5);
  float circle1_z_pos =
      circle1_path_scale / 800. * max_height * sin(2. * circle1_time) / 2. +
      min_height + (max_height * 0.5);
  vec3 circle1_pos(circle1_x_pos, circle1_y_pos, circle1_z_pos);
  Object circle1(Object::NewCircle(circle1_pos, 120.f, blue_));

  float circle1o_y_pos = circle1_y_pos + (sin(circle1_time * 2) * 200.);
  float circle1o_z_pos = circle1_z_pos + (cos(circle1_time * 2) * 3.);
  vec3 circle1o_pos(circle1_x_pos, circle1o_y_pos, circle1o_z_pos);
  Object circle1o(Object::NewCircle(circle1o_pos, 40.f, red_));

  // animate the position along a figure-eight
  float circle3_time_offset = 2.f;
  float circle3_time = current_time_sec + circle3_time_offset;
  float circle3_path_scale = 2. / (3. - cos(2. * circle3_time)) * 800.;
  float circle3_x_pos =
      circle3_path_scale * cos(circle3_time) + (screen_width * 0.5);
  float circle3_y_pos =
      circle3_path_scale * sin(2. * circle3_time) / 2. + (screen_height * 0.5);
  float circle3_z_pos =
      circle3_path_scale / 800. * max_height * sin(2. * circle3_time) / 2. +
      min_height + (max_height * 0.5);
  vec3 circle3_pos(circle3_x_pos, circle3_y_pos, circle3_z_pos);
  Object circle3(Object::NewCircle(circle3_pos, 120.f, blue_));

  float circle3o_y_pos = circle3_y_pos + (cos(circle3_time * 2) * 200.);
  float circle3o_z_pos = circle3_z_pos + (sin(circle3_time * 2) * 3.);
  vec3 circle3o_pos(circle3_x_pos, circle3o_y_pos, circle3o_z_pos);
  Object circle3o(Object::NewCircle(circle3o_pos, 40.f, red_));

  // animate the position along a figure-eight
  float circle2_time_offset = 1.f;
  float circle2_time = current_time_sec + circle2_time_offset;
  float circle2_path_scale = 2. / (3. - cos(2. * circle2_time)) * 800.;
  float circle2_x_pos =
      circle2_path_scale * -sin(2. * circle2_time) / 2. + (screen_width * 0.5);
  float circle2_y_pos =
      circle2_path_scale * -cos(circle2_time) + (screen_height * 0.5);
  float circle2_z_pos =
      circle2_path_scale / 800. * max_height * sin(2. * circle2_time) / 2. +
      min_height + (max_height * 0.5);
  vec3 circle2_pos(circle2_x_pos, circle2_y_pos, circle2_z_pos);
  Object circle2(Object::NewCircle(circle2_pos, 120.f, blue_));

  float circle2o_x_pos = circle2_x_pos + (cos(circle2_time * 2) * 200.);
  float circle2o_z_pos = circle2_z_pos + (sin(circle2_time * 2) * 3.);
  vec3 circle2o_pos(circle2o_x_pos, circle2_y_pos, circle2o_z_pos);
  Object circle2o(Object::NewCircle(circle2o_pos, 40.f, red_));

  // animate the position along a figure-eight
  float circle4_time_offset = 4.f;
  float circle4_time = current_time_sec + circle4_time_offset;
  float circle4_path_scale = 2. / (3. - cos(2. * circle4_time)) * 800.;
  float circle4_x_pos =
      circle4_path_scale * -sin(2. * circle4_time) / 2. + (screen_width * 0.5);
  float circle4_y_pos =
      circle4_path_scale * -cos(circle4_time) + (screen_height * 0.5);
  float circle4_z_pos =
      circle4_path_scale / 800. * max_height * sin(2. * circle4_time) / 2. +
      min_height + (max_height * 0.5);
  vec3 circle4_pos(circle4_x_pos, circle4_y_pos, circle4_z_pos);
  Object circle4(Object::NewCircle(circle4_pos, 120.f, blue_));

  float circle4o_x_pos = circle4_x_pos + (sin(circle4_time * 2) * 200.);
  float circle4o_z_pos = circle4_z_pos + (cos(circle4_time * 2) * 3.);
  vec3 circle4o_pos(circle4o_x_pos, circle4_y_pos, circle4o_z_pos);
  Object circle4o(Object::NewCircle(circle4o_pos, 40.f, red_));

  Object rectangle(Object::NewRect(vec3(0.f, 0.f, 1.f),
                                   vec2(screen_width, screen_height), bg_));

  vec3 ring1_pos(screen_width * 0.5, screen_height * 0.5, 10.f);
  Object ring1(ring1_pos, ring_mesh2_, purple_);
  ring1.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring1b_pos(screen_width * 0.5, screen_height * 0.5, 5.f);
  Object ring1b(ring1b_pos, ring_mesh1_, purple_);
  ring1b.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring2_pos(screen_width * 0.15, screen_height * 0.5, 10.f);
  Object ring2(ring2_pos, ring_mesh2_, purple_);
  ring2.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring2b_pos(screen_width * 0.15, screen_height * 0.5, 5.f);
  Object ring2b(ring2b_pos, ring_mesh1_, purple_);
  ring2b.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring3_pos(screen_width * 0.85, screen_height * 0.5, 10.f);
  Object ring3(ring3_pos, ring_mesh2_, purple_);
  ring3.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring3b_pos(screen_width * 0.85, screen_height * 0.5, 5.f);
  Object ring3b(ring3b_pos, ring_mesh1_, purple_);
  ring3b.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring4_pos(screen_width * 0.325, screen_height * 0.15, 2.f);
  Object ring4(ring4_pos, ring_mesh2_, purple_);
  ring4.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring4b_pos(screen_width * 0.325, screen_height * 0.15, 22.f);
  Object ring4b(ring4b_pos, ring_mesh1_, purple_);
  ring4b.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring6_pos(screen_width * 0.325, screen_height * 0.15, 22.f);
  Object ring6(ring6_pos, ring_mesh2_, purple_);
  ring6.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring6b_pos(screen_width * 0.325, screen_height * 0.15, 2.f);
  Object ring6b(ring6b_pos, ring_mesh1_, purple_);
  ring6b.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring5_pos(screen_width * 0.675, screen_height * 0.15, 2.f);
  Object ring5(ring5_pos, ring_mesh2_, purple_);
  ring5.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring5b_pos(screen_width * 0.675, screen_height * 0.15, 22.f);
  Object ring5b(ring5b_pos, ring_mesh1_, purple_);
  ring5b.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring7_pos(screen_width * 0.675, screen_height * 0.15, 22.f);
  Object ring7(ring7_pos, ring_mesh2_, purple_);
  ring7.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring7b_pos(screen_width * 0.675, screen_height * 0.15, 2.f);
  Object ring7b(ring7b_pos, ring_mesh1_, purple_);
  ring7b.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring8_pos(screen_width * 0.325, screen_height * 0.85, 23.f);
  Object ring8(ring8_pos, ring_mesh2_, purple_);
  ring8.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring8b_pos(screen_width * 0.325, screen_height * 0.85, 2.f);
  Object ring8b(ring8b_pos, ring_mesh1_, purple_);
  ring8b.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring9_pos(screen_width * 0.675, screen_height * 0.85, 23.f);
  Object ring9(ring9_pos, ring_mesh2_, purple_);
  ring9.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring9b_pos(screen_width * 0.675, screen_height * 0.85, 2.f);
  Object ring9b(ring9b_pos, ring_mesh1_, purple_);
  ring9b.set_shape_modifiers(ShapeModifier::kWobble);

  std::vector<Object> objects{
      rectangle, circle1,  circle1o, circle2, circle3, circle3o, circle2o,
      circle4,   circle4o, ring1,    ring1b,  ring2,   ring2b,   ring3,
      ring3b,    ring4,    ring4b,   ring5,   ring5b,  ring6,    ring6b,
      ring7,     ring7b,   ring8,    ring8b,  ring9,   ring9b};

  // Create the Model
  model_ = std::make_unique<escher::Model>(std::move(objects));
  model_->set_time(current_time_sec);

  return model_.get();
}

escher::Model* UberScene2::UpdateOverlay(const escher::Stopwatch& stopwatch,
                                         uint64_t frame_count, uint32_t width,
                                         uint32_t height) {
  const float quarter_width = static_cast<float>(width) * 0.25f;
  const float half_height = static_cast<float>(height) * 0.5f;
  const float radius = quarter_width * 0.9f;
  Object circle1(Object::NewCircle(vec3(quarter_width, half_height, 24.f),
                                   radius, gray1_));
  Object circle2(Object::NewCircle(vec3(3.f * quarter_width, half_height, 24.f),
                                   radius, gray2_));
  std::vector<Object> objects{circle1, circle2};
  overlay_model_ = std::make_unique<escher::Model>(std::move(objects));

  return overlay_model_.get();
}
