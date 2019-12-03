// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>

#include <map>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/ledger/bin/tests/integration/test_utils.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"

namespace ledger {
namespace {

class MergingIntegrationTest : public IntegrationTest {
 public:
  MergingIntegrationTest() = default;
  MergingIntegrationTest(const MergingIntegrationTest&) = delete;
  MergingIntegrationTest& operator=(const MergingIntegrationTest&) = delete;
  ~MergingIntegrationTest() override = default;
};

class Watcher : public PageWatcher {
 public:
  Watcher(fidl::InterfaceRequest<PageWatcher> request, fit::closure change_callback)
      : binding_(this, std::move(request)), change_callback_(std::move(change_callback)) {}

  uint changes_seen = 0;
  PageSnapshotPtr last_snapshot_;
  PageChange last_page_change_;

 private:
  // PageWatcher:
  void OnChange(PageChange page_change, ResultState result_state,
                OnChangeCallback callback) override {
    FXL_DCHECK(result_state == ResultState::COMPLETED)
        << "Handling OnChange pagination not implemented yet";
    changes_seen++;
    last_page_change_ = std::move(page_change);
    last_snapshot_.Unbind();
    callback(last_snapshot_.NewRequest());
    change_callback_();
  }

  fidl::Binding<PageWatcher> binding_;
  fit::closure change_callback_;
};

enum class MergeType {
  SIMPLE,
  MULTIPART,
};

class ConflictResolverImpl : public ConflictResolver {
 public:
  explicit ConflictResolverImpl(LoopController* loop_controller,
                                fidl::InterfaceRequest<ConflictResolver> request)
      : loop_controller_(loop_controller),
        resolve_waiter_(loop_controller->NewWaiter()),
        binding_(this, std::move(request)) {
    binding_.set_error_handler(callback::Capture([] {}, &disconnected));
  }
  ~ConflictResolverImpl() override = default;

  struct ResolveRequest {
    fidl::InterfaceHandle<PageSnapshot> left_version;
    fidl::InterfaceHandle<PageSnapshot> right_version;
    fidl::InterfaceHandle<PageSnapshot> common_version;
    MergeResultProviderPtr result_provider;
    zx_status_t result_provider_status = ZX_OK;

    ResolveRequest(LoopController* loop_controller,
                   fidl::InterfaceHandle<PageSnapshot> left_version,
                   fidl::InterfaceHandle<PageSnapshot> right_version,
                   fidl::InterfaceHandle<PageSnapshot> common_version,
                   fidl::InterfaceHandle<MergeResultProvider> result_provider)
        : left_version(std::move(left_version)),
          right_version(std::move(right_version)),
          common_version(std::move(common_version)),
          result_provider(result_provider.Bind()),
          disconnect_waiter_(loop_controller->NewWaiter()),
          loop_controller_(loop_controller) {
      this->result_provider.set_error_handler(
          callback::Capture(disconnect_waiter_->GetCallback(), &result_provider_status));
    }

    // Returns the full list of changes.
    // Returns the full list of changes between branches and makes sure that at
    // least |min_queries| of partial results are returned before retrieving the
    // complete result for the left and for the right changes.
    ::testing::AssertionResult GetFullDiff(std::vector<DiffEntry>* entries,
                                           size_t min_queries = 0) {
      return GetDiff(
          [this](std::unique_ptr<Token> token,
                 fit::function<void(std::vector<DiffEntry>, std::unique_ptr<Token>)>
                     callback) mutable {
            result_provider->GetFullDiff(std::move(token), std::move(callback));
          },
          entries, min_queries);
    }

    ::testing::AssertionResult GetConflictingDiff(std::vector<DiffEntry>* entries,
                                                  size_t min_queries = 0) {
      return GetDiff(
          [this](std::unique_ptr<Token> token,
                 fit::function<void(std::vector<DiffEntry>, std::unique_ptr<Token>)>
                     callback) mutable {
            result_provider->GetConflictingDiff(std::move(token), std::move(callback));
          },
          entries, min_queries);
    }

    // Resolves the conflict by sending the given merge results. If
    // |merge_type| is MULTIPART, the merge will be send in two parts, each
    // sending half of |results|' elements.
    ::testing::AssertionResult Merge(std::vector<MergedValue> results,
                                     MergeType merge_type = MergeType::SIMPLE) {
      FXL_DCHECK(merge_type == MergeType::SIMPLE || results.size() >= 2);

      if (!result_provider) {
        return ::testing::AssertionFailure() << "Merge failed: result_provider is disconnected.";
      }

      if (merge_type == MergeType::SIMPLE) {
        ::testing::AssertionResult merge_status = PartialMerge(std::move(results));
        if (!merge_status) {
          return merge_status;
        }
      } else {
        size_t part1_size = results.size() / 2;
        std::vector<MergedValue> part2;
        for (size_t i = part1_size; i < results.size(); ++i) {
          part2.push_back(std::move(results.at(i)));
        }
        results.resize(part1_size);

        ::testing::AssertionResult merge_status = PartialMerge(std::move(results));
        if (!merge_status) {
          return merge_status;
        }
        merge_status = PartialMerge(std::move(part2));
        if (!merge_status) {
          return merge_status;
        }
      }

      result_provider->Done();
      return RunUntilDisconnected();
    }

