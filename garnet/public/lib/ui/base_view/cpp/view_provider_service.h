// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_SERVICE_H_
#define LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_SERVICE_H_

#include <fuchsia/ui/app/cpp/fidl.h>
#include <vector>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fit/function.h"
#include "lib/ui/base_view/cpp/base_view.h"
#include "lib/ui/base_view/cpp/v1_base_view.h"

namespace scenic {

// A callback to create a view when given a context.
using ViewFactory =
    fit::function<std::unique_ptr<BaseView>(ViewContext context)>;

// A callback to create an older V1 view when given a context.
//
// Deprecated.
using V1ViewFactory =
    fit::function<std::unique_ptr<V1BaseView>(ViewContext context)>;

// Publishes a view provider as an outgoing service of the application.
// The views created by the view provider are owned by it and will be destroyed
// when the view provider itself is destroyed.
//
// This is only intended to be used for simple example programs.
class ViewProviderService : private fuchsia::ui::app::ViewProvider,
                            private fuchsia::ui::viewsv1::ViewProvider {
 public:
  ViewProviderService(component::StartupContext* startup_context,
                      fuchsia::ui::scenic::Scenic* scenic, ViewFactory factory);
  ViewProviderService(component::StartupContext* startup_context,
                      fuchsia::ui::scenic::Scenic* scenic,
                      V1ViewFactory factory);

  ~ViewProviderService();

  // |fuchsia::ui::viewsv1::ViewProvider|
  void CreateView(
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> view_owner,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> services) override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services)
      override;

 private:
  ViewProviderService(component::StartupContext* startup_context,
                      fuchsia::ui::scenic::Scenic* scenic);
  ViewProviderService(const ViewProviderService&) = delete;
  ViewProviderService& operator=(const ViewProviderService&) = delete;

  component::StartupContext* const startup_context_;
  fuchsia::ui::scenic::Scenic* const scenic_;

  ViewFactory view_factory_ = nullptr;
  std::vector<std::unique_ptr<BaseView>> views_;
  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;

  V1ViewFactory old_view_factory_ = nullptr;
  std::vector<std::unique_ptr<V1BaseView>> old_views_;
  fidl::BindingSet<fuchsia::ui::viewsv1::ViewProvider> old_bindings_;
};

}  // namespace scenic

#endif  // LIB_UI_BASE_VIEW_CPP_VIEW_PROVIDER_SERVICE_H_
