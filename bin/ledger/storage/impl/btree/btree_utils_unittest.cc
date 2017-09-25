// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>

#include <algorithm>

#include "gtest/gtest.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/impl/btree/builder.h"
#include "peridot/bin/ledger/storage/impl/btree/diff.h"
#include "peridot/bin/ledger/storage/impl/btree/entry_change_iterator.h"
#include "peridot/bin/ledger/storage/impl/btree/iterator.h"
#include "peridot/bin/ledger/storage/impl/btree/tree_node.h"
#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace storage {
namespace btree {
namespace {

// Pre-determined node level function.
uint8_t GetTestNodeLevel(convert::ExtendedStringView key) {
  if (key == "key03" || key == "key07" || key == "key30" || key == "key60" ||
      key == "key89") {
    return 1;
  }

  if (key == "key50" || key == "key75") {
    return 2;
  }

  return 0;
}

constexpr NodeLevelCalculator kTestNodeLevelCalculator = {&GetTestNodeLevel};

class TrackGetObjectFakePageStorage : public fake::FakePageStorage {
 public:
  explicit TrackGetObjectFakePageStorage(PageId id)
      : fake::FakePageStorage(id) {}
  ~TrackGetObjectFakePageStorage() override {}

  void GetObject(ObjectIdView object_id,
                 Location location,
                 std::function<void(Status, std::unique_ptr<const Object>)>
                     callback) override {
    object_requests.insert(object_id.ToString());
    fake::FakePageStorage::GetObject(object_id, location, callback);
  }

  std::set<ObjectId> object_requests;
};

class BTreeUtilsTest : public StorageTest {
 public:
  BTreeUtilsTest() : fake_storage_("page_id") {}

  ~BTreeUtilsTest() override {}

  // Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    std::srand(0);
  }

 protected:
  PageStorage* GetStorage() override { return &fake_storage_; }

  ObjectId CreateTree(const std::vector<EntryChange>& entries) {
    ObjectId root_id;
    EXPECT_TRUE(GetEmptyNodeId(&root_id));

    Status status;
    ObjectId new_root_id;
    std::unordered_set<ObjectId> new_nodes;
    ApplyChanges(
        &coroutine_service_, &fake_storage_, root_id,
        std::make_unique<EntryChangeIterator>(entries.begin(), entries.end()),
        callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
        &kTestNodeLevelCalculator);
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);
    return new_root_id;
  }

  std::vector<Entry> GetEntriesList(ObjectId root_id) {
    std::vector<Entry> entries;
    auto on_next = [&entries](EntryAndNodeId entry) {
      entries.push_back(entry.entry);
      return true;
    };
    auto on_done = [this](Status status) {
      EXPECT_EQ(Status::OK, status);
      message_loop_.PostQuitTask();
    };
    ForEachEntry(&coroutine_service_, &fake_storage_, root_id, "",
                 std::move(on_next), std::move(on_done));
    EXPECT_FALSE(RunLoopWithTimeout());
    return entries;
  }

  coroutine::CoroutineServiceImpl coroutine_service_;
  TrackGetObjectFakePageStorage fake_storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(BTreeUtilsTest);
};

TEST_F(BTreeUtilsTest, GetNodeLevel) {
  size_t level_distribution[4];
  memset(level_distribution, 0, sizeof(level_distribution));

  for (size_t i = 0; i < 1000; ++i) {
    fxl::StringView key(reinterpret_cast<char*>(&i), sizeof(i));
    uint8_t node_level =
        std::min(arraysize(level_distribution) - 1,
                 static_cast<size_t>(
                     GetDefaultNodeLevelCalculator()->GetNodeLevel(key)));
    level_distribution[node_level]++;
  }

  EXPECT_TRUE(std::is_sorted(level_distribution,
                             level_distribution + arraysize(level_distribution),
                             [](int v1, int v2) { return v2 < v1; }));
  EXPECT_NE(0u, level_distribution[1]);
}

