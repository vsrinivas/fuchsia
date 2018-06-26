// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "gtest/gtest.h"
#include "lib/backoff/exponential_backoff.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/bin/ledger/p2p_provider/impl/p2p_provider_impl.h"
#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_impl.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator_factory.h"
#include "peridot/bin/ledger/testing/cloud_provider/fake_cloud_provider.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"
#include "peridot/bin/ledger/testing/netconnector/netconnector_factory.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/socket/socket_pair.h"
#include "peridot/lib/socket/socket_writer.h"

namespace test {
namespace integration {
namespace {

constexpr zx::duration kBackoffDuration = zx::msec(5);
const char kUserId[] = "user";

ledger::Environment BuildEnvironment(async_t* async) {
  return ledger::EnvironmentBuilder()
      .SetAsync(async)
      .SetBackoffFactory([] {
        return std::make_unique<backoff::ExponentialBackoff>(
            kBackoffDuration, 1u, kBackoffDuration);
      })
      .Build();
}

class FakeUserIdProvider : public p2p_provider::UserIdProvider {
 public:
  FakeUserIdProvider() {}

  void GetUserId(std::function<void(Status, std::string)> callback) override {
    callback(Status::OK, kUserId);
  };
};

class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      LedgerAppInstanceFactory::LoopController* loop_controller,
      async_t* services_dispatcher,
      fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
          repository_factory_request,
      fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
          repository_factory_ptr,
      ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                              ledger::FakeCloudProvider>*
          cloud_provider,
      std::unique_ptr<p2p_sync::UserCommunicatorFactory>
          user_communicator_factory);
  ~LedgerAppInstanceImpl() override;

 private:
  class LedgerRepositoryFactoryContainer {
   public:
    LedgerRepositoryFactoryContainer(
        async_t* async,
        fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
            request,
        std::unique_ptr<p2p_sync::UserCommunicatorFactory>
            user_communicator_factory)
        : environment_(BuildEnvironment(async)),
          factory_impl_(&environment_, std::move(user_communicator_factory)),
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

  // This must be the last field of this class.
  fxl::WeakPtrFactory<LedgerAppInstanceImpl> weak_ptr_factory_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    LedgerAppInstanceFactory::LoopController* loop_controller,
    async_t* services_dispatcher,
    fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
        repository_factory_request,
    fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
        repository_factory_ptr,
    ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                            ledger::FakeCloudProvider>*
        cloud_provider,
    std::unique_ptr<p2p_sync::UserCommunicatorFactory>
        user_communicator_factory)
    : test::LedgerAppInstanceFactory::LedgerAppInstance(
          loop_controller, integration::RandomArray(1),
          std::move(repository_factory_ptr)),
      services_dispatcher_(services_dispatcher),
      cloud_provider_(cloud_provider),
      weak_ptr_factory_(this) {
  loop_.StartThread();
  async::PostTask(
      loop_.async(),
      fxl::MakeCopyable([this, request = std::move(repository_factory_request),
                         user_communicator_factory =
                             std::move(user_communicator_factory)]() mutable {
        factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
            loop_.async(), std::move(request),
            std::move(user_communicator_factory));
      }));
}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  cloud_provider::CloudProviderPtr cloud_provider;
  async::PostTask(services_dispatcher_,
                  fxl::MakeCopyable(callback::MakeScoped(
                      weak_ptr_factory_.GetWeakPtr(),
                      [this, request = cloud_provider.NewRequest()]() mutable {
                        cloud_provider_->AddBinding(std::move(request));
                      })));
  return cloud_provider;
}

LedgerAppInstanceImpl::~LedgerAppInstanceImpl() {
  async::PostTask(loop_.async(), [this] {
    factory_container_.reset();
    loop_.Quit();
  });
  loop_.JoinThreads();
}

class FakeUserCommunicatorFactory : public p2p_sync::UserCommunicatorFactory {
 public:
  FakeUserCommunicatorFactory(async_t* services_dispatcher,
                              ledger::NetConnectorFactory* netconnector_factory,
                              std::string host_name)
      : services_dispatcher_(services_dispatcher),
        netconnector_factory_(netconnector_factory),
        host_name_(std::move(host_name)),
        weak_ptr_factory_(this) {}
  ~FakeUserCommunicatorFactory() override {}

