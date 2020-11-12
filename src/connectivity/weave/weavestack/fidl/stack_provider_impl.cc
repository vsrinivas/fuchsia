// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stack_provider_impl.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <Weave/DeviceLayer/internal/NetworkProvisioningServer.h>

namespace weavestack {

namespace {
using nl::Weave::DeviceLayer::Internal::NetworkProvisioningSvrImpl;
}

// StackProviderImpl definitions -------------------------------------------------------

StackProviderImpl::StackProviderImpl(sys::ComponentContext* context) : context_(context) {}

zx_status_t StackProviderImpl::Init() {
  zx_status_t status = ZX_OK;

  // Register with the context.
  status = context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to register StackImpl handler with status ="
                   << zx_status_get_string(status);
    return status;
  }

  return status;
}

StackProviderImpl::~StackProviderImpl() = default;

void StackProviderImpl::SetWlanNetworkConfigProvider(
    ::fidl::InterfaceHandle<class ::fuchsia::weave::WlanNetworkConfigProvider> provider) {
  NetworkProvisioningSvrImpl().SetWlanNetworkConfigProvider(std::move(provider));
}

}  // namespace weavestack
