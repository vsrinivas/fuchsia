// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/paper_demo_scene1.h"

#include "lib/escher/geometry/clip_planes.h"
#include "lib/escher/geometry/plane_ops.h"
#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/geometry/types.h"
#include "lib/escher/material/material.h"
#include "lib/escher/math/lerp.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/shape/modifier_wobble.h"
#include "lib/escher/util/alloca.h"
#include "lib/escher/util/stopwatch.h"
#include "lib/escher/vk/image.h"
#include "lib/escher/vk/texture.h"
#include "lib/escher/vk/vulkan_context.h"

using escher::MeshAttribute;
using escher::MeshSpec;
using escher::Object;
using escher::RoundedRectSpec;
using escher::ShapeModifier;

using escher::plane2;
using escher::vec2;
using escher::vec3;

PaperDemoScene1::PaperDemoScene1(Demo* demo) : Scene(demo) {}

void PaperDemoScene1::Init(escher::Stage* stage) {
  red_ = fxl::MakeRefCounted<escher::Material>();
  bg_ = fxl::MakeRefCounted<escher::Material>();
  color1_ = fxl::MakeRefCounted<escher::Material>();
  color2_ = fxl::MakeRefCounted<escher::Material>();
  red_->set_color(vec3(0.98f, 0.15f, 0.15f));
  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));
  color1_->set_color(vec3(63.f / 255.f, 138.f / 255.f, 153.f / 255.f));
  color2_->set_color(vec3(143.f / 255.f, 143.f / 255.f, 143.f / 255.f));

  // Generate animated rounded rectangles.  Both their position and shape are
  // animated.
  for (int i = 0; i < 10; ++i) {
    const float x = 20.f * i;
    const float y = 80.f * i;
    const float z = 10.f * i;
    const float big_radius = 75.f;
    const float tiny_radius = 25.f;
    rectangles_.push_back(RectState{
        .animation = {.cycle_duration = 5.f + i,
                      .cycle_count_before_pause = 3,
                      .inter_cycle_pause_duration = 5 - 0.4f * i},
        .material = (i % 2) ? color1_ : red_,
        .pos1 = vec3(400 - x, 400 + y, 7.5f + z),
        .pos2 = vec3(1800 + x, 400 + y, 7.5f + z),
        .spec1 = {350, 250, big_radius, tiny_radius, big_radius, tiny_radius},
        .spec2 = {120, 450, tiny_radius, big_radius, tiny_radius, big_radius},
    });
  }

  // Generate animated clip-planes to clip the above rounded-rectangles.
  object_space_clip_planes_.push_back(ClipPlaneState{
      .animation = {.cycle_duration = 9.f,
                    .cycle_count_before_pause = 2,
                    .inter_cycle_pause_duration = 5},
      .pos1 = vec2(-200, -100),
      .pos2 = vec2(200, 200),
      .radians1 = -M_PI / 6,
      .radians2 = M_PI * 7 / 6,
  });
  world_space_clip_planes_.push_back(ClipPlaneState{
      .animation = {.cycle_duration = 2.f,
                    .cycle_count_before_pause = 3,
                    .inter_cycle_pause_duration = 6},
      .pos1 = vec2(0, 0),
      .pos2 = vec2(2000, 0),
      .radians1 = 0,
      .radians2 = 0,
  });
}

PaperDemoScene1::~PaperDemoScene1() {}

