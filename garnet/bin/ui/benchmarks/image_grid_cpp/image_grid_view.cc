// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/benchmarks/image_grid_cpp/image_grid_view.h"

#include <math.h>

#include <cstdlib>
#include <utility>

namespace image_grid {

namespace {
constexpr float kSecondsPerNanosecond = .000'000'001f;

constexpr float kMinScrollOffset = 0.f;

constexpr float kBackgroundElevation = 0.f;
constexpr float kCardElevation = 8.f;
constexpr float kCardCornerRadius = 8.f;

constexpr int kRows = 3;
constexpr int kColumns = 33;
constexpr int kColumnsPerScreen = 5.f;
}  // namespace

ImageGridView::ImageGridView(scenic::ViewContext view_context)
    : BaseView(std::move(view_context), "Image Grid Benchmark (cpp)"),
      background_node_(session()),
      cards_parent_node_(session()),
      spring_(0.0 /* initial value */, 10.0 /* tension */, 50.0 /* friction */) {}

ImageGridView::~ImageGridView() {}

void ImageGridView::OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size()) {
    return;
  }

  if (max_scroll_offset_ == 0) {
    max_scroll_offset_ = logical_size().x * (kColumns / kColumnsPerScreen);
    spring_.SetTargetValue(max_scroll_offset_);
  }

  if (!scene_created_) {
    CreateScene();
    scene_created_ = true;
  }

  UpdateScene(presentation_info.presentation_time);

  // Animate.
  InvalidateScene();
}

void ImageGridView::CreateScene() {
  root_node().DetachChildren();
  cards_parent_node_.DetachChildren();
  cards_.clear();

  scenic::Material background_material(session());
  background_material.SetColor(0xff, 0xff, 0xff, 0xff);  // White
  background_node_.SetMaterial(background_material);
  root_node().AddChild(background_node_);

  scenic::Rectangle background_shape(session(), logical_size().x, logical_size().y);
  background_node_.SetShape(background_shape);

  background_node_.SetTranslation(logical_size().x * .5f, logical_size().y * .5f,
                                  -kBackgroundElevation);

  root_node().AddChild(cards_parent_node_);

  float x_pos = 0.f;
  for (int i = 0; i < kColumns; i++) {
    float y_pos = 0.f;
    for (int i = 0; i < kRows; i++) {
      float layout_area_width = logical_size().x / kColumnsPerScreen;
      float layout_area_height = logical_size().y / kRows;
      float card_width = layout_area_width * 0.95f;
      float card_height = layout_area_height * 0.95f;

      float center_x = x_pos + layout_area_width / 2.f;
      float center_y = y_pos + layout_area_height / 2.f;

      scenic::ShapeNode card_node(session());
      scenic::Material card_material(session());
      card_material.SetColor(0x67, 0x3a, 0xb7, 0xff);  // Deep Purple 500
      card_node.SetMaterial(card_material);
      cards_parent_node_.AddChild(card_node);

      scenic::RoundedRectangle card_shape(session(), card_width, card_height, kCardCornerRadius,
                                          kCardCornerRadius, kCardCornerRadius, kCardCornerRadius);
      card_node.SetShape(card_shape);
      card_node.SetTranslation({center_x, center_y, -kCardElevation});

      cards_.push_back(std::move(card_node));

      y_pos += layout_area_height;
    }
    x_pos += logical_size().x / 5.f;
  }
}

void ImageGridView::UpdateScene(uint64_t presentation_time) {
  // Update the animation state.
  if (!start_time_) {
    start_time_ = presentation_time;
    last_update_time_ = presentation_time;
  }
  spring_.ElapseTime((presentation_time - last_update_time_) * kSecondsPerNanosecond);
  last_update_time_ = presentation_time;
  x_offset_ = -spring_.GetValue();

  if (spring_.is_done()) {
    if (spring_.target_value() == kMinScrollOffset) {
      spring_.SetTargetValue(max_scroll_offset_);
    } else {
      spring_.SetTargetValue(kMinScrollOffset);
    }
  }

  cards_parent_node_.SetTranslation({x_offset_, 0, 0});
}

}  // namespace image_grid
