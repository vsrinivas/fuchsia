// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <utility>
#include <vector>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/callback/capture.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_printf.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/tests/integration/integration_test.h"
#include "peridot/bin/ledger/tests/integration/test_utils.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {
namespace {

class MergingIntegrationTest : public IntegrationTest {
 public:
  MergingIntegrationTest() {}
  ~MergingIntegrationTest() override {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(MergingIntegrationTest);
};

class Watcher : public PageWatcher {
 public:
  Watcher(fidl::InterfaceRequest<PageWatcher> request,
          fit::closure change_callback)
      : binding_(this, std::move(request)),
        change_callback_(std::move(change_callback)) {}

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
  explicit ConflictResolverImpl(
      LedgerAppInstanceFactory::LoopController* loop_controller,
      fidl::InterfaceRequest<ConflictResolver> request)
      : loop_controller_(loop_controller),
        disconnect_waiter_(loop_controller->NewWaiter()),
        resolve_waiter_(loop_controller->NewWaiter()),
        binding_(this, std::move(request)) {
    binding_.set_error_handler([this] {
      this->disconnected = true;
      disconnect_waiter_->GetCallback()();
    });
  }
  ~ConflictResolverImpl() override {}

  struct ResolveRequest {
    fidl::InterfaceHandle<PageSnapshot> left_version;
    fidl::InterfaceHandle<PageSnapshot> right_version;
    fidl::InterfaceHandle<PageSnapshot> common_version;
    MergeResultProviderPtr result_provider;

    ResolveRequest(LedgerAppInstanceFactory::LoopController* loop_controller,
                   fidl::InterfaceHandle<PageSnapshot> left_version,
                   fidl::InterfaceHandle<PageSnapshot> right_version,
                   fidl::InterfaceHandle<PageSnapshot> common_version,
                   fidl::InterfaceHandle<MergeResultProvider> result_provider)
        : left_version(std::move(left_version)),
          right_version(std::move(right_version)),
          common_version(std::move(common_version)),
          result_provider(result_provider.Bind()),
          loop_controller_(loop_controller) {}

    // Returns the full list of changes.
    // Returns the full list of changes between branches and makes sure that at
    // least |min_queries| of partial results are returned before retrieving the
    // complete result for the left and for the right changes.
    ::testing::AssertionResult GetFullDiff(std::vector<DiffEntry>* entries,
                                           size_t min_queries = 0) {
      return GetDiff(
          [this](std::unique_ptr<Token> token,
                 fit::function<void(Status, fidl::VectorPtr<DiffEntry>,
                                    std::unique_ptr<Token>)>
                     callback) mutable {
            result_provider->GetFullDiff(std::move(token), std::move(callback));
          },
          entries, min_queries);
    }

    ::testing::AssertionResult GetConflictingDiff(
        std::vector<DiffEntry>* entries, size_t min_queries = 0) {
      return GetDiff(
          [this](std::unique_ptr<Token> token,
                 fit::function<void(Status, fidl::VectorPtr<DiffEntry>,
                                    std::unique_ptr<Token>)>
                     callback) mutable {
            result_provider->GetConflictingDiff(std::move(token),
                                                std::move(callback));
          },
          entries, min_queries);
    }

