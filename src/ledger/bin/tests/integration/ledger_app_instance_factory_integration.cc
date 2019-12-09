// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/timekeeper/test_loop_test_clock.h>

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "peridot/lib/rng/random.h"
#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/app/ledger_repository_factory_impl.h"
#include "src/ledger/bin/environment/test_loop_notification.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/fidl_helpers/bound_interface_set.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_impl.h"
#include "src/ledger/bin/p2p_sync/public/user_communicator_factory.h"
#include "src/ledger/bin/testing/ledger_app_instance_factory.h"
#include "src/ledger/bin/testing/loop_controller_test_loop.h"
#include "src/ledger/bin/testing/overnet/overnet_factory.h"
#include "src/ledger/bin/tests/integration/sharding.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/cloud_provider_in_memory/lib/fake_cloud_provider.h"
#include "src/ledger/cloud_provider_in_memory/lib/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/socket/socket_pair.h"
#include "src/ledger/lib/socket/socket_writer.h"
#include "src/ledger/lib/socket/strings.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/fsl/handles/object_info.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

enum class TestDiffs {
  // Test compatibility with non diff-based sync: Ledger has compatibility enabled, and the cloud
  // provider simulates missing diffs.
  TEST_COMPATIBILITY,
  // Test that Ledger operates correctly with diff only.
  TEST_NO_COMPATIBILITY,
  // Test any: the compatibility mode should have no influence on this test. We choose one of the
  // previous modes at random to create some interesting variability.
  TEST_ANY
};

constexpr absl::string_view kLedgerName = "AppTests";
constexpr zx::duration kBackoffDuration = zx::msec(5);
const char kUserId[] = "user";
constexpr absl::string_view kTestTopLevelNodeName = "top-level-of-test node";

// Implementation of rng::Random that delegates to another instance. This is
// needed because EnvironmentBuilder requires taking ownership of the random
// implementation.
class DelegatedRandom final : public rng::Random {
 public:
  explicit DelegatedRandom(rng::Random* base) : base_(base) {}
  ~DelegatedRandom() override = default;

 private:
  void InternalDraw(void* buffer, size_t buffer_size) override { base_->Draw(buffer, buffer_size); }

  rng::Random* base_;
};

Environment BuildEnvironment(async::TestLoop* loop, async_dispatcher_t* dispatcher,
                             async_dispatcher_t* io_dispatcher,
                             sys::ComponentContext* component_context, rng::Random* random,
                             storage::DiffCompatibilityPolicy diff_compatibility_policy) {
  return EnvironmentBuilder()
      .SetAsync(dispatcher)
      .SetIOAsync(io_dispatcher)
      .SetNotificationFactory(ledger::TestLoopNotification::NewFactory(loop))
      .SetStartupContext(component_context)
      .SetBackoffFactory([random] {
        return std::make_unique<backoff::ExponentialBackoff>(kBackoffDuration, 1u, kBackoffDuration,
                                                             random->NewBitGenerator<uint64_t>());
      })
      .SetClock(std::make_unique<timekeeper::TestLoopTestClock>(loop))
      .SetRandom(std::make_unique<DelegatedRandom>(random))
      .SetGcPolicy(kTestingGarbageCollectionPolicy)
      .SetDiffCompatibilityPolicy(diff_compatibility_policy)
      .Build();
}

class LedgerAppInstanceImpl final : public LedgerAppInstanceFactory::LedgerAppInstance {
 public:
  LedgerAppInstanceImpl(
      LoopController* loop_controller, std::unique_ptr<SubLoop> loop,
      std::unique_ptr<SubLoop> io_loop, std::unique_ptr<Environment> environment,
      async_dispatcher_t* services_dispatcher,
      fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory> repository_factory_request,
      fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory> repository_factory_ptr,
      fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider, FakeCloudProvider>*
          cloud_provider,
      std::unique_ptr<p2p_sync::UserCommunicatorFactory> user_communicator_factory,
      fidl::InterfaceRequest<fuchsia::inspect::deprecated::Inspect> inspect_request,
      fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> inspect_ptr);
  ~LedgerAppInstanceImpl() override;

