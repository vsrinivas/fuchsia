// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "examples/waterfall/scenes/shadow_test_scene.h"

#include "escher/base/arraysize.h"
#include "escher/renderer.h"

namespace {

constexpr int kElevations[] = {
    1, 2, 3, 4, 6, 8, 9, 12, 16, 24,
};

constexpr float kPadding = 20.0f;

}  // namespace

ShadowTestScene::ShadowTestScene() {
  card_material_.set_color(
      escher::MakeConstantBinding(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)));
}

ShadowTestScene::~ShadowTestScene() {}

escher::Model ShadowTestScene::GetModel(const escher::SizeI& size) {
  std::vector<escher::Object> objects;

  float center = size.width() / 2.0f;

  float left[] = {
      kPadding, center + kPadding,
  };

  float top = kPadding;
  float tile_size = center - kPadding - kPadding;

  objects.emplace_back(
      escher::Shape::CreateRect(glm::vec2(0.0f, 0.0f),
                                glm::vec2(size.width(), size.height()), 0.0f),
      &card_material_);

  for (int i = 0; i < arraysize(kElevations); ++i) {
    objects.emplace_back(escher::Shape::CreateRect(
                             glm::vec2(left[i % 2], top),
                             glm::vec2(tile_size, tile_size), kElevations[i]),
                         &card_material_);
    if (i % 2 == 1)
      top += tile_size + kPadding + kPadding;
  }

  return escher::Model(std::move(objects));
}
