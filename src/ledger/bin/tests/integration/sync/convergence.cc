// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <trace/event.h>

#include "fuchsia/ledger/cpp/fidl.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/ledger_app_instance_factory.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/strings/concatenate.h"

namespace ledger {
namespace {

std::vector<uint8_t> DoubleToArray(double dbl) {
  std::vector<uint8_t> array(sizeof(double));
  std::memcpy(array.data(), &dbl, sizeof(double));
  return array;
}

::testing::AssertionResult VmoToDouble(const fuchsia::mem::BufferPtr& vmo, double* dbl) {
  *dbl = 0;
  if (vmo->size != sizeof(double)) {
    return ::testing::AssertionFailure()
           << "VMO has the wrong size: " << vmo->size << " instead of " << sizeof(double) << ".";
  }
  zx_status_t status = vmo->vmo.read(dbl, 0, sizeof(double));
  if (status < 0) {
    return ::testing::AssertionFailure() << "Unable to read the VMO.";
  }
  return ::testing::AssertionSuccess();
}

class RefCountedPageSnapshot : public fxl::RefCountedThreadSafe<RefCountedPageSnapshot> {
 public:
  RefCountedPageSnapshot() = default;

  PageSnapshotPtr& operator->() { return snapshot_; }
  PageSnapshotPtr& operator*() { return snapshot_; }

 private:
  PageSnapshotPtr snapshot_;
};

class PageWatcherImpl : public PageWatcher {
 public:
  PageWatcherImpl(fidl::InterfaceRequest<PageWatcher> request,
                  fxl::RefPtr<RefCountedPageSnapshot> base_snapshot)
      : binding_(this, std::move(request)), current_snapshot_(std::move(base_snapshot)) {}

  int changes = 0;

  void GetInlineOnLatestSnapshot(std::vector<uint8_t> key,
                                 PageSnapshot::GetInlineCallback callback) {
    // We need to make sure the PageSnapshotPtr used to make the |GetInline|
    // call survives as long as the call is active, even if a new snapshot
    // arrives in between.
    (*current_snapshot_)
        ->GetInline(std::move(key),
                    [snapshot = current_snapshot_.Clone(), callback = std::move(callback)](
                        fuchsia::ledger::PageSnapshot_GetInline_Result result) mutable {
                      callback(std::move(result));
                    });
  }

 private:
  // PageWatcher:
  void OnChange(PageChange /*page_change*/, ResultState /*result_state*/,
                OnChangeCallback callback) override {
    changes++;
    current_snapshot_ = fxl::MakeRefCounted<RefCountedPageSnapshot>();
    callback((**current_snapshot_).NewRequest());
  }

  fidl::Binding<PageWatcher> binding_;
  fxl::RefPtr<RefCountedPageSnapshot> current_snapshot_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageWatcherImpl);
};

class SyncWatcherImpl : public SyncWatcher {
 public:
  SyncWatcherImpl() : binding_(this) {}

  auto NewBinding() { return binding_.NewBinding(); }

  bool new_state = false;
  SyncState download;
  SyncState upload;

 private:
  // SyncWatcher
  void SyncStateChanged(SyncState download, SyncState upload,
                        SyncStateChangedCallback callback) override {
    this->download = download;
    this->upload = upload;
    new_state = true;
    callback();
  }

  fidl::Binding<SyncWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherImpl);
};

// NonAssociativeConflictResolverImpl uses a merge function which is neither
// associative nor commutative. This means that merging ((1, 2), 3) results in
// a different value than merging ((2, 3), 1), or ((2, 1), 3).
// This conflict resolver only works on numeric data. For values A and B, it
// produces the merged value (4*A+B)/3.
class NonAssociativeConflictResolverImpl : public ConflictResolver {
 public:
  explicit NonAssociativeConflictResolverImpl(fidl::InterfaceRequest<ConflictResolver> request)
      : binding_(this, std::move(request)) {}
  ~NonAssociativeConflictResolverImpl() override = default;

