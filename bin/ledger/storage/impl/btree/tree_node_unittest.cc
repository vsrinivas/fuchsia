// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/tree_node.h"

#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/impl/btree/encoding.h"
#include "apps/ledger/src/storage/public/commit_contents.h"
#include "apps/ledger/src/storage/public/constants.h"
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

class TreeNodeTest : public ::testing::Test {
 public:
  TreeNodeTest() : fake_storage_("page_id") {}

  ~TreeNodeTest() override {}

  // Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    std::srand(0);
  }

 protected:
  std::unique_ptr<const TreeNode> FromId(ObjectIdView id) {
    std::unique_ptr<const TreeNode> node;
    EXPECT_EQ(Status::OK, TreeNode::FromId(&fake_storage_, id, &node));
    return node;
  }

  std::unique_ptr<const TreeNode> FromEntries(
      const std::vector<Entry>& entries,
      const std::vector<ObjectId>& children) {
    ObjectId id;
    EXPECT_EQ(Status::OK,
              TreeNode::FromEntries(&fake_storage_, entries, children, &id));
    return FromId(id);
  }

  Entry GetEntry(const TreeNode* node, int index) {
    Entry found_entry;
    EXPECT_EQ(Status::OK, node->GetEntry(index, &found_entry));
    return found_entry;
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
    std::unique_ptr<const TreeNode> found_child;
    EXPECT_EQ(Status::OK, node->GetChild(index, &found_child));
    return found_child->GetId();
  }

  fake::FakePageStorage fake_storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(TreeNodeTest);
};

TEST_F(TreeNodeTest, CreateGetTreeNode) {
  std::unique_ptr<const TreeNode> node = CreateEmptyNode();

  std::unique_ptr<const TreeNode> found_node;
  EXPECT_EQ(Status::OK,
            TreeNode::FromId(&fake_storage_, node->GetId(), &found_node));

  EXPECT_EQ(Status::NOT_FOUND,
            TreeNode::FromId(&fake_storage_, RandomId(), &found_node));
}

TEST_F(TreeNodeTest, GetEntryChild) {
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

TEST_F(TreeNodeTest, SplitMerge) {
  int size = 10;
  std::vector<Entry> entries = GetEntries(size);
  std::unique_ptr<const TreeNode> node =
      FromEntries(entries, std::vector<ObjectId>(size + 1));

  int split_index = 3;
  ObjectId left_id;
  ObjectId right_id;
  EXPECT_EQ(Status::OK, node->Split(split_index, "", "", &left_id, &right_id));

  std::unique_ptr<const TreeNode> left_node = FromId(left_id);
  EXPECT_EQ(split_index, left_node->GetKeyCount());
  for (int i = 0; i < split_index; ++i) {
    EXPECT_EQ(entries[i], GetEntry(left_node.get(), i));
  }

  std::unique_ptr<const TreeNode> right_node = FromId(right_id);
  EXPECT_EQ(size - split_index, right_node->GetKeyCount());
  for (int i = 0; i < size - split_index; ++i) {
    EXPECT_EQ(entries[split_index + i], GetEntry(right_node.get(), i));
  }

  // Merge
  ObjectId merged_id;
  EXPECT_EQ(Status::OK, TreeNode::Merge(&fake_storage_, std::move(left_node),
                                        std::move(right_node), "", &merged_id));
  std::unique_ptr<const TreeNode> merged_node = FromId(merged_id);
  EXPECT_EQ(size, merged_node->GetKeyCount());
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(entries[i], GetEntry(merged_node.get(), i));
  }
}

TEST_F(TreeNodeTest, FindKeyOrChild) {
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

TEST_F(TreeNodeTest, MutationAddEntry) {
  int size = 2;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));

  Entry entry{"ab", RandomId(), KeyPriority::EAGER};
  ObjectId left = CreateEmptyNode()->GetId();
  ObjectId right = CreateEmptyNode()->GetId();

  ObjectId new_node_id;
  EXPECT_EQ(
      Status::OK,
      node->StartMutation().AddEntry(entry, left, right).Finish(&new_node_id));
  std::unique_ptr<const TreeNode> new_node = FromId(new_node_id);

  // Initial node:
  //   [ a, b]
  //   /  |   \
  // 0    1    2
  //
  // After adding entry ab:
  //   [ a, ab, b]
  //   /  |   |   \
  // 0  left right 2
  EXPECT_EQ(size + 1, new_node->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(new_node.get(), 0));
  EXPECT_EQ(entry, GetEntry(new_node.get(), 1));
  EXPECT_EQ(GetEntry(node.get(), 1), GetEntry(new_node.get(), 2));

  EXPECT_EQ(GetChildId(node.get(), 0), GetChildId(new_node.get(), 0));
  EXPECT_EQ(left, GetChildId(new_node.get(), 1));
  EXPECT_EQ(right, GetChildId(new_node.get(), 2));
  EXPECT_EQ(GetChildId(node.get(), 2), GetChildId(new_node.get(), 3));
}