    ::testing::AssertionResult MergeNonConflictingEntries() {
      result_provider->MergeNonConflictingEntries();
      return Sync();
    }

   private:
    ::testing::AssertionResult Sync() {
      auto waiter = loop_controller_->NewWaiter();
      result_provider->Sync(waiter->GetCallback());
      if (!waiter->RunUntilCalled()) {
        // Printing the |result_provider_status| in case the issue is that the
        // object has been disconnected.
        return ::testing::AssertionFailure()
               << "|Sync| failed to called back. Error provider status: " << result_provider_status;
      }
      return ::testing::AssertionSuccess();
    }

    ::testing::AssertionResult RunUntilDisconnected() {
      if (!disconnect_waiter_->RunUntilCalled()) {
        return ::testing::AssertionFailure()
               << "Timeout while waiting for the ConflictResolver to be "
                  "disconnected from the ResultProvider.";
      }
      return ::testing::AssertionSuccess();
    }

    ::testing::AssertionResult GetDiff(
        fit::function<void(std::unique_ptr<Token>,
                           fit::function<void(std::vector<DiffEntry>, std::unique_ptr<Token>)>)>
            get_diff,
        std::vector<DiffEntry>* entries, size_t min_queries) {
      entries->resize(0);
      size_t num_queries = 0u;
      std::unique_ptr<Token> token;
      do {
        std::vector<DiffEntry> new_entries;
        auto waiter = loop_controller_->NewWaiter();
        std::unique_ptr<Token> new_token;
        get_diff(std::move(token),
                 callback::Capture(waiter->GetCallback(), &new_entries, &new_token));
        if (!waiter->RunUntilCalled()) {
          return ::testing::AssertionFailure() << "|get_diff| failed to called back.";
        }
        token = std::move(new_token);
        entries->insert(entries->end(), std::make_move_iterator(new_entries.begin()),
                        std::make_move_iterator(new_entries.end()));
        ++num_queries;
      } while (token);

      if (num_queries < min_queries) {
        return ::testing::AssertionFailure()
               << "Only " << num_queries << " partial results were found, but at least "
               << min_queries << " were expected";
      }
      return ::testing::AssertionSuccess();
    }

    ::testing::AssertionResult PartialMerge(std::vector<MergedValue> partial_result) {
      result_provider->Merge(std::move(partial_result));
      return Sync();
    }

    std::unique_ptr<CallbackWaiter> disconnect_waiter_;
    LoopController* loop_controller_;
  };

  void RunUntilResolveCalled() { ASSERT_TRUE(resolve_waiter_->RunUntilCalled()); }

  std::vector<ResolveRequest> requests;
  bool disconnected = false;

 private:
  // ConflictResolver:
  void Resolve(fidl::InterfaceHandle<PageSnapshot> left_version,
               fidl::InterfaceHandle<PageSnapshot> right_version,
               fidl::InterfaceHandle<PageSnapshot> common_version,
               fidl::InterfaceHandle<MergeResultProvider> result_provider) override {
    requests.emplace_back(loop_controller_, std::move(left_version), std::move(right_version),
                          std::move(common_version), std::move(result_provider));
    resolve_waiter_->GetCallback()();
  }

  LoopController* loop_controller_;
  std::unique_ptr<CallbackWaiter> resolve_waiter_;
  fidl::Binding<ConflictResolver> binding_;
};

// Custom conflict resolver that doesn't resolve any conflicts.
class DummyConflictResolver : public ConflictResolver {
 public:
  explicit DummyConflictResolver(fidl::InterfaceRequest<ConflictResolver> request)
      : binding_(this, std::move(request)) {}
  ~DummyConflictResolver() override = default;

 private:
  // ConflictResolver:
  void Resolve(fidl::InterfaceHandle<PageSnapshot> /*left_version*/,
               fidl::InterfaceHandle<PageSnapshot> /*right_version*/,
               fidl::InterfaceHandle<PageSnapshot> /*common_version*/,
               fidl::InterfaceHandle<MergeResultProvider>
               /*result_provider*/) override {
    // Do nothing.
  }

  fidl::Binding<ConflictResolver> binding_;
};

class TestConflictResolverFactory : public ConflictResolverFactory {
 public:
  TestConflictResolverFactory(LoopController* loop_controller, MergePolicy policy,
                              fidl::InterfaceRequest<ConflictResolverFactory> request,
                              fit::closure on_get_policy_called_callback,
                              zx::duration response_delay = zx::msec(0))
      : loop_controller_(loop_controller),
        new_conflict_resolver_waiter_(loop_controller->NewWaiter()),
        policy_(policy),
        binding_(this, std::move(request)),
        callback_(std::move(on_get_policy_called_callback)),
        response_delay_(response_delay) {}

  uint get_policy_calls = 0;
  std::map<storage::PageId, ConflictResolverImpl> resolvers;

  void set_use_dummy_resolver(bool use_dummy_resolver) { use_dummy_resolver_ = use_dummy_resolver; }

  void RunUntilNewConflictResolverCalled() {
    ASSERT_TRUE(new_conflict_resolver_waiter_->RunUntilCalled());
  }