TEST_F(BTreeUtilsTest, ApplyChangesFromEmpty) {
  ObjectId root_id;
  ASSERT_TRUE(GetEmptyNodeId(&root_id));
  std::vector<EntryChange> changes;
  ASSERT_TRUE(CreateEntryChanges(3, &changes));

  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  // Expected layout (X is key "keyX"):
  // [00, 01, 02]
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(changes.begin(), changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(1u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(changes.size(), entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    EXPECT_EQ(changes[i].entry, entries[i]);
  }
}

TEST_F(BTreeUtilsTest, ApplyChangeSingleLevel1Entry) {
  ObjectId root_id;
  ASSERT_TRUE(GetEmptyNodeId(&root_id));
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({3}), &golden_entries));

  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  // Expected layout (XX is key "keyXX"):
  // [03]

  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(golden_entries.begin(),
                                            golden_entries.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(1u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size(), entries.size());
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_EQ(golden_entries[i].entry, entries[i]);
  }
}

TEST_F(BTreeUtilsTest, ApplyChangesManyEntries) {
  ObjectId root_id;
  ASSERT_TRUE(GetEmptyNodeId(&root_id));
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));

  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10]
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(golden_entries.begin(),
                                            golden_entries.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(4u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size(), entries.size());
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_EQ(golden_entries[i].entry, entries[i]);
  }

  Entry new_entry = {"key071", MakeObjectId("objectid071"), KeyPriority::EAGER};
  std::vector<EntryChange> new_change{EntryChange{new_entry, false}};
  // Insert key "071" between keys "07" and "08".
  golden_entries.insert(golden_entries.begin() + 8, new_change[0]);

  new_nodes.clear();
  ObjectId new_root_id2;
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [071, 08, 09, 10]
  ApplyChanges(
      &coroutine_service_, &fake_storage_, new_root_id,
      std::make_unique<EntryChangeIterator>(new_change.begin(),
                                            new_change.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id2, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(new_root_id, new_root_id2);
  // The root and the 3rd child have changed.
  EXPECT_EQ(2u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id2) != new_nodes.end());

  entries = GetEntriesList(new_root_id2);
  ASSERT_EQ(golden_entries.size(), entries.size());
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_EQ(golden_entries[i].entry, entries[i]);
  }
}

TEST_F(BTreeUtilsTest, UpdateValue) {
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10, 11]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Update entry.
  std::vector<Entry> entries_to_update{golden_entries[2].entry};
  std::vector<EntryChange> update_changes;
  for (size_t i = 0; i < entries_to_update.size(); ++i) {
    std::unique_ptr<const Object> object;
    ASSERT_TRUE(
        AddObject(fxl::StringPrintf("new_object%02" PRIuMAX, i), &object));
    entries_to_update[i].object_id = object->GetId();
    update_changes.push_back(EntryChange{entries_to_update[i], false});
  }

  // Expected layout is unchanged.
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(update_changes.begin(),
                                            update_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(root_id, new_root_id);
  // The root and the first child have changed.
  EXPECT_EQ(2u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size(), entries.size());
  size_t updated_index = 0;
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    if (updated_index < entries_to_update.size() &&
        golden_entries[i].entry.key == entries_to_update[updated_index].key) {
      EXPECT_EQ(entries_to_update[updated_index], entries[i]);
      // Skip the updated entries.
      updated_index++;
      continue;
    }
    EXPECT_EQ(golden_entries[i].entry, entries[i]);
  }
}

TEST_F(BTreeUtilsTest, UpdateValueLevel1) {
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10, 11]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Update entry.
  std::vector<Entry> entries_to_update{golden_entries[3].entry};
  std::vector<EntryChange> update_changes;
  for (size_t i = 0; i < entries_to_update.size(); ++i) {
    std::unique_ptr<const Object> object;
    ASSERT_TRUE(
        AddObject(fxl::StringPrintf("new_object%02" PRIuMAX, i), &object));
    entries_to_update[i].object_id = object->GetId();
    update_changes.push_back(EntryChange{entries_to_update[i], false});
  }

  // Expected layout is unchanged.
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(update_changes.begin(),
                                            update_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(root_id, new_root_id);
  // Only the root has changed.
  EXPECT_EQ(1u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size(), entries.size());
  size_t updated_index = 0;
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    if (updated_index < entries_to_update.size() &&
        golden_entries[i].entry.key == entries_to_update[updated_index].key) {
      EXPECT_EQ(entries_to_update[updated_index], entries[i]);
      // Skip the updated entries.
      updated_index++;
      continue;
    }
    EXPECT_EQ(golden_entries[i].entry, entries[i]);
  }
}

