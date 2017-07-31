// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_VIEW_APP_H_
#define APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_VIEW_APP_H_

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

// Base class for an application which provides a single instance of a
// single service, which also provides a view.
template <class Service>
class SingleServiceViewApp : public SingleServiceApp<Service>,
                             mozart::ViewProvider {
 public:
  SingleServiceViewApp() : view_provider_binding_(this) {
    // TODO(alhaad): The following line needs to be broken out for reasons not
    // completely clear. Using the more obvious
    //   application_context()->outgoing_services()...
    // results in the error:
    //   use of undeclared identifier 'application_context'
    // Adding the this-> prefix to the same line, or scoping the call with
    // SingleServiceApp<Service>::, results in the error:
    //   use 'template' keyword to treat 'AddService' as a dependent template
    //   name
    app::ApplicationContext* const ctx = this->application_context();
    ctx->outgoing_services()->AddService<mozart::ViewProvider>(
        [this](fidl::InterfaceRequest<mozart::ViewProvider> request) {
          FTL_DCHECK(!view_provider_binding_.is_bound());
          view_provider_binding_.Bind(std::move(request));
        });
  }

  ~SingleServiceViewApp() override = default;

 private:
  fidl::Binding<mozart::ViewProvider> view_provider_binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SingleServiceViewApp);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_SINGLE_SERVICE_VIEW_APP_H_
