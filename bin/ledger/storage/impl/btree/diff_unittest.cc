// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/diff.h"

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/impl/btree/builder.h"
#include "peridot/bin/ledger/storage/impl/btree/entry_change_iterator.h"
#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {
namespace btree {
namespace {
std::unique_ptr<Entry> CreateEntryPtr(std::string key,
                                      ObjectIdentifier object_identifier,
                                      KeyPriority priority) {
  auto e = std::make_unique<Entry>();
  e->key = key;
  e->object_identifier = std::move(object_identifier);
  e->priority = priority;
  return e;
}

std::unique_ptr<Entry> CreateEntryPtr() { return std::unique_ptr<Entry>(); }

class DiffTest : public StorageTest {
 public:
  DiffTest() : fake_storage_("page_id") {}

  ~DiffTest() override {}

  // Test:
  void SetUp() override {
    StorageTest::SetUp();
    std::srand(0);
  }

 protected:
  PageStorage* GetStorage() override { return &fake_storage_; }

  ObjectIdentifier CreateTree(const std::vector<EntryChange>& entries) {
    ObjectIdentifier root_identifier;
    EXPECT_TRUE(GetEmptyNodeIdentifier(&root_identifier));

    ObjectIdentifier identifier;
    EXPECT_TRUE(CreateTreeFromChanges(root_identifier, entries, &identifier));
    return identifier;
  }

  coroutine::CoroutineServiceImpl coroutine_service_;
  fake::FakePageStorage fake_storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DiffTest);
};

TEST_F(DiffTest, ForEachDiff) {
  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::vector<EntryChange> other_changes;
  // Update value for key1.
  other_changes.push_back(
      EntryChange{Entry{"key1", object_identifier, KeyPriority::LAZY}, false});
  // Add entry key255.
  other_changes.push_back(EntryChange{
      Entry{"key255", object_identifier, KeyPriority::LAZY}, false});
  // Remove entry key40.
  other_changes.push_back(
      EntryChange{Entry{"key40", {}, KeyPriority::LAZY}, true});
  ObjectIdentifier other_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, other_changes,
                                    &other_root_identifier));

  // ForEachDiff should return all changes just applied.
  bool called;
  Status status;
  size_t current_change = 0;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_identifier,
              other_root_identifier, "",
              [&other_changes, &current_change](EntryChange e) {
                EXPECT_EQ(other_changes[current_change].deleted, e.deleted);
                if (e.deleted) {
                  EXPECT_EQ(other_changes[current_change].entry.key,
                            e.entry.key);
                } else {
                  EXPECT_EQ(other_changes[current_change].entry, e.entry);
                }
                ++current_change;
                return true;
              },
              callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(other_changes.size(), current_change);
}

TEST_F(DiffTest, ForEachDiffWithMinKey) {
  // Expected base tree layout (XX is key "keyXX"):
  //                     [50]
  //                   /     \
  //       [03, 07, 30]      [65, 76]
  //     /
  // [01, 02]
  std::vector<EntryChange> base_entries;
  ASSERT_TRUE(CreateEntryChanges(
      std::vector<size_t>({1, 2, 3, 7, 30, 50, 65, 76}), &base_entries));
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
  std::set<ObjectDigest> new_nodes;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, changes,
                                    &other_root_identifier));

  // ForEachDiff with a "key0" as min_key should return both changes.
  size_t current_change = 0;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_identifier,
              other_root_identifier, "key0",
              [&changes, &current_change](EntryChange e) {
                EXPECT_EQ(changes[current_change++].entry, e.entry);
                return true;
              },
              callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(changes.size(), current_change);

  // With "key60" as min_key, only key75 should be returned.
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_identifier,
              other_root_identifier, "key60",
              [&changes](EntryChange e) {
                EXPECT_EQ(changes[1].entry, e.entry);
                return true;
              },
              callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
}

TEST_F(DiffTest, ForEachDiffWithMinKeySkipNodes) {
  // Expected base tree layout (XX is key "keyXX"):
  //       [03, 07, 30]
  //     /
  // [01, 02]
  std::vector<EntryChange> base_entries;
  ASSERT_TRUE(
      CreateEntryChanges(std::vector<size_t>({1, 2, 3, 7, 30}), &base_entries));
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
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, changes,
                                    &other_root_identifier));

  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_identifier,
              other_root_identifier, "key01",
              [&changes](EntryChange e) {
                EXPECT_EQ(changes[0].entry, e.entry);
                return true;
              },
              callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
}

