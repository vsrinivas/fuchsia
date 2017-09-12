// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>
#include <utility>
#include <vector>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/types.h"
#include "apps/ledger/src/test/integration/integration_test.h"
#include "apps/ledger/src/test/integration/test_utils.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fsl/tasks/message_loop.h"

namespace test {
namespace integration {
namespace {

class MergingIntegrationTest : public IntegrationTest {
 public:
  MergingIntegrationTest() {}
  ~MergingIntegrationTest() override {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(MergingIntegrationTest);
};

class Watcher : public ledger::PageWatcher {
 public:
  Watcher(fidl::InterfaceRequest<ledger::PageWatcher> request,
          fxl::Closure change_callback)
      : binding_(this, std::move(request)),
        change_callback_(std::move(change_callback)) {}

  uint changes_seen = 0;
  ledger::PageSnapshotPtr last_snapshot_;
  ledger::PageChangePtr last_page_change_;

 private:
  // PageWatcher:
  void OnChange(ledger::PageChangePtr page_change,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override {
    FXL_DCHECK(page_change);
    FXL_DCHECK(result_state == ledger::ResultState::COMPLETED)
        << "Handling OnChange pagination not implemented yet";
    changes_seen++;
    last_page_change_ = std::move(page_change);
    last_snapshot_.reset();
    callback(last_snapshot_.NewRequest());
    change_callback_();
  }

  fidl::Binding<PageWatcher> binding_;
  fxl::Closure change_callback_;
};

ledger::PageChangePtr NewPageChange() {
  ledger::PageChangePtr change = ledger::PageChange::New();
  change->changes = fidl::Array<ledger::EntryPtr>::New(0);
  change->deleted_keys = fidl::Array<fidl::Array<uint8_t>>::New(0);
  return change;
}

void AppendChanges(ledger::PageChangePtr* base, ledger::PageChangePtr changes) {
  (*base)->timestamp = changes->timestamp;
  for (size_t i = 0; i < changes->changes.size(); ++i) {
    (*base)->changes.push_back(std::move(changes->changes[i]));
  }
  for (size_t i = 0; i < changes->deleted_keys.size(); ++i) {
    (*base)->deleted_keys.push_back(std::move(changes->deleted_keys[i]));
  }
}

enum class MergeType {
  SIMPLE,
  MULTIPART,
};

class ConflictResolverImpl : public ledger::ConflictResolver {
 public:
  explicit ConflictResolverImpl(
      fidl::InterfaceRequest<ConflictResolver> request)
      : binding_(this, std::move(request)) {
    binding_.set_connection_error_handler([this] {
      this->disconnected = true;
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
    });
  }
  ~ConflictResolverImpl() override {}

  struct ResolveRequest {
    fidl::InterfaceHandle<ledger::PageSnapshot> left_version;
    fidl::InterfaceHandle<ledger::PageSnapshot> right_version;
    fidl::InterfaceHandle<ledger::PageSnapshot> common_version;
    ledger::MergeResultProviderPtr result_provider;

    ResolveRequest(
        fidl::InterfaceHandle<ledger::PageSnapshot> left_version,
        fidl::InterfaceHandle<ledger::PageSnapshot> right_version,
        fidl::InterfaceHandle<ledger::PageSnapshot> common_version,
        fidl::InterfaceHandle<ledger::MergeResultProvider> result_provider)
        : left_version(std::move(left_version)),
          right_version(std::move(right_version)),
          common_version(std::move(common_version)),
          result_provider(ledger::MergeResultProviderPtr::Create(
              std::move(result_provider))) {}

    // Returns the changes from the left and right branch.
    ::testing::AssertionResult GetDiff(ledger::PageChangePtr* change_left,
                                       ledger::PageChangePtr* change_right) {
      return GetDiff(change_left, change_right, 0);
    }