 private:
  class LedgerRepositoryFactoryContainer {
   public:
    LedgerRepositoryFactoryContainer(
        Environment* environment,
        fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory> request,
        std::unique_ptr<p2p_sync::UserCommunicatorFactory> user_communicator_factory,
        inspect_deprecated::Node repositories_node,
        fidl::InterfaceRequest<fuchsia::inspect::deprecated::Inspect> inspect_request,
        fuchsia::inspect::deprecated::Inspect* inspect_impl)
        : environment_(environment),
          factory_impl_(environment_, std::move(user_communicator_factory),
                        std::move(repositories_node)),
          binding_(&factory_impl_, std::move(request)),
          inspect_binding_(inspect_impl, std::move(inspect_request)) {}
    LedgerRepositoryFactoryContainer(const LedgerRepositoryFactoryContainer&) = delete;
    LedgerRepositoryFactoryContainer& operator=(const LedgerRepositoryFactoryContainer&) = delete;
    ~LedgerRepositoryFactoryContainer() = default;

   private:
    Environment* environment_;
    LedgerRepositoryFactoryImpl factory_impl_;
    SyncableBinding<fuchsia::ledger::internal::LedgerRepositoryFactorySyncableDelegate> binding_;
    fidl::Binding<fuchsia::inspect::deprecated::Inspect> inspect_binding_;
  };

  cloud_provider::CloudProviderPtr MakeCloudProvider() override;
  std::string GetUserId() override;

  std::unique_ptr<SubLoop> loop_;
  std::unique_ptr<SubLoop> io_loop_;
  std::unique_ptr<Environment> environment_;
  inspect_deprecated::Node top_level_inspect_node_;
  std::unique_ptr<LedgerRepositoryFactoryContainer> factory_container_;
  async_dispatcher_t* services_dispatcher_;
  fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider, FakeCloudProvider>* const
      cloud_provider_;

  // This must be the last field of this class.
  fxl::WeakPtrFactory<LedgerAppInstanceImpl> weak_ptr_factory_;
};

LedgerAppInstanceImpl::LedgerAppInstanceImpl(
    LoopController* loop_controller, std::unique_ptr<SubLoop> loop,
    std::unique_ptr<SubLoop> io_loop, std::unique_ptr<Environment> environment,
    async_dispatcher_t* services_dispatcher,
    fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory> repository_factory_request,
    fidl::InterfacePtr<ledger_internal::LedgerRepositoryFactory> repository_factory_ptr,
    fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider, FakeCloudProvider>*
        cloud_provider,
    std::unique_ptr<p2p_sync::UserCommunicatorFactory> user_communicator_factory,
    fidl::InterfaceRequest<fuchsia::inspect::deprecated::Inspect> inspect_request,
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> inspect_ptr)
    : LedgerAppInstanceFactory::LedgerAppInstance(loop_controller, convert::ToArray(kLedgerName),
                                                  std::move(repository_factory_ptr),
                                                  std::move(inspect_ptr)),
      loop_(std::move(loop)),
      io_loop_(std::move(io_loop)),
      environment_(std::move(environment)),
      services_dispatcher_(services_dispatcher),
      cloud_provider_(cloud_provider),
      weak_ptr_factory_(this) {
  auto top_level_objects = component::Object::Make(convert::ToString(kTestTopLevelNodeName));
  auto top_level_object_dir = component::ObjectDir(top_level_objects);
  std::shared_ptr<component::Object> top_level_component_object = top_level_object_dir.object();
  top_level_inspect_node_ = inspect_deprecated::Node(std::move(top_level_object_dir));

  async::PostTask(loop_->dispatcher(), [this, request = std::move(repository_factory_request),
                                        user_communicator_factory =
                                            std::move(user_communicator_factory),
                                        inspect_request = std::move(inspect_request),
                                        top_level_component_object]() mutable {
    factory_container_ = std::make_unique<LedgerRepositoryFactoryContainer>(
        environment_.get(), std::move(request), std::move(user_communicator_factory),
        top_level_inspect_node_.CreateChild(convert::ToString(kRepositoriesInspectPathComponent)),
        std::move(inspect_request), top_level_component_object.get());
  });
}

