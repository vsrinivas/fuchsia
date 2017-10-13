// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/diff.h"

#include "gtest/gtest.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/impl/btree/builder.h"
#include "peridot/bin/ledger/storage/impl/btree/entry_change_iterator.h"
#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace storage {
namespace btree {
namespace {
std::unique_ptr<Entry> CreateEntryPtr(std::string key,
                                      ObjectDigest object_digest,
                                      KeyPriority priority) {
  auto e = std::make_unique<Entry>();
  e->key = key;
  e->object_digest = object_digest;
  e->priority = priority;
  return e;
}

std::unique_ptr<Entry> CreateEntryPtr() {
  return std::unique_ptr<Entry>();
}

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

  ObjectDigest CreateTree(const std::vector<EntryChange>& entries) {
    ObjectDigest root_digest;
    EXPECT_TRUE(GetEmptyNodeDigest(&root_digest));

    ObjectDigest digest;
    EXPECT_TRUE(CreateTreeFromChanges(root_digest, entries, &digest));
    return digest;
  }

  coroutine::CoroutineServiceImpl coroutine_service_;
  fake::FakePageStorage fake_storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DiffTest);
};

TEST_F(DiffTest, ForEachDiff) {
  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectDigest object_digest = object->GetDigest();

  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectDigest base_root_digest = CreateTree(base_changes);

  std::vector<EntryChange> other_changes;
  // Update value for key1.
  other_changes.push_back(
      EntryChange{Entry{"key1", object_digest, KeyPriority::LAZY}, false});
  // Add entry key255.
  other_changes.push_back(
      EntryChange{Entry{"key255", object_digest, KeyPriority::LAZY}, false});
  // Remove entry key40.
  other_changes.push_back(
      EntryChange{Entry{"key40", "", KeyPriority::LAZY}, true});
  ObjectDigest other_root_digest;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_digest, other_changes,
                                    &other_root_digest));

  // ForEachDiff should return all changes just applied.
  Status status;
  size_t current_change = 0;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_digest,
              other_root_digest, "",
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
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
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

  Status status;
  ObjectDigest base_root_digest = CreateTree(base_entries);
  ObjectDigest other_root_digest;
  std::unordered_set<ObjectDigest> new_nodes;
  ASSERT_TRUE(
      CreateTreeFromChanges(base_root_digest, changes, &other_root_digest));

  // ForEachDiff with a "key0" as min_key should return both changes.
  size_t current_change = 0;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_digest,
              other_root_digest, "key0",
              [&changes, &current_change](EntryChange e) {
                EXPECT_EQ(changes[current_change++].entry, e.entry);
                return true;
              },
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(changes.size(), current_change);

  // With "key60" as min_key, only key75 should be returned.
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_digest,
              other_root_digest, "key60",
              [&changes](EntryChange e) {
                EXPECT_EQ(changes[1].entry, e.entry);
                return true;
              },
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
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

  Status status;
  ObjectDigest base_root_digest = CreateTree(base_entries);
  ObjectDigest other_root_digest;
  ASSERT_TRUE(
      CreateTreeFromChanges(base_root_digest, changes, &other_root_digest));

  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_digest,
              other_root_digest, "key01",
              [&changes](EntryChange e) {
                EXPECT_EQ(changes[0].entry, e.entry);
                return true;
              },
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
}

TEST_F(DiffTest, ForEachDiffPriorityChange) {
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectDigest base_root_digest = CreateTree(base_changes);
  Entry base_entry = base_changes[10].entry;

  std::vector<EntryChange> other_changes;
  // Update priority for a key.
  other_changes.push_back(EntryChange{
      Entry{base_entry.key, base_entry.object_digest, KeyPriority::LAZY},
      false});

  Status status;
  ObjectDigest other_root_digest;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_digest, other_changes,
                                    &other_root_digest));

  // ForEachDiff should return all changes just applied.
  size_t change_count = 0;
  EntryChange actual_change;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_digest,
              other_root_digest, "",
              [&actual_change, &change_count](EntryChange e) {
                actual_change = e;
                ++change_count;
                return true;
              },
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(1u, change_count);
  EXPECT_FALSE(actual_change.deleted);
  EXPECT_EQ(base_entry.key, actual_change.entry.key);
  EXPECT_EQ(base_entry.object_digest, actual_change.entry.object_digest);
  EXPECT_EQ(KeyPriority::LAZY, actual_change.entry.priority);
}

