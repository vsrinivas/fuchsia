// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/btree/diff.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/impl/btree/builder.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace btree {
namespace {

using ::testing::SizeIs;

std::unique_ptr<Entry> CreateEntryPtr(std::string key, ObjectIdentifier object_identifier,
                                      KeyPriority priority, std::string entry_id) {
  auto e = std::make_unique<Entry>();
  e->key = key;
  e->object_identifier = std::move(object_identifier);
  e->priority = priority;
  e->entry_id = entry_id;
  return e;
}

std::unique_ptr<Entry> CreateEntryPtr() { return std::unique_ptr<Entry>(); }

class FakePageStorageValidDigest : public fake::FakePageStorage {
 public:
  using fake::FakePageStorage::FakePageStorage;

 protected:
  ObjectDigest FakeDigest(absl::string_view content) const override {
    // BTree code needs storage to return valid digests.
    return MakeObjectDigest(convert::ToString(content));
  }
};

class DiffTest : public StorageTest {
 public:
  DiffTest() : fake_storage_(&environment_, "page_id") {}

  DiffTest(const DiffTest&) = delete;
  DiffTest& operator=(const DiffTest&) = delete;
  ~DiffTest() override = default;

 protected:
  PageStorage* GetStorage() override { return &fake_storage_; }

  ObjectIdentifier CreateTree(const std::vector<EntryChange>& entries) {
    ObjectIdentifier root_identifier;
    EXPECT_TRUE(GetEmptyNodeIdentifier(&root_identifier));

    ObjectIdentifier identifier;
    EXPECT_TRUE(CreateTreeFromChanges(root_identifier, entries, &identifier));
    return identifier;
  }

  FakePageStorageValidDigest fake_storage_;
};

TEST_F(DiffTest, ForEachDiff) {
  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::vector<EntryChange> other_changes;
  // Update value for key01.
  other_changes.push_back(EntryChange{
      Entry{"key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")}, false});
  // Add entry key255.
  other_changes.push_back(
      EntryChange{Entry{"key255", object_identifier, KeyPriority::LAZY, EntryId("id255")}, false});
  // Remove entry key40.
  other_changes.push_back(EntryChange{Entry{"key40", {}, KeyPriority::LAZY, EntryId()}, true});
  ObjectIdentifier other_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, other_changes, &other_root_identifier));

  // ForEachDiff should return all changes just applied.
  bool called;
  Status status;
  size_t current_change = 0;
  ForEachDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "",
      [&other_changes, &current_change](EntryChange e) {
        EXPECT_EQ(e.deleted, other_changes[current_change].deleted);
        if (e.deleted) {
          EXPECT_EQ(e.entry.key, other_changes[current_change].entry.key);
        } else {
          EXPECT_EQ(e.entry, other_changes[current_change].entry);
        }
        ++current_change;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_EQ(current_change, other_changes.size());
}

TEST_F(DiffTest, ForEachDiffWithMinKey) {
  // Expected base tree layout (XX is key "keyXX"):
  //                     [50]
  //                   /     \
  //       [03, 07, 30]      [65, 76]
  //     /
  // [01, 02]
  std::vector<EntryChange> base_entries;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({1, 2, 3, 7, 30, 50, 65, 76}), &base_entries));
  // Expected other tree layout (XX is key "keyXX"):
  //               [50, 75]
  //             /    |    \
  //    [03, 07, 30] [65]  [76]
  //     /           /
  // [01, 02]      [51]
  std::vector<EntryChange> changes;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({51, 75}), &changes));

  bool called;
  Status status;
  ObjectIdentifier base_root_identifier = CreateTree(base_entries);
  ObjectIdentifier other_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, changes, &other_root_identifier));

  // ForEachDiff with a "key0" as min_key should return both changes.
  size_t current_change = 0;
  ForEachDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "key0",
      [&changes, &current_change](EntryChange e) {
        EXPECT_EQ(e.entry, changes[current_change++].entry);
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_EQ(current_change, changes.size());

  // With "key60" as min_key, only key75 should be returned.
  ForEachDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "key60",
      [&changes](EntryChange e) {
        EXPECT_EQ(e.entry, changes[1].entry);
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
}

TEST_F(DiffTest, ForEachDiffWithMinKeySkipNodes) {
  // Expected base tree layout (XX is key "keyXX"):
  //       [03, 07, 30]
  //     /
  // [01, 02]
  std::vector<EntryChange> base_entries;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({1, 2, 3, 7, 30}), &base_entries));
  // Expected other tree layout (XX is key "keyXX"):
  //               [50]
  //             /
  //    [03, 07, 30]
  //     /
  // [01, 02]
  std::vector<EntryChange> changes;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({50}), &changes));

  bool called;
  Status status;
  ObjectIdentifier base_root_identifier = CreateTree(base_entries);
  ObjectIdentifier other_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, changes, &other_root_identifier));

  ForEachDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "key01",
      [&changes](EntryChange e) {
        EXPECT_EQ(e.entry, changes[0].entry);
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
}

