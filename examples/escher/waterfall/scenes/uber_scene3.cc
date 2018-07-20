// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/uber_scene3.h"

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

UberScene3::UberScene3(Demo* demo) : Scene(demo) {}

void UberScene3::Init(escher::Stage* stage) {
  bg_ = fxl::MakeRefCounted<escher::Material>();
  color1_ = fxl::MakeRefCounted<escher::Material>();
  color2_ = fxl::MakeRefCounted<escher::Material>();
  bg_->set_color(vec3(0.8f, 0.8f, 0.8f));
  color1_->set_color(vec3(157.f / 255.f, 183.f / 255.f, 189.f / 255.f));
  color2_->set_color(vec3(63.f / 255.f, 138.f / 255.f, 153.f / 255.f));

  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kPositionOffset |
                MeshAttribute::kPerimeterPos | MeshAttribute::kUV};
  ring_mesh_ = escher::NewRingMesh(escher(), spec, 5, vec2(0.f, 0.f), 75.f,
                                   55.f, 18.f, -15.f);
}

UberScene3::~UberScene3() {}

escher::Model* UberScene3::Update(const escher::Stopwatch& stopwatch,
                                  uint64_t frame_count, escher::Stage* stage,
                                  escher::PaperRenderQueue* render_queue) {
  float current_time_sec = stopwatch.GetElapsedSeconds();

  float screen_width = stage->viewing_volume().width();
  float screen_height = stage->viewing_volume().height();
  float min_height = 5.f;
  float max_height = 30.f;
  float elevation_range = max_height - min_height;

  std::vector<Object> objects;

  constexpr float PI = 3.14159265359f;
  constexpr float TWO_PI = PI * 2.f;
  float hex_circle_diameter = 170.f;
  float hex_circle_radius = hex_circle_diameter / 2.0f;
  float col_width = hex_circle_radius / tan(30.f * 180.f / PI);

  float num_rows_f = screen_height / hex_circle_radius;
  float num_cols_f = screen_width / col_width;

  int num_rows = num_rows_f;
  int num_cols = num_cols_f;

  float hex_current_x_pos = 0.f;
  float hex_current_y_pos = 0.f;
  float hex_x_offset = 0.f;

  float time_mult = 2.f;

  int circle_index = 0;
  int is_even = 0;

  for (int i = 0; i <= num_rows; i++) {
    hex_current_y_pos = i * hex_circle_diameter;
    if (fmod(i, 2) == 0) {
      is_even = 1;
      hex_x_offset = hex_circle_radius;
    } else {
      is_even = 0;
      hex_x_offset = 0.f;
    }

    for (int ii = 0; ii <= num_cols; ii++) {
      float time_offset = ii * 0.2f;
      float circle_elevation = 2.f;
      float circle_scale =
          (sin((current_time_sec + time_offset) * 1.25f) * .5f + .5f) * .5f +
          .5f;
      float circle_scale_alt =
          (cos((current_time_sec + (time_offset * 1.25f)) * 1.5f) * .5f + .5f) *
              .6f +
          .5f;

      hex_current_x_pos = ii * col_width + hex_x_offset;

      if (is_even == 1) {
        circle_elevation =
            sin(current_time_sec + time_offset * time_mult) * elevation_range +
            min_height + (elevation_range / 1.f);
      } else {
        circle_elevation =
            cos(current_time_sec + time_offset * time_mult) * elevation_range +
            min_height + (elevation_range / 1.f);
      }

      Object circle(Object::NewCircle(
          vec3(hex_current_x_pos, hex_current_y_pos, circle_elevation),
          hex_circle_radius * circle_scale, color2_));
      objects.push_back(circle);

      Object circle_bg(Transform(vec3(hex_current_x_pos, hex_current_y_pos,
                                      circle_elevation - 4.f),
                                 vec3(circle_scale_alt, circle_scale_alt, 1.f)),
                       ring_mesh_, color1_);
      circle_bg.set_shape_modifiers(ShapeModifier::kWobble);
      escher::ModifierWobble wobble_data{
          {{-0.3f * TWO_PI, 0.1f, 7.f * TWO_PI},
           {-0.2f * TWO_PI, 0.05f, 23.f * TWO_PI},
           {1.f * TWO_PI, 0.25f, 5.f * TWO_PI}}};
      circle_bg.set_shape_modifier_data(wobble_data);
      objects.push_back(circle_bg);

      circle_index++;
    }
  }

  Object rectangle(Object::NewRect(
      vec2(0.f, 0.f), vec2(screen_width, screen_height), 1.f, bg_));

  objects.push_back(rectangle);

  // Create the Model
  model_ = std::unique_ptr<escher::Model>(new escher::Model(objects));
  model_->set_time(current_time_sec);

  return model_.get();
}
