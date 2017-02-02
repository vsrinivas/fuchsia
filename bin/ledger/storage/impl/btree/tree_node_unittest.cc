// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/tree_node.h"

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/impl/btree/encoding.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/storage/test/storage_test_utils.h"
#include "apps/ledger/src/test/capture.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/logging.h"

namespace storage {
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
    return CreateNodeFromId(GetEmptyNodeId());
  }

  Entry GetEntry(const TreeNode* node, int index) {
    Entry found_entry;
    EXPECT_EQ(Status::OK, node->GetEntry(index, &found_entry));
    return found_entry;
  }

  std::vector<ObjectId> CreateChildren(int size) {
    std::vector<ObjectId> children;
    for (int i = 0; i < size; ++i) {
      children.push_back(CreateEmptyNode()->GetId());
    }
    return children;
  }

  fake::FakePageStorage fake_storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(TreeNodeTest);
};

TEST_F(TreeNodeTest, CreateGetTreeNode) {
  std::unique_ptr<const TreeNode> node = CreateEmptyNode();

  Status status;
  std::unique_ptr<const TreeNode> found_node;
  TreeNode::FromId(&fake_storage_, node->GetId(),
                   ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                   &status, &found_node));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, found_node);

  TreeNode::FromId(&fake_storage_, RandomId(kObjectIdSize),
                   ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                   &status, &found_node));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::NOT_FOUND, status);
}

TEST_F(TreeNodeTest, GetEntryChild) {
  int size = 10;
  std::vector<Entry> entries = CreateEntries(size);
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(entries, std::vector<ObjectId>(size + 1));
  EXPECT_EQ(size, node->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(entries[i], GetEntry(node.get(), i));
  }

  for (int i = 0; i <= size; ++i) {
    Status status;
    std::unique_ptr<const TreeNode> child;
    node->GetChild(i,
                   ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                   &status, &child));
    EXPECT_FALSE(RunLoopWithTimeout());
    ASSERT_EQ(Status::NO_SUCH_CHILD, status);
    EXPECT_TRUE(node->GetChildId(i).empty());
  }
}