TEST_F(DiffTest, ForEachDiffPriorityChange) {
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);
  Entry base_entry = base_changes[10].entry;

  std::vector<EntryChange> other_changes;
  // Update priority for a key.
  other_changes.push_back(EntryChange{
      Entry{base_entry.key, base_entry.object_identifier, KeyPriority::LAZY, EntryId("id")},
      false});

  bool called;
  Status status;
  ObjectIdentifier other_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, other_changes, &other_root_identifier));

  // ForEachDiff should return all changes just applied.
  size_t change_count = 0;
  EntryChange actual_change;
  ForEachDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "",
      [&actual_change, &change_count](EntryChange e) {
        actual_change = e;
        ++change_count;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_EQ(change_count, 1u);
  EXPECT_FALSE(actual_change.deleted);
  EXPECT_EQ(actual_change.entry.key, base_entry.key);
  EXPECT_EQ(actual_change.entry.object_identifier, base_entry.object_identifier);
  EXPECT_EQ(actual_change.entry.priority, KeyPriority::LAZY);
}

TEST_F(DiffTest, ForEachDiffEntryIdChange) {
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  std::vector<EntryChange> other_changes = base_changes;
  other_changes[10].entry.entry_id = "other_entry_id";
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);
  ObjectIdentifier other_root_identifier = CreateTree(other_changes);

  // ForEachDiff should return no changes.
  bool called;
  Status status;
  size_t change_count = 0;
  ForEachDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "",
      [&change_count](EntryChange e) {
        ++change_count;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_EQ(change_count, 0u);
}

TEST_F(DiffTest, ForEachTwoWayDiff) {
  // Construct a tree with 50 entries ("key00" to "key49").
  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("new_value", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::vector<EntryChange> other_changes;
  // Update value for key01.
  other_changes.push_back(EntryChange{
      Entry{"key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")}, false});
  // Add entry key255.
  other_changes.push_back(
      EntryChange{Entry{"key255", object_identifier, KeyPriority::LAZY, EntryId("id255")}, false});
  // Remove entry key40.
  other_changes.push_back(EntryChange{Entry{"key40", {}, KeyPriority::LAZY, EntryId()}, true});
  ObjectIdentifier other_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, other_changes, &other_root_identifier));

  // ForEachTwoWayDiff should return all changes just applied.
  bool called;
  Status status;
  std::vector<TwoWayChange> found_changes;
  ForEachTwoWayDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "",
      [&found_changes](TwoWayChange e) {
        found_changes.push_back(std::move(e));
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);

  EXPECT_THAT(found_changes, SizeIs(other_changes.size()));

  // Updating key01 was the first change.
  ASSERT_NE(found_changes[0].base, nullptr);
  EXPECT_EQ(*found_changes[0].base, base_changes[1].entry);
  ASSERT_NE(found_changes[0].target, nullptr);
  EXPECT_EQ(*found_changes[0].target, other_changes[0].entry);

  // Inserting key255 was the second change:
  EXPECT_EQ(found_changes[1].base, nullptr);
  ASSERT_NE(found_changes[1].target, nullptr);
  EXPECT_EQ(*(found_changes[1].target), other_changes[1].entry);

  // Removing key40 was the last change:
  ASSERT_NE(found_changes[2].base, nullptr);
  EXPECT_EQ(*(found_changes[2].base), base_changes[40].entry);
  EXPECT_EQ(found_changes[2].target, nullptr);
}