    // Resolves the conflict by sending the given merge results. If
    // |merge_type| is MULTIPART, the merge will be send in two parts, each
    // sending half of |results|' elements.
    ::testing::AssertionResult Merge(fidl::VectorPtr<MergedValue> results,
                                     MergeType merge_type = MergeType::SIMPLE) {
      FXL_DCHECK(merge_type == MergeType::SIMPLE || results->size() >= 2);

      if (!result_provider) {
        return ::testing::AssertionFailure()
               << "Merge failed: result_provider is disconnected.";
      }

      if (merge_type == MergeType::SIMPLE) {
        ::testing::AssertionResult merge_status =
            PartialMerge(std::move(results));
        if (!merge_status) {
          return merge_status;
        }
      } else {
        size_t part1_size = results->size() / 2;
        fidl::VectorPtr<MergedValue> part2;
        for (size_t i = part1_size; i < results->size(); ++i) {
          part2.push_back(std::move(results->at(i)));
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

      Status status = Status::UNKNOWN_ERROR;
      auto waiter = loop_controller_->NewWaiter();
      result_provider.set_error_handler(waiter->GetCallback());
      result_provider->Done(callback::Capture(waiter->GetCallback(), &status));
      waiter->RunUntilCalled();
      result_provider.set_error_handler(nullptr);
      if (status != Status::OK) {
        return ::testing::AssertionFailure()
               << "Done failed with status " << fidl::ToUnderlying(status);
      }
      return ::testing::AssertionSuccess();
    }

    ::testing::AssertionResult MergeNonConflictingEntries() {
      Status status = Status::UNKNOWN_ERROR;
      auto waiter = loop_controller_->NewWaiter();
      result_provider.set_error_handler(waiter->GetCallback());
      result_provider->MergeNonConflictingEntries(
          callback::Capture(waiter->GetCallback(), &status));
      waiter->RunUntilCalled();
      result_provider.set_error_handler(nullptr);
      if (status != Status::OK) {
        return ::testing::AssertionFailure()
               << "MergeNonConflictingEntries failed with status "
               << fidl::ToUnderlying(status) << ".";
      }
      return ::testing::AssertionSuccess();
    }

   private:
    ::testing::AssertionResult GetDiff(
        fit::function<
            void(std::unique_ptr<Token>,
                 fit::function<void(Status, fidl::VectorPtr<DiffEntry>,
                                    std::unique_ptr<Token>)>)>
            get_diff,
        std::vector<DiffEntry>* entries, size_t min_queries) {
      entries->resize(0);
      size_t num_queries = 0u;
      Status status;
      std::unique_ptr<Token> token;
      do {
        fidl::VectorPtr<DiffEntry> new_entries;
        status = Status::UNKNOWN_ERROR;
        auto waiter = loop_controller_->NewWaiter();
        result_provider.set_error_handler(waiter->GetCallback());
        get_diff(std::move(token),
                 callback::Capture(waiter->GetCallback(), &status, &new_entries,
                                   &token));
        waiter->RunUntilCalled();
        result_provider.set_error_handler(nullptr);
        if (status != Status::OK && status != Status::PARTIAL_RESULT) {
          return ::testing::AssertionFailure()
                 << "GetDiff failed with status " << fidl::ToUnderlying(status);
        }
        if (!token != (status == Status::OK)) {
          return ::testing::AssertionFailure()
                 << "token is "
                 << (token ? convert::ToString(token->opaque_id) : "null")
                 << ", but status is:" << fidl::ToUnderlying(status);
        }
        entries->insert(entries->end(),
                        std::make_move_iterator(new_entries->begin()),
                        std::make_move_iterator(new_entries->end()));
        ++num_queries;
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
        fidl::VectorPtr<MergedValue> partial_result) {
      Status status = Status::UNKNOWN_ERROR;
      auto waiter = loop_controller_->NewWaiter();
      result_provider.set_error_handler(waiter->GetCallback());
      result_provider->Merge(std::move(partial_result),
                             callback::Capture(waiter->GetCallback(), &status));
      waiter->RunUntilCalled();
      result_provider.set_error_handler(nullptr);
      if (status != Status::OK) {
        return ::testing::AssertionFailure()
               << "Merge failed with status " << fidl::ToUnderlying(status);
      }
      return ::testing::AssertionSuccess();
    }

    LedgerAppInstanceFactory::LoopController* loop_controller_;
  };

  void RunUntilDisconnected() { disconnect_waiter_->RunUntilCalled(); }

  void RunUntilResolveCalled() { resolve_waiter_->RunUntilCalled(); }

  std::vector<ResolveRequest> requests;
  bool disconnected = false;

 private:
  // ConflictResolver:
  void Resolve(
      fidl::InterfaceHandle<PageSnapshot> left_version,
      fidl::InterfaceHandle<PageSnapshot> right_version,
      fidl::InterfaceHandle<PageSnapshot> common_version,
      fidl::InterfaceHandle<MergeResultProvider> result_provider) override {
    requests.emplace_back(loop_controller_, std::move(left_version),
                          std::move(right_version), std::move(common_version),
                          std::move(result_provider));
    resolve_waiter_->GetCallback()();
  }

  LedgerAppInstanceFactory::LoopController* loop_controller_;
  std::unique_ptr<LedgerAppInstanceFactory::CallbackWaiter> disconnect_waiter_;
  std::unique_ptr<LedgerAppInstanceFactory::CallbackWaiter> resolve_waiter_;
  fidl::Binding<ConflictResolver> binding_;
};

// Custom conflict resolver that doesn't resolve any conflicts.
class DummyConflictResolver : public ConflictResolver {
 public:
  explicit DummyConflictResolver(
      fidl::InterfaceRequest<ConflictResolver> request)
      : binding_(this, std::move(request)) {}
  ~DummyConflictResolver() override {}

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
  TestConflictResolverFactory(
      LedgerAppInstanceFactory::LoopController* loop_controller,
      MergePolicy policy,
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

  void set_use_dummy_resolver(bool use_dummy_resolver) {
    use_dummy_resolver_ = use_dummy_resolver;
  }

  void RunUntilNewConflictResolverCalled() {
    new_conflict_resolver_waiter_->RunUntilCalled();
  }

 private:
  // ConflictResolverFactory:
  void GetPolicy(PageId /*page_id*/, GetPolicyCallback callback) override {
    get_policy_calls++;
    async::PostDelayedTask(async_get_default_dispatcher(),
                           [this, callback = std::move(callback)] {
                             callback(policy_);
                             if (callback_) {
                               callback_();
                             }
                           },
                           response_delay_);
  }

  void NewConflictResolver(
      PageId page_id,
      fidl::InterfaceRequest<ConflictResolver> resolver) override {
    if (use_dummy_resolver_) {
      dummy_resolvers_.emplace(
          std::piecewise_construct,
          std::forward_as_tuple(convert::ToString(page_id.id)),
          std::forward_as_tuple(std::move(resolver)));
      new_conflict_resolver_waiter_->GetCallback()();
      return;
    }
    resolvers.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(convert::ToString(page_id.id)),
        std::forward_as_tuple(loop_controller_, std::move(resolver)));
    new_conflict_resolver_waiter_->GetCallback()();
  }

  LedgerAppInstanceFactory::LoopController* loop_controller_;
  std::unique_ptr<LedgerAppInstanceFactory::CallbackWaiter>
      new_conflict_resolver_waiter_;
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

::testing::AssertionResult ValueMatch(const std::string& type,
                                      const ValuePtr& value,
                                      const Optional<std::string>& expected) {
  if (expected) {
    if (!value) {
      return ::testing::AssertionFailure()
             << type << " has no value but expected \"" << *expected << "\".";
    }
    if (ToString(value->value) != *expected) {
      return ::testing::AssertionFailure()
             << type << " has value \"" << ToString(value->value)
             << "\" but expected \"" << *expected << "\".";
    }
  } else if (!expected && value) {
    return ::testing::AssertionFailure()
           << type << " has value \"" << ToString(value->value)
           << "\" but expected no value.";
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
           << "Expected key \"" << expected_key << "\" but found \""
           << found_key << "\"";
  }
  ::testing::AssertionResult result =
      ValueMatch("Base", entry.base, expected_base);
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
  waiter->RunUntilCalled();

  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  PageWatcherPtr watcher1_ptr;
  auto watcher1_waiter = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher1_waiter->GetCallback());

  PageSnapshotPtr snapshot1;
  Status status;
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot1.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher1_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PageWatcherPtr watcher2_ptr;
  auto watcher2_waiter = NewWaiter();
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher2_waiter->GetCallback());

  PageSnapshotPtr snapshot2;
  waiter = NewWaiter();
  page2->GetSnapshot(snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher2_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // Verify that each change is seen by the right watcher.
  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  watcher1_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher1.changes_seen);
  PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change.changed_entries->size());
  EXPECT_EQ("city", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Paris", ToString(change.changed_entries->at(0).value));
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(1).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries->at(1).value));

  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  watcher2_waiter->RunUntilCalled();

