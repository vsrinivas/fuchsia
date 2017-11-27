// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/factory_impl.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/backoff/exponential_backoff.h"

namespace cloud_provider_firestore {

FactoryImpl::FactoryImpl(fxl::RefPtr<fxl::TaskRunner> main_runner)
    : main_runner_(std::move(main_runner)) {}

FactoryImpl::~FactoryImpl() {}

void FactoryImpl::GetCloudProvider(
    ConfigPtr config,
    fidl::InterfaceHandle<modular::auth::TokenProvider> token_provider,
    fidl::InterfaceRequest<cloud_provider::CloudProvider>
        cloud_provider_request,
    const GetCloudProviderCallback& callback) {
  auto token_provider_ptr =
      modular::auth::TokenProviderPtr::Create(std::move(token_provider));
  auto firebase_auth = std::make_unique<firebase_auth::FirebaseAuthImpl>(
      main_runner_, config->api_key, std::move(token_provider_ptr),
      std::make_unique<backoff::ExponentialBackoff>());
  firebase_auth::FirebaseAuthImpl* firebase_auth_ptr = firebase_auth.get();
  auto token_request =
      firebase_auth_ptr->GetFirebaseUserId(fxl::MakeCopyable(
          [this, config = std::move(config),
           firebase_auth = std::move(firebase_auth),
           cloud_provider_request = std::move(cloud_provider_request),
           callback](firebase_auth::AuthStatus status,
                     std::string user_id) mutable {

            if (status != firebase_auth::AuthStatus::OK) {
              FXL_LOG(ERROR)
                  << "Failed to retrieve the user ID from auth token provider";
              callback(cloud_provider::Status::AUTH_ERROR);
              return;
            }

            providers_.emplace(user_id, std::move(firebase_auth),
                               std::move(cloud_provider_request));
            callback(cloud_provider::Status::OK);
          }));
  token_requests_.emplace(token_request);
}

}  // namespace cloud_provider_firestore
