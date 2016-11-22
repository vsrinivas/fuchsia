// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/btree_utils.h"

#include <stdio.h>

#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/impl/btree/commit_contents_impl.h"
#include "apps/ledger/src/storage/impl/btree/entry_change_iterator.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/types.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/data_pipe/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace storage {
namespace {

const int kTestNodeSize = 4;

class TrackGetObjectFakePageStorage : public fake::FakePageStorage {
 public:
  TrackGetObjectFakePageStorage(PageId id) : fake::FakePageStorage(id) {}
  ~TrackGetObjectFakePageStorage() {}

  void GetObject(
      ObjectIdView object_id,
      const std::function<void(Status, std::unique_ptr<const Object>)>&
          callback) override {
    object_requests.insert(object_id.ToString());
    fake::FakePageStorage::GetObject(object_id, callback);
  }

  std::set<ObjectId> object_requests;
};

class BTreeUtilsTest : public ::test::TestWithMessageLoop {
 public:
  BTreeUtilsTest() : fake_storage_("page_id") {}

  ~BTreeUtilsTest() override {}

  // Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    std::srand(0);
  }

  std::vector<EntryChange> CreateEntryChanges(int size) {
    FTL_CHECK(size >= 0 && size < 100);
    std::vector<EntryChange> result;
    for (int i = 0; i < size; ++i) {
      std::unique_ptr<const Object> object;
      EXPECT_EQ(Status::OK, fake_storage_.AddObjectSynchronous(
                                ftl::StringPrintf("object%02d", i), &object));
      result.push_back(EntryChange{Entry{ftl::StringPrintf("key%02d", i),
                                         object->GetId(), KeyPriority::EAGER},
                                   false});
    }
    return result;
  }

  ObjectId CreateEmptyContents() {
    ObjectId id;
    EXPECT_EQ(Status::OK,
              TreeNode::FromEntries(&fake_storage_, std::vector<Entry>(),
                                    std::vector<ObjectId>(1), &id));
    return id;
  }

  ObjectId CreateTree(std::vector<EntryChange>& entries) {
    ObjectId root_id = CreateEmptyContents();
    ObjectId new_root_id;
    btree::ApplyChanges(
        &fake_storage_, root_id, kTestNodeSize,
        std::make_unique<EntryChangeIterator>(entries.begin(), entries.end()),
        [&new_root_id](Status status, ObjectId obj_id,
                       std::unordered_set<ObjectId> new_nodes) {
          EXPECT_EQ(Status::OK, status);
          new_root_id = obj_id;
        });
    return new_root_id;
  }

 protected:
  TrackGetObjectFakePageStorage fake_storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(BTreeUtilsTest);
};

TEST_F(BTreeUtilsTest, ApplyChangesFromEmpty) {
  ObjectId root_id = CreateEmptyContents();
  std::vector<EntryChange> changes = CreateEntryChanges(4);

  // Expected layout (X is key "keyX"):
  // [1, 2, 3, 4]
  ObjectId new_root_id;
  btree::ApplyChanges(
      &fake_storage_, root_id, 4,
      std::make_unique<EntryChangeIterator>(changes.begin(), changes.end()),
      [&new_root_id](Status status, ObjectId obj_id,
                     std::unordered_set<ObjectId> new_nodes) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_EQ(1u, new_nodes.size());
        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
        new_root_id = obj_id;
      });

  CommitContentsImpl reader(new_root_id, &fake_storage_);
  std::unique_ptr<Iterator<const Entry>> entries = reader.begin();
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(entries->Valid());
    EXPECT_EQ(changes[i].entry, **entries);
    entries->Next();
  }
  EXPECT_FALSE(entries->Valid());
}

