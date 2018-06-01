// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/firebase_auth_impl.h"

#include <utility>

#include "lib/callback/cancellable_helper.h"
#include "lib/fxl/functional/make_copyable.h"

namespace firebase_auth {
namespace {

// Maximum number of retries on non-fatal errors.
constexpr int kDefaultMaxRetries = 5;

// Returns true if the authentication failure may be transient.
bool IsRetriableError(modular_auth::Status status) {
  switch (status) {
    // TODO(AUTH-50): update once retriable errors are documented.
    case modular_auth::Status::BAD_REQUEST:
      return false;
    default:
      return true;
  }
}

// Maps modular_auth error space to firebase_auth one.
AuthStatus ConvertAuthErr(modular_auth::AuthErr error) {
  return error.status == modular_auth::Status::OK ? AuthStatus::OK
                                                  : AuthStatus::ERROR;
}
}  // namespace

FirebaseAuthImpl::FirebaseAuthImpl(
    async_t* async, std::string api_key,
    modular_auth::TokenProviderPtr token_provider,
    std::unique_ptr<backoff::Backoff> backoff)
    : FirebaseAuthImpl(async, std::move(api_key), std::move(token_provider),
                       std::move(backoff), kDefaultMaxRetries) {}

FirebaseAuthImpl::FirebaseAuthImpl(
    async_t* async, std::string api_key,
    modular_auth::TokenProviderPtr token_provider,
    std::unique_ptr<backoff::Backoff> backoff, int max_retries)
    : api_key_(std::move(api_key)),
      token_provider_(std::move(token_provider)),
      backoff_(std::move(backoff)),
      max_retries_(max_retries),
      task_runner_(async) {}

void FirebaseAuthImpl::set_error_handler(fxl::Closure on_error) {
  token_provider_.set_error_handler(std::move(on_error));
}

fxl::RefPtr<callback::Cancellable> FirebaseAuthImpl::GetFirebaseToken(
    std::function<void(AuthStatus, std::string)> callback) {
  if (api_key_.empty()) {
    FXL_LOG(WARNING) << "No Firebase API key provided. Connection to Firebase "
                        "may be unauthenticated.";
  }
  auto cancellable = callback::CancellableImpl::Create([] {});
  GetToken(max_retries_,
           [callback = cancellable->WrapCallback(callback)](
               auto status, auto token) { callback(status, token->id_token); });
  return cancellable;
}

fxl::RefPtr<callback::Cancellable> FirebaseAuthImpl::GetFirebaseUserId(
    std::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});
  GetToken(max_retries_,
           [callback = cancellable->WrapCallback(callback)](
               auto status, auto token) { callback(status, token->local_id); });
  return cancellable;
}

void FirebaseAuthImpl::GetToken(
    int max_retries,
    std::function<void(AuthStatus, modular_auth::FirebaseTokenPtr)> callback) {
  token_provider_->GetFirebaseAuthToken(
      api_key_, [this, max_retries, callback = std::move(callback)](
                    modular_auth::FirebaseTokenPtr token,
                    modular_auth::AuthErr error) mutable {
        if (!token || error.status != modular_auth::Status::OK) {
          if (!token && error.status == modular_auth::Status::OK) {
            FXL_LOG(ERROR)
                << "null Firebase token returned from token provider with no "
                << "error reported. This should never happen. Retrying.";
          } else {
            FXL_LOG(ERROR)
                << "Error retrieving the Firebase token from token provider: "
                << error.status << ", '" << error.message << "', retrying.";
          }

          if (max_retries > 0 && IsRetriableError(error.status)) {
            task_runner_.PostDelayedTask(
                [this, max_retries, callback = std::move(callback)]() mutable {
                  GetToken(max_retries - 1, std::move(callback));
                },
                backoff_->GetNext());
            return;
          }
        }

        backoff_->Reset();
        callback(ConvertAuthErr(error), std::move(token));
      });
}

}  // namespace firebase_auth
