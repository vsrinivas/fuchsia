// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/testing/cloud_provider_factory.h"

#include <utility>

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fit/function.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/svc/cpp/services.h>

#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {
namespace http = ::fuchsia::net::oldhttp;

constexpr char kAppUrl[] = "cloud_provider_firestore";

std::string GenerateUserId() {
  // Always use a real random generator for user ids.
  rng::SystemRandom system_random;
  return convert::ToHex(system_random.RandomUniqueBytes());
}

}  // namespace

CloudProviderFactory::UserId::UserId(const UserId& user_id) = default;

CloudProviderFactory::UserId::UserId(UserId&& user_id) = default;

CloudProviderFactory::UserId& CloudProviderFactory::UserId::operator=(
    const UserId& user_id) = default;

CloudProviderFactory::UserId& CloudProviderFactory::UserId::operator=(
    UserId&& user_id) = default;

CloudProviderFactory::UserId CloudProviderFactory::UserId::New() {
  return UserId();
}

CloudProviderFactory::UserId::UserId() : user_id_(GenerateUserId()) {}

class CloudProviderFactory::TokenProviderContainer {
 public:
  TokenProviderContainer(
      component::StartupContext* startup_context,
      async_dispatcher_t* dispatcher, rng::Random* random,
      std::unique_ptr<service_account::Credentials> credentials,
      std::string user_id,
      fidl::InterfaceRequest<fuchsia::modular::auth::TokenProvider> request)
      : startup_context_(startup_context),
        network_wrapper_(
            dispatcher,
            std::make_unique<backoff::ExponentialBackoff>(
                random->NewBitGenerator<uint64_t>()),
            [this] {
              return startup_context_
                  ->ConnectToEnvironmentService<http::HttpService>();
            }),
        token_provider_(&network_wrapper_, std::move(credentials),
                        std::move(user_id)),
        binding_(&token_provider_, std::move(request)) {}

  void set_on_empty(fit::closure on_empty) {
    binding_.set_error_handler(
        [on_empty = std::move(on_empty)](zx_status_t status) { on_empty(); });
  }

 private:
  component::StartupContext* const startup_context_;
  network_wrapper::NetworkWrapperImpl network_wrapper_;
  service_account::ServiceAccountTokenProvider token_provider_;
  fidl::Binding<fuchsia::modular::auth::TokenProvider> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenProviderContainer);
};

CloudProviderFactory::CloudProviderFactory(
    component::StartupContext* startup_context, rng::Random* random,
    std::string api_key,
    std::unique_ptr<service_account::Credentials> credentials)
    : startup_context_(startup_context),
      random_(random),
      api_key_(std::move(api_key)),
      credentials_(std::move(credentials)),
      services_loop_(&kAsyncLoopConfigNoAttachToThread) {
  FXL_DCHECK(startup_context);
  FXL_DCHECK(!api_key_.empty());
}

CloudProviderFactory::~CloudProviderFactory() { services_loop_.Shutdown(); }

void CloudProviderFactory::Init() {
  services_loop_.StartThread();
  component::Services child_services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kAppUrl;
  launch_info.directory_request = child_services.NewRequest();
  startup_context_->launcher()->CreateComponent(
      std::move(launch_info), cloud_provider_controller_.NewRequest());
  child_services.ConnectToService(cloud_provider_factory_.NewRequest());
}

void CloudProviderFactory::MakeCloudProvider(
    UserId user_id,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> request) {
  fuchsia::modular::auth::TokenProviderPtr token_provider;
  MakeTokenProvider(std::move(user_id), token_provider.NewRequest());

  cloud_provider_firestore::Config firebase_config;
  firebase_config.server_id = credentials_->project_id();
  firebase_config.api_key = api_key_;

  cloud_provider_factory_->GetCloudProvider(
      std::move(firebase_config), std::move(token_provider), std::move(request),
      [](cloud_provider::Status status) {
        if (status != cloud_provider::Status::OK) {
          FXL_LOG(ERROR) << "Failed to create a cloud provider: "
                         << fidl::ToUnderlying(status);
        }
      });
}

void CloudProviderFactory::MakeTokenProvider(
    UserId user_id,
    fidl::InterfaceRequest<fuchsia::modular::auth::TokenProvider> request) {
  async::PostTask(services_loop_.dispatcher(),
                  fxl::MakeCopyable([this, user_id = std::move(user_id),
                                     request = std::move(request)]() mutable {
                    token_providers_.emplace(
                        startup_context_, services_loop_.dispatcher(), random_,
                        credentials_->Clone(), std::move(user_id.user_id()),
                        std::move(request));
                  }));
}

}  // namespace cloud_provider_firestore
