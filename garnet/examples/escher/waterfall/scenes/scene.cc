// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/scene.h"

#include "lib/escher/paper/paper_scene.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/scene/stage.h"

Scene::Scene(Demo* demo) : demo_(demo) {}

Scene::~Scene() {}

void Scene::Init(escher::PaperScene* scene) {
  const auto& box = scene->bounding_box;
  FXL_DCHECK(!box.is_empty());

  escher::Stage stage;
  stage.set_viewing_volume(escher::ViewingVolume(box.width(), box.height(),
                                                 box.max().z, box.min().z));
  Init(&stage);
}

void Scene::Update(const escher::Stopwatch& stopwatch, uint64_t frame_count,
                   escher::PaperScene* scene,
                   escher::PaperRenderer* renderer) {
  const auto& box = scene->bounding_box;
  FXL_DCHECK(!box.is_empty());

  escher::Stage stage;
  stage.set_viewing_volume(escher::ViewingVolume(box.width(), box.height(),
                                                 box.max().z, box.min().z));

  escher::Model* model = Update(stopwatch, frame_count, &stage, renderer);
  for (auto& obj : model->objects()) {
    renderer->DrawLegacyObject(obj);
  }
}
