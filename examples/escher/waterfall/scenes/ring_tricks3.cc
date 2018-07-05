// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/ring_tricks3.h"

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

RingTricks3::RingTricks3(Demo* demo) : Scene(demo) {}

void RingTricks3::Init(escher::Stage* stage) {
  bg_ = fxl::MakeRefCounted<escher::Material>();
  color1_ = fxl::MakeRefCounted<escher::Material>();
  color2_ = fxl::MakeRefCounted<escher::Material>();
  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));
  color1_->set_color(vec3(63.f / 255.f, 138.f / 255.f, 153.f / 255.f));
  color2_->set_color(vec3(143.f / 255.f, 143.f / 255.f, 143.f / 255.f));

  // Create meshes for fancy wobble effect.
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                MeshAttribute::kPerimeterPos | MeshAttribute::kUV};
  ring_mesh1_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 285.f,
                                    265.f, 18.f, -15.f);
}

RingTricks3::~RingTricks3() {}

escher::Model* RingTricks3::Update(const escher::Stopwatch& stopwatch,
                                   uint64_t frame_count, escher::Stage* stage) {
  float current_time_sec = stopwatch.GetElapsedSeconds();

  float screen_width = stage->viewing_volume().width();
  float screen_height = stage->viewing_volume().height();
  float min_height = 5.f;
  float max_height = 80.f;
  float elevation_range = max_height - min_height;

  std::vector<Object> objects;

  // animate the position along a figure-eight
  float figure_eight_size = 600.f;
  float circle1_path_scale =
      2. / (3. - cos(2. * current_time_sec)) * figure_eight_size;
  float circle1_x_pos =
      circle1_path_scale * cos(current_time_sec) + (screen_width * 0.5);
  float circle1_y_pos = circle1_path_scale * sin(2. * current_time_sec) / 2. +
                        (screen_height * 0.5);
  float circle1_elevation =
      (sin(2. * current_time_sec) * 0.5 + 0.5) * elevation_range + min_height;

  Object circle1(Object::NewCircle(
      vec3(circle1_x_pos, circle1_y_pos, circle1_elevation), 120.f, color1_));
  objects.push_back(circle1);

  // Create the ring that will do the fancy trick
  // vec3 inner_ring_pos(screen_width * 0.5f, screen_height * 0.5f,
  //                     (elevation_range * 0.75 + min_height));
  vec3 inner_ring_pos(screen_width * 0.5f, screen_height * 0.5f, 30.f);
  Object inner_ring(inner_ring_pos, ring_mesh1_, color2_);
  inner_ring.set_shape_modifiers(ShapeModifier::kWobble);
  objects.push_back(inner_ring);

  // Create our background plane
  Object bg_plane(Object::NewRect(vec2(0.f, 0.f),
                                  vec2(screen_width, screen_height), 0.f, bg_));

  objects.push_back(bg_plane);

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_time(current_time_sec);

  return model_.get();
}