  void Disconnect() { binding_.Unbind(); }

 private:
  // ConflictResolverFactory:
  void GetPolicy(PageId /*page_id*/, GetPolicyCallback callback) override {
    get_policy_calls++;
    async::PostDelayedTask(
        loop_controller_->dispatcher(),
        [this, callback = std::move(callback)] {
          callback(policy_);
          if (callback_) {
            callback_();
          }
        },
        response_delay_);
  }

  void NewConflictResolver(PageId page_id,
                           fidl::InterfaceRequest<ConflictResolver> resolver) override {
    if (use_dummy_resolver_) {
      dummy_resolvers_.try_emplace(convert::ToString(page_id.id), std::move(resolver));
      new_conflict_resolver_waiter_->GetCallback()();
      return;
    }
    resolvers.try_emplace(convert::ToString(page_id.id), loop_controller_, std::move(resolver));
    new_conflict_resolver_waiter_->GetCallback()();
  }

  LoopController* loop_controller_;
  std::unique_ptr<CallbackWaiter> new_conflict_resolver_waiter_;
  MergePolicy policy_;
  bool use_dummy_resolver_ = false;
  std::map<storage::PageId, DummyConflictResolver> dummy_resolvers_;
  fidl::Binding<ConflictResolverFactory> binding_;
  fit::closure callback_;
  zx::duration response_delay_;
};

// Optional is an object that optionally contains another object.
template <typename T>
class Optional {
 public:
  Optional() : obj_() {}
  explicit Optional(T obj) : valid_(true), obj_(std::move(obj)) {}

  constexpr const T& operator*() const& { return obj_; }

  constexpr const T* operator->() const { return &obj_; }

  constexpr explicit operator bool() const { return valid_; }

 private:
  bool const valid_ = false;
  T const obj_;
};

::testing::AssertionResult ValueMatch(const std::string& type, const ValuePtr& value,
                                      const Optional<std::string>& expected) {
  if (expected) {
    if (!value) {
      return ::testing::AssertionFailure()
             << type << " has no value but expected \"" << *expected << "\".";
    }
    if (ToString(value->value) != *expected) {
      return ::testing::AssertionFailure() << type << " has value \"" << ToString(value->value)
                                           << "\" but expected \"" << *expected << "\".";
    }
  } else if (!expected && value) {
    return ::testing::AssertionFailure()
           << type << " has value \"" << ToString(value->value) << "\" but expected no value.";
  }
  return ::testing::AssertionSuccess();
}

::testing::AssertionResult ChangeMatch(std::string expected_key,
                                       Optional<std::string> expected_base,
                                       Optional<std::string> expected_left,
                                       Optional<std::string> expected_right,
                                       const DiffEntry& entry) {
  convert::ExtendedStringView found_key(entry.key);
  if (expected_key != convert::ExtendedStringView(found_key)) {
    return ::testing::AssertionFailure()
           << "Expected key \"" << expected_key << "\" but found \"" << found_key << "\"";
  }
  ::testing::AssertionResult result = ValueMatch("Base", entry.base, expected_base);
  if (!result) {
    return result;
  }
  result = ValueMatch("Left", entry.left, expected_left);
  if (!result) {
    return result;
  }
  return ValueMatch("Right", entry.right, expected_right);
}

TEST_P(MergingIntegrationTest, Merging) {
  auto instance = NewLedgerAppInstance();
  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  PageWatcherPtr watcher1_ptr;
  auto watcher1_waiter = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher1_waiter->GetCallback());

  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), {}, std::move(watcher1_ptr));

  PageWatcherPtr watcher2_ptr;
  auto watcher2_waiter = NewWaiter();
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher2_waiter->GetCallback());

  PageSnapshotPtr snapshot2;
  page2->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher2_ptr));

  page1->StartTransaction();
  page2->StartTransaction();

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"));

  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"));
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"));

  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that each change is seen by the right watcher.
  page1->Commit();
  ASSERT_TRUE(watcher1_waiter->RunUntilCalled());
  EXPECT_EQ(watcher1.changes_seen, 1u);
  PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 2u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "city");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Paris");
  EXPECT_EQ(convert::ToString(change.changed_entries.at(1).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(1).value), "Alice");

  page2->Commit();
  ASSERT_TRUE(watcher2_waiter->RunUntilCalled());

  EXPECT_EQ(watcher2.changes_seen, 1u);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 2u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Bob");
  EXPECT_EQ(convert::ToString(change.changed_entries.at(1).key), "phone");
  EXPECT_EQ(ToString(change.changed_entries.at(1).value), "0123456789");

  ASSERT_TRUE(watcher1_waiter->RunUntilCalled());
  ASSERT_TRUE(watcher2_waiter->RunUntilCalled());

  // Each change is seen once, and by the correct watcher only.
  EXPECT_EQ(watcher1.changes_seen, 2u);
  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 2u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Bob");
  EXPECT_EQ(convert::ToString(change.changed_entries.at(1).key), "phone");
  EXPECT_EQ(ToString(change.changed_entries.at(1).value), "0123456789");

  EXPECT_EQ(watcher2.changes_seen, 2u);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "city");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Paris");
}

