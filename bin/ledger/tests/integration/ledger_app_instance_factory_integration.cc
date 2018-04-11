// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <lib/async/cpp/task.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/lib/callback/synchronous_task.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/bin/ledger/testing/cloud_provider/fake_cloud_provider.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/socket/socket_pair.h"
#include "peridot/lib/socket/socket_writer.h"

namespace test {
namespace integration {
namespace {

class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      async_t* services_dispatcher,
      fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
          repository_factory_request,
      fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
          repository_factory_ptr,
      ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                              ledger::FakeCloudProvider>*
          cloud_provider);
  ~LedgerAppInstanceImpl() override;

 private:
  class LedgerRepositoryFactoryContainer {
   public:
    LedgerRepositoryFactoryContainer(
        async_t* async,
        fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
            request)
        : environment_(async),
          factory_impl_(&environment_, nullptr),
          factory_binding_(&factory_impl_, std::move(request)) {}
    ~LedgerRepositoryFactoryContainer() {}

   private:
    ledger::Environment environment_;
    ledger::LedgerRepositoryFactoryImpl factory_impl_;
    fidl::Binding<ledger_internal::LedgerRepositoryFactory> factory_binding_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryContainer);
  };

  cloud_provider::CloudProviderPtr MakeCloudProvider() override;

  async::Loop loop_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  async_t* services_dispatcher_;
  ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                          ledger::FakeCloudProvider>* const
      cloud_provider_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    async_t* services_dispatcher,
    fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
        repository_factory_request,
    fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
        repository_factory_ptr,
    ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                            ledger::FakeCloudProvider>*
        cloud_provider)
    : test::LedgerAppInstanceFactory::LedgerAppInstance(
          integration::RandomArray(1),
          std::move(repository_factory_ptr)),
      services_dispatcher_(services_dispatcher),
      cloud_provider_(cloud_provider) {
  loop_.StartThread();
  async::PostTask(loop_.async(), fxl::MakeCopyable(
      [this, request = std::move(repository_factory_request)]() mutable {
        factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
            loop_.async(), std::move(request));
      }));
}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  cloud_provider::CloudProviderPtr cloud_provider;
  async::PostTask(services_dispatcher_, fxl::MakeCopyable(
      [this, request = cloud_provider.NewRequest()]() mutable {
        cloud_provider_->AddBinding(std::move(request));
      }));
  return cloud_provider;
}

LedgerAppInstanceImpl::~LedgerAppInstanceImpl() {
  async::PostTask(loop_.async(), [this] {
    factory_container_.reset();
    loop_.Quit();
  });
  loop_.JoinThreads();
}

}  // namespace

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl(ledger::InjectNetworkError inject_network_error)
      : cloud_provider_(
            ledger::FakeCloudProvider::Builder().SetInjectNetworkError(
                inject_network_error)) {}
  ~LedgerAppInstanceFactoryImpl() override;
  void Init();

  void SetServerId(std::string server_id) override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() override;

 private:
  // Loop on which to run services.
  async::Loop services_loop_;
  std::string server_id_ = "server-id";
  ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                          ledger::FakeCloudProvider>
      cloud_provider_;
};

void LedgerAppInstanceFactoryImpl::Init() {
  services_loop_.StartThread();
}

LedgerAppInstanceFactoryImpl::~LedgerAppInstanceFactoryImpl() {
  services_loop_.Quit();
  services_loop_.JoinThreads();
}

void LedgerAppInstanceFactoryImpl::SetServerId(std::string server_id) {
  server_id_ = server_id;
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance() {
  ledger_internal::LedgerRepositoryFactoryPtr repository_factory_ptr;
  fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
      repository_factory_request = repository_factory_ptr.NewRequest();

  auto result = std::make_unique<LedgerAppInstanceImpl>(
      services_loop_.async(), std::move(repository_factory_request),
      std::move(repository_factory_ptr), &cloud_provider_);
  return result;
}

}  // namespace integration

std::vector<LedgerAppInstanceFactory*> GetLedgerAppInstanceFactories() {
  static std::vector<std::unique_ptr<LedgerAppInstanceFactory>> factories_impl;
  static std::once_flag flag;

  auto factories_ptr = &factories_impl;
  std::call_once(flag, [factories_ptr] {
    for (auto inject_error :
         {ledger::InjectNetworkError::NO, ledger::InjectNetworkError::YES}) {
      auto factory =
          std::make_unique<integration::LedgerAppInstanceFactoryImpl>(
              inject_error);
      factory->Init();
      factories_ptr->push_back(std::move(factory));
    }
  });
  std::vector<LedgerAppInstanceFactory*> factories;
  for (const auto& factory : factories_impl) {
    factories.push_back(factory.get());
  }

  return factories;
}

}  // namespace test
