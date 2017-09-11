// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/factory_impl.h"

#include "apps/ledger/src/backoff/exponential_backoff.h"
#include "garnet/public/lib/fxl/functional/make_copyable.h"
#include "garnet/public/lib/fxl/logging.h"

namespace cloud_provider_firebase {

FactoryImpl::FactoryImpl(fxl::RefPtr<fxl::TaskRunner> main_runner)
    : main_runner_(std::move(main_runner)) {
  providers_.set_on_empty([this] { CheckEmpty(); });
  token_requests_.set_on_empty([this] { CheckEmpty(); });
}

FactoryImpl::~FactoryImpl() {}

void FactoryImpl::GetCloudProvider(
    ConfigPtr config,
    fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> cloud_provider,
    const GetCloudProviderCallback& callback) {
  auto token_provider_ptr =
      modular::auth::TokenProviderPtr::Create(std::move(token_provider));
  token_provider_ptr.set_connection_error_handler([this] {
    FXL_LOG(ERROR) << "Lost connection to the token provider, "
                   << "shutting down the cloud provider.";
    // This is excessive - if multiple cloud providers are instantiated,
    // this shuts down all of them. TODO(ppi): handle token provider disconnects
    // per cloud provider.
    if (on_empty_) {
      on_empty_();
    }
  });

  auto auth_provider = std::make_unique<auth_provider::AuthProviderImpl>(
      main_runner_, config->api_key, std::move(token_provider_ptr),
      std::make_unique<backoff::ExponentialBackoff>());
  auth_provider::AuthProviderImpl* auth_provider_ptr = auth_provider.get();
  auto request = auth_provider_ptr->GetFirebaseUserId(fxl::MakeCopyable([
    this, config = std::move(config), auth_provider = std::move(auth_provider),
    cloud_provider = std::move(cloud_provider), callback
  ](auth_provider::AuthStatus status, std::string user_id) mutable {
    if (status != auth_provider::AuthStatus::OK) {
      FXL_LOG(ERROR)
          << "Failed to retrieve the user ID from auth token provider";
      callback(cloud_provider::Status::AUTH_ERROR);
      return;
    }

    providers_.emplace(main_runner_, user_id, std::move(config),
                       std::move(auth_provider), std::move(cloud_provider));
    callback(cloud_provider::Status::OK);
  }));
  token_requests_.emplace(request);
}

bool FactoryImpl::IsEmpty() {
  return token_requests_.empty() && providers_.empty();
}

void FactoryImpl::CheckEmpty() {
  if (IsEmpty() && on_empty_) {
    on_empty_();
  }
}

}  // namespace cloud_provider_firebase
