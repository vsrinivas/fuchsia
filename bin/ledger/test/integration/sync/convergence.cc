// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/integration/sync/lib.h"

#include "lib/fsl/vmo/vector.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/callback/auto_cleanable.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/callback/waiter.h"
#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/test/data_generator.h"
#include "peridot/bin/ledger/test/get_ledger.h"

namespace test {
namespace integration {
namespace sync {
namespace {

fidl::Array<uint8_t> DoubleToArray(double dbl) {
  fidl::Array<uint8_t> array = fidl::Array<uint8_t>::New(sizeof(double));
  std::memcpy(array.data(), &dbl, sizeof(double));
  return array;
}

::testing::AssertionResult VmoToDouble(const zx::vmo& vmo, double* dbl) {
  size_t num_read;
  zx_status_t status = vmo.read(dbl, 0, sizeof(double), &num_read);
  if (status < 0) {
    return ::testing::AssertionFailure() << "Unable to read the VMO.";
  }
  if (num_read != sizeof(double)) {
    return ::testing::AssertionFailure()
           << "VMO read of the wrong size: " << num_read << " instead of "
           << sizeof(double) << ".";
  }

  return ::testing::AssertionSuccess();
}

class RefCountedPageSnapshot
    : public fxl::RefCountedThreadSafe<RefCountedPageSnapshot> {
 public:
  RefCountedPageSnapshot() {}

  ledger::PageSnapshotPtr& operator->() { return snapshot_; }
  ledger::PageSnapshotPtr& operator*() { return snapshot_; }

 private:
  ledger::PageSnapshotPtr snapshot_;
};

class PageWatcherImpl : public ledger::PageWatcher {
 public:
  PageWatcherImpl(fidl::InterfaceRequest<ledger::PageWatcher> request,
                  fxl::RefPtr<RefCountedPageSnapshot> base_snapshot)
      : binding_(this, std::move(request)),
        current_snapshot_(std::move(base_snapshot)) {}

  int changes = 0;

  void GetInlineOnLatestSnapshot(
      fidl::Array<uint8_t> key,
      ledger::PageSnapshot::GetInlineCallback callback) {
    // We need to make sure the PageSnapshotPtr used to make the |GetInline|
    // call survives as long as the call is active, even if a new snapshot
    // arrives in between.
    (*current_snapshot_)->GetInline(std::move(key), [
      snapshot = current_snapshot_.Clone(), callback = std::move(callback)
    ](ledger::Status status, fidl::Array<uint8_t> value) mutable {
      callback(status, std::move(value));
    });
  }

 private:
  // PageWatcher:
  void OnChange(ledger::PageChangePtr /*page_change*/,
                ledger::ResultState /*result_state*/,
                const OnChangeCallback& callback) override {
    changes++;
    current_snapshot_ = fxl::AdoptRef(new RefCountedPageSnapshot());
    callback((**current_snapshot_).NewRequest());
  }

  fidl::Binding<ledger::PageWatcher> binding_;
  fxl::RefPtr<RefCountedPageSnapshot> current_snapshot_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageWatcherImpl);
};

class SyncWatcherImpl : public ledger::SyncWatcher {
 public:
  SyncWatcherImpl() : binding_(this) {}

  auto NewBinding() { return binding_.NewBinding(); }

  bool new_state = false;
  ledger::SyncState download;
  ledger::SyncState upload;

 private:
  // SyncWatcher
  void SyncStateChanged(ledger::SyncState download,
                        ledger::SyncState upload,
                        const SyncStateChangedCallback& callback) override {
    this->download = download;
    this->upload = upload;
    new_state = true;
    callback();
  }

  fidl::Binding<ledger::SyncWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherImpl);
};

// NonAssociativeConflictResolverImpl uses a merge function which is neither
// associative nor commutative. This means that merging ((1, 2), 3) results in
// a different value than merging ((2, 3), 1), or ((2, 1), 3).
// This conflict resolver only works on numeric data. For values A and B, it
// produces the merged value (4*A+B)/3.
class NonAssociativeConflictResolverImpl : public ledger::ConflictResolver {
 public:
  explicit NonAssociativeConflictResolverImpl(
      fidl::InterfaceRequest<ledger::ConflictResolver> request)
      : binding_(this, std::move(request)) {}
  ~NonAssociativeConflictResolverImpl() override {}