TEST_F(DiffTest, ForEachTwoWayDiffMinKey) {
  // Expected base tree layout (XX is key "keyXX"):
  //                     [50]
  //                   /     \
  //       [03, 07, 30]      [65, 76]
  //     /
  // [01, 02]
  std::vector<EntryChange> base_entries;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({1, 2, 3, 7, 30, 50, 65, 76}), &base_entries));
  // Expected other tree layout (XX is key "keyXX"):
  //               [50, 75]
  //             /    |    \
  //    [03, 07, 30] [65]  [76]
  //     /           /
  // [01, 02]      [51]
  std::vector<EntryChange> changes;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({51, 75}), &changes));

  bool called;
  Status status;
  ObjectIdentifier base_root_identifier = CreateTree(base_entries);
  ObjectIdentifier other_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, changes, &other_root_identifier));

  // ForEachTwoWayDiff with "key60" as min_key, only key75 should be returned.
  int change_count = 0;
  ForEachTwoWayDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "key60",
      [&change_count, &changes](TwoWayChange e) {
        change_count++;
        EXPECT_EQ(e.base, nullptr);
        EXPECT_NE(e.target, nullptr);
        EXPECT_EQ(*e.target, changes[1].entry);
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_EQ(change_count, 1);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
}

TEST_F(DiffTest, ForEachTwoWayDiffEntryIdChange) {
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  std::vector<EntryChange> other_changes = base_changes;
  other_changes[10].entry.entry_id = "other_entry_id";
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);
  ObjectIdentifier other_root_identifier = CreateTree(other_changes);

  // ForEachTwoWayDiff should return one change.
  bool called;
  Status status;
  size_t change_count = 0;
  TwoWayChange actual_change;
  ForEachTwoWayDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {other_root_identifier, PageStorage::Location::Local()}, "",
      [&actual_change, &change_count](TwoWayChange e) {
        actual_change = std::move(e);
        ++change_count;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_EQ(change_count, 1u);

  ASSERT_TRUE(actual_change.base);
  EXPECT_EQ(*actual_change.base, base_changes[10].entry);
  ASSERT_TRUE(actual_change.target);
  EXPECT_EQ(*actual_change.target, other_changes[10].entry);
}

TEST_F(DiffTest, ForEachThreeWayDiff) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_object01_identifier = base_changes[1].entry.object_identifier;
  ObjectIdentifier base_object02_identifier = base_changes[2].entry.object_identifier;
  ObjectIdentifier base_object03_identifier = base_changes[3].entry.object_identifier;
  ObjectIdentifier base_object40_identifier = base_changes[40].entry.object_identifier;
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key01.
  left_changes.push_back(EntryChange{
      Entry{"key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")}, false});
  // Update value for key03.
  left_changes.push_back(EntryChange{
      Entry{"key03", object_identifier, KeyPriority::LAZY, EntryId("id03_left")}, false});
  // Add entry key255.
  left_changes.push_back(
      EntryChange{Entry{"key255", object_identifier, KeyPriority::LAZY, EntryId("id255")}, false});
  // Remove entry key40.
  left_changes.push_back(EntryChange{Entry{"key40", {}, KeyPriority::LAZY, EntryId()}, true});

  ObjectIdentifier left_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, left_changes, &left_root_identifier));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectIdentifier object_identifier2 = object2->GetIdentifier();
  std::vector<EntryChange> right_changes;
  // Update to same value for key01.
  right_changes.push_back(EntryChange{
      Entry{"key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")}, false});
  // Update to different value for key02
  right_changes.push_back(EntryChange{
      Entry{"key02", object_identifier2, KeyPriority::LAZY, EntryId("id02_new")}, false});
  // Update to same value for key03 with different entry id.
  right_changes.push_back(EntryChange{
      Entry{"key03", object_identifier, KeyPriority::LAZY, EntryId("id03_right")}, false});
  // Add entry key258.
  right_changes.push_back(
      EntryChange{Entry{"key258", object_identifier, KeyPriority::LAZY, EntryId("id258")}, false});

  ObjectIdentifier right_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, right_changes, &right_root_identifier));

  std::vector<ThreeWayChange> expected_three_way_changes;
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key01", base_object01_identifier, KeyPriority::EAGER, EntryId("id_01")),
      CreateEntryPtr("key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")),
      CreateEntryPtr("key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new"))});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key02", base_object02_identifier, KeyPriority::EAGER, EntryId("id_02")),
      CreateEntryPtr("key02", base_object02_identifier, KeyPriority::EAGER, EntryId("id_02")),
      CreateEntryPtr("key02", object_identifier2, KeyPriority::LAZY, EntryId("id02_new"))});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key03", base_object03_identifier, KeyPriority::EAGER, EntryId("id_03")),
      CreateEntryPtr("key03", object_identifier, KeyPriority::LAZY, EntryId("id03_left")),
      CreateEntryPtr("key03", object_identifier, KeyPriority::LAZY, EntryId("id03_right"))});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(),
      CreateEntryPtr("key255", object_identifier, KeyPriority::LAZY, EntryId("id255")),
      CreateEntryPtr()});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key258", object_identifier, KeyPriority::LAZY, EntryId("id258"))});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key40", base_object40_identifier, KeyPriority::EAGER, EntryId("id_40")),
      CreateEntryPtr(),
      CreateEntryPtr("key40", base_object40_identifier, KeyPriority::EAGER, EntryId("id_40"))});

  bool called;
  Status status;
  unsigned int current_change = 0;
  ForEachThreeWayDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {left_root_identifier, PageStorage::Location::Local()},
      {right_root_identifier, PageStorage::Location::Local()}, "",
      [&expected_three_way_changes, &current_change](ThreeWayChange e) {
        EXPECT_LT(current_change, expected_three_way_changes.size());
        if (current_change >= expected_three_way_changes.size()) {
          return false;
        }
        EXPECT_EQ(e, expected_three_way_changes[current_change]);
        current_change++;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_EQ(expected_three_way_changes.size(), current_change);
}

