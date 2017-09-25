// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/tree_node.h"

#include "gtest/gtest.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/callback/capture.h"
#include "peridot/bin/ledger/glue/crypto/rand.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/impl/btree/encoding.h"
#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace storage {
namespace btree {
namespace {

class TreeNodeTest : public StorageTest {
 public:
  TreeNodeTest() : fake_storage_("page_id") {}

  ~TreeNodeTest() override {}

  // Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    std::srand(0);
  }

 protected:
  PageStorage* GetStorage() override { return &fake_storage_; }

  std::unique_ptr<const TreeNode> CreateEmptyNode() {
    ObjectId root_id;
    EXPECT_TRUE(GetEmptyNodeId(&root_id));
    std::unique_ptr<const TreeNode> node;
    EXPECT_TRUE(CreateNodeFromId(root_id, &node));
    return node;
  }

  Entry GetEntry(const TreeNode* node, int index) {
    Entry found_entry;
    EXPECT_EQ(Status::OK, node->GetEntry(index, &found_entry));
    return found_entry;
  }

  std::vector<ObjectId> CreateChildren(int size) {
    std::vector<ObjectId> children;
    children.reserve(size);
    for (int i = 0; i < size; ++i) {
      children.push_back(CreateEmptyNode()->GetId());
    }
    return children;
  }

  fake::FakePageStorage fake_storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TreeNodeTest);
};

TEST_F(TreeNodeTest, CreateGetTreeNode) {
  std::unique_ptr<const TreeNode> node = CreateEmptyNode();

  Status status;
  std::unique_ptr<const TreeNode> found_node;
  TreeNode::FromId(&fake_storage_, node->GetId(),
                   callback::Capture(MakeQuitTask(), &status, &found_node));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, found_node);

  TreeNode::FromId(&fake_storage_, RandomObjectId(),
                   callback::Capture(MakeQuitTask(), &status, &found_node));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::NOT_FOUND, status);
}

TEST_F(TreeNodeTest, GetEntryChild) {
  int size = 10;
  std::vector<Entry> entries;
  ASSERT_TRUE(CreateEntries(size, &entries));
  std::unique_ptr<const TreeNode> node;
  ASSERT_TRUE(
      CreateNodeFromEntries(entries, std::vector<ObjectId>(size + 1), &node));
  EXPECT_EQ(size, node->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(entries[i], GetEntry(node.get(), i));
  }

  for (int i = 0; i <= size; ++i) {
    Status status;
    std::unique_ptr<const TreeNode> child;
    node->GetChild(i, callback::Capture(MakeQuitTask(), &status, &child));
    EXPECT_FALSE(RunLoopWithTimeout());
    ASSERT_EQ(Status::NO_SUCH_CHILD, status);
    EXPECT_TRUE(node->GetChildId(i).empty());
  }
}

TEST_F(TreeNodeTest, FindKeyOrChild) {
  int size = 10;
  std::vector<Entry> entries;
  ASSERT_TRUE(CreateEntries(size, &entries));
  std::unique_ptr<const TreeNode> node;
  ASSERT_TRUE(
      CreateNodeFromEntries(entries, std::vector<ObjectId>(size + 1), &node));

  int index;
  EXPECT_EQ(Status::OK, node->FindKeyOrChild("key00", &index));
  EXPECT_EQ(0, index);

  EXPECT_EQ(Status::OK, node->FindKeyOrChild("key02", &index));
  EXPECT_EQ(2, index);

  EXPECT_EQ(Status::OK, node->FindKeyOrChild("key09", &index));
  EXPECT_EQ(9, index);

  EXPECT_EQ(Status::NOT_FOUND, node->FindKeyOrChild("0", &index));
  EXPECT_EQ(0, index);

  EXPECT_EQ(Status::NOT_FOUND, node->FindKeyOrChild("key001", &index));
  EXPECT_EQ(1, index);

  EXPECT_EQ(Status::NOT_FOUND, node->FindKeyOrChild("key020", &index));
  EXPECT_EQ(3, index);

  EXPECT_EQ(Status::NOT_FOUND, node->FindKeyOrChild("key999", &index));
  EXPECT_EQ(10, index);
}

TEST_F(TreeNodeTest, Serialization) {
  int size = 3;
  std::vector<Entry> entries;
  ASSERT_TRUE(CreateEntries(size, &entries));
  std::vector<ObjectId> children = CreateChildren(size + 1);
  std::unique_ptr<const TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries(entries, children, &node));

  Status status;
  std::unique_ptr<const Object> object;
  fake_storage_.GetObject(node->GetId(), PageStorage::Location::LOCAL,
                          callback::Capture(MakeQuitTask(), &status, &object));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> retrieved_node;
  ASSERT_TRUE(CreateNodeFromId(object->GetId(), &retrieved_node));

  fxl::StringView data;
  EXPECT_EQ(Status::OK, object->GetData(&data));
  uint8_t level;
  std::vector<Entry> parsed_entries;
  std::vector<ObjectId> parsed_children;
  EXPECT_TRUE(DecodeNode(data, &level, &parsed_entries, &parsed_children));
  EXPECT_EQ(entries, parsed_entries);
  EXPECT_EQ(children, parsed_children);
}

}  // namespace
}  // namespace btree
}  // namespace storage