 private:
  // ConflictResolver:
  void Resolve(fidl::InterfaceHandle<ledger::PageSnapshot> /*left_version*/,
               fidl::InterfaceHandle<ledger::PageSnapshot> /*right_version*/,
               fidl::InterfaceHandle<ledger::PageSnapshot> /*common_version*/,
               fidl::InterfaceHandle<ledger::MergeResultProvider>
                   result_provider) override {
    ledger::MergeResultProviderPtr merge_result_provider =
        ledger::MergeResultProviderPtr::Create(std::move(result_provider));
    auto waiter =
        callback::Waiter<ledger::Status, ledger::PageChangePtr>::Create(
            ledger::Status::OK);
    merge_result_provider->GetLeftDiff(
        nullptr, [callback = waiter->NewCallback()](
                     ledger::Status status, ledger::PageChangePtr change,
                     fidl::Array<uint8_t> next_token) {
          callback(status, std::move(change));
        });
    merge_result_provider->GetRightDiff(
        nullptr, [callback = waiter->NewCallback()](
                     ledger::Status status, ledger::PageChangePtr change,
                     fidl::Array<uint8_t> next_token) {
          callback(status, std::move(change));
        });
    waiter->Finalize(fxl::MakeCopyable([merge_result_provider =
                                            std::move(merge_result_provider)](
        ledger::Status status,
        std::vector<ledger::PageChangePtr> changes) mutable {
      ASSERT_EQ(ledger::Status::OK, status);
      ASSERT_EQ(2u, changes.size());

      EXPECT_EQ(convert::ExtendedStringView(changes[0]->changes[0]->key),
                convert::ExtendedStringView(changes[1]->changes[0]->key));

      double d1, d2;
      EXPECT_TRUE(VmoToDouble(changes[0]->changes[0]->value, &d1));
      EXPECT_TRUE(VmoToDouble(changes[1]->changes[0]->value, &d2));
      double new_value = (4 * d1 + d2) / 3;
      ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
      merged_value->key = std::move(changes[0]->changes[0]->key);
      merged_value->source = ledger::ValueSource::NEW;
      merged_value->new_value = ledger::BytesOrReference::New();
      merged_value->new_value->set_bytes(DoubleToArray(new_value));
      fidl::Array<ledger::MergedValuePtr> merged_values;
      merged_values.push_back(std::move(merged_value));
      ledger::Status merge_status;
      merge_result_provider->Merge(std::move(merged_values),
                                   callback::Capture([] {}, &merge_status));
      ASSERT_TRUE(merge_result_provider.WaitForIncomingResponseWithTimeout(
          fxl::TimeDelta::FromSeconds(1)));
      ASSERT_EQ(ledger::Status::OK, merge_status);
      merge_result_provider->Done(callback::Capture([] {}, &merge_status));
      ASSERT_TRUE(merge_result_provider.WaitForIncomingResponseWithTimeout(
          fxl::TimeDelta::FromSeconds(1)));
      ASSERT_EQ(ledger::Status::OK, merge_status);
    }));
  }

  fidl::Binding<ledger::ConflictResolver> binding_;
};

class TestConflictResolverFactory : public ledger::ConflictResolverFactory {
 public:
  explicit TestConflictResolverFactory(
      fidl::InterfaceRequest<ledger::ConflictResolverFactory> request)
      : binding_(this, std::move(request)) {}

 private:
  // ConflictResolverFactory:
  void GetPolicy(fidl::Array<uint8_t> /*page_id*/,
                 const GetPolicyCallback& callback) override {
    callback(ledger::MergePolicy::CUSTOM);
  }

  void NewConflictResolver(
      fidl::Array<uint8_t> page_id,
      fidl::InterfaceRequest<ledger::ConflictResolver> resolver) override {
    resolvers.emplace(std::piecewise_construct,
                      std::forward_as_tuple(convert::ToString(page_id)),
                      std::forward_as_tuple(std::move(resolver)));
  }

  std::map<storage::PageId, NonAssociativeConflictResolverImpl> resolvers;

