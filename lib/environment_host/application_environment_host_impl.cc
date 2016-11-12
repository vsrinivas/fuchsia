// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/application_environment_host_impl.h"

namespace maxwell {

void ApplicationEnvironmentHostImpl::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<modular::ServiceProvider> environment_services) {
  AddBinding(std::move(environment_services));
}

}  // namespace maxwell