  EXPECT_EQ(1u, watcher2.changes_seen);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(2u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Bob", ToString(change.changed_entries->at(0).value));
  EXPECT_EQ("phone", convert::ToString(change.changed_entries->at(1).key));
  EXPECT_EQ("0123456789", ToString(change.changed_entries->at(1).value));

  watcher1_waiter->RunUntilCalled();
  watcher2_waiter->RunUntilCalled();

  // Each change is seen once, and by the correct watcher only.
  EXPECT_EQ(2u, watcher1.changes_seen);
  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Bob", ToString(change.changed_entries->at(0).value));
  EXPECT_EQ("phone", convert::ToString(change.changed_entries->at(1).key));
  EXPECT_EQ("0123456789", ToString(change.changed_entries->at(1).value));

  EXPECT_EQ(2u, watcher2.changes_seen);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("city", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Paris", ToString(change.changed_entries->at(0).value));
}

TEST_P(MergingIntegrationTest, MergingWithConflictResolutionFactory) {
  auto instance = NewLedgerAppInstance();
  PagePtr page1 = instance->GetTestPage();
  auto waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();

  // Set up a resolver configured not to resolve any conflicts.
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory_waiter = NewWaiter();
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(),
      resolver_factory_waiter->GetCallback());
  resolver_factory->set_use_dummy_resolver(true);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  Status status;
  waiter = NewWaiter();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // Wait for the conflict resolver factory policy to be requested.
  resolver_factory_waiter->RunUntilCalled();

  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  PageWatcherPtr watcher1_ptr;
  auto watcher1_waiter = NewWaiter();
  Watcher watcher1(watcher1_ptr.NewRequest(), watcher1_waiter->GetCallback());
  PageSnapshotPtr snapshot1;
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot1.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher1_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PageWatcherPtr watcher2_ptr;
  auto watcher2_waiter = NewWaiter();
  Watcher watcher2(watcher2_ptr.NewRequest(), watcher2_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  waiter = NewWaiter();
  page2->GetSnapshot(snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher2_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // Verify that each change is seen by the right watcher.
  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  watcher1_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher1.changes_seen);
  PageChange change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change.changed_entries->size());
  EXPECT_EQ("city", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Paris", ToString(change.changed_entries->at(0).value));
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(1).key));
  EXPECT_EQ("Alice", ToString(change.changed_entries->at(1).value));

  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  watcher2_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher2.changes_seen);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(2u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Bob", ToString(change.changed_entries->at(0).value));
  EXPECT_EQ("phone", convert::ToString(change.changed_entries->at(1).key));
  EXPECT_EQ("0123456789", ToString(change.changed_entries->at(1).value));

  // Check that the resolver fectory GetPolicy method is not called.
  EXPECT_TRUE(RunLoopWithTimeout());
  EXPECT_TRUE(resolver_factory_waiter->NotCalledYet());
  EXPECT_EQ(1u, resolver_factory->get_policy_calls);

  // Change the merge strategy, triggering resolution of the conflicts.
  resolver_factory_ptr = nullptr;  // Suppress misc-use-after-move.
  resolver_factory_waiter = NewWaiter();
  resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::LAST_ONE_WINS, resolver_factory_ptr.NewRequest(),
      resolver_factory_waiter->GetCallback());
  waiter = NewWaiter();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory_waiter->RunUntilCalled();
  watcher1_waiter->RunUntilCalled();
  watcher2_waiter->RunUntilCalled();

