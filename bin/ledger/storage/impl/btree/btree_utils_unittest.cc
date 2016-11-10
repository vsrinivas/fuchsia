// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/btree_utils.h"

#include <stdio.h>

#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/impl/btree/btree_builder.h"
#include "apps/ledger/src/storage/impl/btree/entry_change_iterator.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/types.h"
#include "gtest/gtest.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

namespace storage {
namespace {

const int kTestNodeSize = 4;

std::vector<EntryChange> CreateEntryChanges(int size) {
  FTL_CHECK(size >= 0 && size < 100);
  std::vector<EntryChange> result;
  for (int i = 0; i < size; ++i) {
    std::string key = (i < 10 ? "key0" : "key") + ftl::NumberToString(i);
    result.push_back(EntryChange{
        Entry{key, "objectid" + ftl::NumberToString(i), KeyPriority::LAZY},
        false});
  }
  return result;
}

class BTreeUtilsTest : public ::testing::Test {
 public:
  BTreeUtilsTest() : fake_storage_("page_id") {}

  ~BTreeUtilsTest() override {}

  // Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    std::srand(0);
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
    BTreeBuilder::ApplyChanges(
        &fake_storage_, root_id, kTestNodeSize,
        std::unique_ptr<Iterator<const EntryChange>>(
            new EntryChangeIterator(entries.begin(), entries.end())),
        [&new_root_id](Status status, ObjectId obj_id,
                       std::unordered_set<ObjectId>&& new_nodes) {
          EXPECT_EQ(Status::OK, status);
          new_root_id = obj_id;
        });
    return new_root_id;
  }

 protected:
  fake::FakePageStorage fake_storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(BTreeUtilsTest);
};

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
  ASSERT_EQ(Status::OK,
            btree::GetObjects(root_id, &fake_storage_, &objects));
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
  ASSERT_EQ(Status::OK,
            btree::GetObjects(root_id, &fake_storage_, &objects));
  EXPECT_EQ(99u + 25u, objects.size());
  EXPECT_TRUE(objects.find(root_id) != objects.end());
  for (EntryChange& e : entries) {
    EXPECT_TRUE(objects.find(e.entry.object_id) != objects.end());
  }
}

}  // namespace
}  // namespace storage
