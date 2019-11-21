// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/testing/cloud_provider_factory.h"

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <zircon/status.h>

#include <utility>

#include "peridot/lib/convert/convert.h"
#include "src/lib/backoff/exponential_backoff.h"

namespace cloud_provider_firestore {
namespace {
namespace http = ::fuchsia::net::oldhttp;

constexpr char kAppUrl[] =
    "fuchsia-pkg://fuchsia.com/cloud_provider_firestore"
    "#meta/cloud_provider_firestore.cmx";

// Matches cloud_provider_firestore flag defined in
// src/ledger/cloud_provider_firestore/bin/app/app.cc (with leading "--" added).
constexpr fxl::StringView kNoCobaltReporting = "--disable_reporting";

std::string GenerateUserId() {
  // Always use a real random generator for user ids.
  rng::SystemRandom system_random;
  return convert::ToHex(system_random.RandomUniqueBytes());
}

}  // namespace

CloudProviderFactory::UserId::UserId(const UserId& user_id) = default;

CloudProviderFactory::UserId::UserId(UserId&& user_id) noexcept = default;

CloudProviderFactory::UserId& CloudProviderFactory::UserId::operator=(const UserId& user_id) =
    default;

CloudProviderFactory::UserId& CloudProviderFactory::UserId::operator=(UserId&& user_id) noexcept =
    default;

CloudProviderFactory::UserId CloudProviderFactory::UserId::New() { return UserId(); }

CloudProviderFactory::UserId::UserId() : user_id_(GenerateUserId()) {}

class CloudProviderFactory::TokenManagerContainer {
 public:
  TokenManagerContainer(sys::ComponentContext* component_context, async_dispatcher_t* dispatcher,
                        rng::Random* random,
                        std::unique_ptr<service_account::Credentials> credentials,
                        std::string user_id,
                        fidl::InterfaceRequest<fuchsia::auth::TokenManager> request)
      : component_context_(component_context),
        network_wrapper_(
            dispatcher,
            std::make_unique<backoff::ExponentialBackoff>(random->NewBitGenerator<uint64_t>()),
            [this] { return component_context_->svc()->Connect<http::HttpService>(); }),
        token_manager_(dispatcher, &network_wrapper_, std::move(credentials), std::move(user_id)),
        binding_(&token_manager_, std::move(request)) {}

  void SetOnDiscardable(fit::closure on_discardable) {
    binding_.set_error_handler(
        [this, on_discardable = std::move(on_discardable)](zx_status_t status) {
          binding_.Unbind();
          on_discardable();
        });
  }

  bool IsDiscardable() const { return !binding_.is_bound(); }

 private:
  sys::ComponentContext* const component_context_;
  network_wrapper::NetworkWrapperImpl network_wrapper_;
  service_account::ServiceAccountTokenManager token_manager_;
  fidl::Binding<fuchsia::auth::TokenManager> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenManagerContainer);
};

CloudProviderFactory::CloudProviderFactory(
    sys::ComponentContext* component_context, rng::Random* random, std::string api_key,
    std::unique_ptr<service_account::Credentials> credentials)
    : component_context_(component_context),
      random_(random),
      api_key_(std::move(api_key)),
      credentials_(std::move(credentials)),
      services_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      token_managers_(services_loop_.dispatcher()) {
  FXL_DCHECK(component_context);
  FXL_DCHECK(!api_key_.empty());
}

CloudProviderFactory::~CloudProviderFactory() {
  // Kill the cloud provider instance and wait until it disconnects before
  // shutting down the services thread that runs the token manager that is
  // exposed to it.
  cloud_provider_controller_->Kill();
  auto channel = cloud_provider_controller_.Unbind().TakeChannel();
  zx_signals_t observed;
  auto wait_status =
      channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::deadline_after(zx::sec(5)), &observed);
  if (wait_status != ZX_OK) {
    FXL_LOG(WARNING) << "Failed waiting for the cloud provider to close (timeout?): "
                     << zx_status_get_string(wait_status);
  }

  // Now it's safe to shut down the services loop.
  services_loop_.Shutdown();
}

void CloudProviderFactory::Init() {
  services_loop_.StartThread();
  fidl::InterfaceHandle<fuchsia::io::Directory> child_directory;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kAppUrl;
  launch_info.arguments.emplace({kNoCobaltReporting.ToString()});
  launch_info.directory_request = child_directory.NewRequest().TakeChannel();
  fuchsia::sys::LauncherPtr launcher;
  component_context_->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info), cloud_provider_controller_.NewRequest());
  sys::ServiceDirectory child_services(std::move(child_directory));
  child_services.Connect(cloud_provider_factory_.NewRequest());
}

void CloudProviderFactory::MakeCloudProvider(
    UserId user_id, fidl::InterfaceRequest<cloud_provider::CloudProvider> request) {
  fuchsia::auth::TokenManagerPtr token_manager;
  MakeTokenManager(std::move(user_id), token_manager.NewRequest());

  cloud_provider_firestore::Config firebase_config;
  firebase_config.server_id = credentials_->project_id();
  firebase_config.api_key = api_key_;

  cloud_provider_factory_->GetCloudProvider(
      std::move(firebase_config), std::move(token_manager), std::move(request),
      [](cloud_provider::Status status) {
        if (status != cloud_provider::Status::OK) {
          FXL_LOG(ERROR) << "Failed to create a cloud provider: " << fidl::ToUnderlying(status);
        }
      });
}

void CloudProviderFactory::MakeTokenManager(
    UserId user_id, fidl::InterfaceRequest<fuchsia::auth::TokenManager> request) {
  async::PostTask(services_loop_.dispatcher(),
                  [this, user_id = std::move(user_id), request = std::move(request)]() mutable {
                    token_managers_.emplace(component_context_, services_loop_.dispatcher(),
                                            random_, credentials_->Clone(),
                                            std::move(user_id.user_id()), std::move(request));
                  });
}

}  // namespace cloud_provider_firestore
