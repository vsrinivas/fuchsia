// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_
#define APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_

#include <memory>

#include "application/lib/app/application_context.h"
#include "application/services/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

// Base class for an application which provides only a single instance
// of a single service.
template <class Service>
class SingleServiceApp : public Service {
 public:
  SingleServiceApp()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
        service_binding_(this) {
    application_context_->outgoing_services()->AddService<Service>(
        [this](fidl::InterfaceRequest<Service> request) {
          FTL_DCHECK(!service_binding_.is_bound());
          service_binding_.Bind(std::move(request));
        });
  }

  ~SingleServiceApp() override = default;

 protected:
  app::ApplicationContext* application_context() {
    return application_context_.get();
  }

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  fidl::Binding<Service> service_binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SingleServiceApp);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_APP_H_