TEST_F(DiffTest, ForEachThreeWayDiff) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectDigest base_object01_digest = base_changes[1].entry.object_digest;
  ObjectDigest base_object02_digest = base_changes[2].entry.object_digest;
  ObjectDigest base_object40_digest = base_changes[40].entry.object_digest;
  ObjectDigest base_root_digest = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectDigest object_digest = object->GetDigest();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key1.
  left_changes.push_back(
      EntryChange{Entry{"key01", object_digest, KeyPriority::LAZY}, false});
  // Add entry key255.
  left_changes.push_back(
      EntryChange{Entry{"key255", object_digest, KeyPriority::LAZY}, false});
  // Remove entry key40.
  left_changes.push_back(
      EntryChange{Entry{"key40", "", KeyPriority::LAZY}, true});

  ObjectDigest left_root_digest;
  ASSERT_TRUE(
      CreateTreeFromChanges(base_root_digest, left_changes, &left_root_digest));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectDigest object_digest2 = object2->GetDigest();
  std::vector<EntryChange> right_changes;
  // Update to same value for key1.
  right_changes.push_back(
      EntryChange{Entry{"key01", object_digest, KeyPriority::LAZY}, false});
  // Update to different value for key2
  right_changes.push_back(
      EntryChange{Entry{"key02", object_digest2, KeyPriority::LAZY}, false});
  // Add entry key258.
  right_changes.push_back(
      EntryChange{Entry{"key258", object_digest, KeyPriority::LAZY}, false});

  ObjectDigest right_root_digest;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_digest, right_changes,
                                    &right_root_digest));

  std::vector<ThreeWayChange> expected_three_way_changes;
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key01", base_object01_digest, KeyPriority::EAGER),
      CreateEntryPtr("key01", object_digest, KeyPriority::LAZY),
      CreateEntryPtr("key01", object_digest, KeyPriority::LAZY)});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key02", base_object02_digest, KeyPriority::EAGER),
      CreateEntryPtr("key02", base_object02_digest, KeyPriority::EAGER),
      CreateEntryPtr("key02", object_digest2, KeyPriority::LAZY)});
  expected_three_way_changes.push_back(
      ThreeWayChange{CreateEntryPtr(),
                     CreateEntryPtr("key255", object_digest, KeyPriority::LAZY),
                     CreateEntryPtr()});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key258", object_digest, KeyPriority::LAZY)});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key40", base_object40_digest, KeyPriority::EAGER),
      CreateEntryPtr(),
      CreateEntryPtr("key40", base_object40_digest, KeyPriority::EAGER)});

  Status status;
  unsigned int current_change = 0;
  ForEachThreeWayDiff(
      &coroutine_service_, &fake_storage_, base_root_digest, left_root_digest,
      right_root_digest, "",
      [&expected_three_way_changes, &current_change](ThreeWayChange e) {
        EXPECT_LT(current_change, expected_three_way_changes.size());
        if (current_change >= expected_three_way_changes.size()) {
          return false;
        }
        EXPECT_EQ(expected_three_way_changes[current_change], e);
        current_change++;
        return true;
      },
      callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(current_change, expected_three_way_changes.size());
}