 private:
  // ConflictResolver:
  void Resolve(fidl::InterfaceHandle<PageSnapshot> /*left_version*/,
               fidl::InterfaceHandle<PageSnapshot> /*right_version*/,
               fidl::InterfaceHandle<PageSnapshot> /*common_version*/,
               fidl::InterfaceHandle<MergeResultProvider> result_provider) override {
    auto merge_result_provider = std::make_unique<MergeResultProviderPtr>(result_provider.Bind());
    merge_result_provider->set_error_handler([](zx_status_t status) { EXPECT_EQ(status, ZX_OK); });
    MergeResultProvider* merge_result_provider_ptr = merge_result_provider->get();
    merge_result_provider_ptr->GetFullDiff(
        nullptr, [merge_result_provider = std::move(merge_result_provider)](
                     std::vector<DiffEntry> changes, std::unique_ptr<Token> next_token) mutable {
          ASSERT_FALSE(next_token);
          ASSERT_EQ(changes.size(), 1u);

          double d1, d2;
          EXPECT_TRUE(VmoToDouble(changes.at(0).left->value, &d1));
          EXPECT_TRUE(VmoToDouble(changes.at(0).right->value, &d2));
          double new_value = (4 * d1 + d2) / 3;
          MergedValue merged_value;
          merged_value.key = std::move(changes.at(0).key);
          merged_value.source = ValueSource::NEW;
          merged_value.new_value = BytesOrReference::New();
          merged_value.new_value->set_bytes(DoubleToArray(new_value));
          std::vector<MergedValue> merged_values;
          merged_values.push_back(std::move(merged_value));
          (*merge_result_provider)->Merge(std::move(merged_values));
          (*merge_result_provider)->Done();
        });
  }

  fidl::Binding<ConflictResolver> binding_;
};

class TestConflictResolverFactory : public ConflictResolverFactory {
 public:
  explicit TestConflictResolverFactory(fidl::InterfaceRequest<ConflictResolverFactory> request)
      : binding_(this, std::move(request)) {}

 private:
  // ConflictResolverFactory:
  void GetPolicy(PageId /*page_id*/, GetPolicyCallback callback) override {
    callback(MergePolicy::CUSTOM);
  }

  void NewConflictResolver(PageId page_id,
                           fidl::InterfaceRequest<ConflictResolver> resolver) override {
    resolvers.try_emplace(convert::ToString(page_id.id), std::move(resolver));
  }

  std::map<storage::PageId, NonAssociativeConflictResolverImpl> resolvers;

  fidl::Binding<ConflictResolverFactory> binding_;
};

enum class MergeType {
  LAST_ONE_WINS,
  NON_ASSOCIATIVE_CUSTOM,
};

