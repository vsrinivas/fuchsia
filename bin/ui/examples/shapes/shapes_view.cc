// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shapes/shapes_view.h"

#include "lib/ftl/logging.h"

namespace examples {

namespace {
constexpr float kBackgroundElevation = 0.f;
constexpr float kCardElevation = 2.f;
constexpr float kCardCornerRadius = 8.f;
constexpr float kCircleElevation = 8.f;
constexpr float kCircleRadius = 40.f;
}  // namespace

ShapesView::ShapesView(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Shapes"),
      background_node_(session()),
      card_node_(session()),
      circle_node_(session()) {
  mozart::client::Material background_material(session());
  background_material.SetColor(0x21, 0x21, 0x21, 0xff);  // Grey 900
  background_node_.SetMaterial(background_material);
  parent_node().AddChild(background_node_);

  mozart::client::Material card_material(session());
  card_material.SetColor(0x67, 0x3a, 0xb7, 0xff);  // Deep Purple 500
  card_node_.SetMaterial(card_material);
  parent_node().AddChild(card_node_);

  mozart::client::Material circle_material(session());
  circle_material.SetColor(0xf5, 0x00, 0x57, 0xff);  // Pink A400
  circle_node_.SetMaterial(circle_material);
  parent_node().AddChild(circle_node_);
}

ShapesView::~ShapesView() {}

void ShapesView::OnPropertiesChanged(mozart::ViewPropertiesPtr old_properties) {
  if (!has_size())
    return;

  const float center_x = size().width * .5f;
  const float center_y = size().height * .5f;
  float card_corner_radius = kCardCornerRadius * device_pixel_ratio();
  float circle_radius = kCircleRadius * device_pixel_ratio();

  mozart::client::Rectangle background_shape(session(), size().width,
                                             size().height);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(
      (float[]){center_x, center_y, kBackgroundElevation});

  mozart::client::RoundedRectangle card_shape(
      session(), size().width * .9f, size().height * .9f, card_corner_radius,
      card_corner_radius, card_corner_radius, card_corner_radius);
  card_node_.SetShape(card_shape);
  card_node_.SetTranslation((float[]){center_x, center_y, kCardElevation});

  mozart::client::Circle circle_shape(session(), circle_radius);
  circle_node_.SetShape(circle_shape);
  circle_node_.SetTranslation(
      (float[]){size().width * .85f, size().height * .85f, kCircleElevation});

  session()->Present(0, [](mozart2::PresentationInfoPtr info) {});
}

}  // namespace examples
