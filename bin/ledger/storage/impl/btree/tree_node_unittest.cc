// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/tree_node.h"

#include "gtest/gtest.h"
#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/storage/fake/fake_page_storage.h"
#include "peridot/bin/ledger/storage/impl/btree/encoding.h"
#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/constants.h"

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
    ObjectIdentifier root_identifier;
    EXPECT_TRUE(GetEmptyNodeIdentifier(&root_identifier));
    std::unique_ptr<const TreeNode> node;
    EXPECT_TRUE(CreateNodeFromIdentifier(root_identifier, &node));
    return node;
  }

  Entry GetEntry(const TreeNode* node, int index) {
    Entry found_entry;
    EXPECT_EQ(Status::OK, node->GetEntry(index, &found_entry));
    return found_entry;
  }

  std::map<size_t, ObjectIdentifier> CreateChildren(int size) {
    std::map<size_t, ObjectIdentifier> children;
    for (int i = 0; i < size; ++i) {
      children[i] = CreateEmptyNode()->GetIdentifier();
    }
    return children;
  }

  fake::FakePageStorage fake_storage_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TreeNodeTest);
};

TEST_F(TreeNodeTest, CreateGetTreeNode) {
  std::unique_ptr<const TreeNode> node = CreateEmptyNode();

  bool called;
  Status status;
  std::unique_ptr<const TreeNode> found_node;
  TreeNode::FromIdentifier(&fake_storage_, node->GetIdentifier(),
                           callback::Capture(callback::SetWhenCalled(&called),
                                             &status, &found_node));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_NE(nullptr, found_node);

  TreeNode::FromIdentifier(&fake_storage_, RandomObjectIdentifier(),
                           callback::Capture(callback::SetWhenCalled(&called),
                                             &status, &found_node));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::NOT_FOUND, status);
}

TEST_F(TreeNodeTest, GetEntryChild) {
  int size = 10;
  std::vector<Entry> entries;
  ASSERT_TRUE(CreateEntries(size, &entries));
  std::unique_ptr<const TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));
  EXPECT_EQ(size, node->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(entries[i], GetEntry(node.get(), i));
  }

  bool called;
  for (int i = 0; i <= size; ++i) {
    Status status;
    std::unique_ptr<const TreeNode> child;
    node->GetChild(i, callback::Capture(callback::SetWhenCalled(&called),
                                        &status, &child));
    RunLoopFor(kSufficientDelay);
    ASSERT_TRUE(called);
    ASSERT_EQ(Status::NO_SUCH_CHILD, status);
    EXPECT_EQ(node->children_identifiers().find(i),
              node->children_identifiers().end());
  }
}

TEST_F(TreeNodeTest, FindKeyOrChild) {
  int size = 10;
  std::vector<Entry> entries;
  ASSERT_TRUE(CreateEntries(size, &entries));
  std::unique_ptr<const TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));

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
  std::map<size_t, ObjectIdentifier> children = CreateChildren(size + 1);
  std::unique_ptr<const TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries(entries, children, &node));

  bool called;
  Status status;
  std::unique_ptr<const Object> object;
  fake_storage_.GetObject(
      node->GetIdentifier(), PageStorage::Location::LOCAL,
      callback::Capture(callback::SetWhenCalled(&called), &status, &object));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> retrieved_node;
  EXPECT_EQ(node->GetIdentifier(), object->GetIdentifier());
  ASSERT_TRUE(CreateNodeFromIdentifier(node->GetIdentifier(), &retrieved_node));

  fxl::StringView data;
  EXPECT_EQ(Status::OK, object->GetData(&data));
  uint8_t level;
  std::vector<Entry> parsed_entries;
  std::map<size_t, ObjectIdentifier> parsed_children;
  EXPECT_TRUE(DecodeNode(data, &level, &parsed_entries, &parsed_children));
  EXPECT_EQ(entries, parsed_entries);
  EXPECT_EQ(children, parsed_children);
}

}  // namespace
}  // namespace btree
}  // namespace storage