  fidl::Binding<ledger::ConflictResolverFactory> binding_;
};

enum class MergeType {
  LAST_ONE_WINS,
  NON_ASSOCIATIVE_CUSTOM,
};

class ConvergenceTest
    : public SyncTest,
      public ::testing::WithParamInterface<std::tuple<MergeType, int>> {
 public:
  ConvergenceTest(){};
  ~ConvergenceTest() override{};

  void SetUp() override {
    SyncTest::SetUp();
    std::tie(merge_function_type_, num_ledgers_) = GetParam();

    ASSERT_GT(num_ledgers_, 1);

    fidl::Array<uint8_t> page_id;
    for (int i = 0; i < num_ledgers_; i++) {
      auto ledger_instance = NewLedgerAppInstance();
      if (i == 0) {
        ledger_instance->EraseTestLedgerRepository();
      }
      ASSERT_TRUE(ledger_instance);
      ledger_instances_.push_back(std::move(ledger_instance));
      pages_.emplace_back();
      ledger::LedgerPtr ledger_ptr = ledger_instances_[i]->GetTestLedger();
      ledger::Status status = test::GetPageEnsureInitialized(
          &message_loop_, &ledger_ptr,
          // The first ledger gets a random page id, the others use the
          // same id for their pages.
          i == 0 ? nullptr : page_id.Clone(), &pages_[i], &page_id);
      ASSERT_EQ(ledger::Status::OK, status);
    }
  }

 protected:
  std::unique_ptr<PageWatcherImpl> WatchPageContents(ledger::PagePtr* page) {
    ledger::PageWatcherPtr page_watcher;
    fxl::RefPtr<RefCountedPageSnapshot> page_snapshot =
        fxl::AdoptRef(new RefCountedPageSnapshot());
    fidl::InterfaceRequest<ledger::PageSnapshot> page_snapshot_request =
        (**page_snapshot).NewRequest();
    std::unique_ptr<PageWatcherImpl> watcher =
        std::make_unique<PageWatcherImpl>(page_watcher.NewRequest(),
                                          std::move(page_snapshot));
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    (*page)->GetSnapshot(std::move(page_snapshot_request), nullptr,
                         std::move(page_watcher),
                         callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout(fxl::TimeDelta::FromSeconds(10)));
    EXPECT_EQ(ledger::Status::OK, status);
    return watcher;
  }

  std::unique_ptr<SyncWatcherImpl> WatchPageSyncState(ledger::PagePtr* page) {
    std::unique_ptr<SyncWatcherImpl> watcher =
        std::make_unique<SyncWatcherImpl>();
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    (*page)->SetSyncStateWatcher(watcher->NewBinding(),
                                 callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);
    return watcher;
  }

  // Returns true if the values for |key| on all the watchers are identical.
  bool AreValuesIdentical(
      const std::vector<std::unique_ptr<PageWatcherImpl>>& watchers,
      std::string key) {
    std::vector<fidl::Array<uint8_t>> values;
    for (int i = 0; i < num_ledgers_; i++) {
      values.emplace_back();
      ledger::Status status = ledger::Status::UNKNOWN_ERROR;
      watchers[i]->GetInlineOnLatestSnapshot(
          convert::ToArray(key),
          callback::Capture(MakeQuitTask(), &status, &values[i]));
      EXPECT_FALSE(RunLoopWithTimeout(fxl::TimeDelta::FromSeconds(10)));
      EXPECT_EQ(ledger::Status::OK, status);
    }

    bool values_are_identical = true;
    for (int i = 1; i < num_ledgers_; i++) {
      values_are_identical &= convert::ExtendedStringView(values[0]) ==
                              convert::ExtendedStringView(values[i]);
    }
    return values_are_identical;
  }

  int num_ledgers_;
  MergeType merge_function_type_;

  std::vector<std::unique_ptr<LedgerAppInstanceFactory::LedgerAppInstance>>
      ledger_instances_;
  std::vector<ledger::PagePtr> pages_;
  test::DataGenerator data_generator_;
};

// Verify that the Ledger converges over different settings of merging functions
// and number of ledger instances.
TEST_P(ConvergenceTest, NLedgersConverge) {
  std::vector<std::unique_ptr<PageWatcherImpl>> watchers;
  std::vector<std::unique_ptr<SyncWatcherImpl>> sync_watchers;

  std::vector<std::unique_ptr<TestConflictResolverFactory>> resolver_factories;
  std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t>
      generator;
  std::uniform_real_distribution<> distribution(1, 100);

  for (int i = 0; i < num_ledgers_; i++) {
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    if (merge_function_type_ == MergeType::NON_ASSOCIATIVE_CUSTOM) {
      ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
      resolver_factories.push_back(
          std::make_unique<TestConflictResolverFactory>(
              resolver_factory_ptr.NewRequest()));
      ledger::LedgerPtr ledger = ledger_instances_[i]->GetTestLedger();
      ledger->SetConflictResolverFactory(
          std::move(resolver_factory_ptr),
          callback::Capture(MakeQuitTask(), &status));
      EXPECT_FALSE(RunLoopWithTimeout(fxl::TimeDelta::FromSeconds(10)));
      EXPECT_EQ(ledger::Status::OK, status);
    }

    watchers.push_back(WatchPageContents(&pages_[i]));
    sync_watchers.push_back(WatchPageSyncState(&pages_[i]));

    pages_[i]->StartTransaction(callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);

    if (merge_function_type_ == MergeType::NON_ASSOCIATIVE_CUSTOM) {
      pages_[i]->Put(convert::ToArray("value"),
                     DoubleToArray(distribution(generator)),
                     callback::Capture(MakeQuitTask(), &status));
    } else {
      pages_[i]->Put(convert::ToArray("value"), data_generator_.MakeValue(50),
                     callback::Capture(MakeQuitTask(), &status));
    }
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);
  }