  std::unique_ptr<p2p_sync::UserCommunicator> GetUserCommunicator(
      ledger::DetachedPath /*user_directory*/) override {
    fuchsia::netconnector::NetConnectorPtr netconnector;
    async::PostTask(
        services_dispatcher_,
        callback::MakeScoped(
            weak_ptr_factory_.GetWeakPtr(),
            fxl::MakeCopyable([this,
                               request = netconnector.NewRequest()]() mutable {
              netconnector_factory_->AddBinding(host_name_, std::move(request));
            })));
    std::unique_ptr<p2p_provider::P2PProvider> provider =
        std::make_unique<p2p_provider::P2PProviderImpl>(
            host_name_, std::move(netconnector),
            std::make_unique<FakeUserIdProvider>());
    return std::make_unique<p2p_sync::UserCommunicatorImpl>(
        std::move(provider));
  }

 private:
  async_t* const services_dispatcher_;
  ledger::NetConnectorFactory* const netconnector_factory_;
  std::string host_name_;

  // This must be the last field of this class.
  fxl::WeakPtrFactory<FakeUserCommunicatorFactory> weak_ptr_factory_;
};

}  // namespace

enum EnableP2PMesh { NO, YES };

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl(ledger::InjectNetworkError inject_network_error,
                               EnableP2PMesh enable_p2p_mesh)
      : cloud_provider_(
            ledger::FakeCloudProvider::Builder().SetInjectNetworkError(
                inject_network_error)),
        enable_p2p_mesh_(enable_p2p_mesh) {}
  ~LedgerAppInstanceFactoryImpl() override;
  void Init();

  void SetServerId(std::string server_id) override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance(
      LoopController* loop_controller) override;

 private:
  // Loop on which to run services.
  async::Loop services_loop_;
  std::string server_id_ = "server-id";
  ledger::fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                          ledger::FakeCloudProvider>
      cloud_provider_;
  int app_instance_counter_ = 0;
  ledger::NetConnectorFactory netconnector_factory_;
  const EnableP2PMesh enable_p2p_mesh_;
};

void LedgerAppInstanceFactoryImpl::Init() { services_loop_.StartThread(); }

LedgerAppInstanceFactoryImpl::~LedgerAppInstanceFactoryImpl() {
  services_loop_.Quit();
  services_loop_.JoinThreads();
}

void LedgerAppInstanceFactoryImpl::SetServerId(std::string server_id) {
  server_id_ = server_id;
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance(
    LoopController* loop_controller) {
  ledger_internal::LedgerRepositoryFactoryPtr repository_factory_ptr;
  fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
      repository_factory_request = repository_factory_ptr.NewRequest();

  std::unique_ptr<p2p_sync::UserCommunicatorFactory> user_communicator_factory;
  if (enable_p2p_mesh_ == EnableP2PMesh::YES) {
    std::string host_name = "host_" + std::to_string(app_instance_counter_);
    user_communicator_factory = std::make_unique<FakeUserCommunicatorFactory>(
        services_loop_.async(), &netconnector_factory_, std::move(host_name));
  }
  auto result = std::make_unique<LedgerAppInstanceImpl>(
      loop_controller, services_loop_.async(),
      std::move(repository_factory_request), std::move(repository_factory_ptr),
      &cloud_provider_, std::move(user_communicator_factory));
  app_instance_counter_++;
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
      for (auto enable_p2p :
           {integration::EnableP2PMesh::NO, integration::EnableP2PMesh::YES}) {
        if (enable_p2p == integration::EnableP2PMesh::YES &&
            inject_error != ledger::InjectNetworkError::YES) {
          // Only enable p2p when cloud has errors. This helps ensure our tests
          // are fast enough for the CQ.
          continue;
        }
        auto factory =
            std::make_unique<integration::LedgerAppInstanceFactoryImpl>(
                inject_error, enable_p2p);
        factory->Init();
        factories_ptr->push_back(std::move(factory));
      }
    }
  });
  std::vector<LedgerAppInstanceFactory*> factories;
  for (const auto& factory : factories_impl) {
    factories.push_back(factory.get());
  }

  return factories;
}

}  // namespace test
