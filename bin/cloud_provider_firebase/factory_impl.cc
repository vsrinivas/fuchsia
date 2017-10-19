// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/factory_impl.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/backoff/exponential_backoff.h"

namespace cloud_provider_firebase {

FactoryImpl::FactoryImpl(fxl::RefPtr<fxl::TaskRunner> main_runner,
                         ledger::NetworkService* network_service)
    : main_runner_(std::move(main_runner)), network_service_(network_service) {}

FactoryImpl::~FactoryImpl() {}

void FactoryImpl::GetCloudProvider(
    ConfigPtr config,
    fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> cloud_provider,
    const GetCloudProviderCallback& callback) {
  auto token_provider_ptr =
      modular::auth::TokenProviderPtr::Create(std::move(token_provider));
  auto auth_provider = std::make_unique<auth_provider::AuthProviderImpl>(
      main_runner_, config->api_key, std::move(token_provider_ptr),
      std::make_unique<backoff::ExponentialBackoff>());
  auth_provider::AuthProviderImpl* auth_provider_ptr = auth_provider.get();
  auto request =
      auth_provider_ptr->GetFirebaseUserId(fxl::MakeCopyable(
          [this, config = std::move(config),
           auth_provider = std::move(auth_provider),
           cloud_provider = std::move(cloud_provider), callback](
              auth_provider::AuthStatus status, std::string user_id) mutable {
            if (status != auth_provider::AuthStatus::OK) {
              FXL_LOG(ERROR)
                  << "Failed to retrieve the user ID from auth token provider";
              callback(cloud_provider::Status::AUTH_ERROR);
              return;
            }

            providers_.emplace(main_runner_, network_service_, user_id,
                               std::move(config), std::move(auth_provider),
                               std::move(cloud_provider));
            callback(cloud_provider::Status::OK);
          }));
  token_requests_.emplace(request);
}

}  // namespace cloud_provider_firebase