  // Each change is seen once, and by the correct watcher only.
  EXPECT_EQ(2u, watcher1.changes_seen);
  change = std::move(watcher1.last_page_change_);
  ASSERT_EQ(2u, change.changed_entries->size());
  EXPECT_EQ("name", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Bob", ToString(change.changed_entries->at(0).value));
  EXPECT_EQ("phone", convert::ToString(change.changed_entries->at(1).key));
  EXPECT_EQ("0123456789", ToString(change.changed_entries->at(1).value));

  EXPECT_EQ(2u, watcher2.changes_seen);
  change = std::move(watcher2.last_page_change_);
  ASSERT_EQ(1u, change.changed_entries->size());
  EXPECT_EQ("city", convert::ToString(change.changed_entries->at(0).key));
  EXPECT_EQ("Paris", ToString(change.changed_entries->at(0).value));

  EXPECT_EQ(1u, resolver_factory->get_policy_calls);
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionNoConflict) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl->requests.size());

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetFullDiff(&changes));

  EXPECT_EQ(4u, changes.size());
  EXPECT_TRUE(ChangeMatch("city", Optional<std::string>(),
                          Optional<std::string>(),
                          Optional<std::string>("Paris"), changes[0]));
  EXPECT_TRUE(ChangeMatch("email", Optional<std::string>(),
                          Optional<std::string>("alice@example.org"),
                          Optional<std::string>(), changes[1]));
  EXPECT_TRUE(ChangeMatch("name", Optional<std::string>(),
                          Optional<std::string>(),
                          Optional<std::string>("Alice"), changes[2]));
  EXPECT_TRUE(ChangeMatch("phone", Optional<std::string>(),
                          Optional<std::string>("0123456789"),
                          Optional<std::string>(), changes[3]));

  // Common ancestor is empty.
  PageSnapshotPtr snapshot = resolver_impl->requests[0].common_version.Bind();
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(0u, entries.size());

  // Prepare the merged values
  fidl::VectorPtr<MergedValue> merged_values;
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
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  watcher_waiter->RunUntilCalled();

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(3u, final_entries.size());
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[0].key));
  EXPECT_EQ("pager", convert::ExtendedStringView(final_entries[1].key));
  EXPECT_EQ("phone", convert::ExtendedStringView(final_entries[2].key));
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionGetDiffMultiPart) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  int N = 50;
  std::vector<std::string> page1_keys;
  for (int i = 0; i < N; ++i) {
    page1_keys.push_back(fxl::StringPrintf("page1_key_%02d", i));
    waiter = NewWaiter();
    page1->Put(convert::ToArray(page1_keys.back()), convert::ToArray("value"),
               callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(Status::OK, status);
  }

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  std::vector<std::string> page2_keys;
  for (int i = 0; i < N; ++i) {
    page2_keys.push_back(fxl::StringPrintf("page2_key_%02d", i));
    waiter = NewWaiter();
    page2->Put(convert::ToArray(page2_keys.back()), convert::ToArray("value"),
               callback::Capture(waiter->GetCallback(), &status));
    waiter->RunUntilCalled();
    EXPECT_EQ(Status::OK, status);
  }

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // We now have a conflict, wait for the resolve to be called.
  resolver_factory->RunUntilNewConflictResolverCalled();
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl->requests.size());

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetFullDiff(&changes, 1));

  EXPECT_EQ(2u * N, changes.size());
  // Keys are in order, so we expect to have all the page1_key_* keys before the
  // page2_key_* keys.
  for (int i = 0; i < N; ++i) {
    // Left change is the most recent, so the one made on |page2|; right change
    // comes from |page1|.
    EXPECT_TRUE(ChangeMatch(page1_keys[i], Optional<std::string>(),
                            Optional<std::string>(),
                            Optional<std::string>("value"), changes[i]));

    EXPECT_TRUE(ChangeMatch(page2_keys[i], Optional<std::string>(),
                            Optional<std::string>("value"),
                            Optional<std::string>(), changes[N + i]));
  }
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionClosingPipe) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // Kill the resolver
  resolver_factory->resolvers.clear();
  EXPECT_EQ(0u, resolver_factory->resolvers.size());

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We should ask again for a resolution.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl->requests.size());

  // Remove all references to a page:
  page1 = nullptr;
  page2 = nullptr;
  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(500)));

  // Resolution should not crash the Ledger
  fidl::VectorPtr<MergedValue> merged_values =
      fidl::VectorPtr<MergedValue>::New(0);
  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));
  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(200)));
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionResetFactory) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  EXPECT_FALSE(resolver_impl->disconnected);
  resolver_impl->RunUntilResolveCalled();
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // Change the factory.
  ConflictResolverFactoryPtr resolver_factory_ptr2;
  auto resolver_factory2 = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr2.NewRequest(), nullptr);
  waiter = NewWaiter();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr2),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // Waiting for the conflict resolution request and for the disconnect.
  resolver_impl->RunUntilDisconnected();
  resolver_factory2->RunUntilNewConflictResolverCalled();

  // The previous resolver should have been disconnected.
  EXPECT_TRUE(resolver_impl->disconnected);
  // It shouldn't have been called again.
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // We should ask again for a resolution on a new resolver.
  EXPECT_EQ(1u, resolver_factory2->resolvers.size());
  ASSERT_NE(
      resolver_factory2->resolvers.end(),
      resolver_factory2->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl2 =
      &(resolver_factory2->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl2->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl2->requests.size());

  // Remove all references to a page:
  page1 = nullptr;
  page2 = nullptr;
  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(500)));

  // Resolution should not crash the Ledger
  fidl::VectorPtr<MergedValue> merged_values =
      fidl::VectorPtr<MergedValue>::New(0);

  EXPECT_TRUE(resolver_impl2->requests[0].Merge(std::move(merged_values)));
  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(200)));
}

