// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/waterfall/scenes/paper_demo_scene1.h"

#include "src/ui/lib/escher/geometry/plane_ops.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/material/material.h"
#include "src/ui/lib/escher/math/lerp.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/scene/model.h"
#include "src/ui/lib/escher/util/alloca.h"
#include "src/ui/lib/escher/util/stopwatch.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/texture.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

using escher::MeshAttribute;
using escher::MeshSpec;
using escher::Object;
using escher::RoundedRectSpec;

using escher::plane3;
using escher::vec2;
using escher::vec3;
using escher::vec4;

PaperDemoScene1::PaperDemoScene1(Demo* demo, const escher::TexturePtr& translucent_texture)
    : Scene(demo), tex_(translucent_texture) {}

void PaperDemoScene1::Init(escher::PaperScene* scene) {
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
  for (uint16_t i = 0; i < 10; ++i) {
    const float x = 20.f * i;
    const float y = 400.f + 80.f * i;
    const float z = -(187.5f - 20.f * i);
    const float big_radius = 75.f;
    const float tiny_radius = 25.f;
    rectangles_.push_back(RectState{
        .animation = {.cycle_duration = 5.f + i,
                      .cycle_count_before_pause = 3,
                      .inter_cycle_pause_duration = 5 - 0.4f * i},
        .material = (i % 2) ? color1_ : red_,
        .pos1 = vec3(400 - x, y, z),
        .pos2 = vec3(1800 + x, y, z),
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
      .radians1 = static_cast<float>(-M_PI / 6),
      .radians2 = static_cast<float>(M_PI * 7 / 6),
  });
  world_space_clip_planes_.push_back(ClipPlaneState{
      .animation = {.cycle_duration = 4.f,
                    .cycle_count_before_pause = 2,
                    .inter_cycle_pause_duration = 5},
      .pos1 = vec2(0, 0.9f * scene->bounding_box.height()),
      .pos2 = vec2(0, 0.15f * scene->bounding_box.height()),
      .radians1 = static_cast<float>(M_PI * 1.5f),
      .radians2 = static_cast<float>(M_PI * 1.5f),
  });

  // Generate animated translucent rounded rectangle, not clipped by any
  // of the planes above.
  {
    translucent_ = fxl::MakeRefCounted<escher::Material>();
    translucent_->set_type(escher::Material::Type::kTranslucent);
    if (tex_) {
      translucent_->SetTexture(tex_);
      translucent_->set_color(vec4(1.f, 1.f, 1.f, 0.7f));
    } else {
      translucent_->set_color(vec4(0.2f, 0.8f, 0.5f, 0.7f));
    }

    const float big_radius = 200.f;
    const float tiny_radius = 30.f;
    translucent_rectangle_ = RectState{
        .animation = {.cycle_duration = 10.f,
                      .cycle_count_before_pause = 1,
                      .inter_cycle_pause_duration = 0.5f},
        .material = translucent_,
        .pos1 = vec3(-600, 0, -90),
        .pos2 = vec3(300, 0, -90),
        .spec1 = {600, 800, big_radius, tiny_radius, big_radius, tiny_radius},
        .spec2 = {1200, 800, tiny_radius, big_radius, tiny_radius, big_radius},
    };
  }
}

PaperDemoScene1::~PaperDemoScene1() {}

void PaperDemoScene1::Update(const escher::Stopwatch& stopwatch, escher::PaperScene* scene,
                             escher::PaperRenderer* renderer) {
  UpdateGraphWithExampleData(scene, renderer);

  const float current_time_sec = static_cast<float>(stopwatch.GetElapsedSeconds());
  const float screen_width = scene->bounding_box.width();
  const float screen_height = scene->bounding_box.height();

  escher::PaperTransformStack* transform_stack = renderer->transform_stack();

  // Create our background plane.  Don't waste GPU cycles casting shadows from
  // it, because there is nothing beneath it.
  transform_stack->PushElevation(-10);
  // Rounded rectangles are centered around their origin.
  transform_stack->PushTranslation(0.5f * vec2(screen_width, screen_height));
  constexpr float kCornerRadius = 30.f;
  renderer->DrawRoundedRect(
      {screen_width, screen_height, kCornerRadius, kCornerRadius, kCornerRadius, kCornerRadius},
      bg_, escher::PaperDrawableFlagBits::kDisableShadowCasting);
  transform_stack->Pop();  // pop translation only

  // Render clipped rounded rectangles obtained from PaperShapeCache.
  {
    const size_t num_world_space_planes = world_space_clip_planes_.size();
    const size_t num_object_space_planes = object_space_clip_planes_.size();

    // Allocate enough space for all clip-planes, including additional
    // scratch-space for world-space clip-planes, which must be transformed for
    // each object.
    plane3* object_space_planes =
        ESCHER_ALLOCA(plane3, num_object_space_planes + num_world_space_planes);
    plane3* world_space_planes = object_space_planes + num_object_space_planes;

    // Animate the clip-planes.
    for (size_t i = 0; i < num_object_space_planes; ++i) {
      object_space_planes[i] = object_space_clip_planes_[i].Update(current_time_sec);
    }
    for (size_t i = 0; i < num_world_space_planes; ++i) {
      world_space_planes[i] = world_space_clip_planes_[i].Update(current_time_sec);
    }

    transform_stack->AddClipPlanes(world_space_planes, num_world_space_planes);

    // Animate and render the clipped rounded-rectangles.
    for (auto& rect : rectangles_) {
      const float t = rect.animation.Update(current_time_sec);
      const vec3 position = escher::Lerp(rect.pos1, rect.pos2, t);
      const RoundedRectSpec rect_spec = escher::Lerp(rect.spec1, rect.spec2, t);

      transform_stack->PushTranslation(position);
      transform_stack->AddClipPlanes(object_space_planes, num_object_space_planes);
      renderer->DrawRoundedRect(rect_spec, rect.material);
      transform_stack->Pop();
    }

    // Pop world-space clip-planes and background-plane elevation.
    transform_stack->Pop();

    // Draw translucent rectangle.
    {
      const float t = translucent_rectangle_.animation.Update(current_time_sec);
      const vec3 base_position(screen_width * 0.5f, screen_height * 0.5f, 0.f);
      const vec3 position =
          base_position + escher::Lerp(translucent_rectangle_.pos1, translucent_rectangle_.pos2, t);
      const RoundedRectSpec rect_spec =
          escher::Lerp(translucent_rectangle_.spec1, translucent_rectangle_.spec2, t);

      transform_stack->PushTranslation(position);
      renderer->DrawRoundedRect(rect_spec, translucent_,
                                escher::PaperDrawableFlagBits::kDisableShadowCasting);
      transform_stack->Pop();
    }
  }

  // Animated stack of circles, and a clip plane.

  const vec3 kInitialCenterOfStack(100, 100, 0);
  transform_stack->PushTranslation(kInitialCenterOfStack);
  plane3 circle_stack_clip_plane(vec3(0, 0, 0), glm::normalize(vec3(1, 1, 0)));
  transform_stack->AddClipPlanes(&circle_stack_clip_plane, 1);
  transform_stack->PushTranslation(vec3(70.f + 70.f * sin(current_time_sec * 1.5), 0, 0));

  transform_stack->PushElevation(35);
  renderer->DrawCircle(90, red_);
  transform_stack->Pop().PushElevation(45);
  renderer->DrawCircle(80, color2_);
  transform_stack->Pop().PushElevation(55);
  renderer->DrawCircle(70, color1_);
  transform_stack->Pop().PushElevation(65);
  renderer->DrawCircle(60, red_);
  transform_stack->Pop().PushElevation(75);
  renderer->DrawCircle(50, color2_);
  transform_stack->Pop().PushElevation(85);
  renderer->DrawCircle(40, color1_);
  transform_stack->Pop();
  transform_stack->Pop();
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
  } else if (time_in_state > cycle_duration * static_cast<float>(cycle_count_before_pause)) {
    // Was running, now paused.
    paused = true;
    state_start_time = current_time_sec;
  } else {
    t = 0.5f - 0.5f * cos(time_in_state / cycle_duration * 2.f * static_cast<float>(M_PI));
  }

  return t;
}

escher::plane3 PaperDemoScene1::ClipPlaneState::Update(float current_time_sec) {
  const float t = animation.Update(current_time_sec);
  const vec2 pos = escher::Lerp(pos1, pos2, t);
  const float radians = escher::Lerp(radians1, radians2, t);
  const vec2 dir(cos(radians), sin(radians));
  return plane3(vec3(pos, 0), vec3(dir, 0));
}