TEST_P(MergingIntegrationTest, MergingWithConflictResolutionFactory) {
  auto instance = NewLedgerAppInstance();
  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Set up a resolver configured not to resolve any conflicts.
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory_waiter = NewWaiter();
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(),
      resolver_factory_waiter->GetCallback());
  resolver_factory->set_use_dummy_resolver(true);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  // Wait for the conflict resolver factory policy to be requested.
  ASSERT_TRUE(resolver_factory_waiter->RunUntilCalled());

  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  PageWatcherPtr watcher1_ptr;
  auto watcher1_waiter = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher1_waiter->GetCallback());
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), {}, std::move(watcher1_ptr));

  PageWatcherPtr watcher2_ptr;
  auto watcher2_waiter = NewWaiter();
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher2_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page2->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher2_ptr));

  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"));
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"));

  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // Verify that each change is seen by the right watcher.
  page1->Commit();

  ASSERT_TRUE(watcher1_waiter->RunUntilCalled());
  EXPECT_EQ(watcher1.changes_seen, 1u);
  PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 2u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "city");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Paris");
  EXPECT_EQ(convert::ToString(change.changed_entries.at(1).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(1).value), "Alice");

  page2->Commit();

  ASSERT_TRUE(watcher2_waiter->RunUntilCalled());
  EXPECT_EQ(watcher2.changes_seen, 1u);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 2u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Bob");
  EXPECT_EQ(convert::ToString(change.changed_entries.at(1).key), "phone");
  EXPECT_EQ(ToString(change.changed_entries.at(1).value), "0123456789");

  // Check that the resolver fectory GetPolicy method is not called.
  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(resolver_factory_waiter->NotCalledYet());
  EXPECT_EQ(resolver_factory->get_policy_calls, 1u);

  // Change the merge strategy, triggering resolution of the conflicts.
  resolver_factory_ptr = nullptr;  // Suppress misc-use-after-move.
  resolver_factory_waiter = NewWaiter();
  resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr.NewRequest(),
      resolver_factory_waiter->GetCallback());
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  ASSERT_TRUE(resolver_factory_waiter->RunUntilCalled());
  ASSERT_TRUE(watcher1_waiter->RunUntilCalled());
  ASSERT_TRUE(watcher2_waiter->RunUntilCalled());

  // Each change is seen once, and by the correct watcher only.
  EXPECT_EQ(watcher1.changes_seen, 2u);
  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 2u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Bob");
  EXPECT_EQ(convert::ToString(change.changed_entries.at(1).key), "phone");
  EXPECT_EQ(ToString(change.changed_entries.at(1).value), "0123456789");

  EXPECT_EQ(watcher2.changes_seen, 2u);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "city");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Paris");

  EXPECT_EQ(resolver_factory->get_policy_calls, 1u);
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionNoConflict) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"));
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetFullDiff(&changes));

  EXPECT_EQ(changes.size(), 4u);
  EXPECT_TRUE(ChangeMatch("city", Optional<std::string>(), Optional<std::string>(),
                          Optional<std::string>("Paris"), changes[0]));
  EXPECT_TRUE(ChangeMatch("email", Optional<std::string>(),
                          Optional<std::string>("alice@example.org"), Optional<std::string>(),
                          changes[1]));
  EXPECT_TRUE(ChangeMatch("name", Optional<std::string>(), Optional<std::string>(),
                          Optional<std::string>("Alice"), changes[2]));
  EXPECT_TRUE(ChangeMatch("phone", Optional<std::string>(), Optional<std::string>("0123456789"),
                          Optional<std::string>(), changes[3]));

  // Common ancestor is empty.
  PageSnapshotPtr snapshot = resolver_impl->requests[0].common_version.Bind();
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(entries.size(), 0u);

  // Prepare the merged values
  std::vector<MergedValue> merged_values;
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("name");
    merged_value.source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("email");
    merged_value.source = ValueSource::DELETE;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("pager");
    merged_value.source = ValueSource::NEW;
    BytesOrReferencePtr value = BytesOrReference::New();
    value->set_bytes(convert::ToArray("pager@example.org"));
    merged_value.new_value = std::move(value);
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page1->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher_ptr));

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(final_entries.size(), 3u);
  EXPECT_EQ(convert::ExtendedStringView(final_entries[0].key), "name");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[1].key), "pager");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[2].key), "phone");
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionMergeValuesOrder) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetFullDiff(&changes));

  EXPECT_EQ(changes.size(), 2u);
  EXPECT_TRUE(ChangeMatch("email", Optional<std::string>(),
                          Optional<std::string>("alice@example.org"), Optional<std::string>(),
                          changes[0]));
  EXPECT_TRUE(ChangeMatch("name", Optional<std::string>(), Optional<std::string>(),
                          Optional<std::string>("Alice"), changes[1]));

  // Common ancestor is empty.
  PageSnapshotPtr snapshot = resolver_impl->requests[0].common_version.Bind();
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(entries.size(), 0u);

  // Prepare the merged values: Initially add, but then delete the entry with
  // key "name".
  std::vector<MergedValue> merged_values;
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("name");
    merged_value.source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("name");
    merged_value.source = ValueSource::DELETE;
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page1->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher_ptr));

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(final_entries.size(), 1u);
  EXPECT_EQ(convert::ExtendedStringView(final_entries[0].key), "email");
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionGetDiffMultiPart) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  int N = 50;
  std::vector<std::string> page1_keys;
  for (int i = 0; i < N; ++i) {
    page1_keys.push_back(absl::StrFormat("page1_key_%02d", i));
    page1->Put(convert::ToArray(page1_keys.back()), convert::ToArray("value"));
  }

  page2->StartTransaction();
  std::vector<std::string> page2_keys;
  for (int i = 0; i < N; ++i) {
    page2_keys.push_back(absl::StrFormat("page2_key_%02d", i));
    page2->Put(convert::ToArray(page2_keys.back()), convert::ToArray("value"));
  }

  // Ensure the first commit is older than the second.
  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  // We now have a conflict, wait for the resolve to be called.
  resolver_factory->RunUntilNewConflictResolverCalled();
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetFullDiff(&changes, 1));

  EXPECT_EQ(changes.size(), 2u * N);
  // Keys are in order, so we expect to have all the page1_key_* keys before the
  // page2_key_* keys.
  for (int i = 0; i < N; ++i) {
    // Left change is the most recent, so the one made on |page2|; right change
    // comes from |page1|.
    EXPECT_TRUE(ChangeMatch(page1_keys[i], Optional<std::string>(), Optional<std::string>(),
                            Optional<std::string>("value"), changes[i]));

    EXPECT_TRUE(ChangeMatch(page2_keys[i], Optional<std::string>(), Optional<std::string>("value"),
                            Optional<std::string>(), changes[N + i]));
  }
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionClosingPipe) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  EXPECT_EQ(resolver_impl->requests.size(), 1u);

  // Kill the resolver
  resolver_factory->resolvers.clear();
  EXPECT_EQ(resolver_factory->resolvers.size(), 0u);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We should ask again for a resolution.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  resolver_impl = &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  // Remove all references to a page:
  page1 = nullptr;
  page2 = nullptr;
  RunLoopFor(zx::msec(500));

  // Resolution should not crash the Ledger
  std::vector<MergedValue> merged_values;
  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));
  RunLoopFor(zx::msec(200));
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionResetFactory) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  EXPECT_FALSE(resolver_impl->disconnected);
  resolver_impl->RunUntilResolveCalled();
  EXPECT_EQ(resolver_impl->requests.size(), 1u);

  // Change the factory.
  ConflictResolverFactoryPtr resolver_factory_ptr2;
  auto resolver_factory2 = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr2.NewRequest(), nullptr);
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr2));

  resolver_factory->Disconnect();

  // Waiting for the conflict resolution request
  resolver_factory2->RunUntilNewConflictResolverCalled();

  // We should ask again for a resolution on a new resolver.
  EXPECT_EQ(resolver_factory2->resolvers.size(), 1u);
  ASSERT_NE(resolver_factory2->resolvers.end(),
            resolver_factory2->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl2 =
      &(resolver_factory2->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl2->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl2->requests.size(), 1u);

  // Remove all references to a page:
  page1 = nullptr;
  page2 = nullptr;
  RunLoopFor(zx::msec(500));

  // Resolution should not crash the Ledger
  std::vector<MergedValue> merged_values;

  EXPECT_TRUE(resolver_impl2->requests[0].Merge(std::move(merged_values)));
  RunLoopFor(zx::msec(200));
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionMultipartMerge) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  // Prepare the merged values
  std::vector<MergedValue> merged_values;
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("name");
    merged_value.source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("email");
    merged_value.source = ValueSource::DELETE;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("pager");
    merged_value.source = ValueSource::NEW;
    BytesOrReferencePtr value = BytesOrReference::New();
    value->set_bytes(convert::ToArray("pager@example.org"));
    merged_value.new_value = std::move(value);
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot;
  page1->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values), MergeType::MULTIPART));

  // Wait for the watcher to be called.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(final_entries.size(), 2u);
  EXPECT_EQ(convert::ExtendedStringView(final_entries[0].key), "name");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[1].key), "pager");
}