class ConvergenceTest : public BaseIntegrationTest,
                        public ::testing::WithParamInterface<
                            std::tuple<MergeType, int, const LedgerAppInstanceFactoryBuilder*>> {
 public:
  ConvergenceTest()
      : BaseIntegrationTest(std::get<const LedgerAppInstanceFactoryBuilder*>(GetParam())) {}
  ~ConvergenceTest() override = default;

  void SetUp() override {
    BaseIntegrationTest::SetUp();

    data_generator_ = std::make_unique<DataGenerator>(GetRandom());

    std::tie(merge_function_type_, num_ledgers_, std::ignore) = GetParam();

    ASSERT_GT(num_ledgers_, 1);

    PageId page_id;

    for (int i = 0; i < num_ledgers_; i++) {
      auto ledger_instance = NewLedgerAppInstance();
      ASSERT_TRUE(ledger_instance);
      ledger_instances_.push_back(std::move(ledger_instance));
      pages_.emplace_back();
      LedgerPtr ledger_ptr = ledger_instances_[i]->GetTestLedger();
      Status status;
      auto loop_waiter = NewWaiter();
      GetPageEnsureInitialized(
          &ledger_ptr,
          // The first ledger gets a random page id, the others use the
          // same id for their pages.
          i == 0 ? nullptr : fidl::MakeOptional(page_id), DelayCallback::NO,
          [&] {
            ADD_FAILURE() << "Page should not be disconnected.";
            StopLoop();
          },
          callback::Capture(loop_waiter->GetCallback(), &status, &pages_[i], &page_id));
      ASSERT_TRUE(loop_waiter->RunUntilCalled());
      ASSERT_EQ(status, Status::OK);
    }
  }

 protected:
  std::unique_ptr<PageWatcherImpl> WatchPageContents(PagePtr* page) {
    PageWatcherPtr page_watcher;
    auto page_snapshot = fxl::MakeRefCounted<RefCountedPageSnapshot>();
    fidl::InterfaceRequest<PageSnapshot> page_snapshot_request = (**page_snapshot).NewRequest();
    std::unique_ptr<PageWatcherImpl> watcher =
        std::make_unique<PageWatcherImpl>(page_watcher.NewRequest(), std::move(page_snapshot));
    (*page)->GetSnapshot(std::move(page_snapshot_request), {}, std::move(page_watcher));
    return watcher;
  }

  std::unique_ptr<SyncWatcherImpl> WatchPageSyncState(PagePtr* page) {
    std::unique_ptr<SyncWatcherImpl> watcher = std::make_unique<SyncWatcherImpl>();
    (*page)->SetSyncStateWatcher(watcher->NewBinding());
    return watcher;
  }

  // Returns true if the values for |key| on all the watchers are identical.
  bool AreValuesIdentical(const std::vector<std::unique_ptr<PageWatcherImpl>>& watchers,
                          std::string key) {
    std::vector<InlinedValue> values;
    for (int i = 0; i < num_ledgers_; i++) {
      auto loop_waiter = NewWaiter();
      fuchsia::ledger::PageSnapshot_GetInline_Result result;
      watchers[i]->GetInlineOnLatestSnapshot(
          convert::ToArray(key), callback::Capture(loop_waiter->GetCallback(), &result));
      EXPECT_TRUE(loop_waiter->RunUntilCalled());
      EXPECT_TRUE(result.is_response());
      values.emplace_back(std::move(result.response().value));
    }

    bool values_are_identical = true;
    for (int i = 1; i < num_ledgers_; i++) {
      values_are_identical &= convert::ExtendedStringView(values[0].value) ==
                              convert::ExtendedStringView(values[i].value);
    }
    return values_are_identical;
  }

  int num_ledgers_;
  MergeType merge_function_type_;

  std::vector<std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>> ledger_instances_;
  std::vector<PagePtr> pages_;
  std::unique_ptr<DataGenerator> data_generator_;
};

