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
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/callback/synchronous_task.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/bin/ledger/glue/socket/socket_pair.h"
#include "peridot/bin/ledger/glue/socket/socket_writer.h"
#include "peridot/bin/ledger/test/cloud_provider/fake_cloud_provider.h"
#include "peridot/bin/ledger/test/integration/test_utils.h"

namespace test {
namespace {
class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      fxl::RefPtr<fxl::TaskRunner> services_task_runner,
      fidl::InterfaceRequest<ledger::LedgerRepositoryFactory>
          repository_factory_request,
      fidl::InterfacePtr<ledger::LedgerRepositoryFactory>
          repository_factory_ptr,
      ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                              ledger::FakeCloudProvider>*
          cloud_provider);
  ~LedgerAppInstanceImpl() override;

 private:
  class LedgerRepositoryFactoryContainer {
   public:
    LedgerRepositoryFactoryContainer(
        fxl::RefPtr<fxl::TaskRunner> task_runner,
        fidl::InterfaceRequest<ledger::LedgerRepositoryFactory> request)
        : environment_(task_runner),
          factory_impl_(&environment_),
          factory_binding_(&factory_impl_, std::move(request)) {}
    ~LedgerRepositoryFactoryContainer() {}

   private:
    ledger::Environment environment_;
    ledger::LedgerRepositoryFactoryImpl factory_impl_;
    fidl::Binding<ledger::LedgerRepositoryFactory> factory_binding_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryContainer);
  };

  cloud_provider::CloudProviderPtr MakeCloudProvider() override;

  fxl::RefPtr<fxl::TaskRunner> services_task_runner_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  std::thread thread_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                          ledger::FakeCloudProvider>* const
      cloud_provider_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    fxl::RefPtr<fxl::TaskRunner> services_task_runner,
    fidl::InterfaceRequest<ledger::LedgerRepositoryFactory>
        repository_factory_request,
    fidl::InterfacePtr<ledger::LedgerRepositoryFactory> repository_factory_ptr,
    ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                            ledger::FakeCloudProvider>*
        cloud_provider)
    : test::LedgerAppInstanceFactory::LedgerAppInstance(
          integration::RandomArray(1),
          std::move(repository_factory_ptr)),
      services_task_runner_(std::move(services_task_runner)),
      cloud_provider_(cloud_provider) {
  thread_ = fsl::CreateThread(&task_runner_);
  task_runner_->PostTask(fxl::MakeCopyable(
      [this, request = std::move(repository_factory_request)]() mutable {
        factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
            task_runner_, std::move(request));
      }));
}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  cloud_provider::CloudProviderPtr cloud_provider;
  services_task_runner_->PostTask(fxl::MakeCopyable(
      [this, request = cloud_provider.NewRequest()]() mutable {
        cloud_provider_->AddBinding(std::move(request));
      }));
  return cloud_provider;
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
  // Thread used to run services.
  std::thread services_thread_;
  fxl::RefPtr<fxl::TaskRunner> services_task_runner_;
  std::string server_id_ = "server-id";
  ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                          ledger::FakeCloudProvider>
      cloud_provider_;
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
  ledger::LedgerRepositoryFactoryPtr repository_factory_ptr;
  fidl::InterfaceRequest<ledger::LedgerRepositoryFactory>
      repository_factory_request = repository_factory_ptr.NewRequest();

  auto result = std::make_unique<LedgerAppInstanceImpl>(
      services_task_runner_, std::move(repository_factory_request),
      std::move(repository_factory_ptr), &cloud_provider_);
  return result;
}

}  // namespace

std::unique_ptr<LedgerAppInstanceFactory> GetLedgerAppInstanceFactory() {
  auto factory = std::make_unique<LedgerAppInstanceFactoryImpl>();
  factory->Init();
  return factory;
}

}  // namespace test