TEST_F(DiffTest, ForEachThreeWayDiffMinKey) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_object01_identifier = base_changes[1].entry.object_identifier;
  ObjectIdentifier base_object02_identifier = base_changes[2].entry.object_identifier;
  ObjectIdentifier base_object40_identifier = base_changes[40].entry.object_identifier;
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key01.
  left_changes.push_back(EntryChange{
      Entry{"key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")}, false});
  // Add entry key255.
  left_changes.push_back(
      EntryChange{Entry{"key255", object_identifier, KeyPriority::LAZY, EntryId("id255")}, false});
  // Remove entry key40.
  left_changes.push_back(EntryChange{Entry{"key40", {}, KeyPriority::LAZY, EntryId("id40")}, true});

  ObjectIdentifier left_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, left_changes, &left_root_identifier));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectIdentifier object_identifier2 = object2->GetIdentifier();
  std::vector<EntryChange> right_changes;
  // Update to same value for key01.
  right_changes.push_back(EntryChange{
      Entry{"key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")}, false});
  // Update to different value for key02
  right_changes.push_back(EntryChange{
      Entry{"key02", object_identifier2, KeyPriority::LAZY, EntryId("id02_new")}, false});
  // Add entry key258.
  right_changes.push_back(
      EntryChange{Entry{"key258", object_identifier, KeyPriority::LAZY, EntryId("id258")}, false});

  ObjectIdentifier right_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, right_changes, &right_root_identifier));

  std::vector<ThreeWayChange> expected_three_way_changes;
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key258", object_identifier, KeyPriority::LAZY, EntryId("id258"))});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key40", base_object40_identifier, KeyPriority::EAGER, EntryId("id_40")),
      CreateEntryPtr(),
      CreateEntryPtr("key40", base_object40_identifier, KeyPriority::EAGER, EntryId("id_40"))});

  bool called;
  Status status;
  unsigned int current_change = 0;
  ForEachThreeWayDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {left_root_identifier, PageStorage::Location::Local()},
      {right_root_identifier, PageStorage::Location::Local()}, "key257",
      [&expected_three_way_changes, &current_change](ThreeWayChange e) {
        EXPECT_LT(current_change, expected_three_way_changes.size());
        if (current_change >= expected_three_way_changes.size()) {
          return false;
        }
        EXPECT_EQ(e, expected_three_way_changes[current_change]);
        current_change++;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_EQ(expected_three_way_changes.size(), current_change);
}