    // Returns the changes from the left and right branch and makes sure that at
    // least |min_queries| of partial results are returned before retrieving the
    // complete result for the left and for the right changes.
    ::testing::AssertionResult GetDiff(ledger::PageChangePtr* change_left,
                                       ledger::PageChangePtr* change_right,
                                       int min_queries) {
      *change_left = NewPageChange();
      *change_right = NewPageChange();
      ::testing::AssertionResult left_result =
          GetDiff(nullptr, change_left, 0, min_queries,
                  [this](fidl::Array<uint8_t> token,
                         const std::function<void(
                             ledger::Status, ledger::PageChangePtr change,
                             fidl::Array<uint8_t> next_token)>& callback) {
                    result_provider->GetLeftDiff(std::move(token), callback);
                  });
      if (!left_result) {
        return left_result;
      }
      return GetDiff(nullptr, change_right, 0, min_queries,
                     [this](fidl::Array<uint8_t> token,
                            const std::function<void(
                                ledger::Status, ledger::PageChangePtr change,
                                fidl::Array<uint8_t> next_token)>& callback) {
                       result_provider->GetRightDiff(std::move(token),
                                                     callback);
                     });
    }

    // Resolves the conflict by sending the given merge results. If
    // |merge_type| is MULTIPART, the merge will be send in two parts, each
    // sending half of |results|' elements.
    ::testing::AssertionResult Merge(
        fidl::Array<ledger::MergedValuePtr> results,
        MergeType merge_type = MergeType::SIMPLE) {
      FXL_DCHECK(merge_type == MergeType::SIMPLE || results.size() >= 2);
      if (merge_type == MergeType::SIMPLE) {
        ::testing::AssertionResult merge_status =
            PartialMerge(std::move(results));
        if (!merge_status) {
          return merge_status;
        }
      } else {
        size_t part1_size = results.size() / 2;
        fidl::Array<ledger::MergedValuePtr> part2;
        for (size_t i = part1_size; i < results.size(); ++i) {
          part2.push_back(std::move(results[i]));
        }
        results.resize(part1_size);

        ::testing::AssertionResult merge_status =
            PartialMerge(std::move(results));
        if (!merge_status) {
          return merge_status;
        }
        merge_status = PartialMerge(std::move(part2));
        if (!merge_status) {
          return merge_status;
        }
      }

      ledger::Status status;
      result_provider->Done([&status](ledger::Status s) { status = s; });
      if (!result_provider.WaitForIncomingResponse()) {
        return ::testing::AssertionFailure() << "Done failed.";
      }
      if (status != ledger::Status::OK) {
        return ::testing::AssertionFailure()
               << "Done failed with status " << status;
      }
      return ::testing::AssertionSuccess();
    }

   private:
    ::testing::AssertionResult GetDiff(
        fidl::Array<uint8_t> token,
        ledger::PageChangePtr* page_change,
        int num_queries,
        int min_queries,
        std::function<void(fidl::Array<uint8_t>,
                           const std::function<void(ledger::Status,
                                                    ledger::PageChangePtr,
                                                    fidl::Array<uint8_t>)>&)>
            get_left_or_right_diff) {
      ledger::Status status;
      fidl::Array<uint8_t> next_token;
      do {
        get_left_or_right_diff(
            std::move(token),
            [&status, page_change, &next_token](ledger::Status s,
                                                ledger::PageChangePtr change,
                                                fidl::Array<uint8_t> next) {
              status = s;
              AppendChanges(page_change, std::move(change));
              next_token = std::move(next);
            });
        if (!result_provider.WaitForIncomingResponse()) {
          return ::testing::AssertionFailure() << "GetLeftDiff failed.";
        }
        if (status != ledger::Status::OK &&
            status != ledger::Status::PARTIAL_RESULT) {
          return ::testing::AssertionFailure()
                 << "GetLeftDiff failed with status " << status;
        }
        if (!next_token != (status == ledger::Status::OK)) {
          return ::testing::AssertionFailure()
                 << "next_token is " << convert::ToString(next_token)
                 << ", but status is:" << status;
        }
        ++num_queries;

        token = std::move(next_token);
        next_token = nullptr;  // Suppress misc-use-after-move.
      } while (token);

      if (num_queries < min_queries) {
        return ::testing::AssertionFailure()
               << "Only " << num_queries
               << " partial results were found, but at least " << min_queries
               << " were expected";
      }
      return ::testing::AssertionSuccess();
    }

