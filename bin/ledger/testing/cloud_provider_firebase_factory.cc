// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/cloud_provider_firebase_factory.h"

#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/svc/cpp/services.h>

namespace test {
namespace {
constexpr char kCloudProviderFirebaseAppUrl[] = "cloud_provider_firebase";
}  // namespace

CloudProviderFirebaseFactory::CloudProviderFirebaseFactory(
    component::StartupContext* startup_context)
    : startup_context_(startup_context),
      loop_(&kAsyncLoopConfigNoAttachToThread) {}

CloudProviderFirebaseFactory::~CloudProviderFirebaseFactory() {
  loop_.Shutdown();
}

void CloudProviderFirebaseFactory::Init() {
  loop_.StartThread();
  component::Services child_services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kCloudProviderFirebaseAppUrl;
  launch_info.directory_request = child_services.NewRequest();
  launch_info.arguments.push_back("--disable_reporting");
  startup_context_->launcher()->CreateComponent(
      std::move(launch_info), cloud_provider_controller_.NewRequest());
  child_services.ConnectToService(cloud_provider_factory_.NewRequest());
}

void CloudProviderFirebaseFactory::MakeCloudProvider(
    std::string server_id, std::string api_key,
    fidl::InterfaceRequest<cloud_provider::CloudProvider> request) {
  fuchsia::modular::auth::TokenProviderPtr token_provider;
  async::PostTask(loop_.dispatcher(),
                  [this, request = token_provider.NewRequest()]() mutable {
                    token_provider_.AddBinding(std::move(request));
                  });

  fuchsia::ledger::cloud::firebase::Config firebase_config;
  firebase_config.server_id = server_id;
  firebase_config.api_key = api_key;

  cloud_provider_factory_->GetCloudProvider(
      std::move(firebase_config), std::move(token_provider), std::move(request),
      [](cloud_provider::Status status) {
        if (status != cloud_provider::Status::OK) {
          FXL_LOG(ERROR) << "Failed to create a cloud provider: " << status;
        }
      });
}

}  // namespace test
