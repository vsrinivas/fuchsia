// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/inspected_commit.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/inspect/inspect.h"
#include "src/ledger/bin/storage/public/commit.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/storage/testing/id_and_parent_ids_commit.h"
#include "src/ledger/bin/storage/testing/page_storage_empty_impl.h"
#include "src/ledger/bin/testing/inspect.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/vmo/vector.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {
namespace {

using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::UnorderedElementsAreArray;

constexpr absl::string_view kTestTopLevelNodeName = "top-level-of-test node";

constexpr int kMinimumConcurrency = 3;
constexpr int kMaximumConcurrency = 30;

// TODO(nathaniel): Deduplicate this duplicated-throughout-a-few-tests utility function into a
// library in... peridot? In the rng namespace?
bool NextBool(rng::Random* random) {
  auto bit_generator = random->NewBitGenerator<bool>();
  return bool(std::uniform_int_distribution(0, 1)(bit_generator));
}

storage::Entry CreateStorageEntry(const std::string& key, uint32_t index) {
  return storage::Entry{key, storage::ObjectIdentifier{index, storage::ObjectDigest(""), nullptr},
                        storage::KeyPriority::EAGER, "This string is not a real storage::EntryId."};
}

// TODO(nathaniel): Deduplicate this duplicated-throughout-a-few-tests utility function.
std::unique_ptr<MergeResolver> GetDummyResolver(Environment* environment,
                                                storage::PageStorage* storage) {
  return std::make_unique<MergeResolver>(
      [] {}, environment, storage,
      std::make_unique<backoff::ExponentialBackoff>(
          zx::sec(0), 1u, zx::sec(0), environment->random()->NewBitGenerator<uint64_t>()));
}

// TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=36298): Deduplicate and canonicalize
// this test substitute.
class SubstitutePageStorage final : public storage::PageStorageEmptyImpl {
 public:
  explicit SubstitutePageStorage(const std::map<std::string, std::vector<uint8_t>>& entries,
                                 rng::Random* random, async_dispatcher_t* dispatcher)
      : random_(random), dispatcher_(dispatcher), fail_(-1) {
    for (const auto& [key, value] : entries) {
      uint32_t index = entries_.size();
      entries_.try_emplace(key, value, index);
      keys_by_index_.emplace(index, key);
    }
  }
  ~SubstitutePageStorage() override = default;

  void fail_after_successful_calls(int64_t successful_call_count) { fail_ = successful_call_count; }

 private:
  // storage::PageStorageEmptyImpl:
  void AddCommitWatcher(storage::CommitWatcher* watcher) override {}
  void RemoveCommitWatcher(storage::CommitWatcher* watcher) override {}
  void GetObjectPart(storage::ObjectIdentifier object_identifier, int64_t offset, int64_t max_size,
                     storage::PageStorage::Location location,
                     fit::function<void(storage::Status, ledger::SizedVmo)> callback) override {
    if (offset != 0) {
      LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
    }
    if (max_size != 1024) {
      LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
    }
    if (location != storage::PageStorage::Location::Local()) {
      LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
    }

    auto implementation = [this, index = object_identifier.key_index(),
                           callback = std::move(callback)] {
      if (fail_ == 0) {
        callback(storage::Status::INTERNAL_ERROR, {});
        return;
      }
      if (fail_ > 0) {
        fail_--;
      }

      auto index_it = keys_by_index_.find(index);
      if (index_it == keys_by_index_.end()) {
        callback(storage::Status::INTERNAL_NOT_FOUND, {});
        return;
      }
      auto value_it = entries_.find(index_it->second);
      if (value_it == entries_.end()) {
        callback(storage::Status::INTERNAL_NOT_FOUND, {});
        return;
      }
      ledger::SizedVmo sized_vmo;
      bool result = ledger::VmoFromVector(value_it->second.first, &sized_vmo);
      LEDGER_DCHECK(result) << "That was really not expected to fail in this test!";
      callback(storage::Status::OK, std::move(sized_vmo));
    };
    if (NextBool(random_)) {
      async::PostTask(dispatcher_, std::move(implementation));
    } else {
      implementation();
    }
  }
  void GetCommitContents(const storage::Commit& commit, std::string min_key,
                         fit::function<bool(storage::Entry)> on_next,
                         fit::function<void(storage::Status)> on_done) override {
    if (!min_key.empty()) {
      LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
    }

    auto implementation = [this, on_next = std::move(on_next), on_done = std::move(on_done)] {
      if (fail_ == 0) {
        on_done(storage::Status::INTERNAL_ERROR);
        return;
      }
      if (fail_ > 0) {
        fail_--;
      }

      // TODO(nathaniel): Randomly delay to a later task (or not) between individual on-next calls
      // and before the on-done call.
      for (const auto& [key, value_and_index] : entries_) {
        if (!on_next(CreateStorageEntry(key, value_and_index.second))) {
          LEDGER_NOTIMPLEMENTED();  // Feel free to implement!
        }
      }
      on_done(storage::Status::OK);
    };
    if (NextBool(random_)) {
      async::PostTask(dispatcher_, std::move(implementation));
    } else {
      implementation();
    }
  }
  void GetEntryFromCommit(const storage::Commit& commit, std::string key,
                          fit::function<void(storage::Status, storage::Entry)> on_done) override {
    auto implementation = [this, key = std::move(key), on_done = std::move(on_done)] {
      if (fail_ == 0) {
        on_done(storage::Status::INTERNAL_ERROR, {});
        return;
      }
      if (fail_ > 0) {
        fail_--;
      }

      auto it = entries_.find(key);
      if (it == entries_.end()) {
        on_done(storage::Status::KEY_NOT_FOUND, {});
        return;
      }

      on_done(storage::Status::OK, CreateStorageEntry(key, it->second.second));
    };
    if (NextBool(random_)) {
      async::PostTask(dispatcher_, std::move(implementation));
    } else {
      implementation();
    }
  }