TEST_F(BTreeUtilsTest, ApplyChangesManyEntries) {
  ObjectId root_id = CreateEmptyContents();
  std::vector<EntryChange> golden_entries = CreateEntryChanges(11);

  ObjectId new_root_id;
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10]
  btree::ApplyChanges(&fake_storage_, root_id, 4,
                      std::make_unique<EntryChangeIterator>(
                          golden_entries.begin(), golden_entries.end()),
                      [&new_root_id](Status status, ObjectId obj_id,
                                     std::unordered_set<ObjectId> new_nodes) {
                        EXPECT_EQ(Status::OK, status);
                        EXPECT_EQ(4u, new_nodes.size());
                        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
                        new_root_id = obj_id;
                      });

  CommitContentsImpl reader(new_root_id, &fake_storage_);
  std::unique_ptr<Iterator<const Entry>> entries = reader.begin();
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_TRUE(entries->Valid());
    EXPECT_EQ(golden_entries[i].entry, **entries)
        << "Expected " << golden_entries[i].entry.key << " but found "
        << (*entries)->key;
    entries->Next();
  }
  EXPECT_FALSE(entries->Valid());

  Entry new_entry = {"key071", "objectid071", KeyPriority::EAGER};
  std::vector<EntryChange> new_change{EntryChange{new_entry, false}};
  // Insert key "071" between keys "07" and "08".
  golden_entries.insert(golden_entries.begin() + 8, new_change[0]);

  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [071, 08, 09, 10]
  ObjectId new_root_id2;
  btree::ApplyChanges(&fake_storage_, new_root_id, 4,
                      std::make_unique<EntryChangeIterator>(new_change.begin(),
                                                            new_change.end()),
                      [&new_root_id2](Status status, ObjectId obj_id,
                                      std::unordered_set<ObjectId> new_nodes) {
                        EXPECT_EQ(Status::OK, status);
                        // The root and the 3rd child have changed.
                        EXPECT_EQ(2u, new_nodes.size());
                        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
                        new_root_id2 = obj_id;
                      });

  CommitContentsImpl reader2(new_root_id2, &fake_storage_);
  entries = reader2.begin();
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_TRUE(entries->Valid());
    EXPECT_EQ(golden_entries[i].entry, **entries)
        << "Expected " << golden_entries[i].entry.key << " but found "
        << (*entries)->key;
    entries->Next();
  }
  EXPECT_FALSE(entries->Valid());
}

TEST_F(BTreeUtilsTest, DeleteChanges) {
  ObjectId root_id = CreateEmptyContents();
  std::vector<EntryChange> golden_entries = CreateEntryChanges(11);

  std::vector<Entry> entries_to_delete{golden_entries[2].entry,
                                       golden_entries[4].entry};

  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [071, 08, 09, 10]
  ObjectId tmp_root_id;
  btree::ApplyChanges(&fake_storage_, root_id, 4,
                      std::make_unique<EntryChangeIterator>(
                          golden_entries.begin(), golden_entries.end()),
                      [&tmp_root_id](Status status, ObjectId obj_id,
                                     std::unordered_set<ObjectId> new_nodes) {
                        EXPECT_EQ(Status::OK, status);
                        EXPECT_EQ(4u, new_nodes.size());
                        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
                        tmp_root_id = obj_id;
                      });

  // Delete entries.
  std::vector<EntryChange> delete_changes;
  for (size_t i = 0; i < entries_to_delete.size(); ++i) {
    delete_changes.push_back(EntryChange{entries_to_delete[i], true});
  }

  // Expected layout (XX is key "keyXX"):
  //            [03, 07]
  //         /     |        \
  // [00, 01]  [05, 06]    [071, 08, 09, 10]
  ObjectId new_root_id;
  btree::ApplyChanges(&fake_storage_, tmp_root_id, 4,
                      std::make_unique<EntryChangeIterator>(
                          delete_changes.begin(), delete_changes.end()),
                      [&new_root_id](Status status, ObjectId obj_id,
                                     std::unordered_set<ObjectId> new_nodes) {
                        EXPECT_EQ(Status::OK, status);
                        // The root and the first 2 children have changed.
                        EXPECT_EQ(3u, new_nodes.size());
                        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
                        new_root_id = obj_id;
                      });

  CommitContentsImpl reader(new_root_id, &fake_storage_);
  std::unique_ptr<Iterator<const Entry>> entries = reader.begin();
  size_t deleted_index = 0;
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    if (golden_entries[i].entry == entries_to_delete[deleted_index]) {
      // Skip deleted entries
      deleted_index++;
      continue;
    }
    EXPECT_TRUE(entries->Valid());
    EXPECT_EQ(golden_entries[i].entry, **entries)
        << "Expected " << golden_entries[i].entry.key << " but found "
        << (*entries)->key;
    entries->Next();
  }
  EXPECT_FALSE(entries->Valid());
}

