// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_
#define APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/service_provider.fidl.h"
#include "apps/modular/services/lifecycle/lifecycle.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

// Base class for a simple application which provides a single instance of a
// single service, the ViewProvider service, and the Lifecycle service.
template <class Service>
class SingleServiceApp : protected Service,
                         private mozart::ViewProvider,
                         private Lifecycle {
 public:
  SingleServiceApp()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        service_binding_(new fidl::Binding<Service>(this)),
        view_provider_binding_(this),
        lifecycle_binding_(this) {
    application_context_->outgoing_services()->AddService<Service>(
        [this](fidl::InterfaceRequest<Service> request) {
          FTL_DCHECK(!service_binding_->is_bound());
          service_binding_->Bind(std::move(request));
        });
    application_context_->outgoing_services()->AddService<mozart::ViewProvider>(
        [this](fidl::InterfaceRequest<mozart::ViewProvider> request) {
          FTL_DCHECK(!view_provider_binding_.is_bound());
          view_provider_binding_.Bind(std::move(request));
        });
    application_context_->outgoing_services()->AddService<Lifecycle>(
        [this](fidl::InterfaceRequest<Lifecycle> request) {
          FTL_DCHECK(!lifecycle_binding_.is_bound());
          lifecycle_binding_.Bind(std::move(request));
        });
  }

  ~SingleServiceApp() override = default;

 protected:
  app::ApplicationContext* application_context() {
    return application_context_.get();
  }

  // Allows the service binding to survive the destructor invocation, thus
  // allowing a callback of a Terminate() method to be invoked after |this| is
  // deleted. Cf. TestUserShellApp::Terminate() in test_user_shell.cc for an
  // example.
  std::unique_ptr<fidl::Binding<Service>> PassBinding() {
    return std::move(service_binding_);
  }

 private:
  // |ViewProvider| -- Derived classes may override this method.
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> /*view_owner_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*services*/) override {}

  // |Lifecycle|
  void Terminate() override {}

  std::unique_ptr<app::ApplicationContext> application_context_;
  std::unique_ptr<fidl::Binding<Service>> service_binding_;
  fidl::Binding<mozart::ViewProvider> view_provider_binding_;
  fidl::Binding<Lifecycle> lifecycle_binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SingleServiceApp);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_