TEST_F(BTreeUtilsTest, UpdateValueSplitChange) {
  // Expected layout (XX is key "keyXX"):
  // [00, 04]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({
                                     0,
                                     4,
                                 }),
                                 &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Add level 1 entry.
  std::vector<EntryChange> update_changes;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({3}), &update_changes));
  // Expected layout (XX is key "keyXX"):
  //    [03]
  //   /    \
  // [00]  [04]

  // Apply update.
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(update_changes.begin(),
                                            update_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(root_id, new_root_id);
  // The tree nodes are new.
  EXPECT_EQ(3u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size() + update_changes.size(), entries.size());
  size_t updated_index = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (updated_index < update_changes.size() &&
        entries[i] == update_changes[updated_index].entry) {
      // Skip the updated entries.
      updated_index++;
      continue;
    }
    ASSERT_GT(golden_entries.size(), i - updated_index);
    EXPECT_EQ(golden_entries[i - updated_index].entry, entries[i]);
  }
}

TEST_F(BTreeUtilsTest, NoOpUpdateChange) {
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10, 11]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Apply all entries again.
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(golden_entries.begin(),
                                            golden_entries.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(root_id, new_root_id);
  // The root and the first child have changed.
  EXPECT_EQ(0u, new_nodes.size());
}

TEST_F(BTreeUtilsTest, DeleteChanges) {
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10, 11]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Delete entries.
  std::vector<EntryChange> delete_changes;
  ASSERT_TRUE(
      CreateEntryChanges(std::vector<size_t>({2, 4}), &delete_changes, true));

  // Expected layout (XX is key "keyXX"):
  //            [03, 07]
  //         /     |        \
  // [00, 01]  [05, 06]    [08, 09, 10, 11]
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(delete_changes.begin(),
                                            delete_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(root_id, new_root_id);
  // The root and the first 2 children have changed.
  EXPECT_EQ(3u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size() - delete_changes.size(), entries.size());
  size_t deleted_index = 0;
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    if (deleted_index < delete_changes.size() &&
        golden_entries[i].entry.key ==
            delete_changes[deleted_index].entry.key) {
      // Skip the deleted entries.
      deleted_index++;
      continue;
    }
    ASSERT_LT(i - deleted_index, entries.size());
    EXPECT_EQ(golden_entries[i].entry, entries[i - deleted_index]);
  }
}

TEST_F(BTreeUtilsTest, DeleteLevel1Changes) {
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10, 11]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Delete entry.
  std::vector<EntryChange> delete_changes;
  ASSERT_TRUE(
      CreateEntryChanges(std::vector<size_t>({3}), &delete_changes, true));

  // Expected layout (XX is key "keyXX"):
  //                         [07]
  //                        /    \
  // [00, 01, 02, 04, 05, 06]    [08, 09, 10, 11]
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(delete_changes.begin(),
                                            delete_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(root_id, new_root_id);
  // The root and one child have changed.
  EXPECT_EQ(2u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size() - delete_changes.size(), entries.size());
  size_t deleted_index = 0;
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    if (deleted_index < delete_changes.size() &&
        golden_entries[i].entry.key ==
            delete_changes[deleted_index].entry.key) {
      // Skip the deleted entries.
      deleted_index++;
      continue;
    }
    ASSERT_LT(i - deleted_index, entries.size());
    EXPECT_EQ(golden_entries[i].entry, entries[i - deleted_index]);
  }
}