TEST_F(BTreeUtilsTest, GetObjectFromEmpty) {
  ObjectId root_id = CreateEmptyContents();
  std::set<ObjectId> objects;
  ASSERT_EQ(Status::OK, btree::GetObjects(root_id, &fake_storage_, &objects));
  EXPECT_EQ(1u, objects.size());
  EXPECT_TRUE(objects.find(root_id) != objects.end());
}

TEST_F(BTreeUtilsTest, GetObjectOneNodeTree) {
  std::vector<EntryChange> entries = CreateEntryChanges(kTestNodeSize);
  ObjectId root_id = CreateTree(entries);

  std::set<ObjectId> objects;
  ASSERT_EQ(Status::OK, btree::GetObjects(root_id, &fake_storage_, &objects));
  EXPECT_EQ(5u, objects.size());
  EXPECT_TRUE(objects.find(root_id) != objects.end());
  for (EntryChange& e : entries) {
    EXPECT_TRUE(objects.find(e.entry.object_id) != objects.end());
  }
}

TEST_F(BTreeUtilsTest, GetObjectBigTree) {
  std::vector<EntryChange> entries = CreateEntryChanges(99);
  ObjectId root_id = CreateTree(entries);

  std::set<ObjectId> objects;
  ASSERT_EQ(Status::OK, btree::GetObjects(root_id, &fake_storage_, &objects));
  EXPECT_EQ(99u + 25u, objects.size());
  EXPECT_TRUE(objects.find(root_id) != objects.end());
  for (EntryChange& e : entries) {
    EXPECT_TRUE(objects.find(e.entry.object_id) != objects.end());
  }
}

TEST_F(BTreeUtilsTest, GetObjectsFromSync) {
  std::vector<EntryChange> entries = CreateEntryChanges(5);
  entries[3].entry.priority = KeyPriority::LAZY;
  ObjectId root_id = CreateTree(entries);

  // Expected layout (XX is key "keyXX"):
  //        [02]
  //      /      \
  // [00, 01]  [03, 04]
  fake_storage_.object_requests.clear();
  btree::GetObjectsFromSync(root_id, &fake_storage_, [this](Status s) {
    EXPECT_EQ(Status::OK, s);
    message_loop_.PostQuitTask();
  });
  EXPECT_FALSE(RunLoopWithTimeout());

  std::vector<ObjectId> object_requests;
  std::copy(fake_storage_.object_requests.begin(),
            fake_storage_.object_requests.end(),
            std::back_inserter(object_requests));
  // There are 8 objects: 3 nodes and 4 eager values and 1 lazy. Except from the
  // lazy object, all others should have been requested.
  EXPECT_EQ(3 + 4u, object_requests.size());

  std::set<ObjectId> objects;
  ASSERT_EQ(Status::OK, btree::GetObjects(root_id, &fake_storage_, &objects));
  ASSERT_EQ(3 + 5u, objects.size());
  for (ObjectId& id : object_requests) {
    // entries[3] contains the lazy value.
    if (id != entries[3].entry.object_id) {
      EXPECT_TRUE(objects.find(id) != objects.end());
    }
  }
}

}  // namespace
}  // namespace storage
