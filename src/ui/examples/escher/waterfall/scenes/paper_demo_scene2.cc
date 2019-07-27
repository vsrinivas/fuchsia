// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/waterfall/scenes/paper_demo_scene2.h"

#include "src/ui/lib/escher/debug/debug_rects.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/scene/model.h"

PaperDemoScene2::PaperDemoScene2(Demo* demo) : Scene(demo) {}

void PaperDemoScene2::Init(escher::PaperScene* scene) {}

PaperDemoScene2::~PaperDemoScene2() {}

void PaperDemoScene2::Update(const escher::Stopwatch& stopwatch, uint64_t frame_count,
                             escher::PaperScene* scene, escher::PaperRenderer* renderer) {
  // Draws graph outline
  renderer->DrawDebugGraph("TIME", "FRAMES", escher::DebugRects::kWhite);

  // Testing Graph Data Drawing
  const float width = scene->bounding_box.width();
  const float height = scene->bounding_box.height();

  int x = 160;
  int y = height - 100;
  int bar_height = height - 110;
  int bar_width = 50;  // related to the number of frames that event occurs

  for (int i = 0; i < 79; i++) {
    renderer->DrawVLine(escher::DebugRects::kPurple, x, y, bar_height, bar_width);

    if (x <= width - 200)
      x += 20;
    else
      x = 160;
    if (bar_height >= 100)
      bar_height -= 10;
    else
      bar_height = height - 100;
  }
  renderer->DrawHLine(escher::DebugRects::kGreen, 475, 160, width - 150, 5);
}
