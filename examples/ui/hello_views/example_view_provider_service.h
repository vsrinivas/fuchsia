// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_HELLO_VIEWS_VIEW_PROVIDER_SERVICE_H_
#define GARNET_EXAMPLES_UI_HELLO_VIEWS_VIEW_PROVIDER_SERVICE_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/zx/eventpair.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"

namespace hello_views {

// Parameters for creating a view.
struct ViewContext {
  fuchsia::sys::StartupContext* startup_context;
  zx::eventpair token;
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> incoming_services;
  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services;
};

// A callback to create a view in response to a call to
// |ViewProvider.CreateView()|.
using ViewFactory = std::function<void(ViewContext context)>;

// A basic implementation of the |ViewProvider| interface which Scenic clients
// can use to create and expose custom Views to other Scenic clients.
class ExampleViewProviderService : public fuchsia::ui::app::ViewProvider {
 public:
  ExampleViewProviderService(::fuchsia::sys::StartupContext* startup_ctx,
                             ViewFactory factory);
  ~ExampleViewProviderService() override;
  ExampleViewProviderService(const ExampleViewProviderService&) = delete;
  ExampleViewProviderService& operator=(const ExampleViewProviderService&) =
      delete;

  // |ui::ViewProvider|
  void CreateView(
      ::zx::eventpair token,
      ::fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      ::fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services)
      override;

  // |sys::ServiceProvider|
  void ConnectToService(fidl::StringPtr service_name, ::zx::channel channel) {}

 private:
  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;
  ::fuchsia::sys::StartupContext* startup_ctx_;
  ViewFactory view_factory_fn_;
};

}  // namespace hello_views

#endif  // GARNET_EXAMPLES_UI_HELLO_VIEWS_VIEW_PROVIDER_SERVICE_H_
