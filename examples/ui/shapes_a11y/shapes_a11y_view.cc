// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shapes_a11y/shapes_a11y_view.h"

namespace examples {

namespace {
constexpr float kBackgroundElevation = 0.f;
}  // namespace

ShapesA11yView::ShapesA11yView(
    ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request)
    : BaseView(std::move(view_manager), std::move(view_owner_request),
               "Shapes_A11y"),
      background_node_(session()) {
  scenic::Material background_material(session());
  background_material.SetColor(0xf2, 0xd8, 0x5b, 0xff);
  background_node_.SetMaterial(background_material);
  parent_node().AddChild(background_node_);

  StartA11yClient();
}

ShapesA11yView::~ShapesA11yView() {}

void ShapesA11yView::StartA11yClient() {
  fuchsia::sys::ServiceProviderPtr provider_ptr;
  a11y_provider_.AddBinding(provider_ptr.NewRequest());
  a11y_provider_.AddServiceForName(
      [this](zx::channel c) {
        fidl::InterfaceRequest<fuchsia::ui::a11y::A11yClient> request(
            std::move(c));
        this->a11y_client_app_.AddBinding(std::move(request));
      },
      fuchsia::ui::a11y::A11yClient::Name_);
  auto names = fidl::VectorPtr<fidl::StringPtr>::New(0);
  names.push_back(fuchsia::ui::a11y::A11yClient::Name_);
  view()->OfferServiceProvider(provider_ptr.Unbind(), std::move(names));
}

void ShapesA11yView::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  if (!has_logical_size())
    return;

  const float center_x = logical_size().width * .5f;
  const float center_y = logical_size().height * .5f;

  scenic::Rectangle background_shape(session(), logical_size().width,
                                         logical_size().height);
  background_node_.SetShape(background_shape);
  background_node_.SetTranslation(center_x, center_y, kBackgroundElevation);
}

}  // namespace examples
