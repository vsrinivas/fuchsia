// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/fsl/socket/strings.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/timekeeper/test_loop_test_clock.h>

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "peridot/lib/rng/random.h"
#include "peridot/lib/rng/test_random.h"
#include "peridot/lib/socket/socket_pair.h"
#include "peridot/lib/socket/socket_writer.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/fidl_helpers/bound_interface_set.h"
#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_impl.h"
#include "src/ledger/bin/p2p_sync/public/user_communicator_factory.h"
#include "src/ledger/bin/testing/ledger_app_instance_factory.h"
#include "src/ledger/bin/testing/loop_controller_test_loop.h"
#include "src/ledger/bin/testing/netconnector/netconnector_factory.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/cloud_provider_in_memory/lib/fake_cloud_provider.h"
#include "src/ledger/cloud_provider_in_memory/lib/types.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace ledger {
namespace {

constexpr fxl::StringView kLedgerName = "AppTests";
constexpr zx::duration kBackoffDuration = zx::msec(5);
const char kUserId[] = "user";

// Implementation of rng::Random that delegates to another instance. This is
// needed because EnvironmentBuilder requires taking ownership of the random
// implementation.
class DelegatedRandom final : public rng::Random {
 public:
  DelegatedRandom(rng::Random* base) : base_(base) {}
  ~DelegatedRandom() override = default;

 private:
  void InternalDraw(void* buffer, size_t buffer_size) {
    base_->Draw(buffer, buffer_size);
  }

  rng::Random* base_;
};

Environment BuildEnvironment(async::TestLoop* loop,
                             async_dispatcher_t* dispatcher,
                             async_dispatcher_t* io_dispatcher,
                             sys::ComponentContext* component_context,
                             rng::Random* random) {
  return EnvironmentBuilder()
      .SetAsync(dispatcher)
      .SetIOAsync(io_dispatcher)
      .SetStartupContext(component_context)
      .SetBackoffFactory([random] {
        return std::make_unique<backoff::ExponentialBackoff>(
            kBackoffDuration, 1u, kBackoffDuration,
            random->NewBitGenerator<uint64_t>());
      })
      .SetClock(std::make_unique<timekeeper::TestLoopTestClock>(loop))
      .SetRandom(std::make_unique<DelegatedRandom>(random))
      .Build();
}

class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      LoopControllerTestLoop* loop_controller,
      async_dispatcher_t* services_dispatcher, rng::Random* random,
      fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
          repository_factory_request,
      fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
          repository_factory_ptr,
      fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                      FakeCloudProvider>* cloud_provider,
      std::unique_ptr<p2p_sync::UserCommunicatorFactory>
          user_communicator_factory);
  ~LedgerAppInstanceImpl() override;

 private:
  class LedgerRepositoryFactoryContainer {
   public:
    LedgerRepositoryFactoryContainer(
        async::TestLoop* loop, async_dispatcher_t* dispatcher,
        async_dispatcher_t* io_dispatcher, rng::Random* random,
        fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
            request,
        std::unique_ptr<p2p_sync::UserCommunicatorFactory>
            user_communicator_factory)
        : environment_(BuildEnvironment(loop, dispatcher, io_dispatcher,
                                        component_context_provider_.context(),
                                        random)),
          factory_impl_(&environment_, std::move(user_communicator_factory),
                        inspect::Node("unused_in_test_inspect_node")),
          binding_(&factory_impl_, std::move(request)) {}
    ~LedgerRepositoryFactoryContainer() {}

   private:
    sys::testing::ComponentContextProvider component_context_provider_;
    Environment environment_;
    LedgerRepositoryFactoryImpl factory_impl_;
    SyncableBinding<
        fuchsia::ledger::internal::LedgerRepositoryFactorySyncableDelegate>
        binding_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryContainer);
  };

  cloud_provider::CloudProviderPtr MakeCloudProvider() override;
  std::string GetUserId() override;

  std::unique_ptr<SubLoop> loop_;
  std::unique_ptr<SubLoop> io_loop_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  async_dispatcher_t* services_dispatcher_;
  fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                  FakeCloudProvider>* const cloud_provider_;

  // This must be the last field of this class.
  fxl::WeakPtrFactory<LedgerAppInstanceImpl> weak_ptr_factory_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    LoopControllerTestLoop* loop_controller,
    async_dispatcher_t* services_dispatcher, rng::Random* random,
    fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
        repository_factory_request,
    fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
        repository_factory_ptr,
    fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                    FakeCloudProvider>* cloud_provider,
    std::unique_ptr<p2p_sync::UserCommunicatorFactory>
        user_communicator_factory)
    : LedgerAppInstanceFactory::LedgerAppInstance(
          loop_controller, convert::ToArray(kLedgerName),
          std::move(repository_factory_ptr)),
      loop_(loop_controller->StartNewLoop()),
      io_loop_(loop_controller->StartNewLoop()),
      services_dispatcher_(services_dispatcher),
      cloud_provider_(cloud_provider),
      weak_ptr_factory_(this) {
  async::PostTask(loop_->dispatcher(),
                  [this, loop_controller, random,
                   request = std::move(repository_factory_request),
                   user_communicator_factory =
                       std::move(user_communicator_factory)]() mutable {
                    factory_container_ =
                        std::make_unique<LedgerRepositoryFactoryContainer>(
                            &loop_controller->test_loop(), loop_->dispatcher(),
                            io_loop_->dispatcher(), random, std::move(request),
                            std::move(user_communicator_factory));
                  });
}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  cloud_provider::CloudProviderPtr cloud_provider;
  async::PostTask(services_dispatcher_,
                  callback::MakeScoped(
                      weak_ptr_factory_.GetWeakPtr(),
                      [this, request = cloud_provider.NewRequest()]() mutable {
                        cloud_provider_->AddBinding(std::move(request));
                      }));
  return cloud_provider;
}

