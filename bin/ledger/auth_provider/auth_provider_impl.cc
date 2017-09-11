// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/auth_provider/auth_provider_impl.h"

#include <utility>

#include "apps/ledger/src/callback/cancellable_helper.h"
#include "lib/fxl/functional/make_copyable.h"

namespace auth_provider {

AuthProviderImpl::AuthProviderImpl(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    std::string api_key,
    modular::auth::TokenProviderPtr token_provider,
    std::unique_ptr<backoff::Backoff> backoff)
    : task_runner_(std::move(task_runner)),
      api_key_(std::move(api_key)),
      token_provider_(std::move(token_provider)),
      backoff_(std::move(backoff)),
      weak_factory_(this) {}

fxl::RefPtr<callback::Cancellable> AuthProviderImpl::GetFirebaseToken(
    std::function<void(auth_provider::AuthStatus, std::string)> callback) {
  if (api_key_.empty()) {
    FXL_LOG(WARNING) << "No Firebase API key provided. Connection to Firebase "
                        "may be unauthenticated.";
  }
  auto cancellable = callback::CancellableImpl::Create([] {});
  GetToken([callback = cancellable->WrapCallback(callback)](
      auto status, auto token) { callback(status, token->id_token); });
  return cancellable;
}

fxl::RefPtr<callback::Cancellable> AuthProviderImpl::GetFirebaseUserId(
    std::function<void(auth_provider::AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});
  GetToken([callback = cancellable->WrapCallback(callback)](
      auto status, auto token) { callback(status, token->local_id); });
  return cancellable;
}

void AuthProviderImpl::GetToken(
    std::function<void(auth_provider::AuthStatus,
                       modular::auth::FirebaseTokenPtr)> callback) {
  token_provider_->GetFirebaseAuthToken(
      api_key_, [ this, callback = std::move(callback) ](
                    modular::auth::FirebaseTokenPtr token,
                    modular::auth::AuthErrPtr error) mutable {
        if (!token || error->status != modular::auth::Status::OK) {
          if (!token) {
            // This should not happen - the token provider returns nullptr when
            // running in the guest mode, but in this case we don't initialize
            // sync and should never call auth provider.
            FXL_LOG(ERROR)
                << "null Firebase token returned from token provider, "
                << "this should never happen. Retrying.";
          } else {
            FXL_LOG(ERROR)
                << "Error retrieving the Firebase token from token provider: "
                << error->status << ", '" << error->message << "', retrying.";
          }
          task_runner_->PostDelayedTask(
              [
                weak_this = weak_factory_.GetWeakPtr(),
                callback = std::move(callback)
              ]() mutable {
                if (weak_this) {
                  weak_this->GetToken(std::move(callback));
                }
              },
              backoff_->GetNext());
          return;
        }

        backoff_->Reset();
        callback(auth_provider::AuthStatus::OK, std::move(token));
      });
}

}  // namespace auth_provider