// Tests for a race between setting the new conflict resolver and sending the
// resolution request. Specifically, the resolution request must be sent to the
// new resolver, not the old one.
TEST_P(MergingIntegrationTest,
       CustomConflictResolutionResetFactory_FactoryRace) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  EXPECT_FALSE(resolver_impl->disconnected);
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // Change the factory.
  ConflictResolverFactoryPtr resolver_factory_ptr2;
  auto resolver_factory2 = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr2.NewRequest(), nullptr,
      zx::msec(250));
  waiter = NewWaiter();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr2),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // Waiting for the conflict resolution request and for the disconnect.
  resolver_impl->RunUntilDisconnected();
  resolver_factory2->RunUntilNewConflictResolverCalled();

  // The previous resolver should have been disconnected.
  EXPECT_TRUE(resolver_impl->disconnected);
  // It shouldn't have been called again.
  EXPECT_EQ(1u, resolver_impl->requests.size());

  // We should ask again for a resolution on a new resolver.
  EXPECT_EQ(1u, resolver_factory2->resolvers.size());
  ASSERT_NE(
      resolver_factory2->resolvers.end(),
      resolver_factory2->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl2 =
      &(resolver_factory2->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl2->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl2->requests.size());
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionMultipartMerge) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl->requests.size());

  // Prepare the merged values
  fidl::VectorPtr<MergedValue> merged_values =
      fidl::VectorPtr<MergedValue>::New(0);
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
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values),
                                               MergeType::MULTIPART));

  // Wait for the watcher to be called.
  watcher_waiter->RunUntilCalled();

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(2u, final_entries.size());
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[0].key));
  EXPECT_EQ("pager", convert::ExtendedStringView(final_entries[1].key));
}

