// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "peridot/bin/ledger/test/ledger_app_instance_factory.h"

#include "gtest/gtest.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/cloud_provider_firebase/fidl/factory.fidl.h"
#include "peridot/bin/ledger/callback/synchronous_task.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/bin/ledger/test/cloud_provider_firebase_factory.h"
#include "peridot/bin/ledger/test/fake_token_provider.h"

namespace test {
namespace {
constexpr fxl::StringView kLedgerName = "AppTests";

class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      app::ApplicationControllerPtr controller,
      ledger::LedgerRepositoryFactoryPtr ledger_repository_factory,
      CloudProviderFirebaseFactory* cloud_provider_firebase_factory,
      std::string server_id);

 private:
  cloud_provider::CloudProviderPtr MakeCloudProvider() override;

  app::ApplicationControllerPtr controller_;
  CloudProviderFirebaseFactory* const cloud_provider_firebase_factory_;
  const std::string server_id_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    app::ApplicationControllerPtr controller,
    ledger::LedgerRepositoryFactoryPtr ledger_repository_factory,
    CloudProviderFirebaseFactory* cloud_provider_firebase_factory,
    std::string server_id)
    : test::LedgerAppInstanceFactory::LedgerAppInstance(
          convert::ToArray(kLedgerName),
          std::move(ledger_repository_factory)),
      controller_(std::move(controller)),
      cloud_provider_firebase_factory_(cloud_provider_firebase_factory),
      server_id_(std::move(server_id)) {}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  return cloud_provider_firebase_factory_->MakeCloudProvider(server_id_,
                                                             "client_id");
}

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl()
      : application_context_(
            app::ApplicationContext::CreateFromStartupInfoNotChecked()),
        cloud_provider_firebase_factory_(application_context_.get()) {}
  ~LedgerAppInstanceFactoryImpl() override;
  void Init();

  void SetServerId(std::string server_id) override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() override;

 private:
  std::unique_ptr<app::ApplicationContext> application_context_;
  CloudProviderFirebaseFactory cloud_provider_firebase_factory_;

  std::string server_id_;
};

void LedgerAppInstanceFactoryImpl::Init() {
  cloud_provider_firebase_factory_.Init();
}

LedgerAppInstanceFactoryImpl::~LedgerAppInstanceFactoryImpl() {}

void LedgerAppInstanceFactoryImpl::SetServerId(std::string server_id) {
  server_id_ = server_id;
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance() {
  app::ApplicationControllerPtr controller;
  ledger::LedgerRepositoryFactoryPtr repository_factory;
  app::ServiceProviderPtr child_services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = "ledger";
  launch_info->services = child_services.NewRequest();
  launch_info->arguments.push_back("--no_minfs_wait");
  launch_info->arguments.push_back("--no_statistics_reporting_for_testing");

  application_context_->launcher()->CreateApplication(std::move(launch_info),
                                                      controller.NewRequest());
  app::ConnectToService(child_services.get(), repository_factory.NewRequest());

  auto result = std::make_unique<LedgerAppInstanceImpl>(
      std::move(controller), std::move(repository_factory),
      &cloud_provider_firebase_factory_, server_id_);
  return result;
}

}  // namespace

std::unique_ptr<LedgerAppInstanceFactory> GetLedgerAppInstanceFactory() {
  auto factory = std::make_unique<LedgerAppInstanceFactoryImpl>();
  factory->Init();
  return factory;
}

}  // namespace test
