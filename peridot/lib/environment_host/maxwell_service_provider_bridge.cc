// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/environment_host/maxwell_service_provider_bridge.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/component/cpp/connect.h>

namespace maxwell {
namespace {

// TODO(abarth): Get this constant from a generated header once netstack uses
// FIDL.
constexpr char kNetstack[] = "net.Netstack";

}  // namespace

MaxwellServiceProviderBridge::MaxwellServiceProviderBridge(
    fuchsia::sys::Environment* parent_env)
    : vfs_(async_get_default_dispatcher()),
      services_dir_(fbl::AdoptRef(new fs::PseudoDir)) {
  AddService<fuchsia::sys::Loader>(
      [parent_env](fidl::InterfaceRequest<fuchsia::sys::Loader> request) {
        fuchsia::sys::ServiceProviderPtr services;
        parent_env->GetServices(services.NewRequest());
        component::ConnectToService(services.get(), std::move(request));
      });
  AddServiceWithName(
      kNetstack,
      fbl::AdoptRef(new fs::Service([parent_env](zx::channel request) {
        fuchsia::sys::ServiceProviderPtr services;
        parent_env->GetServices(services.NewRequest());
        services->ConnectToService(kNetstack, std::move(request));
        return ZX_OK;
      })));
}

void MaxwellServiceProviderBridge::AddServiceWithName(
    const char* name, fbl::RefPtr<fs::Service> service) {
  service_names_.push_back(name);
  services_dir_->AddEntry(name, service);
}

zx::channel MaxwellServiceProviderBridge::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_.ServeDirectory(services_dir_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

}  // namespace maxwell
