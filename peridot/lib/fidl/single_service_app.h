// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_
#define PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/interface_request.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <lib/zx/eventpair.h>
#include <memory>

namespace modular {

// Base class for a simple application which only provides the ViewProvider
// service. It also implements a Terminate() method that makes it suitable to be
// used as an Impl class of AppDriver.
class ViewApp : private fuchsia::ui::app::ViewProvider {
 public:
  ViewApp(component::StartupContext* const startup_context)
      : startup_context_(startup_context), view_provider_binding_(this) {
    startup_context_->outgoing()
        .AddPublicService<fuchsia::ui::app::ViewProvider>(
            [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>
                       request) {
              FXL_DCHECK(!view_provider_binding_.is_bound());
              view_provider_binding_.Bind(std::move(request));
            });
  }

  ~ViewApp() override = default;

  virtual void Terminate(fit::function<void()> done) { done(); }

 protected:
  component::StartupContext* startup_context() const {
    return startup_context_;
  }

 private:
  // |ViewProvider| -- Derived classes should override this method.
  void CreateView(
      zx::eventpair /*view_token*/,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      fidl::InterfaceHandle<
          fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {}

  component::StartupContext* const startup_context_;
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewApp);
};

// Base class for a simple application which provides a single instance of a
// single service and the ViewProvider service.
template <class Service>
class SingleServiceApp : public ViewApp, protected Service {
 public:
  SingleServiceApp(component::StartupContext* const start_context)
      : ViewApp(start_context), service_binding_(this) {
    // The 'template' is required here because AddPublicService is a dependent
    // template name.
    startup_context()->outgoing().template AddPublicService<Service>(
        [this](fidl::InterfaceRequest<Service> request) {
          FXL_DCHECK(!service_binding_.is_bound());
          service_binding_.Bind(std::move(request));
        });
  }

  ~SingleServiceApp() override = default;

 private:
  fidl::Binding<Service> service_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SingleServiceApp);
};

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_