TEST_F(DiffTest, ForEachThreeWayDiffNoDiff) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_object01_identifier = base_changes[1].entry.object_identifier;
  ObjectIdentifier base_object02_identifier = base_changes[2].entry.object_identifier;
  ObjectIdentifier base_object40_identifier = base_changes[40].entry.object_identifier;
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key01.
  left_changes.push_back(EntryChange{
      Entry{"key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")}, false});
  // Add entry key255.
  left_changes.push_back(
      EntryChange{Entry{"key255", object_identifier, KeyPriority::LAZY, EntryId("id255")}, false});
  // Remove entry key40.
  left_changes.push_back(EntryChange{Entry{"key40", {}, KeyPriority::LAZY, EntryId("id40")}, true});

  ObjectIdentifier left_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, left_changes, &left_root_identifier));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectIdentifier object_identifier2 = object2->GetIdentifier();
  std::vector<EntryChange> right_changes;
  // Update to same value for key01.
  right_changes.push_back(EntryChange{
      Entry{"key01", object_identifier, KeyPriority::LAZY, EntryId("id01_new")}, false});
  // Update to different value for key2
  right_changes.push_back(EntryChange{
      Entry{"key02", object_identifier2, KeyPriority::LAZY, EntryId("id02_new")}, false});
  // Add entry key258.
  right_changes.push_back(
      EntryChange{Entry{"key258", object_identifier, KeyPriority::LAZY, EntryId("id258")}, false});

  ObjectIdentifier right_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, right_changes, &right_root_identifier));

  bool called;
  Status status;
  // No change is expected.
  ForEachThreeWayDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {left_root_identifier, PageStorage::Location::Local()},
      {right_root_identifier, PageStorage::Location::Local()}, "key5",
      [](ThreeWayChange e) {
        ADD_FAILURE();
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
}

TEST_F(DiffTest, ForEachThreeWayNoBaseChange) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::unique_ptr<const Object> object1, object2, object3, object4;
  ASSERT_TRUE(AddObject("change1", &object1));
  ObjectIdentifier object1_identifier = object1->GetIdentifier();
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectIdentifier object2_identifier = object2->GetIdentifier();
  ASSERT_TRUE(AddObject("change3", &object3));
  ObjectIdentifier object3_identifier = object3->GetIdentifier();
  ASSERT_TRUE(AddObject("change4", &object4));
  ObjectIdentifier object4_identifier = object4->GetIdentifier();

  // Left tree.
  std::vector<EntryChange> left_changes;
  left_changes.push_back(
      EntryChange{Entry{"key01", object1_identifier, KeyPriority::EAGER, EntryId("id01")}, false});
  left_changes.push_back(
      EntryChange{Entry{"key03", object3_identifier, KeyPriority::EAGER, EntryId("id03")}, false});

  ObjectIdentifier left_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, left_changes, &left_root_identifier));

  // Right tree.
  std::vector<EntryChange> right_changes;
  right_changes.push_back(
      EntryChange{Entry{"key02", object2_identifier, KeyPriority::EAGER, EntryId("id02")}, false});
  right_changes.push_back(
      EntryChange{Entry{"key04", object4_identifier, KeyPriority::EAGER, EntryId("id04")}, false});

  ObjectIdentifier right_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, right_changes, &right_root_identifier));

  std::vector<ThreeWayChange> expected_three_way_changes;
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(),
      CreateEntryPtr("key01", object1_identifier, KeyPriority::EAGER, EntryId("id01")),
      CreateEntryPtr()});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key02", object2_identifier, KeyPriority::EAGER, EntryId("id02"))});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(),
      CreateEntryPtr("key03", object3_identifier, KeyPriority::EAGER, EntryId("id03")),
      CreateEntryPtr()});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key04", object4_identifier, KeyPriority::EAGER, EntryId("id04"))});

  bool called;
  Status status;
  unsigned int current_change = 0;
  ForEachThreeWayDiff(
      environment_.coroutine_service(), &fake_storage_,
      {base_root_identifier, PageStorage::Location::Local()},
      {left_root_identifier, PageStorage::Location::Local()},
      {right_root_identifier, PageStorage::Location::Local()}, "",
      [&expected_three_way_changes, &current_change](ThreeWayChange e) {
        EXPECT_LT(current_change, expected_three_way_changes.size());
        if (current_change >= expected_three_way_changes.size()) {
          return false;
        }
        EXPECT_EQ(e, expected_three_way_changes[current_change]);
        current_change++;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(status, Status::OK);
  EXPECT_EQ(expected_three_way_changes.size(), current_change);
}

}  // namespace
}  // namespace btree
}  // namespace storage