    ::testing::AssertionResult PartialMerge(
        fidl::Array<ledger::MergedValuePtr> partial_result) {
      ledger::Status status;
      result_provider->Merge(std::move(partial_result),
                             [&status](ledger::Status s) { status = s; });
      if (!result_provider.WaitForIncomingResponse()) {
        return ::testing::AssertionFailure() << "Merge failed.";
      }
      if (status != ledger::Status::OK) {
        return ::testing::AssertionFailure()
               << "Merge failed with status " << status;
      }
      return ::testing::AssertionSuccess();
    }
  };

  std::vector<ResolveRequest> requests;
  bool disconnected = false;

 private:
  // ConflictResolver:
  void Resolve(fidl::InterfaceHandle<ledger::PageSnapshot> left_version,
               fidl::InterfaceHandle<ledger::PageSnapshot> right_version,
               fidl::InterfaceHandle<ledger::PageSnapshot> common_version,
               fidl::InterfaceHandle<ledger::MergeResultProvider>
                   result_provider) override {
    requests.emplace_back(std::move(left_version), std::move(right_version),
                          std::move(common_version),
                          std::move(result_provider));
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  fidl::Binding<ConflictResolver> binding_;
};

class TestConflictResolverFactory : public ledger::ConflictResolverFactory {
 public:
  TestConflictResolverFactory(
      ledger::MergePolicy policy,
      fidl::InterfaceRequest<ledger::ConflictResolverFactory> request,
      fxl::Closure on_get_policy_called_callback,
      fxl::TimeDelta response_delay = fxl::TimeDelta::FromMilliseconds(0))
      : policy_(policy),
        binding_(this, std::move(request)),
        callback_(std::move(on_get_policy_called_callback)),
        response_delay_(response_delay) {}

  uint get_policy_calls = 0;
  std::unordered_map<storage::PageId, ConflictResolverImpl> resolvers;

 private:
  // ConflictResolverFactory:
  void GetPolicy(fidl::Array<uint8_t> /*page_id*/,
                 const GetPolicyCallback& callback) override {
    get_policy_calls++;
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this, callback] {
          callback(policy_);
          if (callback_) {
            callback_();
          }
        },
        response_delay_);
  }

  void NewConflictResolver(
      fidl::Array<uint8_t> page_id,
      fidl::InterfaceRequest<ledger::ConflictResolver> resolver) override {
    resolvers.emplace(std::piecewise_construct,
                      std::forward_as_tuple(convert::ToString(page_id)),
                      std::forward_as_tuple(std::move(resolver)));
  }

