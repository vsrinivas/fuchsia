// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/store/object_store.h"

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/impl/store/tree_node.h"
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
    Entry entry{std::string(1, 'a' + i), RandomId(), KeyPriority::EAGER};
    entries.push_back(entry);
  }
  return entries;
}

}  // namespace

class ObjectStoreTest : public ::testing::Test {
 public:
  ObjectStoreTest() {}

  ~ObjectStoreTest() override {}

  // Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    std::srand(0);
  }

 protected:
  std::unique_ptr<const TreeNode> FromId(const ObjectId& id) {
    std::unique_ptr<const TreeNode> node;
    EXPECT_EQ(Status::OK, TreeNode::FromId(&store_, id, &node));
    return node;
  }

  std::unique_ptr<const TreeNode> FromEntries(
      const std::vector<Entry>& entries,
      const std::vector<ObjectId>& children) {
    ObjectId id;
    EXPECT_EQ(Status::OK,
              TreeNode::FromEntries(&store_, entries, children, &id));
    return FromId(id);
  }

  Entry GetEntry(const TreeNode* node, int index) {
    Entry foundEntry;
    EXPECT_EQ(Status::OK, node->GetEntry(index, &foundEntry));
    return foundEntry;
  }

  std::unique_ptr<const TreeNode> CreateEmptyNode() {
    return FromEntries(std::vector<Entry>(), std::vector<ObjectId>(1));
  }

  std::vector<ObjectId> CreateChildren(int size) {
    std::vector<ObjectId> children;
    for (int i = 0; i < size; ++i) {
      children.push_back(CreateEmptyNode()->GetId());
    }
    return children;
  }

  ObjectId GetChildId(const TreeNode* node, int index) {
    std::unique_ptr<const TreeNode> foundChild;
    EXPECT_EQ(Status::OK, node->GetChild(index, &foundChild));
    return foundChild->GetId();
  }

  ObjectStore store_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ObjectStoreTest);
};

TEST_F(ObjectStoreTest, CreateGetTreeNode) {
  std::unique_ptr<const Object> node = CreateEmptyNode();

  std::unique_ptr<const TreeNode> foundNode;
  EXPECT_EQ(Status::OK, store_.GetTreeNode(node->GetId(), &foundNode));

  EXPECT_EQ(Status::NOT_FOUND, store_.GetTreeNode(RandomId(), &foundNode));
}

TEST_F(ObjectStoreTest, TreeNodeGetEntryChild) {
  int size = 10;
  std::vector<Entry> entries = GetEntries(size);
  std::unique_ptr<const TreeNode> node =
      FromEntries(entries, std::vector<ObjectId>(size + 1));
  EXPECT_EQ(size, node->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(entries[i], GetEntry(node.get(), i));
  }

  for (int i = 0; i <= size; ++i) {
    std::unique_ptr<const TreeNode> child;
    EXPECT_EQ(Status::NOT_FOUND, node->GetChild(i, &child));
  }
}

TEST_F(ObjectStoreTest, TreeNodeSplitMerge) {
  int size = 10;
  std::vector<Entry> entries = GetEntries(size);
  std::unique_ptr<const TreeNode> node =
      FromEntries(entries, std::vector<ObjectId>(size + 1));

  int splitIndex = 3;
  ObjectId leftId;
  ObjectId rightId;
  EXPECT_EQ(Status::OK, node->Split(splitIndex, "", "", &leftId, &rightId));

  std::unique_ptr<const TreeNode> leftNode = FromId(leftId);
  EXPECT_EQ(splitIndex, leftNode->GetKeyCount());
  for (int i = 0; i < splitIndex; ++i) {
    EXPECT_EQ(entries[i], GetEntry(leftNode.get(), i));
  }

  std::unique_ptr<const TreeNode> rightNode = FromId(rightId);
  EXPECT_EQ(size - splitIndex, rightNode->GetKeyCount());
  for (int i = 0; i < size - splitIndex; ++i) {
    EXPECT_EQ(entries[splitIndex + i], GetEntry(rightNode.get(), i));
  }

  // Merge
  ObjectId mergedId;
  EXPECT_EQ(Status::OK,
            TreeNode::Merge(&store_, leftId, rightId, "", &mergedId));
  std::unique_ptr<const TreeNode> mergedNode = FromId(mergedId);
  EXPECT_EQ(size, mergedNode->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(entries[i], GetEntry(mergedNode.get(), i));
  }
}

TEST_F(ObjectStoreTest, TreeNodeFindKeyOrChild) {
  int size = 10;
  std::vector<Entry> entries = GetEntries(size);
  std::unique_ptr<const TreeNode> node =
      FromEntries(entries, std::vector<ObjectId>(size + 1));

  int index;
  EXPECT_EQ(Status::OK, node->FindKeyOrChild("a", &index));
  EXPECT_EQ(0, index);

  EXPECT_EQ(Status::OK, node->FindKeyOrChild("c", &index));
  EXPECT_EQ(2, index);

  EXPECT_EQ(Status::OK, node->FindKeyOrChild("j", &index));
  EXPECT_EQ(9, index);

  EXPECT_EQ(Status::NOT_FOUND, node->FindKeyOrChild("0", &index));
  EXPECT_EQ(0, index);

  EXPECT_EQ(Status::NOT_FOUND, node->FindKeyOrChild("aa", &index));
  EXPECT_EQ(1, index);

  EXPECT_EQ(Status::NOT_FOUND, node->FindKeyOrChild("cc", &index));
  EXPECT_EQ(3, index);

  EXPECT_EQ(Status::NOT_FOUND, node->FindKeyOrChild("z", &index));
  EXPECT_EQ(10, index);
}