TEST_F(BTreeUtilsTest, NoOpDeleteChange) {
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10, 11]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Delete entry.
  std::vector<EntryChange> delete_changes;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({12, 13, 14}),
                                 &delete_changes, true));

  // Apply deletion.
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(delete_changes.begin(),
                                            delete_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(root_id, new_root_id);
  // The root and the first child have changed.
  EXPECT_EQ(0u, new_nodes.size());
}

TEST_F(BTreeUtilsTest, SplitMergeUpdate) {
  // Expected layout (XX is key "keyXX"):
  //        [50]
  //     /        \
  // [03]          [60, 89]
  //            /      |     \
  //        [55] [65, 74, 76] [99]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(
      std::vector<size_t>({3, 50, 55, 60, 65, 74, 76, 89, 99}),
      &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Add level 2 entry.
  std::vector<EntryChange> update_changes;
  ASSERT_TRUE(CreateEntryChanges(std::vector<size_t>({75}), &update_changes));
  // Expected layout (XX is key "keyXX"):
  //          [50, 75]
  //     /       |       \
  // [03]     [60]         [89]
  //         /    \       /   \
  //      [55] [65, 74] [76] [99]

  // Apply update.
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(update_changes.begin(),
                                            update_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(root_id, new_root_id);
  // The tree nodes are new.
  EXPECT_EQ(5u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size() + update_changes.size(), entries.size());
  size_t updated_index = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (updated_index < update_changes.size() &&
        entries[i] == update_changes[updated_index].entry) {
      // Skip the updated entries.
      updated_index++;
      continue;
    }
    ASSERT_LT(i - updated_index, golden_entries.size());
    EXPECT_EQ(golden_entries[i - updated_index].entry, entries[i]);
  }

  // Remove the new entry.
  std::vector<EntryChange> delete_changes;
  ASSERT_TRUE(
      CreateEntryChanges(std::vector<size_t>({75}), &delete_changes, true));

  ObjectId final_node_id;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, new_root_id,
      std::make_unique<EntryChangeIterator>(delete_changes.begin(),
                                            delete_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &final_node_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(root_id, final_node_id);
}

TEST_F(BTreeUtilsTest, DeleteAll) {
  // Create an initial tree.
  std::vector<size_t> values({0, 1, 2, 3, 4, 5, 6, 7});
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(values, &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Delete everything.
  std::vector<EntryChange> delete_changes;
  ASSERT_TRUE(CreateEntryChanges(values, &delete_changes, true));
  // Apply update.
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, root_id,
      std::make_unique<EntryChangeIterator>(delete_changes.begin(),
                                            delete_changes.end()),
      callback::Capture(MakeQuitTask(), &status, &new_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(root_id, new_root_id);
  EXPECT_NE("", new_root_id);
  // The empty node is new.
  EXPECT_EQ(1u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());
}

TEST_F(BTreeUtilsTest, GetObjectIdsFromEmpty) {
  ObjectId root_id;
  ASSERT_TRUE(GetEmptyNodeId(&root_id));
  Status status;
  std::set<ObjectId> object_ids;
  GetObjectIds(&coroutine_service_, &fake_storage_, root_id,
               callback::Capture(MakeQuitTask(), &status, &object_ids));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(1u, object_ids.size());
  EXPECT_TRUE(object_ids.find(root_id) != object_ids.end());
}

TEST_F(BTreeUtilsTest, GetObjectOneNodeTree) {
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(4, &entries));
  ObjectId root_id = CreateTree(entries);

  Status status;
  std::set<ObjectId> object_ids;
  GetObjectIds(&coroutine_service_, &fake_storage_, root_id,
               callback::Capture(MakeQuitTask(), &status, &object_ids));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(6u, object_ids.size());
  EXPECT_TRUE(object_ids.find(root_id) != object_ids.end());
  for (EntryChange& e : entries) {
    EXPECT_TRUE(object_ids.find(e.entry.object_id) != object_ids.end());
  }
}

TEST_F(BTreeUtilsTest, GetObjectIdsBigTree) {
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(99, &entries));
  ObjectId root_id = CreateTree(entries);

  Status status;
  std::set<ObjectId> object_ids;
  GetObjectIds(&coroutine_service_, &fake_storage_, root_id,
               callback::Capture(MakeQuitTask(), &status, &object_ids));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(99u + 12, object_ids.size());
  EXPECT_TRUE(object_ids.find(root_id) != object_ids.end());
  for (EntryChange& e : entries) {
    EXPECT_TRUE(object_ids.find(e.entry.object_id) != object_ids.end());
  }
}

TEST_F(BTreeUtilsTest, GetObjectsFromSync) {
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(5, &entries));
  entries[3].entry.priority = KeyPriority::LAZY;
  ObjectId root_id = CreateTree(entries);

  fake_storage_.object_requests.clear();
  Status status;
  // Expected layout (XX is key "keyXX"):
  //          [03]
  //       /        \
  // [00, 01, 02]  [04]
  GetObjectsFromSync(&coroutine_service_, &fake_storage_, root_id,
                     callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  std::vector<ObjectId> object_requests;
  std::copy(fake_storage_.object_requests.begin(),
            fake_storage_.object_requests.end(),
            std::back_inserter(object_requests));
  // There are 8 objects: 3 nodes and 4 eager values and 1 lazy. Except from the
  // lazy object, all others should have been requested.
  EXPECT_EQ(3 + 4u, object_requests.size());

  std::set<ObjectId> object_ids;
  GetObjectIds(&coroutine_service_, &fake_storage_, root_id,
               callback::Capture(MakeQuitTask(), &status, &object_ids));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  ASSERT_EQ(3 + 5u, object_ids.size());
  for (ObjectId& id : object_requests) {
    // entries[3] contains the lazy value.
    if (id != entries[3].entry.object_id) {
      EXPECT_TRUE(object_ids.find(id) != object_ids.end());
    }
  }
}

TEST_F(BTreeUtilsTest, ForEachEmptyTree) {
  std::vector<EntryChange> entries = {};
  ObjectId root_id = CreateTree(entries);
  auto on_next = [](EntryAndNodeId e) {
    // Fail: There are no elements in the tree.
    EXPECT_TRUE(false);
    return false;
  };
  auto on_done = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  ForEachEntry(&coroutine_service_, &fake_storage_, root_id, "",
               std::move(on_next), std::move(on_done));
  ASSERT_FALSE(RunLoopWithTimeout());
}

TEST_F(BTreeUtilsTest, ForEachAllEntries) {
  // Create a tree from entries with keys from 00-99.
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(100, &entries));
  ObjectId root_id = CreateTree(entries);

  int current_key = 0;
  auto on_next = [&current_key](EntryAndNodeId e) {
    EXPECT_EQ(fxl::StringPrintf("key%02d", current_key), e.entry.key);
    current_key++;
    return true;
  };
  auto on_done = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  ForEachEntry(&coroutine_service_, &fake_storage_, root_id, "", on_next,
               on_done);
  ASSERT_FALSE(RunLoopWithTimeout());
}

