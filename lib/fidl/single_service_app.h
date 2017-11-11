// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_
#define PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"

namespace modular {

// Base class for a simple application which provides a single instance of a
// single service and the ViewProvider service. It also implements a Terminate()
// method that makes it suitable to be used as an Impl class of AppDriver.
template <class Service>
class SingleServiceApp : protected Service,
                         private mozart::ViewProvider {
 public:
  SingleServiceApp(app::ApplicationContext* const application_context)
      : application_context_(application_context),
        service_binding_(new fidl::Binding<Service>(this)),
        view_provider_binding_(this) {
    application_context_->outgoing_services()->AddService<Service>(
        [this](fidl::InterfaceRequest<Service> request) {
          FXL_DCHECK(!service_binding_->is_bound());
          service_binding_->Bind(std::move(request));
        });
    application_context_->outgoing_services()->AddService<mozart::ViewProvider>(
        [this](fidl::InterfaceRequest<mozart::ViewProvider> request) {
          FXL_DCHECK(!view_provider_binding_.is_bound());
          view_provider_binding_.Bind(std::move(request));
        });
  }

  ~SingleServiceApp() override = default;

  virtual void Terminate(std::function<void()> done) { done(); }

 protected:
  app::ApplicationContext* application_context() const {
    return application_context_;
  }

 private:
  // |ViewProvider| -- Derived classes may override this method.
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> /*view_owner_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*services*/) override {}

  app::ApplicationContext* const application_context_;
  std::unique_ptr<fidl::Binding<Service>> service_binding_;
  fidl::Binding<mozart::ViewProvider> view_provider_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SingleServiceApp);
};

}  // namespace modular

#endif  // PERIDOT_LIB_FIDL_SINGLE_SERVICE_APP_H_
