// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_FACTORY_IMPL_H_
#define GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_FACTORY_IMPL_H_

#include "lib/app/cpp/application_context.h"
#include "lib/auth/fidl/token_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
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
  void GetTokenManager(const f1dl::StringPtr& user_id,
                       f1dl::Array<AuthProviderConfigPtr> auth_provider_configs,
                       f1dl::InterfaceRequest<TokenManager> request) override;

  component::ApplicationContext* const app_context_;

  f1dl::BindingSet<TokenManager, std::unique_ptr<TokenManager>>
      token_manager_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenManagerFactoryImpl);
};

}  // namespace auth

#endif  // GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_FACTORY_IMPL_H_
