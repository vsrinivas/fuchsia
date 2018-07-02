// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fuchsia/ledger/cloud/firebase/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/svc/cpp/services.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/bin/ledger/testing/cloud_provider_firebase_factory.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"
#include "peridot/lib/convert/convert.h"
#include "peridot/lib/firebase_auth/testing/fake_token_provider.h"

namespace test {
namespace {
constexpr fxl::StringView kLedgerName = "AppTests";

class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      LedgerAppInstanceFactory::LoopController* loop_controller,
      fuchsia::sys::ComponentControllerPtr controller,
      ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory,
      CloudProviderFirebaseFactory* cloud_provider_firebase_factory,
      std::string server_id);

 private:
  cloud_provider::CloudProviderPtr MakeCloudProvider() override;

  fuchsia::sys::ComponentControllerPtr controller_;
  CloudProviderFirebaseFactory* const cloud_provider_firebase_factory_;
  const std::string server_id_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    LedgerAppInstanceFactory::LoopController* loop_controller,
    fuchsia::sys::ComponentControllerPtr controller,
    ledger_internal::LedgerRepositoryFactoryPtr ledger_repository_factory,
    CloudProviderFirebaseFactory* cloud_provider_firebase_factory,
    std::string server_id)
    : test::LedgerAppInstanceFactory::LedgerAppInstance(
          loop_controller, convert::ToArray(kLedgerName),
          std::move(ledger_repository_factory)),
      controller_(std::move(controller)),
      cloud_provider_firebase_factory_(cloud_provider_firebase_factory),
      server_id_(std::move(server_id)) {}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  cloud_provider::CloudProviderPtr cloud_provider;
  cloud_provider_firebase_factory_->MakeCloudProvider(
      server_id_, "client_id", cloud_provider.NewRequest());
  return cloud_provider;
}

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl()
      : startup_context_(
            fuchsia::sys::StartupContext::CreateFromStartupInfoNotChecked()),
        cloud_provider_firebase_factory_(startup_context_.get()) {}
  ~LedgerAppInstanceFactoryImpl() override;
  void Init();

  void SetServerId(std::string server_id) override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance(
      LoopController* loop_controller) override;

 private:
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
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
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance(
    LedgerAppInstanceFactory::LoopController* loop_controller) {
  fuchsia::sys::ComponentControllerPtr controller;
  ledger_internal::LedgerRepositoryFactoryPtr repository_factory;
  fuchsia::sys::Services child_services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "ledger";
  launch_info.directory_request = child_services.NewRequest();
  launch_info.arguments.push_back("--no_minfs_wait");
  launch_info.arguments.push_back("--disable_reporting");

  startup_context_->launcher()->CreateComponent(std::move(launch_info),
                                                controller.NewRequest());
  child_services.ConnectToService(repository_factory.NewRequest());

  auto result = std::make_unique<LedgerAppInstanceImpl>(
      loop_controller, std::move(controller), std::move(repository_factory),
      &cloud_provider_firebase_factory_, server_id_);
  return result;
}

}  // namespace

std::vector<LedgerAppInstanceFactory*> GetLedgerAppInstanceFactories() {
  static std::unique_ptr<LedgerAppInstanceFactory> factory;
  static std::once_flag flag;

  auto factory_ptr = &factory;
  std::call_once(flag, [factory_ptr] {
    auto factory = std::make_unique<LedgerAppInstanceFactoryImpl>();
    factory->Init();
    *factory_ptr = std::move(factory);
  });

  return {factory.get()};
}

}  // namespace test
