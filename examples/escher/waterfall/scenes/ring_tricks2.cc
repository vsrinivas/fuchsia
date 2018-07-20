// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/ring_tricks2.h"

#include "lib/escher/geometry/clip_planes.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/material/material.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/shape/modifier_wobble.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/texture.h"
#include "lib/escher/vk/vulkan_context.h"

using escher::MeshAttribute;
using escher::MeshSpec;
using escher::Object;
using escher::RoundedRectSpec;
using escher::ShapeModifier;
using escher::vec2;
using escher::vec3;

RingTricks2::RingTricks2(Demo* demo)
    : Scene(demo), factory_(demo->GetEscherWeakPtr()) {}

void RingTricks2::Init(escher::Stage* stage) {
  red_ = fxl::MakeRefCounted<escher::Material>();
  bg_ = fxl::MakeRefCounted<escher::Material>();
  color1_ = fxl::MakeRefCounted<escher::Material>();
  color2_ = fxl::MakeRefCounted<escher::Material>();
  red_->set_color(vec3(0.98f, 0.15f, 0.15f));
  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));
  color1_->set_color(vec3(63.f / 255.f, 138.f / 255.f, 153.f / 255.f));
  color2_->set_color(vec3(143.f / 255.f, 143.f / 255.f, 143.f / 255.f));

  gradient_ = fxl::MakeRefCounted<escher::Material>();
  gradient_->SetTexture(escher()->NewTexture(
      escher()->NewGradientImage(128, 128), vk::Filter::eLinear));
  gradient_->set_color(vec3(0.98f, 0.15f, 0.15f));

  // Create meshes for fancy wobble effect.
  {
    MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                  MeshAttribute::kPerimeterPos | MeshAttribute::kUV};
    ring_mesh1_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 285.f,
                                      265.f, 18.f, -15.f);
  }

  // Create rounded rectangles.
  {
    MeshSpec mesh_spec{MeshAttribute::kPosition2D | MeshAttribute::kUV};
    rounded_rect1_ = factory_.NewRoundedRect(
        RoundedRectSpec(200, 400, 90, 20, 20, 50), mesh_spec);
  }

  // Create sphere.
  {
    MeshSpec spec{MeshAttribute::kPosition3D | MeshAttribute::kUV};
    sphere_ = escher::NewSphereMesh(escher(), spec, 3, vec3(0, 0, 0), 100);
  }
}

RingTricks2::~RingTricks2() {}

escher::Model* RingTricks2::Update(const escher::Stopwatch& stopwatch,
                                   uint64_t frame_count, escher::Stage* stage,
                                   escher::PaperRenderQueue* render_queue) {
  float current_time_sec = stopwatch.GetElapsedSeconds();

  float screen_width = stage->viewing_volume().width();
  float screen_height = stage->viewing_volume().height();
  float min_elevation = 5.f;
  float max_elevation = 95.f;
  float mid_elevation = 0.5f * (min_elevation + max_elevation);
  float elevation_range = max_elevation - min_elevation;

  std::vector<Object> objects;

  // Orbiting circle1.
  float circle1_orbit_radius = 275.f;
  vec3 circle1_pos(sin(current_time_sec * 1.f) * circle1_orbit_radius +
                       (screen_width * 0.5f),
                   cos(current_time_sec * 1.f) * circle1_orbit_radius +
                       (screen_height * 0.5f),
                   mid_elevation + 10.f);
  Object circle1(Object::NewCircle(circle1_pos, 60.f, red_));
  objects.push_back(circle1);

  // Orbiting circle2.
  float circle2_orbit_radius = 120.f;
  vec2 circle2_offset(sin(current_time_sec * 2.f) * circle2_orbit_radius,
                      cos(current_time_sec * 2.f) * circle2_orbit_radius);

  float circle2_elevation =
      (cos(current_time_sec * 1.5f) * 0.5 + 0.5) * elevation_range +
      min_elevation;
  vec3 circle2_pos(vec2(circle1_pos) + circle2_offset, circle2_elevation);
  Object circle2(Object::NewCircle(circle2_pos, 30.f, color1_));
  objects.push_back(circle2);

  // Create the ring that will do the fancy trick.
  vec3 inner_ring_pos(screen_width * 0.5f, screen_height * 0.5f, mid_elevation);
  Object inner_ring(inner_ring_pos, ring_mesh1_, color2_);
  objects.push_back(inner_ring);

  // Create our background plane.
  Object bg_plane(
      Object::NewRect(vec3(0, 0, 0), vec2(screen_width, screen_height), bg_));
  objects.push_back(bg_plane);

  // Stack of circles.
  Object circle4(Object::NewCircle(vec2(100, 100), 90.f, 35.f, red_));
  objects.push_back(circle4);

  Object circle5(Object::NewCircle(vec2(100, 100), 80.f, 45.f, color2_));
  objects.push_back(circle5);

  Object circle6(Object::NewCircle(vec2(100, 100), 70.f, 55.f, color1_));
  objects.push_back(circle6);

  Object circle7(Object::NewCircle(vec2(100, 100), 60.f, 65.f, red_));
  objects.push_back(circle7);

  Object circle8(Object::NewCircle(vec2(100, 100), 50.f, 75.f, color2_));
  objects.push_back(circle8);

  Object circle9(Object::NewCircle(vec2(100, 100), 40.f, 85.f, color1_));
  objects.push_back(circle9);

  // Rounded rect.
  Object round_rect1(vec3(300, 700, 30.f), rounded_rect1_, gradient_);
  objects.push_back(round_rect1);

  // Sphere.
  Object sphere(vec3(800, 300, 0.f), sphere_, color1_);
  objects.push_back(sphere);

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_time(current_time_sec);

  // The following code allows the scene to be rendered in both the Waterfall
  // and Waterfall2 demos.  In the near-ish future, only Waterfall2 will remain,
  // and this method signature will be changed to no longer return a Model.
  // Therefore it will no longer be necessary to collect these objects in a
  // vector.
  if (render_queue) {
    render_queue->PushObject(circle1);
    render_queue->PushObject(circle2);
    render_queue->PushObject(inner_ring);
    render_queue->PushObject(bg_plane);
    render_queue->PushObject(round_rect1);
    render_queue->PushObject(sphere);

    // Animate a clip plane to wipe the stack of circles.
    auto clip_planes =
        escher::ClipPlanes::FromBox(stage->viewing_volume().bounding_box());
    float dist_from_origin = glm::length(vec2(100, 100));
    vec3 clip_dir(1, 1, 0);
    clip_dir = glm::normalize(clip_dir);
    float x_clip = dist_from_origin + 70.f * sin(current_time_sec * 1.5);
    clip_planes.planes[0] = escher::vec4(-clip_dir, x_clip);
    render_queue->SetClipPlanes(clip_planes);

    render_queue->PushObject(circle4);
    render_queue->PushObject(circle5);
    render_queue->PushObject(circle6);
    render_queue->PushObject(circle7);
    render_queue->PushObject(circle8);
    render_queue->PushObject(circle9);
  }

  return model_.get();
}