TEST_P(MergingIntegrationTest, AutoConflictResolutionNoConflict) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::AUTOMATIC_WITH_FALLBACK, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  // Watch for changes.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page1->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher_ptr));

  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"));
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"));

  waiter = NewWaiter();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  page1->Commit();
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  // We should have seen the first commit at this point.
  EXPECT_EQ(watcher.changes_seen, 1u);

  page2->Commit();

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have an automatically-resolved conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);

  // The waiter is notified of the second change while the resolver has not been
  // asked to resolve anything.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(resolver_impl->requests.size(), 0u);
  EXPECT_EQ(watcher.changes_seen, 2u);

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(final_entries.size(), 4u);
  EXPECT_EQ(convert::ExtendedStringView(final_entries[0].key), "city");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[1].key), "email");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[2].key), "name");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[3].key), "phone");
}

TEST_P(MergingIntegrationTest, AutoConflictResolutionWithConflict) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::AUTOMATIC_WITH_FALLBACK, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  PageId test_page_id;
  auto waiter = NewWaiter();
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page2->Put(convert::ToArray("city"), convert::ToArray("San Francisco"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  waiter = NewWaiter();

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetFullDiff(&changes));

  EXPECT_EQ(changes.size(), 2u);
  // Left change is the most recent, so the one made on |page2|.
  EXPECT_TRUE(ChangeMatch("city", Optional<std::string>(), Optional<std::string>("San Francisco"),
                          Optional<std::string>("Paris"), changes[0]));
  EXPECT_TRUE(ChangeMatch("name", Optional<std::string>(), Optional<std::string>("Alice"),
                          Optional<std::string>(), changes[1]));

  // Common ancestor is empty.
  PageSnapshotPtr snapshot = resolver_impl->requests[0].common_version.Bind();
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(entries.size(), 0u);

  // Prepare the merged values
  std::vector<MergedValue> merged_values;
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("city");
    merged_value.source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page1->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher_ptr));

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(final_entries.size(), 2u);
  EXPECT_EQ(convert::ExtendedStringView(final_entries[0].key), "city");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[1].key), "name");
}