TEST_P(MergingIntegrationTest, AutoConflictResolutionNoConflict) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::AUTOMATIC_WITH_FALLBACK,
      resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  // Watch for changes.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot2;
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  watcher_waiter->RunUntilCalled();
  // We should have seen the first commit at this point.
  EXPECT_EQ(1u, watcher.changes_seen);

  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have an automatically-resolved conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);

  // The waiter is notified of the second change while the resolver has not been
  // asked to resolve anything.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(0u, resolver_impl->requests.size());
  EXPECT_EQ(2u, watcher.changes_seen);

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(4u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0].key));
  EXPECT_EQ("email", convert::ExtendedStringView(final_entries[1].key));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[2].key));
  EXPECT_EQ("phone", convert::ExtendedStringView(final_entries[3].key));
}

TEST_P(MergingIntegrationTest, AutoConflictResolutionWithConflict) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::AUTOMATIC_WITH_FALLBACK,
      resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  Status status;
  auto waiter = NewWaiter();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  PageId test_page_id;
  waiter = NewWaiter();
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("city"), convert::ToArray("San Francisco"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl->requests.size());

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetFullDiff(&changes));

  EXPECT_EQ(2u, changes.size());
  // Left change is the most recent, so the one made on |page2|.
  EXPECT_TRUE(ChangeMatch("city", Optional<std::string>(),
                          Optional<std::string>("San Francisco"),
                          Optional<std::string>("Paris"), changes[0]));
  EXPECT_TRUE(ChangeMatch("name", Optional<std::string>(),
                          Optional<std::string>("Alice"),
                          Optional<std::string>(), changes[1]));

  // Common ancestor is empty.
  PageSnapshotPtr snapshot = resolver_impl->requests[0].common_version.Bind();
  auto entries = SnapshotGetEntries(this, &snapshot);
  EXPECT_EQ(0u, entries.size());

  // Prepare the merged values
  fidl::VectorPtr<MergedValue> merged_values =
      fidl::VectorPtr<MergedValue>::New(0);
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
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  watcher_waiter->RunUntilCalled();

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(2u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0].key));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[1].key));
}

