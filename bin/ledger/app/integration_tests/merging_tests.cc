// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/app/integration_tests/integration_test.h"
#include "apps/ledger/src/app/integration_tests/test_utils.h"
#include "apps/ledger/src/convert/convert.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace integration_tests {
namespace {

class MergingIntegrationTest : public IntegrationTest {
 public:
  MergingIntegrationTest() {}
  ~MergingIntegrationTest() override {}

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(MergingIntegrationTest);
};

class Watcher : public PageWatcher {
 public:
  Watcher(fidl::InterfaceRequest<PageWatcher> request,
          ftl::Closure change_callback)
      : binding_(this, std::move(request)), change_callback_(change_callback) {}

  uint changes_seen = 0;
  PageSnapshotPtr last_snapshot_;
  PageChangePtr last_page_change_;

 private:
  // PageWatcher:
  void OnChange(PageChangePtr page_change,
                ResultState result_state,
                const OnChangeCallback& callback) override {
    FTL_DCHECK(page_change);
    FTL_DCHECK(result_state == ResultState::COMPLETED)
        << "Handling OnChange pagination not implemented yet";
    changes_seen++;
    last_page_change_ = std::move(page_change);
    last_snapshot_.reset();
    callback(last_snapshot_.NewRequest());
    change_callback_();
  }