TEST_F(TreeNodeTest, SplitMerge) {
  int size = 10;
  std::vector<Entry> entries = CreateEntries(size);
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(entries, std::vector<ObjectId>(size + 1));

  int split_index = 3;
  Status status;
  ObjectId left_id;
  ObjectId right_id;
  node->Split(split_index, "", "",
              ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                              &left_id, &right_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  std::unique_ptr<const TreeNode> left_node = CreateNodeFromId(left_id);
  EXPECT_EQ(split_index, left_node->GetKeyCount());
  for (int i = 0; i < split_index; ++i) {
    EXPECT_EQ(entries[i], GetEntry(left_node.get(), i));
  }

  std::unique_ptr<const TreeNode> right_node = CreateNodeFromId(right_id);
  EXPECT_EQ(size - split_index, right_node->GetKeyCount());
  for (int i = 0; i < size - split_index; ++i) {
    EXPECT_EQ(entries[split_index + i], GetEntry(right_node.get(), i));
  }

  // Merge
  ObjectId merged_id;
  TreeNode::Merge(&fake_storage_, std::move(left_node), std::move(right_node),
                  "",
                  ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                  &status, &merged_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  std::unique_ptr<const TreeNode> merged_node = CreateNodeFromId(merged_id);
  EXPECT_EQ(size, merged_node->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(entries[i], GetEntry(merged_node.get(), i));
  }
}

TEST_F(TreeNodeTest, FindKeyOrChild) {
  int size = 10;
  std::vector<Entry> entries = CreateEntries(size);
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(entries, std::vector<ObjectId>(size + 1));

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

TEST_F(TreeNodeTest, MutationAddEntry) {
  int size = 2;
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(CreateEntries(size), CreateChildren(size + 1));

  Entry entry{"key001", RandomId(kObjectIdSize), KeyPriority::EAGER};
  ObjectId left = CreateEmptyNode()->GetId();
  ObjectId right = CreateEmptyNode()->GetId();

  Status status;
  ObjectId new_node_id;
  node->StartMutation()
      .AddEntry(entry, left, right)
      .Finish(::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                              &new_node_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> new_node = CreateNodeFromId(new_node_id);

  // Initial node:
  //   [ 00, 01]
  //    /  |   \
  //  0    1    2
  //
  // After adding entry 001:
  //   [ 00, 001, 01]
  //    /  |   |   \
  //  0  left right 2
  EXPECT_EQ(size + 1, new_node->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(new_node.get(), 0));
  EXPECT_EQ(entry, GetEntry(new_node.get(), 1));
  EXPECT_EQ(GetEntry(node.get(), 1), GetEntry(new_node.get(), 2));

  EXPECT_EQ(node->GetChildId(0), new_node->GetChildId(0));
  EXPECT_EQ(left, new_node->GetChildId(1));
  EXPECT_EQ(right, new_node->GetChildId(2));
  EXPECT_EQ(node->GetChildId(2), new_node->GetChildId(3));
}

TEST_F(TreeNodeTest, MutationUpdateEntry) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(CreateEntries(size), CreateChildren(size + 1));

  Entry entry{"key01", RandomId(kObjectIdSize), KeyPriority::EAGER};
  Status status;
  ObjectId new_node_id;
  node->StartMutation().UpdateEntry(entry).Finish(::test::Capture(
      [this] { message_loop_.PostQuitTask(); }, &status, &new_node_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> new_node = CreateNodeFromId(new_node_id);

  // Initial node:
  //   [ 00, 01, 02]
  //    /  |   |  \
  //  0    1   2   3
  //
  // After updating entry b:
  // (same with different value for b)
  EXPECT_EQ(size, new_node->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(new_node.get(), 0));
  EXPECT_EQ(entry, GetEntry(new_node.get(), 1));
  EXPECT_EQ(GetEntry(node.get(), 2), GetEntry(new_node.get(), 2));

  for (int i = 0; i <= size; ++i) {
    EXPECT_EQ(node->GetChildId(i), new_node->GetChildId(i));
  }
}

TEST_F(TreeNodeTest, MutationRemoveEntry) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(CreateEntries(size), CreateChildren(size + 1));

  ObjectId child = CreateEmptyNode()->GetId();
  Status status;
  ObjectId new_node_id;
  node->StartMutation()
      .RemoveEntry("key01", child)
      .Finish(::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                              &new_node_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> new_node = CreateNodeFromId(new_node_id);

  // Initial node:
  //   [ 00, 01, 02]
  //    /  |   |   \
  //  0    1   2    3
  //
  // After removing entry b:
  //   [ 00, 02]
  //    /  |   \
  //  0  child  3
  EXPECT_EQ(size - 1, new_node->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(new_node.get(), 0));
  EXPECT_EQ(GetEntry(node.get(), 2), GetEntry(new_node.get(), 1));

  EXPECT_EQ(node->GetChildId(0), new_node->GetChildId(0));
  EXPECT_EQ(child, new_node->GetChildId(1));
  EXPECT_EQ(node->GetChildId(3), new_node->GetChildId(2));
}

TEST_F(TreeNodeTest, MutationUpdateChildId) {
  int size = 2;
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(CreateEntries(size), CreateChildren(size + 1));

  ObjectId child = CreateEmptyNode()->GetId();
  Status status;
  ObjectId new_node_id;
  node->StartMutation()
      .UpdateChildId("key01", child)
      .Finish(::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                              &new_node_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> new_node = CreateNodeFromId(new_node_id);

  // Initial node:
  //   [ 00, 01]
  //    /  |   \
  //  0    1    2
  //
  // After updating the child before b:
  //   [ 00, 01]
  //    /  |   \
  //  0  child  2
  EXPECT_EQ(size, new_node->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(new_node.get(), 0));
  EXPECT_EQ(GetEntry(node.get(), 1), GetEntry(new_node.get(), 1));

  EXPECT_EQ(node->GetChildId(0), new_node->GetChildId(0));
  EXPECT_EQ(child, new_node->GetChildId(1));
  EXPECT_EQ(node->GetChildId(2), new_node->GetChildId(2));
}

TEST_F(TreeNodeTest, EmptyMutation) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(CreateEntries(size), CreateChildren(size + 1));

  // Note that creating an empty mutation is inefficient and should be avoided
  // when possible.
  Status status;
  ObjectId new_node_id;
  node->StartMutation().Finish(::test::Capture(
      [this] { message_loop_.PostQuitTask(); }, &status, &new_node_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> new_node = CreateNodeFromId(new_node_id);
  ASSERT_EQ(size, new_node->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(GetEntry(node.get(), i), GetEntry(new_node.get(), i));
  }
  for (int i = 0; i <= size; ++i) {
    EXPECT_EQ(node->GetChildId(i), new_node->GetChildId(i));
  }
}

TEST_F(TreeNodeTest, Serialization) {
  int size = 3;
  std::vector<Entry> entries = CreateEntries(size);
  std::vector<ObjectId> children = CreateChildren(size + 1);
  std::unique_ptr<const TreeNode> node =
      CreateNodeFromEntries(entries, children);

  Status status;
  std::unique_ptr<const Object> object;
  fake_storage_.GetObject(
      node->GetId(),
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                      &object));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> retrieved_node =
      CreateNodeFromId(object->GetId());

  ftl::StringView data;
  EXPECT_EQ(Status::OK, object->GetData(&data));
  std::vector<Entry> parsed_entries;
  std::vector<ObjectId> parsed_children;
  EXPECT_TRUE(DecodeNode(data, &parsed_entries, &parsed_children));
  EXPECT_EQ(entries, parsed_entries);
  EXPECT_EQ(children, parsed_children);
}

}  // namespace
}  // namespace storage
