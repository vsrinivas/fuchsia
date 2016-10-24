// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/debug.h"

namespace maxwell {

const char DebugSupport::kTestApp[] = "mojo:maxwell_test";

void DebugSupport::AddService(
    mojo::Shell* shell,
    mojo::ServiceProviderImpl* service_provider_impl) {
  service_provider_impl->AddService<Debug>(
      [this](const mojo::ConnectionContext& connection_context,
             mojo::InterfaceRequest<Debug> request) {
        // For now, whitelist the test harness.
        if (connection_context.remote_url == kTestApp) {
          debug_bindings_.AddBinding(this, std::move(request));
        }
      });
}

}  // namespace maxwell
