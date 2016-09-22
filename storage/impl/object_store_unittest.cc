// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/object_store.h"

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/impl/tree_node.h"
#include "apps/ledger/storage/public/commit_contents.h"
#include "apps/ledger/storage/public/constants.h"
#include "gtest/gtest.h"
#include "lib/ftl/logging.h"

namespace storage {

namespace {

ObjectId RandomId() {
  std::string result;
  result.resize(kObjectIdSize);
  glue::RandBytes(&result[0], kObjectIdSize);
  return result;
}

std::vector<Entry> GetEntries(int size) {
  // Lower case letters are used as keys.
  FTL_DCHECK(size < 26);
  std::vector<Entry> entries;
  for (int i = 0; i < size; ++i) {
    Entry entry;
    entry.key = std::string(1, 'a' + i);
    entry.blob_id = RandomId();
    entry.priority = KeyPriority::EAGER;
    entries.push_back(entry);
  }
  return entries;
}

void ExpectEntriesEqual(const Entry& expected, const Entry& found) {
  EXPECT_EQ(expected.key, found.key);
  EXPECT_EQ(expected.blob_id, found.blob_id);
  EXPECT_EQ(expected.priority, found.priority);
}

}  // namespace

class ObjectStoreTest : public ::testing::Test {
 public:
  ObjectStoreTest() {}

  ~ObjectStoreTest() override {}

 protected:
  std::unique_ptr<TreeNode> FromId(const ObjectId& id) {
    std::unique_ptr<TreeNode> node;
    EXPECT_EQ(Status::OK, TreeNode::FromId(&store_, id, &node));
    return node;
  }

  std::unique_ptr<TreeNode> FromEntries(const std::vector<Entry>& entries,
                                        const std::vector<ObjectId>& children) {
    ObjectId id;
    EXPECT_EQ(Status::OK,
              TreeNode::FromEntries(&store_, entries, children, &id));
    return FromId(id);
  }

  ObjectStore store_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ObjectStoreTest);
};

TEST_F(ObjectStoreTest, CreateGetTreeNode) {
  std::unique_ptr<Object> node =
      FromEntries(std::vector<Entry>(), std::vector<ObjectId>(1));

  std::unique_ptr<TreeNode> foundNode;
  EXPECT_EQ(Status::OK, store_.GetTreeNode(node->GetId(), &foundNode));

  EXPECT_EQ(Status::NOT_FOUND, store_.GetTreeNode(RandomId(), &foundNode));
}

TEST_F(ObjectStoreTest, TreeNodeGetEntryChild) {
  int size = 10;
  std::vector<Entry> entries = GetEntries(size);
  std::unique_ptr<TreeNode> node =
      FromEntries(entries, std::vector<ObjectId>(size + 1));
  EXPECT_EQ(size, node->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    Entry foundEntry;
    EXPECT_EQ(Status::OK, node->GetEntry(i, &foundEntry));
    ExpectEntriesEqual(entries[i], foundEntry);
  }

  for (int i = 0; i <= size; ++i) {
    std::unique_ptr<TreeNode> child;
    EXPECT_EQ(Status::NOT_FOUND, node->GetChild(i, &child));
  }
}

TEST_F(ObjectStoreTest, TreeNodeSplitMerge) {
  int size = 10;
  std::vector<Entry> entries = GetEntries(size);
  std::unique_ptr<TreeNode> node =
      FromEntries(entries, std::vector<ObjectId>(size + 1));

  int splitIndex = 3;
  ObjectId leftId;
  ObjectId rightId;
  EXPECT_EQ(Status::OK, node->Split(splitIndex, "", "", &leftId, &rightId));

  std::unique_ptr<TreeNode> leftNode = FromId(leftId);
  EXPECT_EQ(splitIndex, leftNode->GetKeyCount());
  for (int i = 0; i < splitIndex; ++i) {
    Entry foundEntry;
    leftNode->GetEntry(i, &foundEntry);
    ExpectEntriesEqual(entries[i], foundEntry);
  }

  std::unique_ptr<TreeNode> rightNode = FromId(rightId);
  EXPECT_EQ(size - splitIndex, rightNode->GetKeyCount());
  for (int i = 0; i < size - splitIndex; ++i) {
    Entry foundEntry;
    EXPECT_EQ(Status::OK, rightNode->GetEntry(i, &foundEntry));
    ExpectEntriesEqual(entries[splitIndex + i], foundEntry);
  }

  // Merge
  ObjectId mergedId;
  EXPECT_EQ(Status::OK,
            TreeNode::Merge(&store_, leftId, rightId, "", &mergedId));
  std::unique_ptr<TreeNode> mergedNode = FromId(mergedId);
  EXPECT_EQ(size, mergedNode->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    Entry foundEntry;
    EXPECT_EQ(Status::OK, mergedNode->GetEntry(i, &foundEntry));
    ExpectEntriesEqual(entries[i], foundEntry);
  }
}

}  // namespace
