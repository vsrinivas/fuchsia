// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHAPES_A11Y_SHAPES_A11Y_VIEW_H_
#define GARNET_EXAMPLES_UI_SHAPES_A11Y_SHAPES_A11Y_VIEW_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/a11y/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "garnet/examples/ui/shapes_a11y/a11y_client_app.h"
#include "lib/component/cpp/service_provider_impl.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/services.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/view_framework/base_view.h"

class SkCanvas;

namespace examples {

class ShapesA11yView : public mozart::BaseView {
 public:
  ShapesA11yView(fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
                 fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
                     view_owner_request);

  ~ShapesA11yView() override;

 private:
  void StartA11yClient();

  // |BaseView|:
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  scenic::ShapeNode background_node_;

  component::ServiceProviderImpl a11y_provider_;
  fuchsia::sys::ComponentControllerPtr controller_;

  examples::A11yClientApp a11y_client_app_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShapesA11yView);
};

}  // namespace examples

#endif  // GARNET_EXAMPLES_UI_SHAPES_A11Y_SHAPES_A11Y_VIEW_H_