cloud_provider::CloudProviderPtr LedgerAppInstanceImpl::MakeCloudProvider() {
  if (!cloud_provider_) {
    return nullptr;
  }
  cloud_provider::CloudProviderPtr cloud_provider;
  async::PostTask(services_dispatcher_,
                  callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
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
  FakeUserCommunicatorFactory(Environment* environment, async_dispatcher_t* services_dispatcher,
                              OvernetFactory* overnet_factory, uint64_t host_id)
      : environment_(environment),
        services_dispatcher_(services_dispatcher),
        overnet_factory_(overnet_factory),
        host_id_(std::move(host_id)),
        weak_ptr_factory_(this) {}
  ~FakeUserCommunicatorFactory() override = default;

  std::unique_ptr<p2p_sync::UserCommunicator> GetUserCommunicator(
      std::unique_ptr<p2p_provider::UserIdProvider> user_id_provider) override {
    fuchsia::overnet::OvernetPtr overnet;
    async::PostTask(services_dispatcher_,
                    callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                                         [this, request = overnet.NewRequest()]() mutable {
                                           overnet_factory_->AddBinding(host_id_,
                                                                        std::move(request));
                                         }));
    std::unique_ptr<p2p_provider::P2PProvider> provider =
        std::make_unique<p2p_provider::P2PProviderImpl>(
            std::move(overnet), std::move(user_id_provider), environment_->random());
    return std::make_unique<p2p_sync::UserCommunicatorImpl>(environment_, std::move(provider));
  }

 private:
  Environment* environment_;
  async_dispatcher_t* const services_dispatcher_;
  OvernetFactory* const overnet_factory_;
  uint64_t host_id_;

  // This must be the last field of this class.
  fxl::WeakPtrFactory<FakeUserCommunicatorFactory> weak_ptr_factory_;
};

enum EnableP2PMesh { NO, YES };

// Whether to enable sync or not.
enum class EnableSync { YES, NO };

}  // namespace

class LedgerAppInstanceFactoryImpl : public LedgerAppInstanceFactory {
 public:
  LedgerAppInstanceFactoryImpl(EnableSync enable_sync, InjectNetworkError inject_network_error,
                               EnableP2PMesh enable_p2p_mesh, TestDiffs test_diffs)
      : loop_controller_(&loop_),
        random_(loop_.initial_state()),
        test_diffs_(test_diffs != TestDiffs::TEST_ANY
                        ? test_diffs
                        : random_.Draw<uint8_t>() % 2 == 0 ? TestDiffs::TEST_COMPATIBILITY
                                                           : TestDiffs::TEST_NO_COMPATIBILITY),
        services_loop_(loop_controller_.StartNewLoop()),
        cloud_provider_loop_(loop_controller_.StartNewLoop()),
        cloud_provider_(
            std::move(FakeCloudProvider::Builder(cloud_provider_loop_->dispatcher(), &random_)
                          .SetInjectNetworkError(inject_network_error)
                          .SetInjectMissingDiff(test_diffs_ == TestDiffs::TEST_COMPATIBILITY
                                                    ? InjectMissingDiff::YES
                                                    : InjectMissingDiff::NO))),
        overnet_factory_(services_loop_->dispatcher()),
        enable_sync_(enable_sync),
        enable_p2p_mesh_(enable_p2p_mesh) {}

  ~LedgerAppInstanceFactoryImpl() override;

  std::unique_ptr<LedgerAppInstance> NewLedgerAppInstance() override;

  LoopController* GetLoopController() override;

  rng::Random* GetRandom() override;

 private:
  async::TestLoop loop_;
  sys::testing::ComponentContextProvider component_context_provider_;
  LoopControllerTestLoop loop_controller_;
  rng::TestRandom random_;
  const TestDiffs test_diffs_;
  // Loop on which to run services.
  std::unique_ptr<SubLoop> services_loop_;
  // Loop on which to run the cloud provider.
  std::unique_ptr<SubLoop> cloud_provider_loop_;
  fidl_helpers::BoundInterfaceSet<cloud_provider::CloudProvider, FakeCloudProvider> cloud_provider_;
  int app_instance_counter_ = 0;
  OvernetFactory overnet_factory_;
  const EnableSync enable_sync_;
  const EnableP2PMesh enable_p2p_mesh_;
};

