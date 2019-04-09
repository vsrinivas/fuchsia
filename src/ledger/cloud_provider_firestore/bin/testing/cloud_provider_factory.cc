// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/testing/cloud_provider_factory.h"

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fit/function.h>
#include <lib/svc/cpp/services.h>

#include <utility>

#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {
namespace {
namespace http = ::fuchsia::net::oldhttp;

constexpr char kAppUrl[] =
    "fuchsia-pkg://fuchsia.com/cloud_provider_firestore"
    "#meta/cloud_provider_firestore.cmx";

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

class CloudProviderFactory::TokenManagerContainer {
 public:
  TokenManagerContainer(
      sys::ComponentContext* component_context, async_dispatcher_t* dispatcher,
      rng::Random* random,
      std::unique_ptr<service_account::Credentials> credentials,
      std::string user_id,
      fidl::InterfaceRequest<fuchsia::auth::TokenManager> request)
      : component_context_(component_context),
        network_wrapper_(
            dispatcher,
            std::make_unique<backoff::ExponentialBackoff>(
                random->NewBitGenerator<uint64_t>()),
            [this] {
              return component_context_->svc()->Connect<http::HttpService>();
            }),
        token_manager_(&network_wrapper_, std::move(credentials),
                       std::move(user_id)),
        binding_(&token_manager_, std::move(request)) {}

  void set_on_empty(fit::closure on_empty) {
    binding_.set_error_handler(
        [on_empty = std::move(on_empty)](zx_status_t status) { on_empty(); });
  }

 private:
  sys::ComponentContext* const component_context_;
  network_wrapper::NetworkWrapperImpl network_wrapper_;
  service_account::ServiceAccountTokenManager token_manager_;
  fidl::Binding<fuchsia::auth::TokenManager> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenManagerContainer);
};

CloudProviderFactory::CloudProviderFactory(
    sys::ComponentContext* component_context, rng::Random* random,
    std::string api_key,
    std::unique_ptr<service_account::Credentials> credentials)
    : component_context_(component_context),
      random_(random),
      api_key_(std::move(api_key)),
      credentials_(std::move(credentials)),
      services_loop_(&kAsyncLoopConfigNoAttachToThread) {
  FXL_DCHECK(component_context);
  FXL_DCHECK(!api_key_.empty());
}

CloudProviderFactory::~CloudProviderFactory() { services_loop_.Shutdown(); }

void CloudProviderFactory::Init() {
  services_loop_.StartThread();
  component::Services child_services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kAppUrl;
  launch_info.directory_request = child_services.NewRequest();
  fuchsia::sys::LauncherPtr launcher;
  component_context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info),
                            cloud_provider_controller_.NewRequest());
  child_services.ConnectToService(cloud_provider_factory_.NewRequest());
}

void CloudProviderFactory::MakeCloudProvider(
    UserId user_id,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> request) {
  fuchsia::auth::TokenManagerPtr token_manager;
  MakeTokenManager(std::move(user_id), token_manager.NewRequest());

  cloud_provider_firestore::Config firebase_config;
  firebase_config.server_id = credentials_->project_id();
  firebase_config.api_key = api_key_;

  cloud_provider_factory_->GetCloudProvider(
      std::move(firebase_config), std::move(token_manager), std::move(request),
      [](cloud_provider::Status status) {
        if (status != cloud_provider::Status::OK) {
          FXL_LOG(ERROR) << "Failed to create a cloud provider: "
                         << fidl::ToUnderlying(status);
        }
      });
}

void CloudProviderFactory::MakeTokenManager(
    UserId user_id,
    fidl::InterfaceRequest<fuchsia::auth::TokenManager> request) {
  async::PostTask(
      services_loop_.dispatcher(), [this, user_id = std::move(user_id),
                                    request = std::move(request)]() mutable {
        token_managers_.emplace(component_context_, services_loop_.dispatcher(),
                                random_, credentials_->Clone(),
                                std::move(user_id.user_id()),
                                std::move(request));
      });
}

}  // namespace cloud_provider_firestore
