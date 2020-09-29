// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/escher/waterfall/scenes/scene.h"

#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/scene/model.h"

Scene::Scene(Demo* demo) : demo_(demo) {}

Scene::~Scene() {}

bool Scene::ToggleGraph() {
  if (graph_) {
    graph_.reset();
    return false;
  } else {
    graph_ = std::make_unique<escher::PaperTimestampGraph>();
    return true;
  }
}

void Scene::UpdateGraphWithExampleData(escher::PaperScene* scene, escher::PaperRenderer* renderer) {
  if (graph_) {
    // Generate fake example data.
    escher::PaperRenderer::Timestamp ts;
    constexpr int min = 5;
    constexpr int max = 20;
    ts.latch_point = rand() % 15 + 1;
    ts.update_done = rand() % 15 + 1;
    ts.render_start = rand() % 10 + 5;
    ts.render_done = ts.render_start + min + rand() % (max - min);
    ts.target_present = rand() % 15 + 1;
    ts.actual_present = (rand() % 15 + 1) + (rand() % -2 + 2);

    graph_->AddTimestamp(ts);

    // Draw graph.
    constexpr uint32_t kInset = 20;
    constexpr uint32_t kGraphHeight = 500;
    graph_->DrawOn(
        renderer,
        {vk::Offset2D(kInset, static_cast<uint32_t>(scene->height() - kInset - kGraphHeight)),
         vk::Extent2D(static_cast<uint32_t>(scene->width() - 2 * kInset), kGraphHeight)});
  }
}
