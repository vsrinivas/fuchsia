// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/fsl/socket/strings.h>
#include <lib/fxl/files/scoped_temp_dir.h>
#include <lib/timekeeper/test_clock.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"
#include "peridot/bin/ledger/fidl_helpers/bound_interface_set.h"
#include "peridot/bin/ledger/p2p_provider/impl/p2p_provider_impl.h"
#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_impl.h"
#include "peridot/bin/ledger/p2p_sync/public/user_communicator_factory.h"
#include "peridot/bin/ledger/testing/cloud_provider/fake_cloud_provider.h"
#include "peridot/bin/ledger/testing/cloud_provider/types.h"
#include "peridot/bin/ledger/testing/ledger_app_instance_factory.h"
#include "peridot/bin/ledger/testing/loop_controller_test_loop.h"
#include "peridot/bin/ledger/testing/netconnector/netconnector_factory.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/rng/random.h"
#include "peridot/lib/rng/test_random.h"
#include "peridot/lib/socket/socket_pair.h"
#include "peridot/lib/socket/socket_writer.h"

namespace ledger {
namespace {

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

Environment BuildEnvironment(async_dispatcher_t* dispatcher,
                             async_dispatcher_t* io_dispatcher,
                             rng::Random* random) {
  return EnvironmentBuilder()
      .SetAsync(dispatcher)
      .SetIOAsync(io_dispatcher)
      .SetBackoffFactory([] {
        return std::make_unique<backoff::ExponentialBackoff>(
            kBackoffDuration, 1u, kBackoffDuration);
      })
      .SetClock(std::make_unique<timekeeper::TestClock>())
      .SetRandom(std::make_unique<DelegatedRandom>(random))
      .Build();
}

class FakeUserIdProvider : public p2p_provider::UserIdProvider {
 public:
  FakeUserIdProvider() {}

  void GetUserId(fit::function<void(Status, std::string)> callback) override {
    callback(Status::OK, kUserId);
  };
};

class LedgerAppInstanceImpl final
    : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      LoopController* loop_controller, async_dispatcher_t* services_dispatcher,
      rng::Random* random,
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
        async_dispatcher_t* dispatcher, async_dispatcher_t* io_dispatcher,
        rng::Random* random,
        fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
            request,
        std::unique_ptr<p2p_sync::UserCommunicatorFactory>
            user_communicator_factory)
        : environment_(BuildEnvironment(dispatcher, io_dispatcher, random)),
          factory_impl_(&environment_, std::move(user_communicator_factory)),
          factory_binding_(&factory_impl_, std::move(request)) {}
    ~LedgerRepositoryFactoryContainer() {}

   private:
    Environment environment_;
    LedgerRepositoryFactoryImpl factory_impl_;
    fidl::Binding<ledger_internal::LedgerRepositoryFactory> factory_binding_;

    FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryFactoryContainer);
  };

  cloud_provider::CloudProviderPtr MakeCloudProvider() override;

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
    LoopController* loop_controller, async_dispatcher_t* services_dispatcher,
    rng::Random* random,
    fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory>
        repository_factory_request,
    fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory>
        repository_factory_ptr,
    fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider,
                                    FakeCloudProvider>* cloud_provider,
    std::unique_ptr<p2p_sync::UserCommunicatorFactory>
        user_communicator_factory)
    : LedgerAppInstanceFactory::LedgerAppInstance(
          loop_controller, RandomArray(1), std::move(repository_factory_ptr)),
      loop_(loop_controller->StartNewLoop()),
      io_loop_(loop_controller->StartNewLoop()),
      services_dispatcher_(services_dispatcher),
      cloud_provider_(cloud_provider),
      weak_ptr_factory_(this) {
  async::PostTask(
      loop_->dispatcher(),
      [this, random, request = std::move(repository_factory_request),
       user_communicator_factory =
           std::move(user_communicator_factory)]() mutable {
        factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
            loop_->dispatcher(), io_loop_->dispatcher(), random,
            std::move(request), std::move(user_communicator_factory));
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

LedgerAppInstanceImpl::~LedgerAppInstanceImpl() {
  async::PostTask(loop_->dispatcher(), [this] { factory_container_.reset(); });
  loop_->DrainAndQuit();
  loop_.release();
}

class FakeUserCommunicatorFactory : public p2p_sync::UserCommunicatorFactory {
 public:
  FakeUserCommunicatorFactory(async_dispatcher_t* services_dispatcher,
                              rng::Random* random,
                              NetConnectorFactory* netconnector_factory,
                              std::string host_name)
      : services_dispatcher_(services_dispatcher),
        environment_(
            BuildEnvironment(services_dispatcher, services_dispatcher, random)),
        netconnector_factory_(netconnector_factory),
        host_name_(std::move(host_name)),
        weak_ptr_factory_(this) {}
  ~FakeUserCommunicatorFactory() override {}

  std::unique_ptr<p2p_sync::UserCommunicator> GetUserCommunicator(
      DetachedPath /*user_directory*/) override {
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
            host_name_, std::move(netconnector),
            std::make_unique<FakeUserIdProvider>());
    return std::make_unique<p2p_sync::UserCommunicatorImpl>(
        std::move(provider), environment_.coroutine_service());
  }

 private:
  async_dispatcher_t* const services_dispatcher_;
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
  LedgerAppInstanceFactoryImpl(
      std::unique_ptr<LoopControllerTestLoop> loop_controller,
      InjectNetworkError inject_network_error, EnableP2PMesh enable_p2p_mesh)
      : loop_controller_(std::move(loop_controller)),
        random_(std::make_unique<rng::TestRandom>(
            loop_controller_->test_loop().initial_state())),
        services_loop_(loop_controller_->StartNewLoop()),
        cloud_provider_(FakeCloudProvider::Builder().SetInjectNetworkError(
            inject_network_error)),
        enable_p2p_mesh_(enable_p2p_mesh) {}
  ~LedgerAppInstanceFactoryImpl() override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() override;

  LoopController* GetLoopController() override;

 private:
  std::unique_ptr<LoopControllerTestLoop> loop_controller_;
  std::unique_ptr<rng::Random> random_;
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
        services_loop_->dispatcher(), random_.get(), &netconnector_factory_,
        std::move(host_name));
  }
  auto result = std::make_unique<LedgerAppInstanceImpl>(
      loop_controller_.get(), services_loop_->dispatcher(), random_.get(),
      std::move(repository_factory_request), std::move(repository_factory_ptr),
      &cloud_provider_, std::move(user_communicator_factory));
  app_instance_counter_++;
  return result;
}

LoopController* LedgerAppInstanceFactoryImpl::GetLoopController() {
  return loop_controller_.get();
}

namespace {

class FactoryBuilderIntegrationImpl : public LedgerAppInstanceFactoryBuilder {
 public:
  FactoryBuilderIntegrationImpl(InjectNetworkError inject_error,
                                EnableP2PMesh enable_p2p)
      : inject_error_(inject_error), enable_p2p_(enable_p2p){};

  std::unique_ptr<LedgerAppInstanceFactory> NewFactory() const override {
    return std::make_unique<LedgerAppInstanceFactoryImpl>(
        std::make_unique<LoopControllerTestLoop>(), inject_error_, enable_p2p_);
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
