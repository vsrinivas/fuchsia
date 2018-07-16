// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/auth/token_manager/token_manager_factory_impl.h"
#include "garnet/bin/auth/store/auth_db_file_impl.h"
#include "garnet/bin/auth/token_manager/token_manager_impl.h"

namespace auth {

TokenManagerFactoryImpl::TokenManagerFactoryImpl(
    component::StartupContext* context)
    : app_context_(context) {
  FXL_CHECK(app_context_);
}

TokenManagerFactoryImpl::~TokenManagerFactoryImpl() {}

void TokenManagerFactoryImpl::GetTokenManager(
    fidl::StringPtr user_id, fidl::StringPtr application_url,
    fidl::VectorPtr<fuchsia::auth::AuthProviderConfig> auth_provider_configs,
    fidl::InterfaceHandle<fuchsia::auth::AuthenticationContextProvider>
        auth_context_provider,
    fidl::InterfaceRequest<fuchsia::auth::TokenManager> request) {
  auto file_name = kAuthDbPath + user_id.get() + kAuthDbPostfix;

  auto auth_db_file = std::make_unique<store::AuthDbFileImpl>(file_name);
  auto auth_db_status = auth_db_file->Load();
  if (auth_db_status != store::Status::kOK) {
    FXL_LOG(ERROR) << "Auth DB failed to load file " << file_name
                   << " with status " << auth_db_status;
    // TODO: propagate error instead of returning void
    return;
  }

  // TODO: Share the TokenManagerImpl instance per user across connections.
  std::unique_ptr<TokenManagerImpl> token_manager_impl =
      std::make_unique<TokenManagerImpl>(app_context_, std::move(auth_db_file),
                                         std::move(auth_provider_configs),
                                         std::move(auth_context_provider));

  token_manager_bindings_.AddBinding(std::move(token_manager_impl),
                                     std::move(request));
}

}  // namespace auth
