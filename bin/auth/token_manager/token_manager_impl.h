// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_IMPL_H_
#define GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_IMPL_H_

#include <map>

#include "garnet/bin/auth/cache/token_cache.h"
#include "garnet/bin/auth/token_manager/test/dev_auth_provider_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/auth/fidl/auth_provider.fidl.h"
#include "lib/auth/fidl/token_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/macros.h"

namespace auth {

using auth::AuthProviderPtr;
using auth::AuthProviderType;

constexpr int kMaxCacheSize = 10;

class TokenManagerImpl : public TokenManager {
 public:
  TokenManagerImpl(app::ApplicationContext* context,
                   f1dl::Array<AuthProviderConfigPtr> auth_provider_configs);

  ~TokenManagerImpl() override;

 private:
  // |TokenManager|
  void Authorize(const auth::AuthProviderType auth_provider_type,
                 const f1dl::InterfaceHandle<auth::AuthenticationUIContext>
                     auth_ui_context,
                 const AuthorizeCallback& callback) override;

  void GetAccessToken(const auth::AuthProviderType auth_provider_type,
                      const f1dl::String& user_profile_id,
                      const f1dl::String& app_client_id,
                      const f1dl::Array<f1dl::String> app_scopes,
                      const GetAccessTokenCallback& callback) override;

  void GetIdToken(const auth::AuthProviderType auth_provider_type,
                  const f1dl::String& user_profile_id,
                  const f1dl::String& audience,
                  const GetIdTokenCallback& callback) override;

  void GetFirebaseToken(const auth::AuthProviderType auth_provider_type,
                        const f1dl::String& user_profile_id,
                        const f1dl::String& audience,
                        const f1dl::String& firebase_api_key,
                        const GetFirebaseTokenCallback& callback) override;

  void DeleteAllTokens(const auth::AuthProviderType auth_provider_type,
                       const f1dl::String& user_profile_id,
                       const DeleteAllTokensCallback& callback) override;

  std::map<AuthProviderType, app::ApplicationControllerPtr>
      auth_provider_controllers_;

  std::map<AuthProviderType, auth::AuthProviderPtr> auth_providers_;

  cache::TokenCache token_cache_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenManagerImpl);
};

}  // namespace auth

#endif  // GARNET_BIN_AUTH_TOKEN_MANAGER_TOKEN_MANAGER_IMPL_H_
