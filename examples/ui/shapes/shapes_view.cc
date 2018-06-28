// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shapes/shapes_view.h"

#include "lib/fxl/logging.h"

namespace examples {

namespace {
constexpr float kBackgroundElevation = 0.f;
constexpr float kCardElevation = 2.f;
constexpr float kCardCornerRadius = 8.f;
constexpr float kCircleElevation = 8.f;
constexpr float kCircleRadius = 40.f;
}  // namespace

ShapesView::ShapesView(
    ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request),
               "Shapes"),
      background_node_(session()),
      card_node_(session()),
      circle_node_(session()) {
  scenic::Material background_material(session());
  background_material.SetColor(0x21, 0x21, 0x21, 0xff);  // Grey 900
  background_node_.SetMaterial(background_material);
  parent_node().AddChild(background_node_);

  scenic::Material card_material(session());
  card_material.SetColor(0x67, 0x3a, 0xb7, 0xff);  // Deep Purple 500
  card_node_.SetMaterial(card_material);
  parent_node().AddChild(card_node_);

  scenic::Material circle_material(session());
  circle_material.SetColor(0xf5, 0x00, 0x57, 0xff);  // Pink A400
  circle_node_.SetMaterial(circle_material);
  parent_node().AddChild(circle_node_);
}

ShapesView::~ShapesView() {}

void ShapesView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size())
    return;

  const float center_x = logical_size().width * .5f;
  const float center_y = logical_size().height * .5f;

  scenic::Rectangle background_shape(session(), logical_size().width,
                                         logical_size().height);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(center_x, center_y, kBackgroundElevation);

  scenic::RoundedRectangle card_shape(session(), logical_size().width * .9f,
                                          logical_size().height * .9f,
                                          kCardCornerRadius, kCardCornerRadius,
                                          kCardCornerRadius, kCardCornerRadius);
  card_node_.SetShape(card_shape);
  card_node_.SetTranslation((float[]){center_x, center_y, kCardElevation});

  scenic::Circle circle_shape(session(), kCircleRadius);
  circle_node_.SetShape(circle_shape);
  circle_node_.SetTranslation(logical_size().width * .85f,
                              logical_size().height * .85f, kCircleElevation);
}

}  // namespace examples