TEST_P(MergingIntegrationTest, AutoConflictResolutionMultipartMerge) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::AUTOMATIC_WITH_FALLBACK, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page2->Put(convert::ToArray("city"), convert::ToArray("San Francisco"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  // Prepare the merged values
  std::vector<MergedValue> merged_values;
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("city");
    merged_value.source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("previous_city");
    merged_value.source = ValueSource::NEW;
    merged_value.new_value = BytesOrReference::New();
    merged_value.new_value->set_bytes(convert::ToArray("San Francisco"));
    merged_values.push_back(std::move(merged_value));
  }

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot;
  page1->GetSnapshot(snapshot.NewRequest(), {}, std::move(watcher_ptr));

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values), MergeType::MULTIPART));

  // Wait for the watcher to be called.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(final_entries.size(), 3u);
  EXPECT_EQ(convert::ExtendedStringView(final_entries[0].key), "city");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[1].key), "name");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[2].key), "previous_city");
}

// Tests a merge in which the right side contains no change (e.g. a change was
// made in a commit, then reverted in another commit).
TEST_P(MergingIntegrationTest, AutoConflictResolutionNoRightChange) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::AUTOMATIC_WITH_FALLBACK, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  // Watch for changes.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot1;
  page1->GetSnapshot(snapshot1.NewRequest(), {}, std::move(watcher_ptr));

  page1->StartTransaction();
  page2->StartTransaction();

  waiter = NewWaiter();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page1->Commit();

  // We should have seen the first commit of page 1.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.changes_seen, 1u);

  page1->StartTransaction();
  page1->Delete(convert::ToArray("name"));
  page1->Commit();

  // We should have seen the second commit of page 1.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher.changes_seen, 2u);

  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"));
  page2->Commit();

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have an automatically-resolved conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  ASSERT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);

  // The waiter is notified of the third change while the resolver has not been
  // asked to resolve anything.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(resolver_impl->requests.size(), 0u);
  EXPECT_EQ(watcher.changes_seen, 3u);

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(final_entries.size(), 1u);
  EXPECT_EQ(convert::ExtendedStringView(final_entries[0].key), "email");
}

TEST_P(MergingIntegrationTest, WaitForCustomMerge) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  // Create a conflict: two pointers to the same page.
  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  // Parallel put in transactions.
  page1->StartTransaction();
  page2->StartTransaction();

  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // Check that we have a resolver and pending conflict resolution request.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  // Try to wait for conflicts resolution.
  auto conflicts_resolved_callback_waiter = NewWaiter();
  ConflictResolutionWaitStatus wait_status;
  page1->WaitForConflictResolution(
      callback::Capture(conflicts_resolved_callback_waiter->GetCallback(), &wait_status));

  // Check that conflicts_resolved_callback is not called, as there are merge
  // requests pending.
  RunLoopFor(zx::msec(250));
  EXPECT_TRUE(conflicts_resolved_callback_waiter->NotCalledYet());

  // Merge manually.
  std::vector<MergedValue> merged_values;
  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values), MergeType::SIMPLE));
  EXPECT_TRUE(conflicts_resolved_callback_waiter->NotCalledYet());

  // Now conflict_resolved_callback can run.
  ASSERT_TRUE(conflicts_resolved_callback_waiter->RunUntilCalled());
  EXPECT_EQ(wait_status, ConflictResolutionWaitStatus::CONFLICTS_RESOLVED);
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionConflictingMerge) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page1 = instance->GetTestPage();
  PageId test_page_id;
  auto waiter = NewWaiter();
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  page1->StartTransaction();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"));
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"));

  page2->StartTransaction();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"));
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"));

  waiter = NewWaiter();
  page1->Commit();
  page1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page2->Commit();
  page2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(resolver_factory->resolvers.size(), 1u);
  EXPECT_NE(resolver_factory->resolvers.end(),
            resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(resolver_impl->requests.size(), 1u);

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetConflictingDiff(&changes));

  EXPECT_EQ(changes.size(), 1u);
  EXPECT_TRUE(ChangeMatch("name", Optional<std::string>(), Optional<std::string>("Bob"),
                          Optional<std::string>("Alice"), changes[0]));

  // Prepare the merged values
  std::vector<MergedValue> merged_values;
  {
    MergedValue merged_value;
    merged_value.key = convert::ToArray("name");
    merged_value.source = ValueSource::RIGHT;
    merged_values.push_back(std::move(merged_value));
  }
  ASSERT_TRUE(resolver_impl->requests[0].MergeNonConflictingEntries());

  // Watch for the change.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page1->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher_ptr));

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(final_entries.size(), 3u);
  EXPECT_EQ(convert::ExtendedStringView(final_entries[0].key), "city");
  EXPECT_EQ(ToString(final_entries[0].value), "Paris");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[1].key), "name");
  EXPECT_EQ(ToString(final_entries[1].value), "Alice");
  EXPECT_EQ(convert::ExtendedStringView(final_entries[2].key), "phone");
  EXPECT_EQ(ToString(final_entries[2].value), "0123456789");
}

