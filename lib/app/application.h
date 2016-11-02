// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_APP_APPLICATION_H_
#define APPS_MODULAR_LIB_APP_APPLICATION_H_

#include <memory>

#include "apps/modular/lib/app/service_provider_impl.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

class Application {
 public:
  Application(fidl::InterfaceHandle<ServiceProvider> environment_services,
              fidl::InterfaceRequest<ServiceProvider> outgoing_services);

  ~Application();

  static std::unique_ptr<Application> CreateFromStartupInfo();

  ServiceProvider* environment_services() const {
    return environment_services_.get();
  }

  ServiceProviderImpl* outgoing_services() { return &outgoing_services_; }

 private:
  ServiceProviderPtr environment_services_;
  ServiceProviderImpl outgoing_services_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Application);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_APP_APPLICATION_H_