  fidl::Binding<PageWatcher> binding_;
  ftl::Closure change_callback_;
};

PageChangePtr NewPageChange() {
  PageChangePtr change = PageChange::New();
  change->changes = fidl::Array<EntryPtr>::New(0);
  change->deleted_keys = fidl::Array<fidl::Array<uint8_t>>::New(0);
  return change;
}

void AppendChanges(PageChangePtr* base, PageChangePtr changes) {
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

class ConflictResolverImpl : public ConflictResolver {
 public:
  explicit ConflictResolverImpl(
      fidl::InterfaceRequest<ConflictResolver> request)
      : binding_(this, std::move(request)) {
    binding_.set_connection_error_handler([this] {
      this->disconnected = true;
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
    });
  }
  ~ConflictResolverImpl() {}

  struct ResolveRequest {
    fidl::InterfaceHandle<PageSnapshot> left_version;
    fidl::InterfaceHandle<PageSnapshot> right_version;
    fidl::InterfaceHandle<PageSnapshot> common_version;
    MergeResultProviderPtr result_provider;

    ResolveRequest(fidl::InterfaceHandle<PageSnapshot> left_version,
                   fidl::InterfaceHandle<PageSnapshot> right_version,
                   fidl::InterfaceHandle<PageSnapshot> common_version,
                   fidl::InterfaceHandle<MergeResultProvider> result_provider)
        : left_version(std::move(left_version)),
          right_version(std::move(right_version)),
          common_version(std::move(common_version)),
          result_provider(
              MergeResultProviderPtr::Create(std::move(result_provider))) {}

    // Returns the changes from the left and right branch.
    ::testing::AssertionResult GetDiff(PageChangePtr* change_left,
                                       PageChangePtr* change_right) {
      return GetDiff(change_left, change_right, 0);
    }

    // Returns the changes from the left and right branch and makes sure that at
    // least |min_queries| of partial results are returned before retrieving the
    // complete result for the left and for the right changes.
    ::testing::AssertionResult GetDiff(PageChangePtr* change_left,
                                       PageChangePtr* change_right,
                                       int min_queries) {
      *change_left = NewPageChange();
      *change_right = NewPageChange();
      ::testing::AssertionResult left_result = GetDiff(
          nullptr, change_left, 0, min_queries,
          [this](fidl::Array<uint8_t> token,
                 const std::function<void(Status, PageChangePtr change,
                                          fidl::Array<uint8_t> next_token)>&
                     callback) {
            result_provider->GetLeftDiff(std::move(token), callback);
          });
      if (!left_result) {
        return left_result;
      }
      return GetDiff(
          nullptr, change_right, 0, min_queries,
          [this](fidl::Array<uint8_t> token,
                 const std::function<void(Status, PageChangePtr change,
                                          fidl::Array<uint8_t> next_token)>&
                     callback) {
            result_provider->GetRightDiff(std::move(token), callback);
          });
    }

    // Resolves the conflict by sending the given merge results. If
    // |merge_type| is MULTIPART, the merge will be send in two parts, each
    // sending half of |results|' elements.
    ::testing::AssertionResult Merge(fidl::Array<MergedValuePtr> results,
                                     MergeType merge_type = MergeType::SIMPLE) {
      FTL_DCHECK(merge_type == MergeType::SIMPLE || results.size() >= 2);
      if (merge_type == MergeType::SIMPLE) {
        ::testing::AssertionResult merge_status =
            PartialMerge(std::move(results));
        if (!merge_status) {
          return merge_status;
        }
      } else {
        size_t part1_size = results.size() / 2;
        fidl::Array<MergedValuePtr> part2;
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

      Status status;
      result_provider->Done([&status](Status s) { status = s; });
      if (!result_provider.WaitForIncomingResponse()) {
        return ::testing::AssertionFailure() << "Done failed.";
      }
      if (status != Status::OK) {
        return ::testing::AssertionFailure()
               << "Done failed with status " << status;
      }
      return ::testing::AssertionSuccess();
    }

   private:
    ::testing::AssertionResult GetDiff(
        fidl::Array<uint8_t> token,
        PageChangePtr* page_change,
        int num_queries,
        int min_queries,
        std::function<
            void(fidl::Array<uint8_t>,
                 const std::function<
                     void(Status, PageChangePtr, fidl::Array<uint8_t>)>&)>
            get_left_or_right_diff) {
      Status status;
      fidl::Array<uint8_t> next_token;
      do {
        get_left_or_right_diff(
            std::move(token),
            [&status, page_change, &next_token](Status s, PageChangePtr change,
                                                fidl::Array<uint8_t> next) {
              status = s;
              AppendChanges(page_change, std::move(change));
              next_token = std::move(next);
            });
        if (!result_provider.WaitForIncomingResponse()) {
          return ::testing::AssertionFailure() << "GetLeftDiff failed.";
        }
        if (status != Status::OK && status != Status::PARTIAL_RESULT) {
          return ::testing::AssertionFailure()
                 << "GetLeftDiff failed with status " << status;
        }
        if (!next_token != (status == Status::OK)) {
          return ::testing::AssertionFailure()
                 << "next_token is " << convert::ToString(next_token)
                 << ", but status is:" << status;
        }
        ++num_queries;

        token = std::move(next_token);
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
        fidl::Array<MergedValuePtr> partial_result) {
      Status status;
      result_provider->Merge(std::move(partial_result),
                             [&status](Status s) { status = s; });
      if (!result_provider.WaitForIncomingResponse()) {
        return ::testing::AssertionFailure() << "Merge failed.";
      }
      if (status != Status::OK) {
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
  void Resolve(
      fidl::InterfaceHandle<PageSnapshot> left_version,
      fidl::InterfaceHandle<PageSnapshot> right_version,
      fidl::InterfaceHandle<PageSnapshot> common_version,
      fidl::InterfaceHandle<MergeResultProvider> result_provider) override {
    requests.emplace_back(std::move(left_version), std::move(right_version),
                          std::move(common_version),
                          std::move(result_provider));
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  fidl::Binding<ConflictResolver> binding_;
};

class TestConflictResolverFactory : public ConflictResolverFactory {
 public:
  TestConflictResolverFactory(
      MergePolicy policy,
      fidl::InterfaceRequest<ConflictResolverFactory> request,
      ftl::Closure on_get_policy_called_callback,
      ftl::TimeDelta response_delay = ftl::TimeDelta::FromMilliseconds(0))
      : policy_(policy),
        binding_(this, std::move(request)),
        callback_(on_get_policy_called_callback),
        response_delay_(response_delay) {}

  uint get_policy_calls = 0;
  std::unordered_map<storage::PageId, ConflictResolverImpl> resolvers;

 private:
  // ConflictResolverFactory:
  void GetPolicy(fidl::Array<uint8_t> page_id,
                 const GetPolicyCallback& callback) override {
    get_policy_calls++;
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
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
      fidl::InterfaceRequest<ConflictResolver> resolver) override {
    resolvers.emplace(std::piecewise_construct,
                      std::forward_as_tuple(convert::ToString(page_id)),
                      std::forward_as_tuple(std::move(resolver)));
  }

  MergePolicy policy_;
  fidl::Binding<ConflictResolverFactory> binding_;
  ftl::Closure callback_;
  ftl::TimeDelta response_delay_;
};

::testing::AssertionResult ChangesMatch(
    std::vector<std::string> expected_keys,
    std::vector<std::string> expected_values,
    const fidl::Array<EntryPtr>& found_entries) {
  FTL_DCHECK(expected_keys.size() == expected_values.size());
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
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PagePtr page2 = GetPage(test_page_id, Status::OK);

  PageWatcherPtr watcher1_ptr;
  Watcher watcher1(GetProxy(&watcher1_ptr),
                   [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), nullptr, std::move(watcher1_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(GetProxy(&watcher2_ptr),
                   [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot2;
  page2->GetSnapshot(snapshot2.NewRequest(), nullptr, std::move(watcher2_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  // Verify that each change is seen by the right watcher.
  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  ASSERT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(1u, watcher1.changes_seen);
  PageChangePtr change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change->changes.size());
  EXPECT_EQ("city", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Paris", ToString(change->changes[0]->value));
  EXPECT_EQ("name", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("Alice", ToString(change->changes[1]->value));

  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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
  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  // Set up a resolver
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::NONE, GetProxy(&resolver_factory_ptr),
          [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  PagePtr page2 = GetPage(test_page_id, Status::OK);

  PageWatcherPtr watcher1_ptr;
  Watcher watcher1(GetProxy(&watcher1_ptr),
                   [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), nullptr, std::move(watcher1_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(GetProxy(&watcher2_ptr),
                   [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot2;
  page2->GetSnapshot(snapshot2.NewRequest(), nullptr, std::move(watcher2_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  // Verify that each change is seen by the right watcher.
  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_EQ(1u, watcher1.changes_seen);
  PageChangePtr change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change->changes.size());
  EXPECT_EQ("city", convert::ToString(change->changes[0]->key));
  EXPECT_EQ("Paris", ToString(change->changes[0]->value));
  EXPECT_EQ("name", convert::ToString(change->changes[1]->key));
  EXPECT_EQ("Alice", ToString(change->changes[1]->value));

  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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
  resolver_factory_ptr.reset();
  resolver_factory = std::make_unique<TestConflictResolverFactory>(
      MergePolicy::LAST_ONE_WINS, GetProxy(&resolver_factory_ptr),
      [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
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
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr), nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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

  PageChangePtr change_left;
  PageChangePtr change_right;
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
  PageSnapshotPtr snapshot = PageSnapshotPtr::Create(
      std::move(resolver_impl->requests[0].common_version));
  fidl::Array<EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, entries.size());

  // Prepare the merged values
  fidl::Array<MergedValuePtr> merged_values =
      fidl::Array<MergedValuePtr>::New(0);
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("name");
    merged_value->source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("email");
    merged_value->source = ValueSource::DELETE;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("pager");
    merged_value->source = ValueSource::NEW;
    BytesOrReferencePtr value = BytesOrReference::New();
    value->set_bytes(convert::ToArray("pager@example.org"));
    merged_value->new_value = std::move(value);
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot2;
  page1->GetSnapshot(snapshot2.NewRequest(), nullptr, std::move(watcher_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  EXPECT_FALSE(RunLoopWithTimeout());

  fidl::Array<EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(3u, final_entries.size());
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("pager", convert::ExtendedStringView(final_entries[1]->key));
  EXPECT_EQ("phone", convert::ExtendedStringView(final_entries[2]->key));
}

TEST_F(MergingIntegrationTest, CustomConflictResolutionGetDiffMultiPart) {
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr), nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  std::function<void(Status)> status_ok_callback = [](Status status) {
    EXPECT_EQ(status, Status::OK);
  };
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr),
                                         status_ok_callback);
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  page1->StartTransaction(status_ok_callback);
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  int N = 50;
  std::vector<std::string> page1_keys;
  for (int i = 0; i < N; ++i) {
    page1_keys.push_back(ftl::StringPrintf("page1_key_%02d", i));
    page1->Put(convert::ToArray(page1_keys.back()), convert::ToArray("value"),
               status_ok_callback);
    EXPECT_TRUE(page1.WaitForIncomingResponse());
  }

  page2->StartTransaction(status_ok_callback);
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  std::vector<std::string> page2_keys;
  for (int i = 0; i < N; ++i) {
    page2_keys.push_back(ftl::StringPrintf("page2_key_%02d", i));
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

  PageChangePtr change_left;
  PageChangePtr change_right;
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
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr), nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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
  EXPECT_TRUE(RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(500)));

  // Resolution should not crash the Ledger
  fidl::Array<MergedValuePtr> merged_values =
      fidl::Array<MergedValuePtr>::New(0);
  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));
  EXPECT_TRUE(RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(200)));
}

TEST_F(MergingIntegrationTest, CustomConflictResolutionResetFactory) {
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr), nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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
  ConflictResolverFactoryPtr resolver_factory_ptr2;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory2 =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr2), nullptr);
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr2),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
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
  EXPECT_TRUE(RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(500)));

  // Resolution should not crash the Ledger
  fidl::Array<MergedValuePtr> merged_values =
      fidl::Array<MergedValuePtr>::New(0);

  EXPECT_TRUE(resolver_impl2->requests[0].Merge(std::move(merged_values)));
  EXPECT_TRUE(RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(200)));
}

// Tests for a race between setting the new conflict resolver and sending the
// resolution request. Specifically, the resolution request must be sent to the
// new resolver, not the old one.
TEST_F(MergingIntegrationTest,
       CustomConflictResolutionResetFactory_FactoryRace) {
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr), nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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
  ConflictResolverFactoryPtr resolver_factory_ptr2;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory2 =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr2), nullptr,
          ftl::TimeDelta::FromMilliseconds(500));
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr2),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
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
  EXPECT_TRUE(RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(500)));

  // Resolution should not crash the Ledger
  fidl::Array<MergedValuePtr> merged_values =
      fidl::Array<MergedValuePtr>::New(0);

  EXPECT_TRUE(resolver_impl2->requests[0].Merge(std::move(merged_values)));
  EXPECT_TRUE(RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(200)));
}

TEST_F(MergingIntegrationTest, CustomConflictResolutionMultipartMerge) {
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::CUSTOM, GetProxy(&resolver_factory_ptr), nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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
  fidl::Array<MergedValuePtr> merged_values =
      fidl::Array<MergedValuePtr>::New(0);
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("name");
    merged_value->source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("email");
    merged_value->source = ValueSource::DELETE;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("pager");
    merged_value->source = ValueSource::NEW;
    BytesOrReferencePtr value = BytesOrReference::New();
    value->set_bytes(convert::ToArray("pager@example.org"));
    merged_value->new_value = std::move(value);
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot;
  page1->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values),
                                               MergeType::MULTIPART));

  // Wait for the watcher to be called.
  EXPECT_FALSE(RunLoopWithTimeout());

  fidl::Array<EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(2u, final_entries.size());
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("pager", convert::ExtendedStringView(final_entries[1]->key));
}

TEST_F(MergingIntegrationTest, AutoConflictResolutionNoConflict) {
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::AUTOMATIC_WITH_FALLBACK, GetProxy(&resolver_factory_ptr),
          nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  // Watch for changes.
  PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  []() { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot2;
  page1->GetSnapshot(snapshot2.NewRequest(), nullptr, std::move(watcher_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_FALSE(RunLoopWithTimeout());
  // We should have seen the first commit at this point.
  EXPECT_EQ(1u, watcher.changes_seen);

  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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

  fidl::Array<EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(4u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("email", convert::ExtendedStringView(final_entries[1]->key));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[2]->key));
  EXPECT_EQ("phone", convert::ExtendedStringView(final_entries[3]->key));
}

TEST_F(MergingIntegrationTest, AutoConflictResolutionWithConflict) {
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::AUTOMATIC_WITH_FALLBACK, GetProxy(&resolver_factory_ptr),
          nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("city"), convert::ToArray("San Francisco"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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

  PageChangePtr change_left;
  PageChangePtr change_right;
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
  PageSnapshotPtr snapshot = PageSnapshotPtr::Create(
      std::move(resolver_impl->requests[0].common_version));
  fidl::Array<EntryPtr> entries =
      SnapshotGetEntries(&snapshot, fidl::Array<uint8_t>());
  EXPECT_EQ(0u, entries.size());

  // Prepare the merged values
  fidl::Array<MergedValuePtr> merged_values =
      fidl::Array<MergedValuePtr>::New(0);
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("city");
    merged_value->source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  []() { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot2;
  page1->GetSnapshot(snapshot2.NewRequest(), nullptr, std::move(watcher_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  EXPECT_FALSE(RunLoopWithTimeout());

  fidl::Array<EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(2u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0]->key));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[1]->key));
}

TEST_F(MergingIntegrationTest, AutoConflictResolutionMultipartMerge) {
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::AUTOMATIC_WITH_FALLBACK, GetProxy(&resolver_factory_ptr),
          nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(ledger_ptr.WaitForIncomingResponse());

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId([&test_page_id](fidl::Array<uint8_t> page_id) {
    test_page_id = std::move(page_id);
  });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  page1->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  page2->StartTransaction([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());
  page2->Put(convert::ToArray("city"), convert::ToArray("San Francisco"),
             [](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page2.WaitForIncomingResponse());

  page1->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());
  page2->Commit([](Status status) { EXPECT_EQ(status, Status::OK); });
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
  fidl::Array<MergedValuePtr> merged_values =
      fidl::Array<MergedValuePtr>::New(0);
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("city");
    merged_value->source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValuePtr merged_value = MergedValue::New();
    merged_value->key = convert::ToArray("previous_city");
    merged_value->source = ValueSource::NEW;
    merged_value->new_value = BytesOrReference::New();
    merged_value->new_value->set_bytes(convert::ToArray("San Francisco"));
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr),
                  []() { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  PageSnapshotPtr snapshot;
  page1->GetSnapshot(snapshot.NewRequest(), nullptr, std::move(watcher_ptr),
                     [](Status status) { EXPECT_EQ(Status::OK, status); });
  EXPECT_TRUE(page1.WaitForIncomingResponse());

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values),
                                               MergeType::MULTIPART));

  // Wait for the watcher to be called.
  EXPECT_FALSE(RunLoopWithTimeout());

  fidl::Array<EntryPtr> final_entries =
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
  ConflictResolverFactoryPtr resolver_factory_ptr;
  std::unique_ptr<TestConflictResolverFactory> resolver_factory =
      std::make_unique<TestConflictResolverFactory>(
          MergePolicy::AUTOMATIC_WITH_FALLBACK, GetProxy(&resolver_factory_ptr),
          nullptr);
  LedgerPtr ledger_ptr = GetTestLedger();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  PagePtr page1 = GetTestPage();
  fidl::Array<uint8_t> test_page_id;
  page1->GetId(callback::Capture(MakeQuitTask(), &test_page_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  PagePtr page2 = GetPage(test_page_id, Status::OK);

  // Watch for changes.
  PageWatcherPtr watcher_ptr;
  Watcher watcher(GetProxy(&watcher_ptr), MakeQuitTask());
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), nullptr, std::move(watcher_ptr),
                     callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  page1->StartTransaction(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  page2->StartTransaction(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  page1->Commit(callback::Capture(MakeQuitTask(), &status));

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  // We should have seen the first commit of page 1.
  EXPECT_EQ(1u, watcher.changes_seen);

  page1->StartTransaction(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  page1->Delete(convert::ToArray("name"),
                callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  page1->Commit(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  // We should have seen the second commit of page 1.
  EXPECT_EQ(2u, watcher.changes_seen);

  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

  page2->Commit(callback::Capture(MakeQuitTask(), &status));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(status, Status::OK);

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

  fidl::Array<EntryPtr> final_entries =
      SnapshotGetEntries(&watcher.last_snapshot_, fidl::Array<uint8_t>());
  ASSERT_EQ(1u, final_entries.size());
  EXPECT_EQ("email", convert::ExtendedStringView(final_entries[0]->key));
}

}  // namespace
}  // namespace integration_tests
}  // namespace ledger