// Test that multiple ConflictResolverFactories can be registered, and that
// when registering a new one:
//  - the existing conflict resolvers are not updated
//  - the first factory is still used for new pages
TEST_P(MergingIntegrationTest, ConflictResolverFactoryNotChanged) {
  auto resolver_factory_waiter1 = NewWaiter();
  auto resolver_factory_waiter2 = NewWaiter();
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr1;
  auto resolver_factory1 = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr1.NewRequest(),
      resolver_factory_waiter1->GetCallback());
  ConflictResolverFactoryPtr resolver_factory_ptr2;
  auto resolver_factory2 = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr2.NewRequest(),
      resolver_factory_waiter2->GetCallback());
  LedgerPtr ledger_ptr = instance->GetTestLedger();

  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr1));

  PagePtr page1 = instance->GetTestPage();

  // resolver_factory1 has received one request for page1
  ASSERT_TRUE(resolver_factory_waiter1->RunUntilCalled());
  EXPECT_EQ(resolver_factory1->get_policy_calls, 1u);

  // Connect resolver_factory2 on ledger_ptr1. It does not receive requests
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr2));

  RunLoopFor(zx::msec(250));
  EXPECT_EQ(resolver_factory2->get_policy_calls, 0u);

  PagePtr page2 = instance->GetTestPage();
  // resolver_factory1 has received one request for page2
  ASSERT_TRUE(resolver_factory_waiter1->RunUntilCalled());
  EXPECT_EQ(resolver_factory1->get_policy_calls, 2u);
  EXPECT_EQ(resolver_factory2->get_policy_calls, 0u);
}

// Tests that when a conflict resolution factory disconnects:
//  - the next factory is used
//  - already open pages update their policy
TEST_P(MergingIntegrationTest, ConflictResolutionFactoryFailover) {
  auto resolver_factory_waiter1 = NewWaiter();
  auto resolver_factory_waiter2 = NewWaiter();
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr1;
  auto resolver_factory1 = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr1.NewRequest(),
      resolver_factory_waiter1->GetCallback());
  ConflictResolverFactoryPtr resolver_factory_ptr2;
  auto resolver_factory2 = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr2.NewRequest(),
      resolver_factory_waiter2->GetCallback());
  LedgerPtr ledger_ptr = instance->GetTestLedger();

  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr1));

  PagePtr page1 = instance->GetTestPage();

  // resolver_factory1 has received one request for page1
  ASSERT_TRUE(resolver_factory_waiter1->RunUntilCalled());
  EXPECT_EQ(resolver_factory1->get_policy_calls, 1u);

  // Connect resolver_factory2 on ledger_ptr1. It does not receive requests
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr2));

  // Disconnect resolver_factory1
  resolver_factory1->Disconnect();
  ASSERT_TRUE(resolver_factory_waiter2->RunUntilCalled());
  EXPECT_EQ(resolver_factory2->get_policy_calls, 1u);

  PagePtr page2 = instance->GetTestPage();
  // resolver_factory2 has received one request for page2
  ASSERT_TRUE(resolver_factory_waiter2->RunUntilCalled());
  EXPECT_EQ(resolver_factory2->get_policy_calls, 2u);
}

// Tests that when a conflict resolution factory disconnects, already
// open pages still get their conflicts resolved
TEST_P(MergingIntegrationTest, ConflictResolutionFactoryUnavailableMergingContinues) {
  auto resolver_factory_waiter = NewWaiter();
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr.NewRequest(),
      resolver_factory_waiter->GetCallback());
  LedgerPtr ledger_ptr = instance->GetTestLedger();

  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  PagePtr page_conn1 = instance->GetTestPage();
  PageId test_page_id;
  auto waiter = NewWaiter();
  page_conn1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page_conn2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  // wait for the conflict resolver to be set up, then disconnect
  ASSERT_TRUE(resolver_factory_waiter->RunUntilCalled());
  resolver_factory->Disconnect();

  PageWatcherPtr watcher1_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot1;
  page_conn1->GetSnapshot(snapshot1.NewRequest(), {}, std::move(watcher1_ptr));

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page_conn2->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher2_ptr));

  page_conn1->StartTransaction();
  page_conn1->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  page_conn2->StartTransaction();
  page_conn2->Put(convert::ToArray("name"), convert::ToArray("Bob"));

  waiter = NewWaiter();
  page_conn1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page_conn2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  page_conn1->Commit();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher1.changes_seen, 1u);
  PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Alice");

  page_conn2->Commit();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher2.changes_seen, 1u);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Bob");

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  PageSnapshotPtr snapshot3;
  page_conn1->GetSnapshot(snapshot3.NewRequest(), {}, nullptr);

  waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_GetInline_Result result1;
  snapshot3->GetInline(convert::ToArray("name"),
                       callback::Capture(waiter->GetCallback(), &result1));
  ASSERT_TRUE(waiter->RunUntilCalled());

  PageSnapshotPtr snapshot4;
  page_conn1->GetSnapshot(snapshot4.NewRequest(), {}, nullptr);

  fuchsia::ledger::PageSnapshot_GetInline_Result result2;
  waiter = NewWaiter();
  snapshot4->GetInline(convert::ToArray("name"),
                       callback::Capture(waiter->GetCallback(), &result2));
  ASSERT_TRUE(waiter->RunUntilCalled());

  ASSERT_TRUE(result1.is_response());
  ASSERT_TRUE(result2.is_response());
  EXPECT_EQ(convert::ToString(result2.response().value.value),
            convert::ToString(result1.response().value.value));
}

