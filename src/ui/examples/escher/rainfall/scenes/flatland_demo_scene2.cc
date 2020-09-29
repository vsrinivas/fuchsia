// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/rainfall/scenes/flatland_demo_scene2.h"

using namespace escher;

FlatlandDemoScene2::FlatlandDemoScene2(RainfallDemo* demo) : Scene(demo) {}

FlatlandDemoScene2::~FlatlandDemoScene2() {}

// Initializes 100 rectangles with random colors,
// widths between (100-230), heights between (70-150)
// and with origin points above the top of the screen.
// This function also gives every rectangle a variable
// "fall" speed.
void FlatlandDemoScene2::Init() {
  fall_speed_.resize(100);
  for (uint32_t i = 0; i < 100; i++) {
    int x_orig = rand() % 2160;
    int y_orig = -(rand() % 1000);
    uint32_t width = rand() % (230 - 100 + 1) + 100;
    uint32_t height = rand() % (150 - 70 + 1) + 70;

    float r = static_cast<float>(static_cast<double>(rand()) / (RAND_MAX));
    float g = static_cast<float>(static_cast<double>(rand()) / (RAND_MAX));
    float b = static_cast<float>(static_cast<double>(rand()) / (RAND_MAX));
    float a = static_cast<float>(static_cast<double>(rand()) / (RAND_MAX));

    Rectangle2D renderable(vec2(x_orig, y_orig), vec2(width, height));
    RectangleCompositor::ColorData color_data(vec4(r, g, b, a), true);
    renderables_.emplace_back(renderable);
    color_data_.emplace_back(color_data);
    fall_speed_[i] = rand() % 6 + 1;
  }
}

// On every update tick, update the origin-y component of each
// rectangle by its fall speed. If the rectangle reaches the
// bottom of the frame, transport it back up to the top of the
// frame, with a new random position and color, to give the
// illusion of a limitless number of rectangles that continue
// to fall endlessly.
void FlatlandDemoScene2::Update(const escher::Stopwatch& stopwatch) {
  std::vector<Rectangle2D> renderables;
  std::vector<RectangleCompositor::ColorData> color_data;
  for (uint32_t i = 0; i < 100; i++) {
    vec2 origin = renderables_[i].origin;
    vec2 extent = renderables_[i].extent;
    vec4 color = color_data_[i].color;
    bool transparent = color_data_[i].is_transparent;
    origin.y += static_cast<float>(fall_speed_[i]);
    if (renderables_[i].origin.y >= 1140) {
      int x_orig = rand() % 2160;
      int y_orig = -(rand() % 1000);

      float r = static_cast<float>(static_cast<double>(rand()) / (RAND_MAX));
      float g = static_cast<float>(static_cast<double>(rand()) / (RAND_MAX));
      float b = static_cast<float>(static_cast<double>(rand()) / (RAND_MAX));
      float a = static_cast<float>(static_cast<double>(rand()) / (RAND_MAX));

      origin = vec2(x_orig, y_orig);
      color = vec4(r, g, b, a);
    }

    Rectangle2D renderable(origin, extent);
    RectangleCompositor::ColorData meta(color, transparent);
    renderables.emplace_back(renderable);
    color_data.emplace_back(meta);
  }

  renderables_.swap(renderables);
  color_data_.swap(color_data);
}
