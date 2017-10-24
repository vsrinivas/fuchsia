// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/cloud_provider_firebase_factory.h"

#include <utility>

#include "lib/app/cpp/connect.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"
#include "lib/fxl/functional/make_copyable.h"

namespace test {
namespace {
constexpr char kCloudProviderFirebaseAppUrl[] = "cloud_provider_firebase";
}  // namespace

CloudProviderFirebaseFactory::CloudProviderFirebaseFactory(
    app::ApplicationContext* application_context)
    : application_context_(application_context),
      token_provider_("", "sync_user", "sync_user@example.com", "client_id") {}

CloudProviderFirebaseFactory::~CloudProviderFirebaseFactory() {
  services_task_runner_->PostTask(
      [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  services_thread_.join();
}

void CloudProviderFirebaseFactory::Init() {
  services_thread_ = fsl::CreateThread(&services_task_runner_);
  app::ServiceProviderPtr child_services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = kCloudProviderFirebaseAppUrl;
  launch_info->services = child_services.NewRequest();
  application_context_->launcher()->CreateApplication(
      std::move(launch_info), cloud_provider_controller_.NewRequest());
  app::ConnectToService(child_services.get(),
                        cloud_provider_factory_.NewRequest());
}

cloud_provider::CloudProviderPtr
CloudProviderFirebaseFactory::MakeCloudProvider(std::string server_id,
                                                std::string api_key) {
  modular::auth::TokenProviderPtr token_provider;
  services_task_runner_->PostTask(fxl::MakeCopyable(
      [this, request = token_provider.NewRequest()]() mutable {
        token_provider_.AddBinding(std::move(request));
      }));

  auto firebase_config = cloud_provider_firebase::Config::New();
  firebase_config->server_id = server_id;
  firebase_config->api_key = api_key;

  cloud_provider::CloudProviderPtr cloud_provider;
  cloud_provider_factory_->GetCloudProvider(
      std::move(firebase_config), std::move(token_provider),
      cloud_provider.NewRequest(), [](cloud_provider::Status status) {
        if (status != cloud_provider::Status::OK) {
          FXL_LOG(ERROR) << "Failed to create a cloud provider: " << status;
        }
      });
  return cloud_provider;
}

}  // namespace test
