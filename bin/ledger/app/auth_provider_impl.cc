// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/auth_provider_impl.h"

#include <utility>

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "lib/ftl/functional/make_copyable.h"

namespace ledger {

AuthProviderImpl::AuthProviderImpl(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    std::string api_key,
    modular::auth::TokenProviderPtr token_provider)
    : task_runner_(task_runner),
      api_key_(std::move(api_key)),
      token_provider_(std::move(token_provider)) {}

ftl::RefPtr<callback::Cancellable> AuthProviderImpl::GetFirebaseToken(
    std::function<void(cloud_sync::AuthStatus, std::string)> callback) {
  if (api_key_.empty()) {
    FTL_LOG(WARNING) << "No Firebase API key provided. Connection to Firebase "
                        "may be unauthenticated.";
  }
  auto cancellable = callback::CancellableImpl::Create([] {});
  token_provider_->GetFirebaseAuthToken(
      api_key_, [callback = cancellable->WrapCallback(callback)](
                    modular::auth::FirebaseTokenPtr token,
                    modular::auth::AuthErrPtr error) {
        if (!token || error->status != modular::auth::Status::OK) {
          // This should not happen - the token provider returns nullptr when
          // running in the guest mode, but in this case we don't initialize
          // sync and should never call auth provider.
          FTL_LOG(ERROR)
              << "Error retrieving the Firebase token from token provider: "
              << error->message;
          callback(cloud_sync::AuthStatus::ERROR, "");
          return;
        }
        callback(cloud_sync::AuthStatus::OK, token->id_token);
      });
  return cancellable;
}

void AuthProviderImpl::GetFirebaseUserId(
    std::function<void(cloud_sync::AuthStatus, std::string)> callback) {
  token_provider_->GetFirebaseAuthToken(
      api_key_, [callback = std::move(callback)](
                    modular::auth::FirebaseTokenPtr token,
                    modular::auth::AuthErrPtr error) {
        if (!token || error->status != modular::auth::Status::OK) {
          // This should not happen - the token provider returns nullptr when
          // running in the guest mode, but in this case we don't initialize
          // sync and should never call auth provider.
          FTL_LOG(ERROR)
              << "Error retrieving the Firebase token from token provider: "
              << error->message;
          callback(cloud_sync::AuthStatus::ERROR, "");
          return;
        }
        callback(cloud_sync::AuthStatus::OK, token->local_id);
      });
}

}  // namespace ledger