TEST_P(MergingIntegrationTest, AutoConflictResolutionMultipartMerge) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::AUTOMATIC_WITH_FALLBACK,
      resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  Status status;
  auto waiter = NewWaiter();
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("city"), convert::ToArray("San Francisco"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl->requests.size());

  // Prepare the merged values
  fidl::VectorPtr<MergedValue> merged_values =
      fidl::VectorPtr<MergedValue>::New(0);
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
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values),
                                               MergeType::MULTIPART));

  // Wait for the watcher to be called.
  watcher_waiter->RunUntilCalled();

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(3u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0].key));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[1].key));
  EXPECT_EQ("previous_city", convert::ExtendedStringView(final_entries[2].key));
}

// Tests a merge in which the right side contains no change (e.g. a change was
// made in a commit, then reverted in another commit).
TEST_P(MergingIntegrationTest, AutoConflictResolutionNoRightChange) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::AUTOMATIC_WITH_FALLBACK,
      resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  // Watch for changes.
  PageWatcherPtr watcher_ptr;
  auto watcher_waiter = NewWaiter();
  Watcher watcher(watcher_ptr.NewRequest(), watcher_waiter->GetCallback());
  PageSnapshotPtr snapshot1;
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot1.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // We should have seen the first commit of page 1.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(1u, watcher.changes_seen);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Delete(convert::ToArray("name"),
                callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // We should have seen the second commit of page 1.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(2u, watcher.changes_seen);

  waiter = NewWaiter();
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have an automatically-resolved conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  ASSERT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);

  // The waiter is notified of the third change while the resolver has not been
  // asked to resolve anything.
  watcher_waiter->RunUntilCalled();
  EXPECT_EQ(0u, resolver_impl->requests.size());
  EXPECT_EQ(3u, watcher.changes_seen);

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(1u, final_entries.size());
  EXPECT_EQ("email", convert::ExtendedStringView(final_entries[0].key));
}

TEST_P(MergingIntegrationTest, WaitForCustomMerge) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  // Create a conflict: two pointers to the same page.
  PagePtr page1 = instance->GetTestPage();
  waiter = NewWaiter();
  PageId test_page_id;
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  // Parallel put in transactions.
  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("email"), convert::ToArray("alice@example.org"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // Check that we have a resolver and pending conflict resolution request.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl->requests.size());

  // Try to wait for conflicts resolution.
  auto conflicts_resolved_callback_waiter = NewWaiter();
  ConflictResolutionWaitStatus wait_status;
  page1->WaitForConflictResolution(callback::Capture(
      conflicts_resolved_callback_waiter->GetCallback(), &wait_status));

  // Check that conflicts_resolved_callback is not called, as there are merge
  // requests pending.
  EXPECT_TRUE(RunLoopWithTimeout(zx::msec(250)));
  EXPECT_TRUE(conflicts_resolved_callback_waiter->NotCalledYet());

  // Merge manually.
  fidl::VectorPtr<MergedValue> merged_values =
      fidl::VectorPtr<MergedValue>::New(0);
  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values),
                                               MergeType::SIMPLE));
  EXPECT_TRUE(conflicts_resolved_callback_waiter->NotCalledYet());

  // Now conflict_resolved_callback can run.
  conflicts_resolved_callback_waiter->RunUntilCalled();
  EXPECT_EQ(ConflictResolutionWaitStatus::CONFLICTS_RESOLVED, wait_status);
}

