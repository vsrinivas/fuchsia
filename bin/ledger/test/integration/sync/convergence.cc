// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/integration/sync/lib.h"

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/types.h"
#include "apps/ledger/src/test/data_generator.h"
#include "apps/ledger/src/test/get_ledger.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/mtl/vmo/vector.h"

namespace test {
namespace integration {
namespace sync {
namespace {

fidl::Array<uint8_t> DoubleToArray(double dbl) {
  fidl::Array<uint8_t> array = fidl::Array<uint8_t>::New(sizeof(double));
  std::memcpy(array.data(), &dbl, sizeof(double));
  return array;
}

::testing::AssertionResult VmoToDouble(const mx::vmo& vmo, double* dbl) {
  size_t num_read;
  mx_status_t status = vmo.read(dbl, 0, sizeof(double), &num_read);
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
    : public ftl::RefCountedThreadSafe<RefCountedPageSnapshot> {
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
                  ftl::RefPtr<RefCountedPageSnapshot> base_snapshot)
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
    current_snapshot_ = ftl::AdoptRef(new RefCountedPageSnapshot());
    callback((**current_snapshot_).NewRequest());
  }

  fidl::Binding<ledger::PageWatcher> binding_;
  ftl::RefPtr<RefCountedPageSnapshot> current_snapshot_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageWatcherImpl);
};

class SyncWatcherImpl : public ledger::SyncWatcher {
 public:
  SyncWatcherImpl() : binding_(this) {}

  auto NewBinding() { return binding_.NewBinding(); }

  ledger::SyncState download;
  ledger::SyncState upload;

 private:
  // SyncWatcher
  void SyncStateChanged(ledger::SyncState download,
                        ledger::SyncState upload,
                        const SyncStateChangedCallback& callback) override {
    this->download = download;
    this->upload = upload;
    callback();
  }

  fidl::Binding<ledger::SyncWatcher> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SyncWatcherImpl);
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
    waiter->Finalize(ftl::MakeCopyable([merge_result_provider =
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
          ftl::TimeDelta::FromSeconds(1)));
      ASSERT_EQ(ledger::Status::OK, merge_status);
      merge_result_provider->Done(callback::Capture([] {}, &merge_status));
      ASSERT_TRUE(merge_result_provider.WaitForIncomingResponseWithTimeout(
          ftl::TimeDelta::FromSeconds(1)));
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

  std::unordered_map<storage::PageId, NonAssociativeConflictResolverImpl>
      resolvers;

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
    ftl::RefPtr<RefCountedPageSnapshot> page_snapshot =
        ftl::AdoptRef(new RefCountedPageSnapshot());
    fidl::InterfaceRequest<ledger::PageSnapshot> page_snapshot_request =
        (**page_snapshot).NewRequest();
    std::unique_ptr<PageWatcherImpl> watcher =
        std::make_unique<PageWatcherImpl>(page_watcher.NewRequest(),
                                          std::move(page_snapshot));
    ledger::Status status = ledger::Status::UNKNOWN_ERROR;
    (*page)->GetSnapshot(std::move(page_snapshot_request), nullptr,
                         std::move(page_watcher),
                         callback::Capture(MakeQuitTask(), &status));
    EXPECT_FALSE(RunLoopWithTimeout(ftl::TimeDelta::FromSeconds(10)));
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
      EXPECT_FALSE(RunLoopWithTimeout(ftl::TimeDelta::FromSeconds(10)));
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

// Verify that the Ledger converges for a non-associative, non-commutative (but
// deterministic) merge function.
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
      EXPECT_FALSE(RunLoopWithTimeout(ftl::TimeDelta::FromSeconds(10)));
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
    for (int i = 0; i < num_ledgers_; i++) {
      if (sync_watchers[i]->download != ledger::SyncState::IDLE ||
          sync_watchers[i]->upload != ledger::SyncState::IDLE) {
        return false;
      }
    }

    return AreValuesIdentical(watchers, "value");
  };

  // If |RunLoopUntil| returns true, the condition is met, thus the ledgers have
  // converged. There is no need for additional tests.
  EXPECT_TRUE(RunLoopUntil(until, ftl::TimeDelta::FromSeconds(60)));
  int num_changes = 0;
  for (int i = 0; i < num_ledgers_; i++) {
    num_changes += watchers[i]->changes;
  }
  EXPECT_GE(num_changes, 2 * num_ledgers_ - 1);

  // Don't check whether all upload/download states are IDLE: maybe they were in
  // some intermediate state of sync and they are not any more. This should not
  // cause the test to fail. See also LE-262.

  EXPECT_TRUE(AreValuesIdentical(watchers, "value"));
}

INSTANTIATE_TEST_CASE_P(
    ManyLedgersConvergenceTest,
    ConvergenceTest,
    ::testing::Combine(::testing::Values(MergeType::LAST_ONE_WINS,
                                         MergeType::NON_ASSOCIATIVE_CUSTOM),
                       ::testing::Range(2, 6)));

}  // namespace
}  // namespace sync
}  // namespace integration
}  // namespace test
