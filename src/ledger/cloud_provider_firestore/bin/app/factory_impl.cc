// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/app/factory_impl.h"

#include <grpc++/grpc++.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/logging.h>

#include "src/ledger/cloud_provider_firestore/bin/firestore/firestore_service_impl.h"

namespace cloud_provider_firestore {

namespace {
std::shared_ptr<grpc::Channel> MakeChannel() {
  auto opts = grpc::SslCredentialsOptions();
  auto credentials = grpc::SslCredentials(opts);
  return grpc::CreateChannel("firestore.googleapis.com:443", credentials);
}

firebase_auth::FirebaseAuthImpl::Config GetFirebaseAuthConfig(
    const std::string& api_key, const std::string& user_profile_id,
    const std::string& cobalt_client_name) {
  firebase_auth::FirebaseAuthImpl::Config config;
  config.api_key = api_key;
  config.user_profile_id = user_profile_id;
  config.cobalt_client_name = cobalt_client_name;

  return config;
}

}  // namespace

FactoryImpl::FactoryImpl(async_dispatcher_t* dispatcher, rng::Random* random,
                         component::StartupContext* startup_context,
                         std::string cobalt_client_name)
    : dispatcher_(dispatcher),
      random_(random),
      startup_context_(startup_context),
      cobalt_client_name_(std::move(cobalt_client_name)) {}

FactoryImpl::~FactoryImpl() {}

void FactoryImpl::ShutDown(fit::closure callback) {
  if (providers_.empty()) {
    callback();
    return;
  }

  providers_.set_on_empty(std::move(callback));
  for (auto& cloud_provider : providers_) {
    cloud_provider.ShutDownAndReportEmpty();
  }
}

void FactoryImpl::GetCloudProvider(
    Config config,
    fidl::InterfaceHandle<fuchsia::auth::TokenManager> token_manager,
    fidl::InterfaceRequest<cloud_provider::CloudProvider>
        cloud_provider_request,
    GetCloudProviderCallback callback) {
  auto firebase_auth = std::make_unique<firebase_auth::FirebaseAuthImpl>(
      GetFirebaseAuthConfig(config.api_key, config.user_profile_id,
                            cobalt_client_name_),
      dispatcher_, random_, token_manager.Bind(), startup_context_);

  GetFirebaseCloudProvider(std::move(config), std::move(firebase_auth),
                           std::move(cloud_provider_request),
                           std::move(callback));
}

void FactoryImpl::GetFirebaseCloudProvider(
    Config config,
    std::unique_ptr<firebase_auth::FirebaseAuthImpl> firebase_auth,
    fidl::InterfaceRequest<cloud_provider::CloudProvider>
        cloud_provider_request,
    fit::function<void(cloud_provider::Status)> callback) {
  FXL_DCHECK(firebase_auth);

  auto token_request =
      firebase_auth->GetFirebaseUserId(
          [this, config = std::move(config),
           firebase_auth = std::move(firebase_auth),
           cloud_provider_request = std::move(cloud_provider_request),
           callback = std::move(callback)](firebase_auth::AuthStatus status,
                                           std::string user_id) mutable {
            if (status != firebase_auth::AuthStatus::OK) {
              FXL_LOG(ERROR)
                  << "Failed to retrieve the user ID from auth token provider";
              callback(cloud_provider::Status::AUTH_ERROR);
              return;
            }

            auto firestore_service = std::make_unique<FirestoreServiceImpl>(
                config.server_id, dispatcher_, MakeChannel());

            providers_.emplace(random_, user_id, std::move(firebase_auth),
                               std::move(firestore_service),
                               std::move(cloud_provider_request));
            callback(cloud_provider::Status::OK);
          });
  token_requests_.emplace(token_request);
}

}  // namespace cloud_provider_firestore
