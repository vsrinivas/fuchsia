// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/firebase_auth_impl.h"

#include <utility>

#include <lib/backoff/exponential_backoff.h>
#include <lib/callback/cancellable_helper.h>
#include <lib/fit/function.h>
#include <lib/fxl/functional/closure.h>
#include <lib/fxl/functional/make_copyable.h>

namespace firebase_auth {
namespace {

// Cobalt constants. See third_party/cobalt_config/fuchsia/ledger/config.yaml.
constexpr int32_t kLedgerCobaltProjectId = 100;
constexpr int32_t kCobaltAuthFailureMetricId = 3;
constexpr int32_t kCobaltNoOpEncodingId = 3;

// Returns true if the authentication failure may be transient.
bool IsRetriableError(fuchsia::modular::auth::Status status) {
  switch (status) {
    case fuchsia::modular::auth::Status::OK:  // This should never happen.
    case fuchsia::modular::auth::Status::BAD_REQUEST:
    case fuchsia::modular::auth::Status::OAUTH_SERVER_ERROR:
    case fuchsia::modular::auth::Status::USER_CANCELLED:
      return false;
    case fuchsia::modular::auth::Status::BAD_RESPONSE:
    case fuchsia::modular::auth::Status::NETWORK_ERROR:
    case fuchsia::modular::auth::Status::INTERNAL_ERROR:
      return true;
  }
  // In case of unexpected status, retry just in case.
  return true;
}
}  // namespace

FirebaseAuthImpl::FirebaseAuthImpl(
    Config config, async_dispatcher_t* dispatcher,
    fuchsia::modular::auth::TokenProviderPtr token_provider,
    component::StartupContext* startup_context)
    : FirebaseAuthImpl(std::move(config), dispatcher, std::move(token_provider),
                       std::make_unique<backoff::ExponentialBackoff>(),
                       startup_context
                           ? cobalt::MakeCobaltContext(dispatcher, startup_context,
                                                       kLedgerCobaltProjectId)
                           : nullptr) {}

FirebaseAuthImpl::FirebaseAuthImpl(
    Config config, async_dispatcher_t* dispatcher,
    fuchsia::modular::auth::TokenProviderPtr token_provider,
    std::unique_ptr<backoff::Backoff> backoff,
    std::unique_ptr<cobalt::CobaltContext> cobalt_context)
    : api_key_(std::move(config.api_key)),
      token_provider_(std::move(token_provider)),
      backoff_(std::move(backoff)),
      max_retries_(config.max_retries),
      cobalt_client_name_(std::move(config.cobalt_client_name)),
      cobalt_context_(std::move(cobalt_context)),
      task_runner_(dispatcher) {}

void FirebaseAuthImpl::set_error_handler(fit::closure on_error) {
  token_provider_.set_error_handler(std::move(on_error));
}

fxl::RefPtr<callback::Cancellable> FirebaseAuthImpl::GetFirebaseToken(
    fit::function<void(AuthStatus, std::string)> callback) {
  if (api_key_.empty()) {
    FXL_LOG(WARNING) << "No Firebase API key provided. Connection to Firebase "
                        "may be unauthenticated.";
  }
  auto cancellable = callback::CancellableImpl::Create([] {});
  GetToken(max_retries_, [callback = cancellable->WrapCallback(
                              std::move(callback))](auto status, auto token) {
    callback(status, token ? token->id_token : "");
  });
  return cancellable;
}

fxl::RefPtr<callback::Cancellable> FirebaseAuthImpl::GetFirebaseUserId(
    fit::function<void(AuthStatus, std::string)> callback) {
  auto cancellable = callback::CancellableImpl::Create([] {});
  GetToken(max_retries_, [callback = cancellable->WrapCallback(
                              std::move(callback))](auto status, auto token) {
    callback(status, token ? token->local_id : "");
  });
  return cancellable;
}

void FirebaseAuthImpl::GetToken(
    int max_retries,
    fit::function<void(AuthStatus, fuchsia::modular::auth::FirebaseTokenPtr)>
        callback) {
  token_provider_->GetFirebaseAuthToken(
      api_key_,
      fxl::MakeCopyable([this, max_retries, callback = std::move(callback)](
                            fuchsia::modular::auth::FirebaseTokenPtr token,
                            fuchsia::modular::auth::AuthErr error) mutable {
        if (!token || error.status != fuchsia::modular::auth::Status::OK) {
          if (!token && error.status == fuchsia::modular::auth::Status::OK) {
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
        if (error.status == fuchsia::modular::auth::Status::OK) {
          callback(AuthStatus::OK, std::move(token));
        } else {
          ReportError(error.status);
          callback(AuthStatus::ERROR, std::move(token));
        }
      }));
}

void FirebaseAuthImpl::ReportError(fuchsia::modular::auth::Status status) {
  if (cobalt_client_name_.empty() || cobalt_context_ == nullptr) {
    return;
  }
  auto parts = fidl::VectorPtr<fuchsia::cobalt::ObservationValue>::New(2);
  parts->at(0).name = "client-name";
  parts->at(0).encoding_id = kCobaltNoOpEncodingId;
  parts->at(0).value.set_string_value(cobalt_client_name_);

  parts->at(1).name = "failure-type";
  parts->at(1).encoding_id = kCobaltNoOpEncodingId;
  parts->at(1).value.set_index_value(static_cast<uint32_t>(status));

  cobalt::CobaltObservation observation(
      static_cast<uint32_t>(kCobaltAuthFailureMetricId), std::move(parts));
  cobalt_context_->ReportObservation(observation);
}
}  // namespace firebase_auth