  std::map<std::string, std::pair<std::vector<uint8_t>, uint32_t>> entries_;
  std::map<uint32_t, std::string> keys_by_index_;
  rng::Random* random_;
  async_dispatcher_t* dispatcher_;

  // The number of calls to complete successfully before terminating calls unsuccessfully. -1 to
  // always complete calls successfully.
  int64_t fail_;
};

class SubstituteInspectablePage : public InspectablePage {
 public:
  explicit SubstituteInspectablePage(std::unique_ptr<ActivePageManager> active_page_manager,
                                     rng::Random* random, async_dispatcher_t* dispatcher)
      : active_page_manager_(std::move(active_page_manager)),
        random_(random),
        dispatcher_(dispatcher) {}

  void NewInspection(fit::function<void(storage::Status status, ExpiringToken token,
                                        ActivePageManager* active_page_manager)>
                         callback) override {
    auto implementation = [this, callback = std::move(callback)] {
      if (active_page_manager_) {
        callback(Status::OK, ExpiringToken(), active_page_manager_.get());
      } else {
        callback(Status::INTERNAL_ERROR, ExpiringToken(), nullptr);
      }
    };
    if (NextBool(random_)) {
      async::PostTask(dispatcher_, std::move(implementation));
    } else {
      implementation();
    }
  }

 private:
  std::unique_ptr<ActivePageManager> active_page_manager_;
  rng::Random* random_;
  async_dispatcher_t* dispatcher_;
};

class InspectedCommitTest : public TestWithEnvironment {
 public:
  InspectedCommitTest() = default;
  InspectedCommitTest(const InspectedCommitTest&) = delete;
  InspectedCommitTest& operator=(const InspectedCommitTest&) = delete;
  ~InspectedCommitTest() override = default;

  // gtest::TestWithEnvironment:
  void SetUp() override {
    top_level_node_ = inspect_deprecated::Node(convert::ToString(kTestTopLevelNodeName));
    attachment_node_ = top_level_node_.CreateChild(
        convert::ToString(kSystemUnderTestAttachmentPointPathComponent));
  }

