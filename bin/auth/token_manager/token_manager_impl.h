// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_IMPL_H_
#define GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_IMPL_H_

#include <map>

#include <auth/cpp/fidl.h>

#include "garnet/bin/auth/cache/token_cache.h"
#include "garnet/bin/auth/store/auth_db.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"

namespace auth {

using auth::AuthProviderPtr;
using auth::AuthProviderType;

constexpr int kMaxCacheSize = 10;

class TokenManagerImpl : public TokenManager {
 public:
  TokenManagerImpl(component::ApplicationContext* context,
                   std::unique_ptr<store::AuthDb> auth_db,
                   fidl::VectorPtr<AuthProviderConfig> auth_provider_configs,
                   fidl::InterfaceHandle<auth::AuthenticationContextProvider>
                       auth_context_provider);

  ~TokenManagerImpl() override;

 private:
  // |TokenManager|
  void Authorize(AppConfig app_config,
                 const fidl::VectorPtr<fidl::StringPtr> app_scopes,
                 fidl::StringPtr user_profile_id,
                 AuthorizeCallback callback) override;

  void GetAccessToken(AppConfig app_config, fidl::StringPtr user_profile_id,
                      const fidl::VectorPtr<fidl::StringPtr> app_scopes,
                      GetAccessTokenCallback callback) override;

  void GetIdToken(AppConfig app_config, fidl::StringPtr user_profile_id,
                  fidl::StringPtr audience,
                  GetIdTokenCallback callback) override;

  void GetFirebaseToken(AppConfig app_config, fidl::StringPtr user_profile_id,
                        fidl::StringPtr audience,
                        fidl::StringPtr firebase_api_key,
                        GetFirebaseTokenCallback callback) override;

  void DeleteAllTokens(AppConfig app_config, fidl::StringPtr user_profile_id,
                       DeleteAllTokensCallback callback) override;

  auth::AuthenticationContextProviderPtr auth_context_provider_;

  std::map<AuthProviderType, component::ComponentControllerPtr>
      auth_provider_controllers_;

  std::map<AuthProviderType, auth::AuthProviderPtr> auth_providers_;

  cache::TokenCache token_cache_;

  std::unique_ptr<store::AuthDb> auth_db_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenManagerImpl);
};

}  // namespace auth

#endif  // GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_IMPL_H_