TEST_F(ObjectStoreTest, TreeNodeMutationAddEntry) {
  int size = 2;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));

  Entry entry{"ab", RandomId(), KeyPriority::EAGER};
  ObjectId left = CreateEmptyNode()->GetId();
  ObjectId right = CreateEmptyNode()->GetId();

  ObjectId newNodeId;
  EXPECT_EQ(
      Status::OK,
      node->StartMutation().AddEntry(entry, left, right).Finish(&newNodeId));
  std::unique_ptr<const TreeNode> newNode = FromId(newNodeId);

  // Initial node:
  //   [ a, b]
  //   /  |   \
  // 0    1    2
  //
  // After adding entry ab:
  //   [ a, ab, b]
  //   /  |   |   \
  // 0  left right 2
  EXPECT_EQ(size + 1, newNode->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(newNode.get(), 0));
  EXPECT_EQ(entry, GetEntry(newNode.get(), 1));
  EXPECT_EQ(GetEntry(node.get(), 1), GetEntry(newNode.get(), 2));

  EXPECT_EQ(GetChildId(node.get(), 0), GetChildId(newNode.get(), 0));
  EXPECT_EQ(left, GetChildId(newNode.get(), 1));
  EXPECT_EQ(right, GetChildId(newNode.get(), 2));
  EXPECT_EQ(GetChildId(node.get(), 2), GetChildId(newNode.get(), 3));
}

TEST_F(ObjectStoreTest, TreeNodeMutationUpdateEntry) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));
  ObjectId newNodeId;

  Entry entry{"b", RandomId(), KeyPriority::EAGER};
  EXPECT_EQ(Status::OK,
            node->StartMutation().UpdateEntry(entry).Finish(&newNodeId));
  std::unique_ptr<const TreeNode> newNode = FromId(newNodeId);

  // Initial node:
  //   [ a, b, c]
  //   /  |   |  \
  // 0    1   2   3
  //
  // After updating entry b:
  // (same with different value for b)
  EXPECT_EQ(size, newNode->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(newNode.get(), 0));
  EXPECT_EQ(entry, GetEntry(newNode.get(), 1));
  EXPECT_EQ(GetEntry(node.get(), 2), GetEntry(newNode.get(), 2));

  for (int i = 0; i <= size; ++i) {
    EXPECT_EQ(GetChildId(node.get(), i), GetChildId(newNode.get(), i));
  }
}

TEST_F(ObjectStoreTest, TreeNodeMutationRemoveEntry) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));

  ObjectId newNodeId;
  ObjectId child = CreateEmptyNode()->GetId();
  EXPECT_EQ(Status::OK,
            node->StartMutation().RemoveEntry("b", child).Finish(&newNodeId));
  std::unique_ptr<const TreeNode> newNode = FromId(newNodeId);

  // Initial node:
  //   [ a, b, c]
  //   /  |   |  \
  // 0    1   2   3
  //
  // After removing entry b:
  //   [ a, c]
  //   /  |   \
  // 0  child  3
  EXPECT_EQ(size - 1, newNode->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(newNode.get(), 0));
  EXPECT_EQ(GetEntry(node.get(), 2), GetEntry(newNode.get(), 1));

  EXPECT_EQ(GetChildId(node.get(), 0), GetChildId(newNode.get(), 0));
  EXPECT_EQ(child, GetChildId(newNode.get(), 1));
  EXPECT_EQ(GetChildId(node.get(), 3), GetChildId(newNode.get(), 2));
}

TEST_F(ObjectStoreTest, TreeNodeMutationUpdateChildId) {
  int size = 2;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));

  ObjectId newNodeId;
  ObjectId child = CreateEmptyNode()->GetId();
  EXPECT_EQ(Status::OK,
            node->StartMutation().UpdateChildId("b", child).Finish(&newNodeId));
  std::unique_ptr<const TreeNode> newNode = FromId(newNodeId);

  // Initial node:
  //   [ a, b]
  //   /  |   \
  // 0    1    2
  //
  // After updating the child before b:
  //   [ a, b]
  //   /  |   \
  // 0  child  2
  EXPECT_EQ(size, newNode->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(newNode.get(), 0));
  EXPECT_EQ(GetEntry(node.get(), 1), GetEntry(newNode.get(), 1));

  EXPECT_EQ(GetChildId(node.get(), 0), GetChildId(newNode.get(), 0));
  EXPECT_EQ(child, GetChildId(newNode.get(), 1));
  EXPECT_EQ(GetChildId(node.get(), 2), GetChildId(newNode.get(), 2));
}

TEST_F(ObjectStoreTest, TreeNodeEmptyMutation) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));

  ObjectId newNodeId;
  // Note that creating an empty mutation is inefficient and should be avoided
  // when possible.
  EXPECT_EQ(Status::OK, node->StartMutation().Finish(&newNodeId));
  std::unique_ptr<const TreeNode> newNode = FromId(newNodeId);
  // TOOD(nellyv): check that the new id is equal to the original one when ids
  // are not randomly assigned.

  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(GetEntry(node.get(), i), GetEntry(newNode.get(), i));
  }

  for (int i = 0; i <= size; ++i) {
    EXPECT_EQ(GetChildId(node.get(), i), GetChildId(newNode.get(), i));
  }
}

}  // namespace storage