TEST_F(DiffTest, ForEachThreeWayDiffMinKey) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectDigest base_object01_digest = base_changes[1].entry.object_digest;
  ObjectDigest base_object02_digest = base_changes[2].entry.object_digest;
  ObjectDigest base_object40_digest = base_changes[40].entry.object_digest;
  ObjectDigest base_root_digest = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectDigest object_digest = object->GetDigest();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key1.
  left_changes.push_back(
      EntryChange{Entry{"key01", object_digest, KeyPriority::LAZY}, false});
  // Add entry key255.
  left_changes.push_back(
      EntryChange{Entry{"key255", object_digest, KeyPriority::LAZY}, false});
  // Remove entry key40.
  left_changes.push_back(
      EntryChange{Entry{"key40", "", KeyPriority::LAZY}, true});

  ObjectDigest left_root_digest;
  ASSERT_TRUE(
      CreateTreeFromChanges(base_root_digest, left_changes, &left_root_digest));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectDigest object_digest2 = object2->GetDigest();
  std::vector<EntryChange> right_changes;
  // Update to same value for key1.
  right_changes.push_back(
      EntryChange{Entry{"key01", object_digest, KeyPriority::LAZY}, false});
  // Update to different value for key2
  right_changes.push_back(
      EntryChange{Entry{"key02", object_digest2, KeyPriority::LAZY}, false});
  // Add entry key258.
  right_changes.push_back(
      EntryChange{Entry{"key258", object_digest, KeyPriority::LAZY}, false});

  ObjectDigest right_root_digest;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_digest, right_changes,
                                    &right_root_digest));

  std::vector<ThreeWayChange> expected_three_way_changes;
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr(), CreateEntryPtr(),
      CreateEntryPtr("key258", object_digest, KeyPriority::LAZY)});
  expected_three_way_changes.push_back(ThreeWayChange{
      CreateEntryPtr("key40", base_object40_digest, KeyPriority::EAGER),
      CreateEntryPtr(),
      CreateEntryPtr("key40", base_object40_digest, KeyPriority::EAGER)});

  Status status;
  unsigned int current_change = 0;
  ForEachThreeWayDiff(
      &coroutine_service_, &fake_storage_, base_root_digest, left_root_digest,
      right_root_digest, "key257",
      [&expected_three_way_changes, &current_change](ThreeWayChange e) {
        EXPECT_LT(current_change, expected_three_way_changes.size());
        if (current_change >= expected_three_way_changes.size()) {
          return false;
        }
        EXPECT_EQ(expected_three_way_changes[current_change], e);
        current_change++;
        return true;
      },
      callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(current_change, expected_three_way_changes.size());
}

TEST_F(DiffTest, ForEachThreeWayDiffNoDiff) {
  // Base tree.
  std::vector<EntryChange> base_changes;
  ASSERT_TRUE(CreateEntryChanges(50, &base_changes));
  ObjectDigest base_object01_digest = base_changes[1].entry.object_digest;
  ObjectDigest base_object02_digest = base_changes[2].entry.object_digest;
  ObjectDigest base_object40_digest = base_changes[40].entry.object_digest;
  ObjectDigest base_root_digest = CreateTree(base_changes);

  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectDigest object_digest = object->GetDigest();

  // Left tree.
  std::vector<EntryChange> left_changes;
  // Update value for key1.
  left_changes.push_back(
      EntryChange{Entry{"key01", object_digest, KeyPriority::LAZY}, false});
  // Add entry key255.
  left_changes.push_back(
      EntryChange{Entry{"key255", object_digest, KeyPriority::LAZY}, false});
  // Remove entry key40.
  left_changes.push_back(
      EntryChange{Entry{"key40", "", KeyPriority::LAZY}, true});

  ObjectDigest left_root_digest;
  ASSERT_TRUE(
      CreateTreeFromChanges(base_root_digest, left_changes, &left_root_digest));

  // Right tree.
  std::unique_ptr<const Object> object2;
  ASSERT_TRUE(AddObject("change2", &object2));
  ObjectDigest object_digest2 = object2->GetDigest();
  std::vector<EntryChange> right_changes;
  // Update to same value for key1.
  right_changes.push_back(
      EntryChange{Entry{"key01", object_digest, KeyPriority::LAZY}, false});
  // Update to different value for key2
  right_changes.push_back(
      EntryChange{Entry{"key02", object_digest2, KeyPriority::LAZY}, false});
  // Add entry key258.
  right_changes.push_back(
      EntryChange{Entry{"key258", object_digest, KeyPriority::LAZY}, false});

  ObjectDigest right_root_digest;
  ASSERT_TRUE(CreateTreeFromChanges(base_root_digest, right_changes,
                                    &right_root_digest));

  Status status;
  // No change is expected.
  ForEachThreeWayDiff(&coroutine_service_, &fake_storage_, base_root_digest,
                      left_root_digest, right_root_digest, "key5",
                      [](ThreeWayChange e) {
                        ADD_FAILURE();
                        return true;
                      },
                      callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
}

}  // namespace
}  // namespace btree
}  // namespace storage