  ledger::MergePolicy policy_;
  fidl::Binding<ConflictResolverFactory> binding_;
  fxl::Closure callback_;
  fxl::TimeDelta response_delay_;
};

::testing::AssertionResult ChangesMatch(
    std::vector<std::string> expected_keys,
    std::vector<std::string> expected_values,
    const fidl::Array<ledger::EntryPtr>& found_entries) {
  FXL_DCHECK(expected_keys.size() == expected_values.size());
  if (found_entries.size() != expected_keys.size()) {
    return ::testing::AssertionFailure()
           << "Wrong changes size. Expected " << expected_keys.size()
           << " but found " << found_entries.size();
  }
  for (size_t i = 0; i < expected_keys.size(); ++i) {
    if (expected_keys[i] !=
        convert::ExtendedStringView(found_entries[i]->key)) {
      return ::testing::AssertionFailure()
             << "Expected key \"" << expected_keys[i] << "\" but found \""
             << convert::ExtendedStringView(found_entries[i]->key) << "\"";
    }
    if (expected_values[i] != ToString(found_entries[i]->value)) {
      return ::testing::AssertionFailure()
             << "Expected value \"" << expected_values[i] << "\" but found \""
             << ToString(found_entries[i]->value) << "\"";
    }
  }
  return ::testing::AssertionSuccess();
}

TEST_F(MergingIntegrationTest, Merging) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  ledger::PageWatcherPtr watcher1_ptr;
  Watcher watcher1(GetProxy(&watcher1_ptr),
                   [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot1;
  page1->GetSnapshot(
      snapshot1.NewRequest(), nullptr, std::move(watcher1_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  ledger::PageWatcherPtr watcher2_ptr;
  Watcher watcher2(GetProxy(&watcher2_ptr),
                   [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot2;
  page2->GetSnapshot(
      snapshot2.NewRequest(), nullptr, std::move(watcher2_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("city"), convert::ToArray("Paris"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("name"), convert::ToArray("Bob"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("phone"), convert::ToArray("0123456789"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  // Verify that each change is seen by the right watcher.
  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, watcher1.changes_seen);
  ledger::PageChangePtr change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change->changes.size());
  EXPECT_EQ("city", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Paris", ToString(change->changes[0]->value));
  EXPECT_EQ("name", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("Alice", ToString(change->changes[1]->value));

  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  ASSERT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher2.changes_seen);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(2u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob", ToString(change->changes[0]->value));
  EXPECT_EQ("phone", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("0123456789", ToString(change->changes[1]->value));

  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_FALSE(RunLoopWithTimeout());
  // Each change is seen once, and by the correct watcher only.
  EXPECT_EQ(2u, watcher1.changes_seen);
  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob", ToString(change->changes[0]->value));
  EXPECT_EQ("phone", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("0123456789", ToString(change->changes[1]->value));

  EXPECT_EQ(2u, watcher2.changes_seen);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("city", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Paris", ToString(change->changes[0]->value));
}

TEST_F(MergingIntegrationTest, MergingWithConflictResolutionFactory) {
  auto instance = NewLedgerAppInstance();
  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  // Set up a resolver
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::NONE, GetProxy(&resolver_factory_ptr),
          [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  ledger::PageWatcherPtr watcher1_ptr;
  Watcher watcher1(GetProxy(&watcher1_ptr),
                   [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot1;
  page1->GetSnapshot(
      snapshot1.NewRequest(), nullptr, std::move(watcher1_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  ledger::PageWatcherPtr watcher2_ptr;
  Watcher watcher2(GetProxy(&watcher2_ptr),
                   [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot2;
  page2->GetSnapshot(
      snapshot2.NewRequest(), nullptr, std::move(watcher2_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("city"), convert::ToArray("Paris"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("name"), convert::ToArray("Bob"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("phone"), convert::ToArray("0123456789"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  // Verify that each change is seen by the right watcher.
  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher1.changes_seen);
  ledger::PageChangePtr change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change->changes.size());
  EXPECT_EQ("city", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Paris", ToString(change->changes[0]->value));
  EXPECT_EQ("name", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("Alice", ToString(change->changes[1]->value));

  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher2.changes_seen);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(2u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob", ToString(change->changes[0]->value));
  EXPECT_EQ("phone", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("0123456789", ToString(change->changes[1]->value));
  EXPECT_TRUE(RunLoopWithTimeout());
  EXPECT_EQ(1u, resolver_factory->get_policy_calls);

  // Change the merge strategy.
  resolver_factory_ptr = nullptr;  // Suppress misc-use-after-move.
  resolver_factory = std::make_unique<TestConflictResolverFactory>(
      ledger::MergePolicy::LAST_ONE_WINS, GetProxy(&resolver_factory_ptr),
      [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());

  // Each change is seen once, and by the correct watcher only.
  EXPECT_EQ(2u, watcher1.changes_seen);
  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change->changes.size());
  EXPECT_EQ("name", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Bob", ToString(change->changes[0]->value));
  EXPECT_EQ("phone", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("0123456789", ToString(change->changes[1]->value));

  EXPECT_EQ(2u, watcher2.changes_seen);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change->changes.size());
  EXPECT_EQ("city", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Paris", ToString(change->changes[0]->value));

  EXPECT_EQ(1u, resolver_factory->get_policy_calls);
}

TEST_F(MergingIntegrationTest, CustomConflictResolutionNoConflict) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr),
          nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("city"), convert::ToArray("Paris"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("phone"), convert::ToArray("0123456789"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("email"), convert::ToArray("alice@example.org"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl->requests.size());

  ledger::PageChangePtr change_left;
  ledger::PageChangePtr change_right;
  ASSERT_TRUE(resolver_impl->requests[0].GetDiff(&change_left, &change_right));

  // Left change is the most recent, so the one made on |page2|.
  EXPECT_TRUE(ChangesMatch(
      std::vector<std::string>({"email", "phone"}),
      std::vector<std::string>({"alice@example.org", "0123456789"}),
      change_left->changes));
  // Right change comes from |page1|.
  EXPECT_TRUE(ChangesMatch(std::vector<std::string>({"city", "name"}),
                           std::vector<std::string>({"Paris", "Alice"}),
                           change_right->changes));

  // Common ancestor is empty.
  ledger::PageSnapshotPtr snapshot = ledger::PageSnapshotPtr::Create(
      std::move(resolver_impl->requests[0].common_version));
  fidl::Array<ledger::EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, entries.size());

  // Prepare the merged values
  fidl::Array<ledger::MergedValuePtr> merged_values =
      fidl::Array<ledger::MergedValuePtr>::New(0);
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("name");
    merged_value->source = ledger::ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("email");
    merged_value->source = ledger::ValueSource::DELETE;
    merged_values.push_back(std::move(merged_value));
  }
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("pager");
    merged_value->source = ledger::ValueSource::NEW;
    ledger::BytesOrReferencePtr value = ledger::BytesOrReference::New();
    value->set_bytes(convert::ToArray("pager@example.org"));
    merged_value->new_value = std::move(value);
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  ledger::PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot2;
  page1->GetSnapshot(
      snapshot2.NewRequest(), nullptr, std::move(watcher_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  EXPECT_FALSE(RunLoopWithTimeout());

  fidl::Array<ledger::EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(3u, final_entries.size());
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("pager", convert::ExtendedStringView(final_entries[1]->key));
  EXPECT_EQ("phone", convert::ExtendedStringView(final_entries[2]->key));
}

TEST_F(MergingIntegrationTest, CustomConflictResolutionGetDiffMultiPart) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr), nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  std::function<void(ledger::Status)> status_ok_callback =
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); };
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr),
                                         status_ok_callback);
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  page1->StartTransaction(status_ok_callback);
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  int N = 50;
  std::vector<std::string> page1_keys;
  for (int i = 0; i < N; ++i) {
    page1_keys.push_back(fxl::StringPrintf("page1_key_%02d", i));
    page1->Put(convert::ToArray(page1_keys.back()), convert::ToArray("value"),
               status_ok_callback);
    EXPECT_TRUE(page1.WaitForIncomingResponse());
  }

  page2->StartTransaction(status_ok_callback);
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  std::vector<std::string> page2_keys;
  for (int i = 0; i < N; ++i) {
    page2_keys.push_back(fxl::StringPrintf("page2_key_%02d", i));
    page2->Put(convert::ToArray(page2_keys.back()), convert::ToArray("value"),
               status_ok_callback);
    EXPECT_TRUE(page2.WaitForIncomingResponse());
  }

  page1->Commit(status_ok_callback);
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit(status_ok_callback);
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl->requests.size());

  ledger::PageChangePtr change_left;
  ledger::PageChangePtr change_right;
  ASSERT_TRUE(
      resolver_impl->requests[0].GetDiff(&change_left, &change_right, 1));

  std::vector<std::string> values;
  values.resize(N, "value");
  // Left change is the most recent, so the one made on |page2|.
  EXPECT_TRUE(ChangesMatch(page2_keys, values, change_left->changes));
  // Right change comes from |page1|.
  EXPECT_TRUE(ChangesMatch(page1_keys, values, change_right->changes));
}

TEST_F(MergingIntegrationTest, CustomConflictResolutionClosingPipe) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr),
          nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("name"), convert::ToArray("Bob"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // Kill the resolver
  resolver_factory->resolvers.clear();
  EXPECT_EQ(0u, resolver_factory->resolvers.size());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We should ask again for a resolution.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl->requests.size());

  // Remove all references to a page:
  page1 = nullptr;
  page2 = nullptr;
  EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(500)));

  // Resolution should not crash the Ledger
  fidl::Array<ledger::MergedValuePtr> merged_values =
      fidl::Array<ledger::MergedValuePtr>::New(0);
  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));
  EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(200)));
}

TEST_F(MergingIntegrationTest, CustomConflictResolutionResetFactory) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr),
          nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("name"), convert::ToArray("Bob"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  EXPECT_FALSE(resolver_impl->disconnected);
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // Change the factory.
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr2;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory2 =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr2),
          nullptr);
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr2),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  // Two runs of the loop: one for the conflict resolution request, one for the
  // disconnect.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());

  // The previous resolver should have been disconnected.
  EXPECT_TRUE(resolver_impl->disconnected);
  // It shouldn't have been called again.
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // We should ask again for a resolution on a new resolver.
  EXPECT_EQ(1u, resolver_factory2->resolvers.size());
  ASSERT_NE(resolver_factory2->resolvers.end(),
            resolver_factory2->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl2 =
      &(resolver_factory2->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl2->requests.size());

  // Remove all references to a page:
  page1 = nullptr;
  page2 = nullptr;
  EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(500)));

  // Resolution should not crash the Ledger
  fidl::Array<ledger::MergedValuePtr> merged_values =
      fidl::Array<ledger::MergedValuePtr>::New(0);

  EXPECT_TRUE(resolver_impl2->requests[0].Merge(std::move(merged_values)));
  EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(200)));
}

// Tests for a race between setting the new conflict resolver and sending the
// resolution request. Specifically, the resolution request must be sent to the
// new resolver, not the old one.
TEST_F(MergingIntegrationTest,
       CustomConflictResolutionResetFactory_FactoryRace) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr),
          nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("name"), convert::ToArray("Bob"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  EXPECT_FALSE(resolver_impl->disconnected);
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // Change the factory.
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr2;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory2 =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr2),
          nullptr, fxl::TimeDelta::FromMilliseconds(250));
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr2),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  // Two runs of the loop: one for the conflict resolution request, one for the
  // disconnect.
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());

  // The previous resolver should have been disconnected.
  EXPECT_TRUE(resolver_impl->disconnected);
  // It shouldn't have been called again.
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // We should ask again for a resolution on a new resolver.
  EXPECT_EQ(1u, resolver_factory2->resolvers.size());
  ASSERT_NE(resolver_factory2->resolvers.end(),
            resolver_factory2->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl2 =
      &(resolver_factory2->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl2->requests.size());
}

TEST_F(MergingIntegrationTest, CustomConflictResolutionMultipartMerge) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr),
          nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("email"), convert::ToArray("alice@example.org"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl->requests.size());

  // Prepare the merged values
  fidl::Array<ledger::MergedValuePtr> merged_values =
      fidl::Array<ledger::MergedValuePtr>::New(0);
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("name");
    merged_value->source = ledger::ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("email");
    merged_value->source = ledger::ValueSource::DELETE;
    merged_values.push_back(std::move(merged_value));
  }
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("pager");
    merged_value->source = ledger::ValueSource::NEW;
    ledger::BytesOrReferencePtr value = ledger::BytesOrReference::New();
    value->set_bytes(convert::ToArray("pager@example.org"));
    merged_value->new_value = std::move(value);
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  ledger::PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot;
  page1->GetSnapshot(
      snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values),
                                               MergeType::MULTIPART));

