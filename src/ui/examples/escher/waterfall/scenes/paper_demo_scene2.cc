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

void PaperDemoScene2::Update(const escher::Stopwatch& stopwatch, escher::PaperScene* scene,
                             escher::PaperRenderer* renderer) {
  // Draws graph outline
  renderer->DrawDebugGraph("TIME", "FRAMES", escher::DebugRects::kWhite);

  int min = 5;
  int max = 20;

  escher::PaperRenderer::TimeStamp ts;
  ts.latch_point = rand() % 15 + 1;
  ts.update_done = rand() % 15 + 1;
  ts.render_start = rand() % 10 + 5;
  ts.render_done = ts.render_start + min + rand() % (max - min);
  ts.target_present = rand() % 15 + 1;
  ts.actual_present = (rand() % 15 + 1) + (rand() % -2 + 2);

  renderer->AddDebugTimeStamp(ts);
}
