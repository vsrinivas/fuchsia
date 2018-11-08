// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_EXAMPLES_SWAP_CPP_MODULE_H_
#define PERIDOT_EXAMPLES_SWAP_CPP_MODULE_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/ui/base_view/cpp/v1_base_view.h>
#include <zx/eventpair.h>

#include "peridot/lib/fidl/single_service_app.h"

namespace modular_example {

class ModuleView : public scenic::V1BaseView {
 public:
  explicit ModuleView(scenic::ViewContext view_context, uint32_t color);

 private:
  // |scenic::V1BaseView|
  void OnPropertiesChanged(
      fuchsia::ui::viewsv1::ViewProperties old_properties) override;

  scenic::ShapeNode background_node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleView);
};

class ModuleApp : public modular::ViewApp {
 public:
  using CreateViewCallback =
      std::function<scenic::V1BaseView*(scenic::ViewContext view_context)>;

  explicit ModuleApp(component::StartupContext* const startup_context,
                     CreateViewCallback create);

 private:
  // |ViewApp|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services)
      override;

  CreateViewCallback create_;
  std::unique_ptr<scenic::V1BaseView> view_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleApp);
};

}  // namespace modular_example

#endif  // PERIDOT_EXAMPLES_SWAP_CPP_MODULE_H_