// Tests that pages opened after disconnection of a conflict resolver
// factory do not see their conflict resolved, including if another connection
// is present with no conflict resolution set
TEST_P(MergingIntegrationTest, ConflictResolutionFactoryUnavailableNewPagesMergeBlocked) {
  auto resolver_factory_waiter = NewWaiter();
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr.NewRequest(),
      resolver_factory_waiter->GetCallback());
  LedgerPtr ledger_ptr = instance->GetTestLedger();

  // Open another connection to check that its (null) strategy is not used
  LedgerPtr ledger_ptr2 = instance->GetTestLedger();

  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr));

  resolver_factory->Disconnect();

  PagePtr page_conn1 = instance->GetTestPage();
  PageId test_page_id;
  auto waiter = NewWaiter();
  page_conn1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  ASSERT_TRUE(waiter->RunUntilCalled());
  PagePtr page_conn2 = instance->GetPage(fidl::MakeOptional(test_page_id));

  PageWatcherPtr watcher1_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot1;
  page_conn1->GetSnapshot(snapshot1.NewRequest(), {}, std::move(watcher1_ptr));

  PageWatcherPtr watcher2_ptr;
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  page_conn2->GetSnapshot(snapshot2.NewRequest(), {}, std::move(watcher2_ptr));

  page_conn1->StartTransaction();
  page_conn1->Put(convert::ToArray("name"), convert::ToArray("Alice"));

  page_conn2->StartTransaction();
  page_conn2->Put(convert::ToArray("name"), convert::ToArray("Bob"));

  waiter = NewWaiter();
  page_conn1->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());
  waiter = NewWaiter();
  page_conn2->Sync(waiter->GetCallback());
  ASSERT_TRUE(waiter->RunUntilCalled());

  page_conn1->Commit();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher1.changes_seen, 1u);
  PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Alice");

  page_conn2->Commit();

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());
  EXPECT_EQ(watcher2.changes_seen, 1u);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(change.changed_entries.size(), 1u);
  EXPECT_EQ(convert::ToString(change.changed_entries.at(0).key), "name");
  EXPECT_EQ(ToString(change.changed_entries.at(0).value), "Bob");

  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(watcher_waiter->NotCalledYet());

  auto resolver_factory_waiter2 = NewWaiter();
  ConflictResolverFactoryPtr resolver_factory_ptr2;
  auto resolver_factory2 = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr2.NewRequest(),
      resolver_factory_waiter2->GetCallback());
  ledger_ptr->SetConflictResolverFactory(std::move(resolver_factory_ptr2));

  ASSERT_TRUE(watcher_waiter->RunUntilCalled());

  PageSnapshotPtr snapshot3;
  page_conn1->GetSnapshot(snapshot3.NewRequest(), {}, nullptr);

  waiter = NewWaiter();
  fuchsia::ledger::PageSnapshot_GetInline_Result result1;
  snapshot3->GetInline(convert::ToArray("name"),
                       callback::Capture(waiter->GetCallback(), &result1));
  ASSERT_TRUE(waiter->RunUntilCalled());

  PageSnapshotPtr snapshot4;
  page_conn1->GetSnapshot(snapshot4.NewRequest(), {}, nullptr);

  fuchsia::ledger::PageSnapshot_GetInline_Result result2;
  waiter = NewWaiter();
  snapshot4->GetInline(convert::ToArray("name"),
                       callback::Capture(waiter->GetCallback(), &result2));
  ASSERT_TRUE(waiter->RunUntilCalled());

  ASSERT_TRUE(result1.is_response());
  ASSERT_TRUE(result2.is_response());
  EXPECT_EQ(convert::ToString(result2.response().value.value),
            convert::ToString(result1.response().value.value));
}

INSTANTIATE_TEST_SUITE_P(MergingIntegrationTest, MergingIntegrationTest,
                         ::testing::ValuesIn(GetLedgerAppInstanceFactoryBuilders()),
                         PrintLedgerAppInstanceFactoryBuilder());

}  // namespace
}  // namespace ledger