TEST_F(TreeNodeTest, MutationUpdateEntry) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));
  ObjectId new_node_id;

  Entry entry{"b", RandomId(), KeyPriority::EAGER};
  EXPECT_EQ(Status::OK,
            node->StartMutation().UpdateEntry(entry).Finish(&new_node_id));
  std::unique_ptr<const TreeNode> new_node = FromId(new_node_id);

  // Initial node:
  //   [ a, b, c]
  //   /  |   |  \
  // 0    1   2   3
  //
  // After updating entry b:
  // (same with different value for b)
  EXPECT_EQ(size, new_node->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(new_node.get(), 0));
  EXPECT_EQ(entry, GetEntry(new_node.get(), 1));
  EXPECT_EQ(GetEntry(node.get(), 2), GetEntry(new_node.get(), 2));

  for (int i = 0; i <= size; ++i) {
    EXPECT_EQ(GetChildId(node.get(), i), GetChildId(new_node.get(), i));
  }
}

TEST_F(TreeNodeTest, MutationRemoveEntry) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));

  ObjectId new_node_id;
  ObjectId child = CreateEmptyNode()->GetId();
  EXPECT_EQ(Status::OK,
            node->StartMutation().RemoveEntry("b", child).Finish(&new_node_id));
  std::unique_ptr<const TreeNode> new_node = FromId(new_node_id);

  // Initial node:
  //   [ a, b, c]
  //   /  |   |  \
  // 0    1   2   3
  //
  // After removing entry b:
  //   [ a, c]
  //   /  |   \
  // 0  child  3
  EXPECT_EQ(size - 1, new_node->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(new_node.get(), 0));
  EXPECT_EQ(GetEntry(node.get(), 2), GetEntry(new_node.get(), 1));

  EXPECT_EQ(GetChildId(node.get(), 0), GetChildId(new_node.get(), 0));
  EXPECT_EQ(child, GetChildId(new_node.get(), 1));
  EXPECT_EQ(GetChildId(node.get(), 3), GetChildId(new_node.get(), 2));
}

TEST_F(TreeNodeTest, MutationUpdateChildId) {
  int size = 2;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));

  ObjectId new_node_id;
  ObjectId child = CreateEmptyNode()->GetId();
  EXPECT_EQ(
      Status::OK,
      node->StartMutation().UpdateChildId("b", child).Finish(&new_node_id));
  std::unique_ptr<const TreeNode> new_node = FromId(new_node_id);

  // Initial node:
  //   [ a, b]
  //   /  |   \
  // 0    1    2
  //
  // After updating the child before b:
  //   [ a, b]
  //   /  |   \
  // 0  child  2
  EXPECT_EQ(size, new_node->GetKeyCount());

  EXPECT_EQ(GetEntry(node.get(), 0), GetEntry(new_node.get(), 0));
  EXPECT_EQ(GetEntry(node.get(), 1), GetEntry(new_node.get(), 1));

  EXPECT_EQ(GetChildId(node.get(), 0), GetChildId(new_node.get(), 0));
  EXPECT_EQ(child, GetChildId(new_node.get(), 1));
  EXPECT_EQ(GetChildId(node.get(), 2), GetChildId(new_node.get(), 2));
}

TEST_F(TreeNodeTest, EmptyMutation) {
  int size = 3;
  std::unique_ptr<const TreeNode> node =
      FromEntries(GetEntries(size), CreateChildren(size + 1));

  ObjectId new_node_id;
  // Note that creating an empty mutation is inefficient and should be avoided
  // when possible.
  EXPECT_EQ(Status::OK, node->StartMutation().Finish(&new_node_id));
  std::unique_ptr<const TreeNode> new_node = FromId(new_node_id);
  // TOOD(nellyv): check that the new id is equal to the original one when ids
  // are not randomly assigned.

  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(GetEntry(node.get(), i), GetEntry(new_node.get(), i));
  }

  for (int i = 0; i <= size; ++i) {
    EXPECT_EQ(GetChildId(node.get(), i), GetChildId(new_node.get(), i));
  }
}

TEST_F(TreeNodeTest, Serialization) {
  int size = 3;
  std::vector<Entry> entries = GetEntries(size);
  std::vector<ObjectId> children = CreateChildren(size + 1);
  std::unique_ptr<const TreeNode> node = FromEntries(entries, children);

  Status status;
  std::unique_ptr<const Object> object;
  // Fake storage is always synchronous.
  fake_storage_.GetObject(
      node->GetId(),
      [this, &status, &object](Status returned_status,
                               std::unique_ptr<const Object> returned_object) {
        status = returned_status;
        object = std::move(returned_object);
      });
  EXPECT_EQ(Status::OK, status);
  std::unique_ptr<const TreeNode> retrieved_node;
  EXPECT_EQ(Status::OK,
            TreeNode::FromId(&fake_storage_, object->GetId(), &node));

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
