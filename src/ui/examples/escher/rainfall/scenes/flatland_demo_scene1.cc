// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/rainfall/scenes/flatland_demo_scene1.h"

using namespace escher;

namespace {

uint32_t orientation = 1;
int32_t radius = 300;

void CreateRing(std::vector<RectangleRenderable>& renderables, escher::Texture* texture,
                float time) {
  vec4 colors[3] = {vec4{1, 0, 0, 0.5}, vec4(0, 0, 1, 0.5), vec4(1, 1, 0, 0.5)};

  uint32_t center_x = (2160 - 50) / 2;
  uint32_t center_y = (1140 - 50) / 2;

  uint32_t num = renderables.size();
  float radians = (360 / num) * 3.14159265 / 180.0;

  if (abs(radius) == 300) {
    orientation *= -1;
  }
  radius += orientation * 1;

  float curr_offset = time;
  for (uint32_t i = 0; i < num; i++) {
    float x = radius * cos(curr_offset) + center_x;
    float y = radius * sin(curr_offset) + center_y;

    RectangleDestinationSpec dest = {
        .origin = vec2(x, y),
        .extent = vec2(100, 100),
    };

    RectangleRenderable renderable = {
        .source = RectangleSourceSpec(),
        .dest = dest,
        .texture = texture,
        .color = colors[i % 3],
        .is_transparent = true,
    };

    renderables[i] = renderable;

    curr_offset += radians;
  }
}

}  // anonymous namespace

FlatlandDemoScene1::FlatlandDemoScene1(RainfallDemo* demo) : Scene(demo) {}

FlatlandDemoScene1::~FlatlandDemoScene1() {}

void FlatlandDemoScene1::Init() {
  renderables_.resize(12);
  CreateRing(renderables_, demo_->default_texture(), 0.f);
}

void FlatlandDemoScene1::Update(const escher::Stopwatch& stopwatch) {
  const float current_time_sec = stopwatch.GetElapsedSeconds();
  CreateRing(renderables_, demo_->default_texture(), current_time_sec);
}
