// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/spinning_square/spinning_square_view.h"

#include <math.h>

#include <algorithm>

namespace examples {
namespace {
constexpr float kBackgroundElevation = 0.f;
constexpr float kSquareElevation = 8.f;

constexpr float kSpeed = 0.25f;
constexpr float kSecondsPerNanosecond = .000'000'001f;
}  // namespace

SpinningSquareView::SpinningSquareView(scenic::ViewContext context)
    : BaseView(std::move(context), "Spinning Square"),
      background_node_(session()),
      square_node_(session()) {
  scenic::Material background_material(session());
  background_material.SetColor(0x67, 0x3a, 0xb7, 0xff);  // Deep Purple 500
  background_node_.SetMaterial(background_material);
  root_node().AddChild(background_node_);

  scenic::Material square_material(session());
  square_material.SetColor(0xf5, 0x00, 0x57, 0xff);  // Pink A400
  square_node_.SetMaterial(square_material);
  root_node().AddChild(square_node_);
}

SpinningSquareView::~SpinningSquareView() {}

void SpinningSquareView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size())
    return;

  uint64_t presentation_time = presentation_info.presentation_time;
  if (!start_time_)
    start_time_ = presentation_time;

  const float center_x = logical_size().x * .5f;
  const float center_y = logical_size().y * .5f;
  const float square_size = std::min(logical_size().x, logical_size().y) * .6f;
  const float t = fmod(
      static_cast<float>(presentation_time - start_time_) * kSecondsPerNanosecond * kSpeed, 1.f);
  const float angle = t * static_cast<float>(M_PI) * 2.f;

  scenic::Rectangle background_shape(session(), logical_size().x, logical_size().y);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation({center_x, center_y, -kBackgroundElevation});

  scenic::Rectangle square_shape(session(), square_size, square_size);
  square_node_.SetShape(square_shape);
  square_node_.SetTranslation({center_x, center_y, -kSquareElevation});
  square_node_.SetRotation({0.f, 0.f, sinf(angle * .5f), cosf(angle * .5f)});

  InvalidateScene();
}

}  // namespace examples
