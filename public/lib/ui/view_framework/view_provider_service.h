// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_VIEW_FRAMEWORK_VIEW_PROVIDER_SERVICE_H_
#define APPS_MOZART_LIB_VIEW_FRAMEWORK_VIEW_PROVIDER_SERVICE_H_

#include <functional>
#include <vector>

#include "lib/app/cpp/application_context.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace mozart {

// Parameters for creating a view.
struct ViewContext {
  app::ApplicationContext* application_context;
  ViewManagerPtr view_manager;
  fidl::InterfaceRequest<ViewOwner> view_owner_request;
  fidl::InterfaceRequest<app::ServiceProvider> outgoing_services;
};

// A callback to create a view in response to a call to
// |ViewProvider.CreateView()|.
using ViewFactory =
    std::function<std::unique_ptr<BaseView>(ViewContext context)>;

// Publishes a view provider as an outgoing service of the application.
// The views created by the view provider are owned by it and will be destroyed
// when the view provider itself is destroyed.
//
// This is only intended to be used for simple example programs.
class ViewProviderService : public ViewProvider {
 public:
  explicit ViewProviderService(app::ApplicationContext* application_context,
                               ViewFactory factory);
  ~ViewProviderService();

  // |ViewProvider|
  void CreateView(
      fidl::InterfaceRequest<ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> view_services) override;

 private:
  app::ApplicationContext* application_context_;
  ViewFactory view_factory_;

  fidl::BindingSet<ViewProvider> bindings_;
  std::vector<std::unique_ptr<BaseView>> views_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewProviderService);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_VIEW_FRAMEWORK_VIEW_PROVIDER_SERVICE_H_