TEST_F(DiffTest, ForEachDiffPriorityChange) {
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);
  Entry base_entry = base_changes[10].entry;

  std::vector<EntryChange> other_changes;
  // Update priority for a key.
  other_changes.push_back(EntryChange{
      Entry{base_entry.key, base_entry.object_identifier, KeyPriority::LAZY},
      false});

  bool called;
  Status status;
  ObjectIdentifier other_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, other_changes,
                                    &other_root_identifier));

  // ForEachDiff should return all changes just applied.
  size_t change_count = 0;
  EntryChange actual_change;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_identifier,
              other_root_identifier, "",
              [&actual_change, &change_count](EntryChange e) {
                actual_change = e;
                ++change_count;
                return true;
              },
              callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(1u, change_count);
  EXPECT_FALSE(actual_change.deleted);
  EXPECT_EQ(base_entry.key, actual_change.entry.key);
  EXPECT_EQ(base_entry.object_identifier,
            actual_change.entry.object_identifier);
  EXPECT_EQ(KeyPriority::LAZY, actual_change.entry.priority);
}

TEST_F(DiffTest, ForEachThreeWayDiff) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_object01_identifier =
      base_changes[1].entry.object_identifier;
  ObjectIdentifier base_object02_identifier =
      base_changes[2].entry.object_identifier;
  ObjectIdentifier base_object40_identifier =
      base_changes[40].entry.object_identifier;
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key1.
  left_changes.push_back(
      EntryChange{Entry{"key01", object_identifier, KeyPriority::LAZY}, false});
  // Add entry key255.
  left_changes.push_back(EntryChange{
      Entry{"key255", object_identifier, KeyPriority::LAZY}, false});
  // Remove entry key40.
  left_changes.push_back(
      EntryChange{Entry{"key40", {}, KeyPriority::LAZY}, true});

  ObjectIdentifier left_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, left_changes,
                                    &left_root_identifier));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectIdentifier object_identifier2 = object2->GetIdentifier();
  std::vector<EntryChange> right_changes;
  // Update to same value for key1.
  right_changes.push_back(
      EntryChange{Entry{"key01", object_identifier, KeyPriority::LAZY}, false});
  // Update to different value for key2
  right_changes.push_back(EntryChange{
      Entry{"key02", object_identifier2, KeyPriority::LAZY}, false});
  // Add entry key258.
  right_changes.push_back(EntryChange{
      Entry{"key258", object_identifier, KeyPriority::LAZY}, false});

  ObjectIdentifier right_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, right_changes,
                                    &right_root_identifier));

  std::vector<ThreeWayChange> expected_three_way_changes;
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key01", base_object01_identifier, KeyPriority::EAGER),
      CreateEntryPtr("key01", object_identifier, KeyPriority::LAZY),
      CreateEntryPtr("key01", object_identifier, KeyPriority::LAZY)});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key02", base_object02_identifier, KeyPriority::EAGER),
      CreateEntryPtr("key02", base_object02_identifier, KeyPriority::EAGER),
      CreateEntryPtr("key02", object_identifier2, KeyPriority::LAZY)});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(),
      CreateEntryPtr("key255", object_identifier, KeyPriority::LAZY),
      CreateEntryPtr()});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key258", object_identifier, KeyPriority::LAZY)});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key40", base_object40_identifier, KeyPriority::EAGER),
      CreateEntryPtr(),
      CreateEntryPtr("key40", base_object40_identifier, KeyPriority::EAGER)});

  bool called;
  Status status;
  unsigned int current_change = 0;
  ForEachThreeWayDiff(
      &coroutine_service_, &fake_storage_, base_root_identifier,
      left_root_identifier, right_root_identifier, "",
      [&expected_three_way_changes, &current_change](ThreeWayChange e) {
        EXPECT_LT(current_change, expected_three_way_changes.size());
        if (current_change >= expected_three_way_changes.size()) {
          return false;
        }
        EXPECT_EQ(expected_three_way_changes[current_change], e);
        current_change++;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(current_change, expected_three_way_changes.size());
}

TEST_F(DiffTest, ForEachThreeWayDiffMinKey) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_object01_identifier =
      base_changes[1].entry.object_identifier;
  ObjectIdentifier base_object02_identifier =
      base_changes[2].entry.object_identifier;
  ObjectIdentifier base_object40_identifier =
      base_changes[40].entry.object_identifier;
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key1.
  left_changes.push_back(
      EntryChange{Entry{"key01", object_identifier, KeyPriority::LAZY}, false});
  // Add entry key255.
  left_changes.push_back(EntryChange{
      Entry{"key255", object_identifier, KeyPriority::LAZY}, false});
  // Remove entry key40.
  left_changes.push_back(
      EntryChange{Entry{"key40", {}, KeyPriority::LAZY}, true});

  ObjectIdentifier left_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, left_changes,
                                    &left_root_identifier));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectIdentifier object_identifier2 = object2->GetIdentifier();
  std::vector<EntryChange> right_changes;
  // Update to same value for key1.
  right_changes.push_back(
      EntryChange{Entry{"key01", object_identifier, KeyPriority::LAZY}, false});
  // Update to different value for key2
  right_changes.push_back(EntryChange{
      Entry{"key02", object_identifier2, KeyPriority::LAZY}, false});
  // Add entry key258.
  right_changes.push_back(EntryChange{
      Entry{"key258", object_identifier, KeyPriority::LAZY}, false});

  ObjectIdentifier right_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, right_changes,
                                    &right_root_identifier));

  std::vector<ThreeWayChange> expected_three_way_changes;
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key258", object_identifier, KeyPriority::LAZY)});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key40", base_object40_identifier, KeyPriority::EAGER),
      CreateEntryPtr(),
      CreateEntryPtr("key40", base_object40_identifier, KeyPriority::EAGER)});

  bool called;
  Status status;
  unsigned int current_change = 0;
  ForEachThreeWayDiff(
      &coroutine_service_, &fake_storage_, base_root_identifier,
      left_root_identifier, right_root_identifier, "key257",
      [&expected_three_way_changes, &current_change](ThreeWayChange e) {
        EXPECT_LT(current_change, expected_three_way_changes.size());
        if (current_change >= expected_three_way_changes.size()) {
          return false;
        }
        EXPECT_EQ(expected_three_way_changes[current_change], e);
        current_change++;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(current_change, expected_three_way_changes.size());
}

TEST_F(DiffTest, ForEachThreeWayDiffNoDiff) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectIdentifier base_object01_identifier =
      base_changes[1].entry.object_identifier;
  ObjectIdentifier base_object02_identifier =
      base_changes[2].entry.object_identifier;
  ObjectIdentifier base_object40_identifier =
      base_changes[40].entry.object_identifier;
  ObjectIdentifier base_root_identifier = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectIdentifier object_identifier = object->GetIdentifier();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key1.
  left_changes.push_back(
      EntryChange{Entry{"key01", object_identifier, KeyPriority::LAZY}, false});
  // Add entry key255.
  left_changes.push_back(EntryChange{
      Entry{"key255", object_identifier, KeyPriority::LAZY}, false});
  // Remove entry key40.
  left_changes.push_back(
      EntryChange{Entry{"key40", {}, KeyPriority::LAZY}, true});

  ObjectIdentifier left_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, left_changes,
                                    &left_root_identifier));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectIdentifier object_identifier2 = object2->GetIdentifier();
  std::vector<EntryChange> right_changes;
  // Update to same value for key1.
  right_changes.push_back(
      EntryChange{Entry{"key01", object_identifier, KeyPriority::LAZY}, false});
  // Update to different value for key2
  right_changes.push_back(EntryChange{
      Entry{"key02", object_identifier2, KeyPriority::LAZY}, false});
  // Add entry key258.
  right_changes.push_back(EntryChange{
      Entry{"key258", object_identifier, KeyPriority::LAZY}, false});

  ObjectIdentifier right_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, right_changes,
                                    &right_root_identifier));

  bool called;
  Status status;
  // No change is expected.
  ForEachThreeWayDiff(
      &coroutine_service_, &fake_storage_, base_root_identifier,
      left_root_identifier, right_root_identifier, "key5",
      [](ThreeWayChange e) {
        ADD_FAILURE();
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
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
  left_changes.push_back(EntryChange{
      Entry{"key01", object1_identifier, KeyPriority::EAGER}, false});
  left_changes.push_back(EntryChange{
      Entry{"key03", object3_identifier, KeyPriority::EAGER}, false});

  ObjectIdentifier left_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, left_changes,
                                    &left_root_identifier));

  // Right tree.
  std::vector<EntryChange> right_changes;
  right_changes.push_back(EntryChange{
      Entry{"key02", object2_identifier, KeyPriority::EAGER}, false});
  right_changes.push_back(EntryChange{
      Entry{"key04", object4_identifier, KeyPriority::EAGER}, false});

  ObjectIdentifier right_root_identifier;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_identifier, right_changes,
                                    &right_root_identifier));

  std::vector<ThreeWayChange> expected_three_way_changes;
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(),
      CreateEntryPtr("key01", object1_identifier, KeyPriority::EAGER),
      CreateEntryPtr()});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key02", object2_identifier, KeyPriority::EAGER)});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(),
      CreateEntryPtr("key03", object3_identifier, KeyPriority::EAGER),
      CreateEntryPtr()});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key04", object4_identifier, KeyPriority::EAGER)});

  bool called;
  Status status;
  unsigned int current_change = 0;
  ForEachThreeWayDiff(
      &coroutine_service_, &fake_storage_, base_root_identifier,
      left_root_identifier, right_root_identifier, "",
      [&expected_three_way_changes, &current_change](ThreeWayChange e) {
        EXPECT_LT(current_change, expected_three_way_changes.size());
        if (current_change >= expected_three_way_changes.size()) {
          return false;
        }
        EXPECT_EQ(expected_three_way_changes[current_change], e);
        current_change++;
        return true;
      },
      callback::Capture(callback::SetWhenCalled(&called), &status));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(current_change, expected_three_way_changes.size());
}

}  // namespace
}  // namespace btree
}  // namespace storage