LedgerAppInstanceFactoryImpl::~LedgerAppInstanceFactoryImpl() {
  services_loop_->DrainAndQuit();
  services_loop_.reset();
}

std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>
LedgerAppInstanceFactoryImpl::NewLedgerAppInstance() {
  ledger_internal::LedgerRepositoryFactoryPtr repository_factory_ptr;
  fidl::InterfaceRequest<ledger_internal::LedgerRepositoryFactory> repository_factory_request =
      repository_factory_ptr.NewRequest();
  fuchsia::inspect::deprecated::InspectPtr inspect_ptr;
  fidl::InterfaceRequest<fuchsia::inspect::deprecated::Inspect> inspect_request =
      inspect_ptr.NewRequest();
  auto loop = loop_controller_.StartNewLoop();
  auto io_loop = loop_controller_.StartNewLoop();
  auto diff_compatibility_policy = test_diffs_ == TestDiffs::TEST_COMPATIBILITY
                                       ? storage::DiffCompatibilityPolicy::USE_DIFFS_AND_TREE_NODES
                                       : storage::DiffCompatibilityPolicy::USE_ONLY_DIFFS;
  auto environment = std::make_unique<Environment>(
      BuildEnvironment(&loop_, loop->dispatcher(), io_loop->dispatcher(),
                       component_context_provider_.context(), &random_, diff_compatibility_policy));
  std::unique_ptr<p2p_sync::UserCommunicatorFactory> user_communicator_factory;
  if (enable_p2p_mesh_ == EnableP2PMesh::YES) {
    user_communicator_factory = std::make_unique<FakeUserCommunicatorFactory>(
        environment.get(), services_loop_->dispatcher(), &overnet_factory_, app_instance_counter_);
  }
  app_instance_counter_++;

  auto cloud_provider = enable_sync_ == EnableSync::YES ? &cloud_provider_ : nullptr;

  return std::make_unique<LedgerAppInstanceImpl>(
      &loop_controller_, std::move(loop), std::move(io_loop), std::move(environment),
      services_loop_->dispatcher(), std::move(repository_factory_request),
      std::move(repository_factory_ptr), cloud_provider, std::move(user_communicator_factory),
      std::move(inspect_request), std::move(inspect_ptr));
}

LoopController* LedgerAppInstanceFactoryImpl::GetLoopController() { return &loop_controller_; }

rng::Random* LedgerAppInstanceFactoryImpl::GetRandom() { return &random_; }

namespace {

class FactoryBuilderIntegrationImpl : public LedgerAppInstanceFactoryBuilder {
 public:
  FactoryBuilderIntegrationImpl(EnableSync enable_sync, InjectNetworkError inject_error,
                                EnableP2PMesh enable_p2p, TestDiffs test_diffs)
      : enable_sync_(enable_sync),
        inject_error_(inject_error),
        enable_p2p_(enable_p2p),
        test_diffs_(test_diffs){};

  std::unique_ptr<LedgerAppInstanceFactory> NewFactory() const override {
    return std::make_unique<LedgerAppInstanceFactoryImpl>(enable_sync_, inject_error_, enable_p2p_,
                                                          test_diffs_);
  }

  std::string TestSuffix() const override {
    return absl::StrCat(
        enable_sync_ == EnableSync::YES ? "Sync" : "NoSync",
        inject_error_ == InjectNetworkError::YES ? "WithNetworkError" : "",
        enable_p2p_ == EnableP2PMesh::YES ? "P2P" : "NoP2P",
        test_diffs_ == TestDiffs::TEST_ANY
            ? ""
            : test_diffs_ == TestDiffs::TEST_COMPATIBILITY ? "DiffCompatibility" : "DiffOnly");
  }

  TestDiffs test_diffs() const { return test_diffs_; }

 private:
  EnableSync enable_sync_;
  InjectNetworkError inject_error_;
  EnableP2PMesh enable_p2p_;
  TestDiffs test_diffs_;
};

}  // namespace