TEST_F(BTreeUtilsTest, ForEachEntryPrefix) {
  // Create a tree from entries with keys from 00-99.
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(100, &entries));
  ObjectId root_id = CreateTree(entries);

  // Find all entries with "key3" prefix in the key.
  std::string prefix = "key3";
  int current_key = 30;
  auto on_next = [&current_key, &prefix](EntryAndNodeId e) {
    if (e.entry.key.substr(0, prefix.length()) != prefix) {
      return false;
    }
    EXPECT_EQ(fxl::StringPrintf("key%02d", current_key++), e.entry.key);
    return true;
  };
  auto on_done = [this, &current_key](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(40, current_key);
    message_loop_.PostQuitTask();
  };
  ForEachEntry(&coroutine_service_, &fake_storage_, root_id, prefix, on_next,
               on_done);
  ASSERT_FALSE(RunLoopWithTimeout());
}

TEST_F(BTreeUtilsTest, ForEachDiff) {
  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectId object_id = object->GetId();

  std::vector<EntryChange> changes;
  ASSERT_TRUE(CreateEntryChanges(50, &changes));
  ObjectId base_root_id = CreateTree(changes);
  changes.clear();
  // Update value for key1.
  changes.push_back(
      EntryChange{Entry{"key1", object_id, KeyPriority::LAZY}, false});
  // Add entry key255.
  changes.push_back(
      EntryChange{Entry{"key255", object_id, KeyPriority::LAZY}, false});
  // Remove entry key40.
  changes.push_back(EntryChange{Entry{"key40", "", KeyPriority::LAZY}, true});

  Status status;
  ObjectId other_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, base_root_id,
      std::make_unique<EntryChangeIterator>(changes.begin(), changes.end()),
      callback::Capture(MakeQuitTask(), &status, &other_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  // ForEachDiff should return all changes just applied.
  size_t current_change = 0;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_id, other_root_id,
              "",
              [&changes, &current_change](EntryChange e) {
                EXPECT_EQ(changes[current_change].deleted, e.deleted);
                if (e.deleted) {
                  EXPECT_EQ(changes[current_change].entry.key, e.entry.key);
                } else {
                  EXPECT_EQ(changes[current_change].entry, e.entry);
                }
                ++current_change;
                return true;
              },
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(changes.size(), current_change);
}

TEST_F(BTreeUtilsTest, ForEachDiffWithMinKey) {
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
  ObjectId base_root_id = CreateTree(base_entries);
  ObjectId other_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, base_root_id,
      std::make_unique<EntryChangeIterator>(changes.begin(), changes.end()),
      callback::Capture(MakeQuitTask(), &status, &other_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  // ForEachDiff with a "key0" as min_key should return both changes.
  size_t current_change = 0;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_id, other_root_id,
              "key0",
              [&changes, &current_change](EntryChange e) {
                EXPECT_EQ(changes[current_change++].entry, e.entry);
                return true;
              },
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(changes.size(), current_change);

  // With "key60" as min_key, only key75 should be returned.
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_id, other_root_id,
              "key60",
              [&changes](EntryChange e) {
                EXPECT_EQ(changes[1].entry, e.entry);
                return true;
              },
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
}

TEST_F(BTreeUtilsTest, ForEachDiffWithMinKeySkipNodes) {
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
  ObjectId base_root_id = CreateTree(base_entries);
  ObjectId other_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, base_root_id,
      std::make_unique<EntryChangeIterator>(changes.begin(), changes.end()),
      callback::Capture(MakeQuitTask(), &status, &other_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_id, other_root_id,
              "key01",
              [&changes](EntryChange e) {
                EXPECT_EQ(changes[0].entry, e.entry);
                return true;
              },
              callback::Capture(MakeQuitTask(), &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
}

TEST_F(BTreeUtilsTest, ForEachDiffPriorityChange) {
  std::vector<EntryChange> changes;
  ASSERT_TRUE(CreateEntryChanges(50, &changes));
  ObjectId base_root_id = CreateTree(changes);
  Entry base_entry = changes[10].entry;
  changes.clear();
  // Update priority for a key.
  changes.push_back(EntryChange{
      Entry{base_entry.key, base_entry.object_id, KeyPriority::LAZY}, false});

  Status status;
  ObjectId other_root_id;
  std::unordered_set<ObjectId> new_nodes;
  ApplyChanges(
      &coroutine_service_, &fake_storage_, base_root_id,
      std::make_unique<EntryChangeIterator>(changes.begin(), changes.end()),
      callback::Capture(MakeQuitTask(), &status, &other_root_id, &new_nodes),
      &kTestNodeLevelCalculator);
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  // ForEachDiff should return all changes just applied.
  size_t change_count = 0;
  EntryChange actual_change;
  ForEachDiff(&coroutine_service_, &fake_storage_, base_root_id, other_root_id,
              "",
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
  EXPECT_EQ(base_entry.object_id, actual_change.entry.object_id);
  EXPECT_EQ(KeyPriority::LAZY, actual_change.entry.priority);
}

}  // namespace
}  // namespace btree
}  // namespace storage
