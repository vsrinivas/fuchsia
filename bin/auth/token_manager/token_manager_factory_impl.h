// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_FACTORY_IMPL_H_
#define GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_FACTORY_IMPL_H_

#include <auth/cpp/fidl.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fxl/macros.h"

namespace auth {

using auth::TokenManager;
using auth::TokenManagerFactory;

// TODO: once namespacing happens for persistent storage, remove
const std::string kAuthDbPath = "/data/auth";

const std::string kAuthDbPostfix = "token_store.db";

class TokenManagerFactoryImpl : public TokenManagerFactory {
 public:
  TokenManagerFactoryImpl(component::ApplicationContext* context);

  ~TokenManagerFactoryImpl() override;

 private:
  // |TokenManagerFactory|
  void GetTokenManager(
      fidl::StringPtr user_id, fidl::StringPtr application_url,
      fidl::VectorPtr<AuthProviderConfig> auth_provider_configs,
      fidl::InterfaceHandle<auth::AuthenticationContextProvider>
          auth_context_provider,
      fidl::InterfaceRequest<TokenManager> request) override;

  component::ApplicationContext* const app_context_;

  fidl::BindingSet<TokenManager, std::unique_ptr<TokenManager>>
      token_manager_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenManagerFactoryImpl);
};

}  // namespace auth

#endif  // GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_FACTORY_IMPL_H_