std::vector<const LedgerAppInstanceFactoryBuilder*> GetLedgerAppInstanceFactoryBuilders(
    EnableSynchronization sync_state) {
  static std::vector<std::unique_ptr<FactoryBuilderIntegrationImpl>>
      static_sync_diffs_relevant_builders;
  static std::vector<std::unique_ptr<FactoryBuilderIntegrationImpl>>
      static_sync_diffs_not_relevant_builders;
  static std::vector<std::unique_ptr<FactoryBuilderIntegrationImpl>> static_offline_builders;
  static std::vector<std::unique_ptr<FactoryBuilderIntegrationImpl>> static_p2p_only_builders;
  static std::once_flag flag;

  auto static_sync_diffs_relevant_builders_ptr = &static_sync_diffs_relevant_builders;
  auto static_sync_diffs_not_relevant_builders_ptr = &static_sync_diffs_not_relevant_builders;
  auto static_offline_builders_ptr = &static_offline_builders;
  auto static_p2p_only_builders_ptr = &static_p2p_only_builders;
  std::call_once(flag, [&static_sync_diffs_relevant_builders_ptr,
                        &static_sync_diffs_not_relevant_builders_ptr, &static_offline_builders_ptr,
                        &static_p2p_only_builders_ptr] {
    for (auto inject_error : {InjectNetworkError::NO, InjectNetworkError::YES}) {
      for (auto enable_p2p : {EnableP2PMesh::NO, EnableP2PMesh::YES}) {
        for (auto test_diffs : {TestDiffs::TEST_COMPATIBILITY, TestDiffs::TEST_NO_COMPATIBILITY}) {
          static_sync_diffs_relevant_builders_ptr->push_back(
              std::make_unique<FactoryBuilderIntegrationImpl>(EnableSync::YES, inject_error,
                                                              enable_p2p, test_diffs));
        }
        static_sync_diffs_not_relevant_builders_ptr->push_back(
            std::make_unique<FactoryBuilderIntegrationImpl>(EnableSync::YES, inject_error,
                                                            enable_p2p, TestDiffs::TEST_ANY));
      }
    }
    static_offline_builders_ptr->push_back(std::make_unique<FactoryBuilderIntegrationImpl>(
        EnableSync::NO, InjectNetworkError::NO, EnableP2PMesh::NO, TestDiffs::TEST_ANY));
    static_p2p_only_builders_ptr->push_back(std::make_unique<FactoryBuilderIntegrationImpl>(
        EnableSync::NO, InjectNetworkError::NO, EnableP2PMesh::YES, TestDiffs::TEST_ANY));
  });

  std::vector<const FactoryBuilderIntegrationImpl*> builders;

  if (sync_state != EnableSynchronization::OFFLINE_ONLY) {
    const auto& static_sync_builders =
        sync_state == EnableSynchronization::SYNC_OR_OFFLINE_DIFFS_IRRELEVANT
            ? static_sync_diffs_not_relevant_builders
            : static_sync_diffs_relevant_builders;
    for (const auto& builder : static_sync_builders) {
      builders.push_back(builder.get());
    }
  }

  if (sync_state != EnableSynchronization::SYNC_ONLY &&
      sync_state != EnableSynchronization::CLOUD_SYNC_ONLY) {
    for (const auto& builder : static_offline_builders) {
      builders.push_back(builder.get());
    }
  }

  if (sync_state == EnableSynchronization::SYNC_ONLY ||
      sync_state == EnableSynchronization::SYNC_OR_OFFLINE) {
    for (const auto& builder : static_p2p_only_builders) {
      builders.push_back(builder.get());
    }
  }

  // Filter builders depending on the test shard.
  auto filter_builders = [](const FactoryBuilderIntegrationImpl* builder) {
    if (builder->test_diffs() == TestDiffs::TEST_COMPATIBILITY) {
      return GetIntegrationTestShard() == IntegrationTestShard::DIFF_COMPATIBILITY;
    } else {
      return GetIntegrationTestShard() == IntegrationTestShard::ALL_EXCEPT_DIFF_COMPATIBILITY;
    }
  };
  std::vector<const LedgerAppInstanceFactoryBuilder*> builders_for_shard;
  std::copy_if(builders.begin(), builders.end(), std::back_inserter(builders_for_shard),
               filter_builders);

  return builders_for_shard;
}

}  // namespace ledger
