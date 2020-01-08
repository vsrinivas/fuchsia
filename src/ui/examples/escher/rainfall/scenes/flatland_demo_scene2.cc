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
  renderables_.resize(100);
  fall_speed_.resize(100);
  for (uint32_t i = 0; i < 100; i++) {
    int x_orig = rand() % 2160;
    int y_orig = -(rand() % 1000);
    uint32_t width = rand() % (230 - 100 + 1) + 100;
    uint32_t height = rand() % (150 - 70 + 1) + 70;

    RectangleDestinationSpec dest = {
        .origin = vec2(x_orig, y_orig),
        .extent = vec2(width, height),
    };

    float r = ((double)rand() / (RAND_MAX));
    float g = ((double)rand() / (RAND_MAX));
    float b = ((double)rand() / (RAND_MAX));
    float a = ((double)rand() / (RAND_MAX));

    RectangleRenderable renderable = {
        .source = RectangleSourceSpec(),
        .dest = dest,
        .texture = demo_->default_texture(),
        .color = vec4(r, g, b, a),
        .is_transparent = true,
    };

    renderables_[i] = renderable;

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
  for (uint32_t i = 0; i < 100; i++) {
    renderables_[i].dest.origin.y += fall_speed_[i];
    if (renderables_[i].dest.origin.y >= 1140) {
      int x_orig = rand() % 2160;
      int y_orig = -(rand() % 1000);

      float r = ((double)rand() / (RAND_MAX));
      float g = ((double)rand() / (RAND_MAX));
      float b = ((double)rand() / (RAND_MAX));
      float a = ((double)rand() / (RAND_MAX));

      renderables_[i].dest.origin = vec2(x_orig, y_orig);
      renderables_[i].color = vec4(r, g, b, a);
    }
  }
}
