// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/auth/token_manager/test/factory_impl.h"

namespace auth {
namespace dev_auth_provider {

void FactoryImpl::GetAuthProvider(
    fidl::InterfaceRequest<auth::AuthProvider> auth_provider,
    const GetAuthProviderCallback& callback) {
  // TODO: Share the DevAuthProviderImpl instance across all connections
  std::unique_ptr<DevAuthProviderImpl> dev_auth_provider_impl =
      std::make_unique<DevAuthProviderImpl>();

  dev_bindings_.AddBinding(std::move(dev_auth_provider_impl),
                           std::move(auth_provider));

  callback(auth::AuthProviderStatus::OK);
}

}  // namespace dev_auth_provider
}  // namespace auth