std::string LedgerAppInstanceImpl::GetUserId() { return kUserId; }

LedgerAppInstanceImpl::~LedgerAppInstanceImpl() {
  async::PostTask(loop_->dispatcher(), [this] { factory_container_.reset(); });
  loop_->DrainAndQuit();
  loop_.release();
}

class FakeUserCommunicatorFactory : public p2p_sync::UserCommunicatorFactory {
 public:
  FakeUserCommunicatorFactory(async::TestLoop* loop,
                              async_dispatcher_t* services_dispatcher,
                              rng::Random* random,
                              NetConnectorFactory* netconnector_factory,
                              std::string host_name)
      : services_dispatcher_(services_dispatcher),
        environment_(
            BuildEnvironment(loop, services_dispatcher, services_dispatcher,
                             component_context_provider_.context(), random)),
        netconnector_factory_(netconnector_factory),
        host_name_(std::move(host_name)),
        weak_ptr_factory_(this) {}
  ~FakeUserCommunicatorFactory() override {}

  std::unique_ptr<p2p_sync::UserCommunicator> GetUserCommunicator(
      std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider) override {
    fuchsia::netconnector::NetConnectorPtr netconnector;
    async::PostTask(services_dispatcher_,
                    callback::MakeScoped(
                        weak_ptr_factory_.GetWeakPtr(),
                        [this, request = netconnector.NewRequest()]() mutable {
                          netconnector_factory_->AddBinding(host_name_,
                                                            std::move(request));
                        }));
    std::unique_ptr<p2p_provider::P2PProvider> provider =
        std::make_unique<p2p_provider::P2PProviderImpl>(
            host_name_, std::move(netconnector), std::move(user_id_provider));
    return std::make_unique<p2p_sync::UserCommunicatorImpl>(
        std::move(provider), environment_.coroutine_service());
  }

 private:
  async_dispatcher_t* const services_dispatcher_;
  sys::testing::ComponentContextProvider component_context_provider_;
  Environment environment_;
  NetConnectorFactory* const netconnector_factory_;
  std::string host_name_;

  // This must be the last field of this class.
  fxl::WeakPtrFactory<FakeUserCommunicatorFactory> weak_ptr_factory_;
};

