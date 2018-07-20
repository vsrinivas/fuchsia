// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/ring_tricks1.h"

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
using escher::Transform;
using escher::vec2;
using escher::vec3;

RingTricks1::RingTricks1(Demo* demo) : Scene(demo) {}

void RingTricks1::Init(escher::Stage* stage) {
  bg_ = fxl::MakeRefCounted<escher::Material>();
  color1_ = fxl::MakeRefCounted<escher::Material>();
  color2_ = fxl::MakeRefCounted<escher::Material>();

  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));

  color1_->set_color(vec3(157.f / 255.f, 183.f / 255.f, 189.f / 255.f));
  color2_->set_color(vec3(63.f / 255.f, 138.f / 255.f, 153.f / 255.f));

  // Create meshes for fancy wobble effect.
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                MeshAttribute::kPerimeterPos | MeshAttribute::kUV};

  ring_mesh1_ = escher::NewRingMesh(escher(), spec, 8, vec2(0.f, 0.f), 300.f,
                                    250.f, 18.f, -15.f);
}

RingTricks1::~RingTricks1() {}

escher::Model* RingTricks1::Update(const escher::Stopwatch& stopwatch,
                                   uint64_t frame_count, escher::Stage* stage,
                                   escher::PaperRenderQueue* render_queue) {
  float current_time_sec = stopwatch.GetElapsedSeconds();

  float screen_width = stage->viewing_volume().width();
  float screen_height = stage->viewing_volume().height();
  float min_height = 5.f;
  float max_height = 80.f;
  float elevation_range = max_height - min_height;

  std::vector<Object> objects;

  float circle_elevation =
      (sin(current_time_sec * 1.f) * 0.5f + 0.5f) * elevation_range +
      min_height;

  float outer_ring_scale =
      (cos(current_time_sec * 1.f) * 0.5f + 0.5f) * 1.25 + .5;

  // Create the ring that will do the fancy trick
  vec3 inner_ring_pos(screen_width * 0.5f, screen_height * 0.5f, 15.f);
  Object inner_ring(inner_ring_pos, ring_mesh1_, color1_);
  inner_ring.set_shape_modifiers(ShapeModifier::kWobble);
  objects.push_back(inner_ring);

  // Create the ring that will do the fancy trick
  vec3 outer_ring_pos(screen_width * 0.5f, screen_height * 0.5f,
                      circle_elevation);
  Object outer_ring(
      Transform(outer_ring_pos,
                vec3(outer_ring_scale, outer_ring_scale, outer_ring_scale)),
      ring_mesh1_, color2_);
  outer_ring.set_shape_modifiers(ShapeModifier::kWobble);
  objects.push_back(outer_ring);

  // Create our background plane
  Object bg_plane(Object::NewRect(vec3(0.f, 0.f, 0.f),
                                  vec2(screen_width, screen_height), bg_));

  objects.push_back(bg_plane);

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_time(current_time_sec);

  return model_.get();
}