escher::Model* PaperDemoScene1::Update(const escher::Stopwatch& stopwatch,
                                       uint64_t frame_count,
                                       escher::Stage* stage,
                                       escher::PaperRenderer2* renderer) {
  FXL_CHECK(renderer)
      << "PaperDemoScene1 can only be rendered via PaperRenderer2.";
  auto render_queue = renderer->render_queue();
  auto shape_cache = renderer->shape_cache();

  float current_time_sec = stopwatch.GetElapsedSeconds();

  float screen_width = stage->viewing_volume().width();
  float screen_height = stage->viewing_volume().height();

  // Create our background plane.
  Object bg_plane(
      Object::NewRect(vec3(0, 0, 0), vec2(screen_width, screen_height), bg_));
  render_queue->PushObject(bg_plane);

  // Render clipped rounded rectangles obtained from PaperShapeCache.
  {
    const size_t num_world_space_clip_planes = world_space_clip_planes_.size();
    const size_t num_object_space_clip_planes =
        object_space_clip_planes_.size();
    const size_t num_clip_planes =
        num_world_space_clip_planes + num_object_space_clip_planes;

    // Allocate enough space for all clip-planes, including additional
    // scratch-space for world-space clip-planes, which must be transformed for
    // each object.
    plane2* clip_planes =
        ESCHER_ALLOCA(plane2, num_clip_planes + num_world_space_clip_planes);
    plane2* untransformed_world_space_clip_planes =
        clip_planes + num_clip_planes;

    // Animate the clip-planes.
    for (size_t i = 0; i < num_world_space_clip_planes; ++i) {
      untransformed_world_space_clip_planes[i] =
          world_space_clip_planes_[i].Update(current_time_sec);
    }
    for (size_t i = 0; i < num_object_space_clip_planes; ++i) {
      clip_planes[i + num_world_space_clip_planes] =
          object_space_clip_planes_[i].Update(current_time_sec);
    }

    // Animate and render the clipped rounded-rectangles.
    for (auto& rect : rectangles_) {
      const float t = rect.animation.Update(current_time_sec);
      const vec3 position = escher::Lerp(rect.pos1, rect.pos2, t);
      const RoundedRectSpec rect_spec = escher::Lerp(rect.spec1, rect.spec2, t);

      // Translate the world-space clip planes into the current rectangle's
      // object-space.
      for (size_t i = 0; i < num_world_space_clip_planes; ++i) {
        clip_planes[i] =
            TranslatePlane(position, untransformed_world_space_clip_planes[i]);
      }

      if (auto* mesh = shape_cache->GetRoundedRectMesh(rect_spec, clip_planes,
                                                       num_clip_planes)) {
        render_queue->PushObject(
            escher::Object(position, escher::MeshPtr(mesh), rect.material));
      }
    }
  }

  // Stack of circles, with an animated clip plane (these use vertex shader clip
  // planes, not the CPU clipping used by PaperShapeCache).
  const vec2 kCenterOfStack(100, 100);
  auto clip_planes =
      escher::ClipPlanes::FromBox(stage->viewing_volume().bounding_box());
  float dist_from_origin = glm::length(kCenterOfStack);
  vec3 clip_dir(1, 1, 0);
  clip_dir = glm::normalize(clip_dir);
  float x_clip = dist_from_origin + 70.f * sin(current_time_sec * 1.5);
  clip_planes.planes[0] = escher::vec4(-clip_dir, x_clip);
  render_queue->SetClipPlanes(clip_planes);
  render_queue->PushObject(Object::NewCircle(kCenterOfStack, 90, 35, red_));
  render_queue->PushObject(Object::NewCircle(kCenterOfStack, 80, 45, color2_));
  render_queue->PushObject(Object::NewCircle(kCenterOfStack, 70, 55, color1_));
  render_queue->PushObject(Object::NewCircle(kCenterOfStack, 60, 65, red_));
  render_queue->PushObject(Object::NewCircle(kCenterOfStack, 50, 75, color2_));
  render_queue->PushObject(Object::NewCircle(kCenterOfStack, 40, 85, color1_));

  // Never used.
  static escher::Model model{std::vector<escher::Object>()};
  return &model;
}

float PaperDemoScene1::AnimatedState::Update(float current_time_sec) {
  float t = 0.f;

  const float time_in_state = current_time_sec - state_start_time;

  if (paused) {
    // Paused, see if it is time to resume action.
    if (time_in_state > inter_cycle_pause_duration) {
      paused = false;
      state_start_time = current_time_sec;
    }
  } else if (time_in_state > cycle_duration * cycle_count_before_pause) {
    // Was running, now paused.
    paused = true;
    state_start_time = current_time_sec;
  } else {
    t = 0.5f - 0.5f * cos(time_in_state / cycle_duration * 2.f * M_PI);
  }

  return t;
}

escher::plane2 PaperDemoScene1::ClipPlaneState::Update(float current_time_sec) {
  const float t = animation.Update(current_time_sec);
  const vec2 pos = escher::Lerp(pos1, pos2, t);
  const float radians = escher::Lerp(radians1, radians2, t);
  const vec2 dir(cos(radians), sin(radians));
  return plane2(pos, dir);
}
