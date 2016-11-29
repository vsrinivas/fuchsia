// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/waterfall/scenes/ring_tricks3.h"

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

RingTricks3::RingTricks3(escher::VulkanContext* vulkan_context,
                         escher::Escher* escher)
    : Scene(vulkan_context, escher) {}

void RingTricks3::Init(escher::Stage* stage) {
  auto checkerboard = ftl::MakeRefCounted<escher::Texture>(
      escher()->NewCheckerboardImage(16, 16), vulkan_context()->device,
      vk::Filter::eNearest);

  blue_ = ftl::MakeRefCounted<escher::Material>();
  red_ = ftl::MakeRefCounted<escher::Material>();
  pink_ = ftl::MakeRefCounted<escher::Material>();
  green_ = ftl::MakeRefCounted<escher::Material>();
  blue_green_ = ftl::MakeRefCounted<escher::Material>();
  // purple_ = ftl::MakeRefCounted<escher::Material>(checkerboard);
  purple_ = ftl::MakeRefCounted<escher::Material>();
  bg_ = ftl::MakeRefCounted<escher::Material>();
  mc1_ = ftl::MakeRefCounted<escher::Material>();
  mc2_ = ftl::MakeRefCounted<escher::Material>();
  mc3_ = ftl::MakeRefCounted<escher::Material>();

  blue_->set_color(vec3(0.188f, 0.188f, 0.788f));
  red_->set_color(vec3(0.98f, 0.15f, 0.15f));
  pink_->set_color(vec3(0.929f, 0.678f, 0.925f));
  green_->set_color(vec3(0.259f, 0.956f, 0.667));
  blue_green_->set_color(vec3(0.039f, 0.788f, 0.788f));
  purple_->set_color(vec3(0.588f, 0.239f, 0.729f));
  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));

  mc1_->set_color(vec3(157.f / 255.f, 183.f / 255.f, 189.f / 255.f));
  mc2_->set_color(vec3(63.f / 255.f, 138.f / 255.f, 153.f / 255.f));
  mc3_->set_color(vec3(143.f / 255.f, 143.f / 255.f, 143.f / 255.f));

  // Create meshes for fancy wobble effect.
  MeshSpec spec{MeshAttribute::kPosition | MeshAttribute::kPositionOffset |
                MeshAttribute::kPerimeterPos | MeshAttribute::kUV};

  ring_mesh1 = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 285.f,
                                   265.f, 18.f, -15.f);
}

RingTricks3::~RingTricks3() {}

escher::Model* RingTricks3::Update(const escher::Stopwatch& stopwatch,
                                   uint64_t frame_count,
                                   escher::Stage* stage) {
  float current_time_sec = stopwatch.GetElapsedSeconds();

  float screen_width = 1600.f;
  float screen_height = 1024.f;
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

  Object circle1(Object::NewCircle(vec2(circle1_x_pos, circle1_y_pos), 120.f,
                                   circle1_elevation, mc2_));
  objects.push_back(circle1);

  // Create the ring that will do the fancy trick
  // vec3 inner_ring_pos(screen_width * 0.5f, screen_height * 0.5f,
  //                     (elevation_range * 0.75 + min_height));
  vec3 inner_ring_pos(screen_width * 0.5f, screen_height * 0.5f, 30.f);
  Object inner_ring(ring_mesh1, inner_ring_pos, mc3_);
  inner_ring.set_shape_modifiers(ShapeModifier::kWobble);
  objects.push_back(inner_ring);

  // Create our background plane
  Object bg_plane(Object::NewRect(vec2(0.f, 0.f),
                                  vec2(screen_width, screen_height), 0.f, bg_));

  objects.push_back(bg_plane);

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_blur_plane_height(12.0f);
  model_->set_time(current_time_sec);

  return model_.get();
}
