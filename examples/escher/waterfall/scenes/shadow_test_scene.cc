// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/waterfall/scenes/shadow_test_scene.h"

#include "escher/renderer.h"
#include "lib/fxl/arraysize.h"

using namespace escher;

namespace {

constexpr int kElevations[] = {
    1, 2, 3, 4, 6, 8, 9, 12, 16, 24,
};

constexpr float kPadding = 20.0f;

}  // namespace

ShadowTestScene::ShadowTestScene() {
  card_material_.set_color(MakeConstantBinding(vec4(1.0f, 1.0f, 1.0f, 1.0f)));
}

ShadowTestScene::~ShadowTestScene() {}

Model ShadowTestScene::GetModel(const ViewingVolume& volume) {
  std::vector<Object> objects;

  float center = volume.width() / 2.0f;

  float left[] = {
      kPadding,
      center + kPadding,
  };

  float top = kPadding;
  float tile_size = center - kPadding - kPadding;

  objects.emplace_back(
      Shape::CreateRect(vec2(0.0f, 0.0f), vec2(volume.width(), volume.height()),
                        0.0f),
      &card_material_);

  for (int i = 0; i < arraysize(kElevations); ++i) {
    objects.emplace_back(
        Shape::CreateRect(vec2(left[i % 2], top), vec2(tile_size, tile_size),
                          kElevations[i]),
        &card_material_);
    if (i % 2 == 1)
      top += tile_size + kPadding + kPadding;
  }

  return Model(std::move(objects));
}
