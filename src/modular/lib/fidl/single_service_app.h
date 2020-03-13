// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_
#define SRC_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/eventpair.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/lib/syslog/cpp/logger.h"

namespace modular {

// Base class for a simple application which only provides the ViewProvider
// service. It also implements a Terminate() method that makes it suitable to be
// used as an Impl class of AppDriver.
class ViewApp : private fuchsia::ui::app::ViewProvider {
 public:
  ViewApp(sys::ComponentContext* const component_context)
      : component_context_(component_context), view_provider_binding_(this) {
    component_context_->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          FX_DCHECK(!view_provider_binding_.is_bound());
          view_provider_binding_.Bind(std::move(request));
        });
  }

  ~ViewApp() override = default;

  virtual void Terminate(fit::function<void()> done) { done(); }

 protected:
  sys::ComponentContext* component_context() const { return component_context_; }

 private:
  // |ViewProvider| -- Derived classes should override this method.
  void CreateView(
      zx::eventpair /*view_token*/,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {}

  sys::ComponentContext* const component_context_;
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewApp);
};

// Base class for a simple application which provides a single instance of a
// single service and the ViewProvider service.
template <class Service>
class SingleServiceApp : public ViewApp, protected Service {
 public:
  SingleServiceApp(sys::ComponentContext* const start_context)
      : ViewApp(start_context), service_binding_(this) {
    // The 'template' is required here because AddPublicService is a dependent
    // template name.
    component_context()->outgoing()->template AddPublicService<Service>(
        [this](fidl::InterfaceRequest<Service> request) {
          FX_DCHECK(!service_binding_.is_bound());
          service_binding_.Bind(std::move(request));
        });
  }

  ~SingleServiceApp() override = default;

 private:
  fidl::Binding<Service> service_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SingleServiceApp);
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_