  for (int i = 0; i < num_ledgers_; i++) {
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    pages_[i]->Commit(callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(ledger::Status::OK, status);
  }

  std::function<bool()> until = [this, &watchers, &sync_watchers]() {
    // At least one change was propagated, and all synchronization is idle.
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
      if (sync_watchers[i]->download != ledger::SyncState::IDLE ||
          sync_watchers[i]->upload != ledger::SyncState::IDLE ||
          sync_watchers[i]->new_state) {
        idle = false;
      }
      // It turns out that merges are not instantaneous (who knew?), so we may
      // be idle on the synchronization, but merging behind the scenes, which
      // will trigger a new upload. So here we don't stop as soon as we have an
      // idle state, but wait a bit to be *really* sure nothing is happening.
      // Once LE-313 is done we may want to do something cleaner.
      sync_watchers[i]->new_state = false;
    }

    return idle && AreValuesIdentical(watchers, "value");
  };

  // If |RunLoopUntil| returns true, the condition is met, thus the ledgers have
  // converged.
  EXPECT_TRUE(RunLoopUntil(until, fxl::TimeDelta::FromSeconds(60),
                           // Checking every 10 milliseconds (the default at
                           // this time) is too short to catch merges.
                           fxl::TimeDelta::FromMilliseconds(100)));
  int num_changes = 0;
  for (int i = 0; i < num_ledgers_; i++) {
    num_changes += watchers[i]->changes;
  }
  EXPECT_GE(num_changes, 2 * num_ledgers_ - 1);

  // All synchronization must still be idle.
  for (int i = 0; i < num_ledgers_; i++) {
    EXPECT_FALSE(sync_watchers[i]->new_state);
    EXPECT_EQ(ledger::SyncState::IDLE, sync_watchers[i]->download);
    EXPECT_EQ(ledger::SyncState::IDLE, sync_watchers[i]->upload);
  }

  EXPECT_TRUE(AreValuesIdentical(watchers, "value"));
}

INSTANTIATE_TEST_CASE_P(
    ManyLedgersConvergenceTest,
    ConvergenceTest,
    // TODO(LE-313): MergeType::LAST_ONE_WINS is disabled as it is flaky.
    // Re-enable once LE-313 is done.
    ::testing::Combine(::testing::Values(MergeType::NON_ASSOCIATIVE_CUSTOM),
                       ::testing::Range(2, 6)));

}  // namespace
}  // namespace sync
}  // namespace integration
}  // namespace test
