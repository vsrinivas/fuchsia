// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/rainfall/scenes/flatland_demo_scene1.h"

using namespace escher;

namespace {

uint32_t orientation = 1;
int32_t radius = 300;

void CreateRing(std::vector<Rectangle2D>& renderables,
                std::vector<RectangleCompositor::ColorData>& color_datas, float time) {
  vec4 colors[3] = {vec4{1, 0, 0, 0.5}, vec4(0, 0, 1, 0.5), vec4(1, 1, 0, 0.5)};

  uint32_t center_x = (2160 - 50) / 2;
  uint32_t center_y = (1140 - 50) / 2;

  const uint16_t num = 12;
  float radians = (360.f / num) * 3.1415926f / 180.f;

  if (abs(radius) == 300) {
    orientation *= -1;
  }
  radius += orientation * 1;

  double curr_offset = time;
  renderables.clear();
  color_datas.clear();
  for (uint32_t i = 0; i < num; i++) {
    float x = static_cast<float>(radius * cos(curr_offset) + center_x);
    float y = static_cast<float>(radius * sin(curr_offset) + center_y);

    Rectangle2D renderable(vec2(x, y), vec2(100, 100));
    RectangleCompositor::ColorData color_data(colors[i % 3], true);

    renderables.emplace_back(renderable);
    color_datas.emplace_back(color_data);

    curr_offset += radians;
  }
}

}  // anonymous namespace

FlatlandDemoScene1::FlatlandDemoScene1(RainfallDemo* demo) : Scene(demo) {}

FlatlandDemoScene1::~FlatlandDemoScene1() {}

void FlatlandDemoScene1::Init() { CreateRing(renderables_, color_data_, 0.f); }

void FlatlandDemoScene1::Update(const escher::Stopwatch& stopwatch) {
  const float current_time_sec = static_cast<float>(stopwatch.GetElapsedSeconds());
  CreateRing(renderables_, color_data_, current_time_sec);
}