TEST_P(MergingIntegrationTest, CustomConflictResolutionConflictingMerge) {
  auto instance = NewLedgerAppInstance();
  ConflictResolverFactoryPtr resolver_factory_ptr;
  auto resolver_factory = std::make_unique<TestConflictResolverFactory>(
      this, MergePolicy::CUSTOM, resolver_factory_ptr.NewRequest(), nullptr);
  LedgerPtr ledger_ptr = instance->GetTestLedger();
  auto waiter = NewWaiter();
  Status status;
  ledger_ptr->SetConflictResolverFactory(
      std::move(resolver_factory_ptr),
      callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  PagePtr page1 = instance->GetTestPage();
  PageId test_page_id;
  waiter = NewWaiter();
  page1->GetId(callback::Capture(waiter->GetCallback(), &test_page_id));
  waiter->RunUntilCalled();
  PagePtr page2 =
      instance->GetPage(fidl::MakeOptional(test_page_id), Status::OK);

  waiter = NewWaiter();
  page1->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("name"), convert::ToArray("Alice"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page1->Put(convert::ToArray("city"), convert::ToArray("Paris"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page2->StartTransaction(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("name"), convert::ToArray("Bob"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Put(convert::ToArray("phone"), convert::ToArray("0123456789"),
             callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  waiter = NewWaiter();
  page1->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);
  waiter = NewWaiter();
  page2->Commit(callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  resolver_factory->RunUntilNewConflictResolverCalled();

  // We now have a conflict.
  EXPECT_EQ(1u, resolver_factory->resolvers.size());
  EXPECT_NE(
      resolver_factory->resolvers.end(),
      resolver_factory->resolvers.find(convert::ToString(test_page_id.id)));
  ConflictResolverImpl* resolver_impl =
      &(resolver_factory->resolvers.find(convert::ToString(test_page_id.id))
            ->second);
  resolver_impl->RunUntilResolveCalled();
  ASSERT_EQ(1u, resolver_impl->requests.size());

  std::vector<DiffEntry> changes;
  ASSERT_TRUE(resolver_impl->requests[0].GetConflictingDiff(&changes));

  EXPECT_EQ(1u, changes.size());
  EXPECT_TRUE(ChangeMatch("name", Optional<std::string>(),
                          Optional<std::string>("Bob"),
                          Optional<std::string>("Alice"), changes[0]));

  // Prepare the merged values
  fidl::VectorPtr<MergedValue> merged_values =
      fidl::VectorPtr<MergedValue>::New(0);
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
  waiter = NewWaiter();
  page1->GetSnapshot(snapshot2.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                     std::move(watcher_ptr),
                     callback::Capture(waiter->GetCallback(), &status));
  waiter->RunUntilCalled();
  EXPECT_EQ(Status::OK, status);

  EXPECT_TRUE(resolver_impl->requests[0].Merge(std::move(merged_values)));

  // Wait for the watcher to be called.
  watcher_waiter->RunUntilCalled();

  auto final_entries = SnapshotGetEntries(this, &watcher.last_snapshot_);
  ASSERT_EQ(3u, final_entries.size());
  EXPECT_EQ("city", convert::ExtendedStringView(final_entries[0].key));
  EXPECT_EQ("Paris", ToString(final_entries[0].value));
  EXPECT_EQ("name", convert::ExtendedStringView(final_entries[1].key));
  EXPECT_EQ("Alice", ToString(final_entries[1].value));
  EXPECT_EQ("phone", convert::ExtendedStringView(final_entries[2].key));
  EXPECT_EQ("0123456789", ToString(final_entries[2].value));
}

INSTANTIATE_TEST_CASE_P(MergingIntegrationTest, MergingIntegrationTest,
                        ::testing::ValuesIn(GetLedgerAppInstanceFactories()));

}  // namespace
}  // namespace ledger