  // Wait for the watcher to be called.
  EXPECT_FALSE(RunLoopWithTimeout());

  fidl::Array<ledger::EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(2u, final_entries.size());
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("pager", convert::ExtendedStringView(final_entries[1]->key));
}

TEST_F(MergingIntegrationTest, AutoConflictResolutionNoConflict) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::AUTOMATIC_WITH_FALLBACK,
          GetProxy(&resolver_factory_ptr), nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  // Watch for changes.
  ledger::PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  []() { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot2;
  page1->GetSnapshot(
      snapshot2.NewRequest(), nullptr, std::move(watcher_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("city"), convert::ToArray("Paris"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("email"), convert::ToArray("alice@example.org"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("phone"), convert::ToArray("0123456789"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());
  // We should have seen the first commit at this point.
  EXPECT_EQ(1u, watcher.changes_seen);

  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have an automatically-resolved conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  // We haven't been asked to resolve anything.
  EXPECT_EQ(0u, resolver_impl->requests.size());

  EXPECT_EQ(2u, watcher.changes_seen);

  fidl::Array<ledger::EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(4u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("email", convert::ExtendedStringView(final_entries[1]->key));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[2]->key));
  EXPECT_EQ("phone", convert::ExtendedStringView(final_entries[3]->key));
}

TEST_F(MergingIntegrationTest, AutoConflictResolutionWithConflict) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::AUTOMATIC_WITH_FALLBACK,
          GetProxy(&resolver_factory_ptr), nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("city"), convert::ToArray("Paris"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("city"), convert::ToArray("San Francisco"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl->requests.size());

  ledger::PageChangePtr change_left;
  ledger::PageChangePtr change_right;
  ASSERT_TRUE(resolver_impl->requests[0].GetDiff(&change_left, &change_right));

  // Left change is the most recent, so the one made on |page2|.
  EXPECT_TRUE(ChangesMatch(std::vector<std::string>({"city", "name"}),
                           std::vector<std::string>({"San Francisco", "Alice"}),
                           change_left->changes));
  // Right change comes from |page1|.
  EXPECT_TRUE(ChangesMatch(std::vector<std::string>({"city"}),
                           std::vector<std::string>({"Paris"}),
                           change_right->changes));
  // Common ancestor is empty.
  ledger::PageSnapshotPtr snapshot = ledger::PageSnapshotPtr::Create(
      std::move(resolver_impl->requests[0].common_version));
  fidl::Array<ledger::EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, entries.size());

  // Prepare the merged values
  fidl::Array<ledger::MergedValuePtr> merged_values =
      fidl::Array<ledger::MergedValuePtr>::New(0);
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("city");
    merged_value->source = ledger::ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  ledger::PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  []() { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot2;
  page1->GetSnapshot(
      snapshot2.NewRequest(), nullptr, std::move(watcher_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  EXPECT_FALSE(RunLoopWithTimeout());

  fidl::Array<ledger::EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(2u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[1]->key));
}

TEST_F(MergingIntegrationTest, AutoConflictResolutionMultipartMerge) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::AUTOMATIC_WITH_FALLBACK,
          GetProxy(&resolver_factory_ptr), nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  page1->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(
      convert::ToArray("city"), convert::ToArray("Paris"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("name"), convert::ToArray("Alice"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(
      convert::ToArray("city"), convert::ToArray("San Francisco"),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit(
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl->requests.size());

  // Prepare the merged values
  fidl::Array<ledger::MergedValuePtr> merged_values =
      fidl::Array<ledger::MergedValuePtr>::New(0);
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("city");
    merged_value->source = ledger::ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    ledger::MergedValuePtr merged_value = ledger::MergedValue::New();
    merged_value->key = convert::ToArray("previous_city");
    merged_value->source = ledger::ValueSource::NEW;
    merged_value->new_value = ledger::BytesOrReference::New();
    merged_value->new_value->set_bytes(convert::ToArray("San Francisco"));
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  ledger::PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  []() { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger::PageSnapshotPtr snapshot;
  page1->GetSnapshot(
      snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
      [](ledger::Status status) { EXPECT_EQ(ledger::Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values),
                                               MergeType::MULTIPART));

  // Wait for the watcher to be called.
  EXPECT_FALSE(RunLoopWithTimeout());

  fidl::Array<ledger::EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(3u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[1]->key));
  EXPECT_EQ("previous_city",
            convert::ExtendedStringView(final_entries[2]->key));
}

// Tests a merge in which the right side contains no change (e.g. a change was
// made in a commit, then reverted in another commit).
TEST_F(MergingIntegrationTest, AutoConflictResolutionNoRightChange) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::AUTOMATIC_WITH_FALLBACK,
          GetProxy(&resolver_factory_ptr), nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger::Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId(callback::Capture(MakeQuitTask(), &test_page_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  // Watch for changes.
  ledger::PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr), MakeQuitTask());
  ledger::PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), nullptr, std::move(watcher_ptr),
                     callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  page1->StartTransaction(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  page2->StartTransaction(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  page1->Commit(callback::Capture(MakeQuitTask(), &status));

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  // We should have seen the first commit of page 1.
  EXPECT_EQ(1u, watcher.changes_seen);

  page1->StartTransaction(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  page1->Delete(convert::ToArray("name"),
                callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  page1->Commit(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  // We should have seen the second commit of page 1.
  EXPECT_EQ(2u, watcher.changes_seen);

  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  page2->Commit(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, ledger::Status::OK);

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have an automatically-resolved conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  ASSERT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  // We haven't been asked to resolve anything.
  EXPECT_EQ(0u, resolver_impl->requests.size());

  EXPECT_EQ(3u, watcher.changes_seen);

  fidl::Array<ledger::EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(1u, final_entries.size());
  EXPECT_EQ("email", convert::ExtendedStringView(final_entries[0]->key));
}

TEST_F(MergingIntegrationTest, DeleteDuringConflictResolution) {
  auto instance = NewLedgerAppInstance();
  ledger::ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          ledger::MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr),
          nullptr);
  ledger::LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](ledger::Status status) { EXPECT_EQ(status, ledger::Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  ledger::PagePtr page1 = instance->GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId(callback::Capture(MakeQuitTask(), &test_page_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ledger::PagePtr page2 = instance->GetPage(test_page_id, ledger::Status::OK);

  ledger::Status status = ledger::Status::UNKNOWN_ERROR;
  page1->StartTransaction(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(ledger::Status::OK, status);

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(ledger::Status::OK, status);

  page2->StartTransaction(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(ledger::Status::OK, status);
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(ledger::Status::OK, status);

  page1->Commit(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(ledger::Status::OK, status);
  page2->Commit(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(ledger::Status::OK, status);

  EXPECT_FALSE(RunLoopWithTimeout());

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id))
            ->second);
  ASSERT_EQ(1u, resolver_impl->requests.size());

  instance->DeletePage(test_page_id, ledger::Status::OK);
  EXPECT_FALSE(resolver_impl->requests[0].Merge(
      fidl::Array<ledger::MergedValuePtr>::New(0)));
}

}  // namespace
}  // namespace integration
}  // namespace test