// Verify that the Ledger converges over different settings of merging functions
// and number of ledger instances.
TEST_P(ConvergenceTest, NLedgersConverge) {
  std::vector<std::unique_ptr<PageWatcherImpl>> watchers;
  std::vector<std::unique_ptr<SyncWatcherImpl>> sync_watchers;

  std::vector<std::unique_ptr<TestConflictResolverFactory>> resolver_factories;
  std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t> generator;
  std::uniform_real_distribution<> distribution(1, 100);

  for (int i = 0; i < num_ledgers_; i++) {
    if (merge_function_type_ == MergeType::NON_ASSOCIATIVE_CUSTOM) {
      ConflictResolverFactoryPtr resolver_factory_ptr;
      resolver_factories.push_back(
          std::make_unique<TestConflictResolverFactory>(resolver_factory_ptr.NewRequest()));
      LedgerPtr ledger = ledger_instances_[i]->GetTestLedger();
      ledger->SetConflictResolverFactory(std::move(resolver_factory_ptr));
    }

    watchers.push_back(WatchPageContents(&pages_[i]));
    sync_watchers.push_back(WatchPageSyncState(&pages_[i]));

    pages_[i]->StartTransaction();

    if (merge_function_type_ == MergeType::NON_ASSOCIATIVE_CUSTOM) {
      pages_[i]->Put(convert::ToArray("value"), DoubleToArray(distribution(generator)));
    } else {
      pages_[i]->Put(convert::ToArray("value"), data_generator_->MakeValue(50));
    }
  }

  auto sync_waiter = fxl::MakeRefCounted<callback::CompletionWaiter>();
  for (int i = 0; i < num_ledgers_; i++) {
    pages_[i]->Commit();
    pages_[i]->Sync(sync_waiter->NewCallback());
  }

  auto loop_waiter = NewWaiter();
  sync_waiter->Finalize(callback::Capture(loop_waiter->GetCallback()));
  ASSERT_TRUE(loop_waiter->RunUntilCalled());

  // Function to verify if the visible Ledger state has not changed since last
  // call and all values are identical.
  fit::function<bool()> has_state_converged = [this, &watchers, &sync_watchers]() {
    // Counts the number of visible changes. Used to verify that the minimal
    // number of changes for all Ledgers to have communicated is accounted for
    // (see also below).
    int num_changes = 0;
    for (int i = 0; i < num_ledgers_; i++) {
      num_changes += watchers[i]->changes;
    }
    // All ledgers should see their own change (num_ledgers_). Then, at least
    // all but one should receive a change with the "final" value. There might
    // be more changes seen, though.
    if (num_changes < 2 * num_ledgers_ - 1) {
      return false;
    }

    // All synchronization must be idle.
    bool idle = true;
    for (int i = 0; i < num_ledgers_; i++) {
      if (sync_watchers[i]->download != SyncState::IDLE ||
          sync_watchers[i]->upload != SyncState::IDLE || sync_watchers[i]->new_state) {
        idle = false;
      }
      // With this, we make sure we can verify if the state changes during the
      // next cycle. If it has changed, we are sure the convergence has not
      // happened yet.
      sync_watchers[i]->new_state = false;
    }

    return idle && AreValuesIdentical(watchers, "value");
  };

  bool merge_done = false;
  ConflictResolutionWaitStatus wait_status = ConflictResolutionWaitStatus::NO_CONFLICTS;
  fxl::RefPtr<callback::StatusWaiter<ConflictResolutionWaitStatus>> waiter;

  // In addition of verifying that the external states of the ledgers have
  // converged, we also verify we are not currently performing a merge in the
  // background, indicating that the convergence did not finish.
  auto is_sync_and_merge_complete = [this, &has_state_converged, &merge_done, &wait_status,
                                     &waiter] {
    TRACE_DURATION("ledger", "ledger_test_is_sync_and_merge_complete");

    if (has_state_converged()) {
      if (merge_done && wait_status == ConflictResolutionWaitStatus::NO_CONFLICTS) {
        return true;
      }
      if (!waiter) {
        waiter = fxl::MakeRefCounted<callback::StatusWaiter<ConflictResolutionWaitStatus>>(
            ConflictResolutionWaitStatus::NO_CONFLICTS);
        for (int i = 0; i < num_ledgers_; i++) {
          pages_[i]->WaitForConflictResolution(waiter->NewCallback());
        }
        waiter->Finalize([&merge_done, &wait_status, &waiter](ConflictResolutionWaitStatus status) {
          merge_done = true;
          wait_status = status;
          waiter = nullptr;
        });
      }
      return false;
    }
    merge_done = false;
    if (waiter) {
      waiter->Cancel();
      waiter = nullptr;
    }
    return false;
  };

  // If |RunLoopUntil| returns, the condition is met, thus the ledgers have
  // converged.
  EXPECT_TRUE(RunLoopUntil(std::move(is_sync_and_merge_complete)));
  int num_changes = 0;
  for (int i = 0; i < num_ledgers_; i++) {
    num_changes += watchers[i]->changes;
  }
  EXPECT_GE(num_changes, 2 * num_ledgers_ - 1);

  // All synchronization must still be idle.
  for (int i = 0; i < num_ledgers_; i++) {
    EXPECT_FALSE(sync_watchers[i]->new_state);
    EXPECT_EQ(sync_watchers[i]->download, SyncState::IDLE);
    EXPECT_EQ(sync_watchers[i]->upload, SyncState::IDLE);
  }

  EXPECT_TRUE(AreValuesIdentical(watchers, "value"));
}

INSTANTIATE_TEST_SUITE_P(
    ManyLedgersConvergenceTest, ConvergenceTest,
    ::testing::Combine(
        ::testing::Values(MergeType::LAST_ONE_WINS, MergeType::NON_ASSOCIATIVE_CUSTOM),
        // Temporarily reduced the number of simulated Ledgers to reduce flaky
        // failures on bots, see LE-752. TODO(ppi): revert back to (2, 6).
        ::testing::Range(2, 3),
        ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders(EnableSynchronization::SYNC_ONLY))),
    [](const ::testing::TestParamInfo<ConvergenceTest::ParamType>& info) {
      std::stringstream ss;
      ss << (std::get<MergeType>(info.param) == MergeType::LAST_ONE_WINS ? "LastOneWins"
                                                                         : "NonAssociativeCustom")
         << "With" << std::get<int>(info.param) << "Ledgers"
         << std::get<const LedgerAppInstanceFactoryBuilder*>(info.param)->TestSuffix();
      return ss.str();
    });

}  // namespace
}  // namespace ledger
