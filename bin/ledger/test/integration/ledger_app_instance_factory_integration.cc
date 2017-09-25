// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/ledger_app_instance_factory.h"

#include <thread>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/create_thread.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/app/erase_remote_repository_operation.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/callback/synchronous_task.h"
#include "peridot/bin/ledger/glue/socket/socket_pair.h"
#include "peridot/bin/ledger/glue/socket/socket_writer.h"
#include "peridot/bin/ledger/network/network_service_impl.h"
#include "peridot/bin/ledger/test/cloud_server/fake_cloud_network_service.h"
#include "peridot/bin/ledger/test/fake_token_provider.h"
#include "peridot/bin/ledger/test/integration/test_utils.h"

namespace test {
namespace {
class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      fxl::RefPtr<fxl::TaskRunner> services_task_runner,
      std::function<network::NetworkServicePtr()> network_factory,
      ledger::FirebaseConfigPtr firebase_config,
      fidl::InterfaceRequest<ledger::LedgerRepositoryFactory>
          repository_factory_request,
      fidl::InterfacePtr<ledger::LedgerRepositoryFactory>
          repository_factory_ptr);
  ~LedgerAppInstanceImpl() override;

 private:
  class LedgerRepositoryFactoryContainer
      : public ledger::LedgerRepositoryFactoryImpl::Delegate {
   public:
    LedgerRepositoryFactoryContainer(
        fxl::RefPtr<fxl::TaskRunner> task_runner,
        std::function<network::NetworkServicePtr()> network_factory,
        fidl::InterfaceRequest<ledger::LedgerRepositoryFactory> request)
        : network_service_(task_runner, std::move(network_factory)),
          environment_(task_runner, &network_service_),
          factory_impl_(
              this,
              &environment_,
              ledger::LedgerRepositoryFactoryImpl::ConfigPersistence::FORGET),
          factory_binding_(&factory_impl_, std::move(request)) {}
    ~LedgerRepositoryFactoryContainer() override {}

   private:
    // LedgerRepositoryFactoryImpl::Delegate:
    void EraseRepository(
        ledger::
            EraseRemoteRepositoryOperation /*erase_remote_repository_operation*/
        ,
        std::function<void(bool)> callback) override {
      FXL_NOTIMPLEMENTED();
      callback(true);
    }
    ledger::NetworkServiceImpl network_service_;
    ledger::Environment environment_;
    ledger::LedgerRepositoryFactoryImpl factory_impl_;
    fidl::Binding<ledger::LedgerRepositoryFactory> factory_binding_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryContainer);
  };

  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  std::thread thread_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    fxl::RefPtr<fxl::TaskRunner> services_task_runner,
    std::function<network::NetworkServicePtr()> network_factory,
    ledger::FirebaseConfigPtr firebase_config,
    fidl::InterfaceRequest<ledger::LedgerRepositoryFactory>
        repository_factory_request,
    fidl::InterfacePtr<ledger::LedgerRepositoryFactory> repository_factory_ptr)
    : test::LedgerAppInstanceFactory::LedgerAppInstance(
          std::move(firebase_config),
          integration::RandomArray(1),
          std::move(repository_factory_ptr),
          std::move(services_task_runner)) {
  thread_ = fsl::CreateThread(&task_runner_);
  task_runner_->PostTask(fxl::MakeCopyable([
    this, request = std::move(repository_factory_request),
    network_factory = std::move(network_factory)
  ]() mutable {
    factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
        task_runner_, std::move(network_factory), std::move(request));
  }));
}

LedgerAppInstanceImpl::~LedgerAppInstanceImpl() {
  task_runner_->PostTask([this]() {
    fsl::MessageLoop::GetCurrent()->QuitNow();
    factory_container_.reset();
  });
  thread_.join();
}

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl() {}
  ~LedgerAppInstanceFactoryImpl() override;
  void Init();

  void SetServerId(std::string server_id) override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() override;

 private:
  // Thread used to do services.
  std::thread services_thread_;
  fxl::RefPtr<fxl::TaskRunner> services_task_runner_;
  ledger::FakeCloudNetworkService network_service_;
  std::string server_id_ = "server-id";
};

void LedgerAppInstanceFactoryImpl::Init() {
  services_thread_ = fsl::CreateThread(&services_task_runner_);
}

LedgerAppInstanceFactoryImpl::~LedgerAppInstanceFactoryImpl() {
  services_task_runner_->PostTask(
      [] { fsl::MessageLoop::GetCurrent()->QuitNow(); });
  services_thread_.join();
}

void LedgerAppInstanceFactoryImpl::SetServerId(std::string server_id) {
  server_id_ = server_id;
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance() {
  auto network_factory = [this]() {
    network::NetworkServicePtr result;
    services_task_runner_->PostTask(
        fxl::MakeCopyable([ this, request = result.NewRequest() ]() mutable {
          network_service_.AddBinding(std::move(request));
        }));
    return result;
  };
  ledger::FirebaseConfigPtr firebase_config;
  firebase_config = ledger::FirebaseConfig::New();
  firebase_config->server_id = server_id_;
  firebase_config->api_key = "api-key";

  ledger::LedgerRepositoryFactoryPtr repository_factory_ptr;
  fidl::InterfaceRequest<ledger::LedgerRepositoryFactory>
      repository_factory_request = repository_factory_ptr.NewRequest();

  auto result = std::make_unique<LedgerAppInstanceImpl>(
      services_task_runner_, std::move(network_factory),
      std::move(firebase_config), std::move(repository_factory_request),
      std::move(repository_factory_ptr));
  return result;
}

}  // namespace

std::unique_ptr<LedgerAppInstanceFactory> GetLedgerAppInstanceFactory() {
  auto factory = std::make_unique<LedgerAppInstanceFactoryImpl>();
  factory->Init();
  return factory;
}

}  // namespace test
