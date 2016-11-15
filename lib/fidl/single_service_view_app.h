// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_VIEW_APP_H_
#define APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_VIEW_APP_H_

#include <memory>
#include <vector>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

// Base class for an application which provides a single instance of a
// single service, which also provides a view.
template<class Service>
class SingleServiceViewApp : public Service, public mozart::ViewProvider {
 public:
  SingleServiceViewApp()
      : application_context_(ApplicationContext::CreateFromStartupInfo()),
        service_binding_(this),
        view_provider_binding_(this) {
    application_context_->outgoing_services()->AddService<Service>(
        [this](fidl::InterfaceRequest<Service> request) {
          FTL_DCHECK(!service_binding_.is_bound());
          service_binding_.Bind(std::move(request));
        });
    application_context_->outgoing_services()->AddService<mozart::ViewProvider>(
        [this](fidl::InterfaceRequest<mozart::ViewProvider> request) {
          FTL_DCHECK(!view_provider_binding_.is_bound());
          view_provider_binding_.Bind(std::move(request));
        });
  }

  ~SingleServiceViewApp() override = default;

 protected:
  ApplicationContext* application_context() {
    return application_context_.get();
  }

 private:
  std::unique_ptr<ApplicationContext> application_context_;
  fidl::Binding<Service> service_binding_;
  fidl::Binding<mozart::ViewProvider> view_provider_binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SingleServiceViewApp);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_VIEW_APP_H_