 protected:
  // TODO(nathaniel): Because we use the ChildrenManager API, we need to do our reads using FIDL,
  // and because we want to use inspect::ReadFromFidl for our reads, we need to have these two
  // objects (one parent, one child, both part of the test, and with the system under test attaching
  // to the child) rather than just one. Even though this is test code this is still a layer of
  // indirection that should be eliminable in Inspect's upcoming "VMO-World".
  inspect_deprecated::Node top_level_node_;
  inspect_deprecated::Node attachment_node_;
};

TEST_F(InspectedCommitTest, OnDiscardableCalledNoChildrenManagement) {
  std::set<storage::CommitId> parents = {
      storage::CommitId("00000000000000000000000000000001", storage::kCommitIdSize),
      storage::CommitId("00000000000000000000000000000002", storage::kCommitIdSize),
      storage::CommitId("00000000000000000000000000000003", storage::kCommitIdSize),
      storage::CommitId("00000000000000000000000000000004", storage::kCommitIdSize),
  };
  storage::CommitId id =
      storage::CommitId("00000000000000000000000000000005", storage::kCommitIdSize);
  bool on_discardable_called;

  std::unique_ptr<storage::IdAndParentIdsCommit> commit =
      std::make_unique<storage::IdAndParentIdsCommit>(id, parents);
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(CommitIdToDisplayName(id));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node), std::move(commit), ExpiringToken(), &inspectable_page);

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  fit::closure detacher = inspected_commit.CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));

  fit::closure first_detacher = inspected_commit.CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  fit::closure second_detacher = inspected_commit.CreateDetacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  first_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);

  second_detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(InspectedCommitTest, GetNames) {
  std::map<std::string, std::vector<uint8_t>> entries = {
      {"one", {1}}, {"two", {2}}, {"three", {3}}};
  std::set<std::string> key_display_names;
  for (const auto& [key, unused_value] : entries) {
    key_display_names.insert(KeyToDisplayName(key));
  }
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      entries, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  std::set<std::string> names;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->GetNames(callback::Capture(callback::SetWhenCalled(&callback_called), &names));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(names, UnorderedElementsAreArray(key_display_names));
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(InspectedCommitTest, ConcurrentGetNames) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t concurrency =
      std::uniform_int_distribution(kMinimumConcurrency, kMaximumConcurrency)(bit_generator);
  std::map<std::string, std::vector<uint8_t>> entries = {
      {"one", {1}}, {"two", {2}}, {"three", {3}}, {"four", {4}}};
  std::set<std::string> key_display_names;
  for (const auto& [key, unused_value] : entries) {
    key_display_names.insert(KeyToDisplayName(key));
  }
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      entries, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  size_t callbacks_called{0};
  std::vector<std::set<std::string>> nameses{concurrency};
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  for (size_t index{0}; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
        ->GetNames(callback::Capture([&] { callbacks_called++; }, &nameses[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  for (const auto& names : nameses) {
    EXPECT_THAT(names, UnorderedElementsAreArray(key_display_names));
  }
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(InspectedCommitTest, Attach) {
  std::map<std::string, std::vector<uint8_t>> entries = {{"one", {1}}, {"two", {2}}};
  std::set<std::string> key_display_names;
  for (const auto& [key, unused_value] : entries) {
    key_display_names.insert(KeyToDisplayName(key));
  }
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      entries, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  fit::closure detacher;
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->Attach(KeyToDisplayName("one"),
               callback::Capture(callback::SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_FALSE(on_discardable_called);

  detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(InspectedCommitTest, AttachAbsentEntry) {
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      std::map<std::string, std::vector<uint8_t>>{{"one", {1}}, {"two", {2}}},
      environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  fit::closure detacher;
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->Attach(KeyToDisplayName("three"),
               callback::Capture(callback::SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_TRUE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(InspectedCommitTest, ConcurrentAttach) {
  std::map<std::string, std::vector<uint8_t>> entries = {
      {"one", {1}},  {"two", {2}}, {"three", {3}}, {"four", {4}},
      {"five", {5}}, {"six", {6}}, {"seven", {7}}};
  std::vector<std::string> key_display_names;
  key_display_names.reserve(entries.size());
  for (const auto& [key, unused_value] : entries) {
    key_display_names.push_back(KeyToDisplayName(key));
  }
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t concurrency =
      std::uniform_int_distribution(kMinimumConcurrency, kMaximumConcurrency)(bit_generator);
  std::vector<std::string> attachment_choices{concurrency};
  for (size_t index{0}; index < concurrency; index++) {
    attachment_choices[index] = key_display_names[std::uniform_int_distribution<size_t>(
        0u, key_display_names.size() - 1)(bit_generator)];
  }

  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      entries, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  size_t callbacks_called{0};
  std::vector<fit::closure> detachers{concurrency};
  bool on_discardable_called;

  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  for (size_t index{0}; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
        ->Attach(attachment_choices[index],
                 callback::Capture([&] { callbacks_called++; }, &detachers[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(detachers, Each(IsTrue()));

  // We expect that the InspectedCommit under test becomes empty when the last detacher is called.
  for (const auto& detacher : detachers) {
    EXPECT_FALSE(on_discardable_called);
    detacher();
    RunLoopUntilIdle();
  }
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(InspectedCommitTest, OnDiscardableCalledSomeChildrenManagement) {
  std::map<std::string, std::vector<uint8_t>> entries = {{"one", {1}}, {"two", {2}}};
  std::set<std::string> key_display_names;
  for (const auto& [key, unused_value] : entries) {
    key_display_names.insert(KeyToDisplayName(key));
  }
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      entries, environment_.random(), test_loop().dispatcher());
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  fit::closure first_commit_detacher = inspected_commit.CreateDetacher();
  EXPECT_FALSE(on_discardable_called);
  fit::closure one_entry_detacher;
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->Attach(KeyToDisplayName("one"),
               callback::Capture(callback::SetWhenCalled(&callback_called), &one_entry_detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(one_entry_detacher);
  EXPECT_FALSE(on_discardable_called);
  fit::closure second_commit_detacher = inspected_commit.CreateDetacher();
  EXPECT_FALSE(on_discardable_called);
  fit::closure two_entry_detacher;
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->Attach(KeyToDisplayName("two"),
               callback::Capture(callback::SetWhenCalled(&callback_called), &two_entry_detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(two_entry_detacher);
  EXPECT_FALSE(on_discardable_called);
  first_commit_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
  one_entry_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
  second_commit_detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
  two_entry_detacher();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(InspectedCommitTest, GetNamesErrorGettingActivePageManager) {
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  std::set<std::string> names;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->GetNames(callback::Capture(callback::SetWhenCalled(&callback_called), &names));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(names, IsEmpty());
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(InspectedCommitTest, GetNamesErrorGettingEntries) {
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      std::map<std::string, std::vector<uint8_t>>{{"one", {1}}, {"two", {2}}},
      environment_.random(), test_loop().dispatcher());
  page_storage->fail_after_successful_calls(0);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  std::set<std::string> names;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->GetNames(callback::Capture(callback::SetWhenCalled(&callback_called), &names));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_THAT(names, IsEmpty());
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(InspectedCommitTest, AttachInvalidName) {
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->Attach("This string is not a valid key display name",
               callback::Capture(callback::SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  // The InspectedCommit under test did not surrender program control during the call to Attach so
  // it never needed to check its emptiness after regaining program control.
  EXPECT_FALSE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&callback_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(InspectedCommitTest, AttachErrorGettingActivePageManager) {
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->Attach(KeyToDisplayName("my happy fun key"),
               callback::Capture(callback::SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_TRUE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(InspectedCommitTest, AttachErrorGettingEntry) {
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage =
      std::make_unique<SubstitutePageStorage>(std::map<std::string, std::vector<uint8_t>>{},
                                              environment_.random(), test_loop().dispatcher());
  page_storage->fail_after_successful_calls(0);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  bool callback_called;
  fit::closure detacher;
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
      ->Attach(KeyToDisplayName("your happy fun key"),
               callback::Capture(callback::SetWhenCalled(&callback_called), &detacher));
  RunLoopUntilIdle();
  ASSERT_TRUE(callback_called);
  EXPECT_TRUE(detacher);
  EXPECT_TRUE(on_discardable_called);

  // The returned detacher is callable but has no discernible effect.
  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  detacher();
  RunLoopUntilIdle();
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(InspectedCommitTest, ConcurrentAttachErrorGettingActivePageManager) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t concurrency =
      std::uniform_int_distribution(kMinimumConcurrency, kMaximumConcurrency)(bit_generator);
  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  SubstituteInspectablePage inspectable_page{nullptr, environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  size_t callbacks_called{0};
  std::vector<fit::closure> detachers{concurrency};
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  for (size_t index{0}; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
        ->Attach(KeyToDisplayName("my happy fun key"),
                 callback::Capture([&] { callbacks_called++; }, &detachers[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(detachers, Each(IsTrue()));
  EXPECT_TRUE(on_discardable_called);

  // The returned detachers are callable but have no discernible effect.
  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  for (const fit::closure& detacher : detachers) {
    detacher();
    EXPECT_FALSE(on_discardable_called);
    RunLoopUntilIdle();
    EXPECT_FALSE(on_discardable_called);
  }
}

TEST_F(InspectedCommitTest, ConcurrentAttachErrorGettingEntry) {
  std::map<std::string, std::vector<uint8_t>> entries = {
      {"one", {1}},  {"two", {2}}, {"three", {3}}, {"four", {4}},
      {"five", {5}}, {"six", {6}}, {"seven", {7}}};
  std::vector<std::string> key_display_names;
  key_display_names.reserve(entries.size());
  for (const auto& [key, unused_value] : entries) {
    key_display_names.push_back(KeyToDisplayName(key));
  }
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t concurrency =
      std::uniform_int_distribution(kMinimumConcurrency, kMaximumConcurrency)(bit_generator);
  std::vector<std::string> attachment_choices{concurrency};
  for (size_t index{0}; index < concurrency; index++) {
    attachment_choices[index] = key_display_names[std::uniform_int_distribution<size_t>(
        0u, key_display_names.size() - 1)(bit_generator)];
  }
  std::set<std::string> chosen_keys{};
  for (const std::string& chosen_key : attachment_choices) {
    chosen_keys.insert(chosen_key);
  }
  // Each successful call to |ActivePageManager::GetValue| makes two calls to the
  // |storage::PageStorage| underlying the |ActivePageManager|, so the |SubstitutePageManager| may
  // make up-to-and-including twice the size of |chosen_keys| minus one and still guarantee that at
  // least one |InspectedCommit::Attach| call will fail to attach an entry.
  size_t successful_storage_call_count =
      std::uniform_int_distribution<size_t>(0, chosen_keys.size() * 2 - 1)(bit_generator);

  inspect_deprecated::Node commit_node = attachment_node_.CreateChild(
      CommitIdToDisplayName(convert::ToString(storage::kFirstPageCommitId)));
  std::unique_ptr<SubstitutePageStorage> page_storage = std::make_unique<SubstitutePageStorage>(
      entries, environment_.random(), test_loop().dispatcher());
  page_storage->fail_after_successful_calls(successful_storage_call_count);
  std::unique_ptr<MergeResolver> merger = GetDummyResolver(&environment_, page_storage.get());
  std::unique_ptr<ActivePageManager> active_page_manager = std::make_unique<ActivePageManager>(
      &environment_, std::move(page_storage), nullptr, std::move(merger),
      ActivePageManager::PageStorageState::NEEDS_SYNC);
  SubstituteInspectablePage inspectable_page{std::move(active_page_manager), environment_.random(),
                                             test_loop().dispatcher()};
  InspectedCommit inspected_commit = InspectedCommit(
      dispatcher(), std::move(commit_node),
      std::make_unique<storage::IdAndParentIdsCommit>(
          convert::ToString(storage::kFirstPageCommitId), std::set<storage::CommitId>()),
      ExpiringToken(), &inspectable_page);
  size_t callbacks_called{0};
  std::vector<fit::closure> detachers{concurrency};
  bool on_discardable_called;

  inspected_commit.SetOnDiscardable(callback::SetWhenCalled(&on_discardable_called));
  for (size_t index{0}; index < concurrency; index++) {
    static_cast<inspect_deprecated::ChildrenManager*>(&inspected_commit)
        ->Attach(attachment_choices[index],
                 callback::Capture([&] { callbacks_called++; }, &detachers[index]));
  }
  RunLoopUntilIdle();
  ASSERT_EQ(callbacks_called, concurrency);
  EXPECT_THAT(detachers, Each(IsTrue()));
  // Because each call to |ActivePageManager::GetValue| makes two calls to the underlying
  // |storage::PageStorage|, for the case of |successful_storage_call_count| being zero or one we
  // know that it is guaranteed that all |InspectedCommit::Attach| calls failed to attach a node and
  // the |InspectedCommit| is guaranteed to have become empty.
  // TODO(https://github.com/google/googletest/issues/2445): Change this to
  // EXPECT_THAT(successful_storage_call_count < 2, IMPLIES(on_discardable_called));
  EXPECT_TRUE(2 <= successful_storage_call_count || on_discardable_called);
  // We expect that the InspectedCommit under test is empty after the last detacher is called.
  // Depending on the randomness with which this test ran it may be empty earlier (it may be empty
  // right now!) but it is only guaranteed to be empty after the last detacher is called.
  for (const auto& detacher : detachers) {
    detacher();
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(on_discardable_called);
}

}  // namespace
}  // namespace ledger