enum EnableP2PMesh { NO, YES };

}  // namespace

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl(InjectNetworkError inject_network_error,
                               EnableP2PMesh enable_p2p_mesh)
      : loop_controller_(&loop_),
        random_(loop_.initial_state()),
        services_loop_(loop_controller_.StartNewLoop()),
        cloud_provider_(FakeCloudProvider::Builder().SetInjectNetworkError(
            inject_network_error)),
        enable_p2p_mesh_(enable_p2p_mesh) {}
  ~LedgerAppInstanceFactoryImpl() override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() override;

  LoopController* GetLoopController() override;

  rng::Random* GetRandom() override;

 private:
  async::TestLoop loop_;
  LoopControllerTestLoop loop_controller_;
  rng::TestRandom random_;
  // Loop on which to run services.
  std::unique_ptr<SubLoop> services_loop_;
  fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                  FakeCloudProvider>
      cloud_provider_;
  int app_instance_counter_ = 0;
  NetConnectorFactory netconnector_factory_;
  const EnableP2PMesh enable_p2p_mesh_;
};

LedgerAppInstanceFactoryImpl::~LedgerAppInstanceFactoryImpl() {
  services_loop_->DrainAndQuit();
  services_loop_.reset();
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance() {
  ledger_internal::LedgerRepositoryFactoryPtr repository_factory_ptr;
  fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
      repository_factory_request = repository_factory_ptr.NewRequest();

  std::unique_ptr<p2p_sync::UserCommunicatorFactory> user_communicator_factory;
  if (enable_p2p_mesh_ == EnableP2PMesh::YES) {
    std::string host_name = "host_" + std::to_string(app_instance_counter_);
    user_communicator_factory = std::make_unique<FakeUserCommunicatorFactory>(
        &loop_controller_.test_loop(), services_loop_->dispatcher(), &random_,
        &netconnector_factory_, std::move(host_name));
  }
  auto result = std::make_unique<LedgerAppInstanceImpl>(
      &loop_controller_, services_loop_->dispatcher(), &random_,
      std::move(repository_factory_request), std::move(repository_factory_ptr),
      &cloud_provider_, std::move(user_communicator_factory));
  app_instance_counter_++;
  return result;
}

LoopController* LedgerAppInstanceFactoryImpl::GetLoopController() {
  return &loop_controller_;
}

rng::Random* LedgerAppInstanceFactoryImpl::GetRandom() { return &random_; }

namespace {

class FactoryBuilderIntegrationImpl : public LedgerAppInstanceFactoryBuilder {
 public:
  FactoryBuilderIntegrationImpl(InjectNetworkError inject_error,
                                EnableP2PMesh enable_p2p)
      : inject_error_(inject_error), enable_p2p_(enable_p2p){};

  std::unique_ptr<LedgerAppInstanceFactory> NewFactory() const override {
    return std::make_unique<LedgerAppInstanceFactoryImpl>(inject_error_,
                                                          enable_p2p_);
  }

 private:
  InjectNetworkError inject_error_;
  EnableP2PMesh enable_p2p_;
};

}  // namespace

std::vector<const LedgerAppInstanceFactoryBuilder*>
GetLedgerAppInstanceFactoryBuilders() {
  static std::vector<std::unique_ptr<FactoryBuilderIntegrationImpl>>
      static_builders;
  static std::once_flag flag;

  auto static_builders_ptr = &static_builders;
  std::call_once(flag, [&static_builders_ptr] {
    for (auto inject_error :
         {InjectNetworkError::NO, InjectNetworkError::YES}) {
      for (auto enable_p2p : {EnableP2PMesh::NO, EnableP2PMesh::YES}) {
        if (enable_p2p == EnableP2PMesh::YES &&
            inject_error != InjectNetworkError::YES) {
          // Only enable p2p when cloud has errors. This helps ensure our tests
          // are fast enough for the CQ.
          continue;
        }
        static_builders_ptr->push_back(
            std::make_unique<FactoryBuilderIntegrationImpl>(inject_error,
                                                            enable_p2p));
      }
    }
  });

  std::vector<const LedgerAppInstanceFactoryBuilder*> builders;

  for (const auto& builder : static_builders) {
    builders.push_back(builder.get());
  }

  return builders;
}

}  // namespace ledger
