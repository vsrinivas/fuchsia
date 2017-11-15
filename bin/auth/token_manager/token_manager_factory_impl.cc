// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/auth/token_manager/token_manager_factory_impl.h"
#include "garnet/bin/auth/token_manager/token_manager_impl.h"

namespace auth {

TokenManagerFactoryImpl::TokenManagerFactoryImpl(
    app::ApplicationContext* context)
    : app_context_(context) {
    FXL_CHECK(app_context_);
}

TokenManagerFactoryImpl::~TokenManagerFactoryImpl() {}

void TokenManagerFactoryImpl::GetTokenManager(
    const fidl::String& user_id,
    fidl::Array<AuthProviderConfigPtr> auth_provider_configs,
    fidl::InterfaceRequest<TokenManager> request) {
  // TODO: Share the TokenManagerImpl instance per user across connections.
  std::unique_ptr<TokenManagerImpl> token_manager_impl =
      std::make_unique<TokenManagerImpl>(app_context_,
                                         std::move(auth_provider_configs));

  token_manager_bindings_.AddBinding(std::move(token_manager_impl),
                                      std::move(request));
}

}  // namespace auth
