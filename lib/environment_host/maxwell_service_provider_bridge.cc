// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/environment_host/maxwell_service_provider_bridge.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/app/cpp/connect.h>

namespace maxwell {
namespace {

// TODO(abarth): Get this constant from a generated header once netstack uses
// FIDL.
constexpr char kNetstack[] = "net.Netstack";

}  // namespace

MaxwellServiceProviderBridge::MaxwellServiceProviderBridge(
    fuchsia::sys::Environment* parent_env) {
  AddService<fuchsia::sys::Loader>(
      [parent_env](fidl::InterfaceRequest<fuchsia::sys::Loader> request) {
        fuchsia::sys::ServiceProviderPtr services;
        parent_env->GetServices(services.NewRequest());
        fuchsia::sys::ConnectToService(services.get(), std::move(request));
      });
  AddServiceForName(
      [parent_env](zx::channel request) {
        fuchsia::sys::ServiceProviderPtr services;
        parent_env->GetServices(services.NewRequest());
        services->ConnectToService(kNetstack, std::move(request));
      },
      kNetstack);
}

}  // namespace maxwell
