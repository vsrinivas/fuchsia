// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/uber_scene.h"

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

UberScene::UberScene(Demo* demo) : Scene(demo) {}

void UberScene::Init(escher::Stage* stage) {
  blue_ = fxl::MakeRefCounted<escher::Material>();
  red_ = fxl::MakeRefCounted<escher::Material>();
  bg_ = fxl::MakeRefCounted<escher::Material>();
  purple_->set_color(vec3(0.588f, 0.239f, 0.729f));
  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));

  // Create meshes for fancy wobble effect.
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                MeshAttribute::kPerimeterPos | MeshAttribute::kUV};
  ring_mesh1_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 300.f,
                                    250.f, 18.f, -15.f);

  ring_mesh2_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 500.f,
                                    400.f, 18.f, -15.f);

  ring_mesh3_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 500.f,
                                    350.f, 18.f, -15.f);

  ring_mesh4_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 150.f,
                                    100.f, 18.f, -15.f);
}

UberScene::~UberScene() {}

escher::Model* UberScene::Update(const escher::Stopwatch& stopwatch,
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

  Object circle1(Object::NewCircle(vec2(circle1_x_pos, circle1_y_pos), 120.f,
                                   circle1_z_pos, blue_));

  float circle1o_y_pos = circle1_y_pos + (sin(circle1_time * 2) * 200.);
  float circle1o_z_pos = circle1_z_pos + (cos(circle1_time * 2) * 3.);
  Object circle1o(Object::NewCircle(vec2(circle1_x_pos, circle1o_y_pos), 40.f,
                                    circle1o_z_pos, red_));

  // animate the position along a figure-eight
  float circle2_time_offset = 0.75f;
  float circle2_time = current_time_sec + circle2_time_offset;
  float circle2_path_scale = 2. / (3. - cos(2. * circle2_time)) * 800.;
  float circle2_x_pos =
      circle2_path_scale * cos(circle2_time) + (screen_width * 0.5);
  float circle2_y_pos =
      circle2_path_scale * sin(2. * circle2_time) / 2. + (screen_height * 0.5);
  float circle2_z_pos =
      circle2_path_scale / 800. * max_height * sin(2. * circle2_time) / 2. +
      min_height + (max_height * 0.5);

  Object circle2(Object::NewCircle(vec2(circle2_x_pos, circle2_y_pos), 120.f,
                                   circle2_z_pos, blue_));

  float circle2o_y_pos = circle2_y_pos + (sin(circle2_time * 2) * 200.);
  float circle2o_z_pos = circle2_z_pos + (cos(circle2_time * 2) * 3.);
  Object circle2o(Object::NewCircle(vec2(circle2_x_pos, circle2o_y_pos), 40.f,
                                    circle2o_z_pos, red_));

  // animate the position along a figure-eight
  float circle3_time_offset = 1.5f;
  float circle3_time = current_time_sec + circle3_time_offset;
  float circle3_path_scale = 2. / (3. - cos(2. * circle3_time)) * 800.;
  float circle3_x_pos =
      circle3_path_scale * cos(circle3_time) + (screen_width * 0.5);
  float circle3_y_pos =
      circle3_path_scale * sin(2. * circle3_time) / 2. + (screen_height * 0.5);
  float circle3_z_pos =
      circle3_path_scale / 800. * max_height * sin(2. * circle3_time) / 2. +
      min_height + (max_height * 0.5);

  Object circle3(Object::NewCircle(vec2(circle3_x_pos, circle3_y_pos), 120.f,
                                   circle3_z_pos, blue_));

  float circle3o_y_pos = circle3_y_pos + (sin(circle3_time * 2) * 200.);
  float circle3o_z_pos = circle3_z_pos + (cos(circle3_time * 2) * 3.);
  Object circle3o(Object::NewCircle(vec2(circle3_x_pos, circle3o_y_pos), 40.f,
                                    circle3o_z_pos, red_));

  // animate the position along a figure-eight
  float circle4_time_offset = 2.25f;
  float circle4_time = current_time_sec + circle4_time_offset;
  float circle4_path_scale = 2. / (3. - cos(2. * circle4_time)) * 800.;
  float circle4_x_pos =
      circle4_path_scale * cos(circle4_time) + (screen_width * 0.5);
  float circle4_y_pos =
      circle4_path_scale * sin(2. * circle4_time) / 2. + (screen_height * 0.5);
  float circle4_z_pos =
      circle4_path_scale / 800. * max_height * sin(2. * circle4_time) / 2. +
      min_height + (max_height * 0.5);

  Object circle4(Object::NewCircle(vec2(circle4_x_pos, circle4_y_pos), 120.f,
                                   circle4_z_pos, blue_));

  float circle4o_y_pos = circle4_y_pos + (sin(circle4_time * 2) * 200.);
  float circle4o_z_pos = circle4_z_pos + (cos(circle4_time * 2) * 3.);
  Object circle4o(Object::NewCircle(vec2(circle4_x_pos, circle4o_y_pos), 40.f,
                                    circle4o_z_pos, red_));

  // animate the position along a figure-eight
  float circle5_time_offset = 3.f;
  float circle5_time = current_time_sec + circle5_time_offset;
  float circle5_path_scale = 2. / (3. - cos(2. * circle5_time)) * 800.;
  float circle5_x_pos =
      circle5_path_scale * cos(circle5_time) + (screen_width * 0.5);
  float circle5_y_pos =
      circle5_path_scale * sin(2. * circle5_time) / 2. + (screen_height * 0.5);
  float circle5_z_pos =
      circle5_path_scale / 800. * max_height * sin(2. * circle5_time) / 2. +
      min_height + (max_height * 0.5);

  Object circle5(Object::NewCircle(vec2(circle5_x_pos, circle5_y_pos), 120.f,
                                   circle5_z_pos, blue_));

  float circle5o_y_pos = circle5_y_pos + (sin(circle5_time * 2) * 200.);
  float circle5o_z_pos = circle5_z_pos + (cos(circle5_time * 2) * 3.);
  Object circle5o(Object::NewCircle(vec2(circle5_x_pos, circle5o_y_pos), 40.f,
                                    circle5o_z_pos, red_));

  // animate the position along a figure-eight
  float circle6_time_offset = 3.75f;
  float circle6_time = current_time_sec + circle6_time_offset;
  float circle6_path_scale = 2. / (3. - cos(2. * circle6_time)) * 800.;
  float circle6_x_pos =
      circle6_path_scale * cos(circle6_time) + (screen_width * 0.5);
  float circle6_y_pos =
      circle6_path_scale * sin(2. * circle6_time) / 2. + (screen_height * 0.5);
  float circle6_z_pos =
      circle6_path_scale / 800. * max_height * sin(2. * circle6_time) / 2. +
      min_height + (max_height * 0.5);

  Object circle6(Object::NewCircle(vec2(circle6_x_pos, circle6_y_pos), 120.f,
                                   circle6_z_pos, blue_));

  float circle6o_y_pos = circle6_y_pos + (sin(circle6_time * 2) * 200.);
  float circle6o_z_pos = circle6_z_pos + (cos(circle6_time * 2) * 3.);
  Object circle6o(Object::NewCircle(vec2(circle6_x_pos, circle6o_y_pos), 40.f,
                                    circle6o_z_pos, red_));

  Object rectangle(Object::NewRect(
      vec2(0.f, 0.f), vec2(screen_width, screen_height), 1.f, bg_));

  vec3 ring1_pos(250., screen_height * 0.5, 10.f);
  Object ring1(ring1_pos, ring_mesh1_, purple_);
  ring1.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring2_pos(screen_width - 250., screen_height * 0.5, 10.f);
  Object ring2(ring2_pos, ring_mesh1_, purple_);
  ring2.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring1a_pos(250., screen_height * 0.5, 22.f);
  Object ring1a(ring1a_pos, ring_mesh4_, purple_);
  ring1a.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring2a_pos(screen_width - 250., screen_height * 0.5, 22.f);
  Object ring2a(ring2a_pos, ring_mesh4_, purple_);
  ring2a.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring3_pos(250., screen_height * 0.5, 1.f);
  Object ring3(ring3_pos, ring_mesh2_, purple_);
  ring3.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring4_pos(screen_width - 250., screen_height * 0.5, 1.f);
  Object ring4(ring4_pos, ring_mesh2_, purple_);
  ring4.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring5_pos(screen_width * 0.5, 0.0, 2.f);
  Object ring5(ring5_pos, ring_mesh3_, purple_);
  ring5.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring6_pos(screen_width * 0.5, screen_height, 2.f);
  Object ring6(ring6_pos, ring_mesh3_, purple_);
  ring6.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring7_pos(screen_width * 0.5, 0.0, 15.f);
  Object ring7(ring7_pos, ring_mesh1_, purple_);
  ring7.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring8_pos(screen_width * 0.5, screen_height, 15.f);
  Object ring8(ring8_pos, ring_mesh1_, purple_);
  ring8.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring7a_pos(screen_width * 0.5, 0.0, 22.f);
  Object ring7a(ring7a_pos, ring_mesh4_, purple_);
  ring7a.set_shape_modifiers(ShapeModifier::kWobble);

  vec3 ring8a_pos(screen_width * 0.5, screen_height, 22.f);
  Object ring8a(ring8a_pos, ring_mesh4_, purple_);
  ring8a.set_shape_modifiers(ShapeModifier::kWobble);

  std::vector<Object> objects{
      rectangle, circle1,  circle1o, circle2, circle2o, circle3, circle3o,
      circle4,   circle4o, ring1,    ring2,   ring3,    ring4,   ring5,
      ring6,     ring7,    ring8,    ring1a,  ring2a,   ring7a,  ring8a};

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_time(current_time_sec);

  return model_.get();
}
