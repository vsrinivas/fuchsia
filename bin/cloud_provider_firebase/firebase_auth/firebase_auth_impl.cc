// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/firebase_auth/firebase_auth_impl.h"

#include <utility>

#include "lib/fxl/functional/make_copyable.h"
#include "peridot/lib/callback/cancellable_helper.h"

namespace firebase_auth {

FirebaseAuthImpl::FirebaseAuthImpl(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    std::string api_key,
    modular::auth::TokenProviderPtr token_provider,
    std::unique_ptr<backoff::Backoff> backoff)
    : api_key_(std::move(api_key)),
      token_provider_(std::move(token_provider)),
      backoff_(std::move(backoff)),
      task_runner_(std::move(task_runner)) {}

void FirebaseAuthImpl::set_connection_error_handler(fxl::Closure on_error) {
  token_provider_.set_connection_error_handler(std::move(on_error));
}

fxl::RefPtr<callback::Cancellable> FirebaseAuthImpl::GetFirebaseToken(
    std::function<void(firebase_auth::AuthStatus, std::string)> callback) {
  if (api_key_.empty()) {
    FXL_LOG(WARNING) << "No Firebase API key provided. Connection to Firebase "
                        "may be unauthenticated.";
  }
  auto cancellable = callback::CancellableImpl::Create([] {});
  GetToken([callback = cancellable->WrapCallback(callback)](
               auto status, auto token) { callback(status, token->id_token); });
  return cancellable;
}

fxl::RefPtr<callback::Cancellable> FirebaseAuthImpl::GetFirebaseUserId(
    std::function<void(firebase_auth::AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});
  GetToken([callback = cancellable->WrapCallback(callback)](
               auto status, auto token) { callback(status, token->local_id); });
  return cancellable;
}

void FirebaseAuthImpl::GetToken(
    std::function<void(firebase_auth::AuthStatus,
                       modular::auth::FirebaseTokenPtr)> callback) {
  token_provider_->GetFirebaseAuthToken(
      api_key_, [this, callback = std::move(callback)](
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
          task_runner_.PostDelayedTask(
              [this, callback = std::move(callback)]() mutable {
                GetToken(std::move(callback));
              },
              backoff_->GetNext());
          return;
        }

        backoff_->Reset();
        callback(firebase_auth::AuthStatus::OK, std::move(token));
      });
}

}  // namespace firebase_auth
