// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/btree/tree_node.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/storage/fake/fake_page_storage.h"
#include "src/ledger/bin/storage/impl/btree/encoding.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/capture.h"
#include "src/lib/callback/set_when_called.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace btree {
namespace {
using ::testing::IsEmpty;
using ::testing::IsSupersetOf;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

class FakePageStorageValidDigest : public fake::FakePageStorage {
 public:
  using fake::FakePageStorage::FakePageStorage;

 protected:
  ObjectDigest FakeDigest(absl::string_view content) const override {
    // BTree code needs storage to return valid digests.
    return MakeObjectDigest(convert::ToString(content));
  }
};

class TreeNodeTest : public StorageTest {
 public:
  TreeNodeTest() : fake_storage_(&environment_, "page_id") {}
  TreeNodeTest(const TreeNodeTest&) = delete;
  TreeNodeTest& operator=(const TreeNodeTest&) = delete;
  ~TreeNodeTest() override = default;

 protected:
  PageStorage* GetStorage() override { return &fake_storage_; }

  std::unique_ptr<const TreeNode> CreateEmptyNode() {
    ObjectIdentifier root_identifier;
    EXPECT_TRUE(GetEmptyNodeIdentifier(&root_identifier));
    std::unique_ptr<const TreeNode> node;
    EXPECT_TRUE(CreateNodeFromIdentifier(root_identifier, PageStorage::Location::Local(), &node));
    return node;
  }

  Entry GetEntry(const TreeNode* node, int index) {
    Entry found_entry;
    EXPECT_EQ(node->GetEntry(index, &found_entry), Status::OK);
    return found_entry;
  }

  std::map<size_t, ObjectIdentifier> CreateChildren(int size) {
    std::map<size_t, ObjectIdentifier> children;
    for (int i = 0; i < size; ++i) {
      children[i] = CreateEmptyNode()->GetIdentifier();
    }
    return children;
  }

  FakePageStorageValidDigest fake_storage_;
};

TEST_F(TreeNodeTest, CreateGetTreeNode) {
  std::unique_ptr<const TreeNode> node = CreateEmptyNode();

  bool called;
  Status status;
  std::unique_ptr<const TreeNode> found_node;
  TreeNode::FromIdentifier(
      &fake_storage_, {node->GetIdentifier(), PageStorage::Location::Local()},
      callback::Capture(callback::SetWhenCalled(&called), &status, &found_node));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  EXPECT_NE(nullptr, found_node);

  TreeNode::FromIdentifier(
      &fake_storage_,
      {RandomObjectIdentifier(environment_.random(), fake_storage_.GetObjectIdentifierFactory()),
       PageStorage::Location::Local()},
      callback::Capture(callback::SetWhenCalled(&called), &status, &found_node));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::INTERNAL_NOT_FOUND);
}

TEST_F(TreeNodeTest, GetEntry) {
  int size = 10;
  std::vector<Entry> entries;
  ASSERT_TRUE(CreateEntries(size, &entries));
  std::unique_ptr<const TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));
  EXPECT_EQ(node->GetKeyCount(), size);
  for (int i = 0; i < size; ++i) {
    EXPECT_EQ(GetEntry(node.get(), i), entries[i]);
  }
}

TEST_F(TreeNodeTest, FindKeyOrChild) {
  int size = 10;
  std::vector<Entry> entries;
  ASSERT_TRUE(CreateEntries(size, &entries));
  std::unique_ptr<const TreeNode> node;
  ASSERT_TRUE(CreateNodeFromEntries(entries, {}, &node));

  int index;
  EXPECT_EQ(node->FindKeyOrChild("key00", &index), Status::OK);
  EXPECT_EQ(index, 0);

  EXPECT_EQ(node->FindKeyOrChild("key02", &index), Status::OK);
  EXPECT_EQ(index, 2);

  EXPECT_EQ(node->FindKeyOrChild("key09", &index), Status::OK);
  EXPECT_EQ(index, 9);

  EXPECT_EQ(node->FindKeyOrChild("0", &index), Status::KEY_NOT_FOUND);
  EXPECT_EQ(index, 0);

  EXPECT_EQ(node->FindKeyOrChild("key001", &index), Status::KEY_NOT_FOUND);
  EXPECT_EQ(index, 1);

  EXPECT_EQ(node->FindKeyOrChild("key020", &index), Status::KEY_NOT_FOUND);
  EXPECT_EQ(index, 3);

  EXPECT_EQ(node->FindKeyOrChild("key999", &index), Status::KEY_NOT_FOUND);
  EXPECT_EQ(index, 10);
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
  fake_storage_.GetObject(node->GetIdentifier(), PageStorage::Location::Local(),
                          callback::Capture(callback::SetWhenCalled(&called), &status, &object));
  RunLoopFor(kSufficientDelay);
  EXPECT_TRUE(called);
  EXPECT_EQ(status, Status::OK);
  std::unique_ptr<const TreeNode> retrieved_node;
  EXPECT_EQ(object->GetIdentifier(), node->GetIdentifier());
  ASSERT_TRUE(CreateNodeFromIdentifier(node->GetIdentifier(), PageStorage::Location::Local(),
                                       &retrieved_node));

  absl::string_view data;
  EXPECT_EQ(object->GetData(&data), Status::OK);
  uint8_t level;
  std::vector<Entry> parsed_entries;
  std::map<size_t, ObjectIdentifier> parsed_children;
  EXPECT_TRUE(DecodeNode(data, fake_storage_.GetObjectIdentifierFactory(), &level, &parsed_entries,
                         &parsed_children));
  EXPECT_EQ(parsed_entries, entries);
  EXPECT_EQ(parsed_children, children);
}

TEST_F(TreeNodeTest, References) {
  // Create a BTree with the following layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10, 11]
  // Each key XX points to "objectYY" with either a lazy or eager link. YY is
  // chosen so as to create a number of collisions to test various edge cases
  // (see actual values below and comments in test expectation).

  // References to inline objects are ignored so we ensure object00 and object01
  // are big enough not to be inlined.
  std::unique_ptr<const Object> object0, object1, object2;
  ASSERT_TRUE(AddObject(
      ObjectData(fake_storage_.GetObjectIdentifierFactory(), "object00", InlineBehavior::PREVENT)
          .value,
      &object0));
  ASSERT_TRUE(AddObject(
      ObjectData(fake_storage_.GetObjectIdentifierFactory(), "object01", InlineBehavior::PREVENT)
          .value,
      &object1));
  // Inline object, the references to it should be skipped.
  ASSERT_TRUE(AddObject("object02", &object2));
  const ObjectIdentifier object0_id = object0->GetIdentifier();
  const ObjectIdentifier object1_id = object1->GetIdentifier();
  const ObjectIdentifier inlined_object_id = object2->GetIdentifier();

  const std::vector<Entry> entries = {
      // A single node pointing to the same value with both eager and lazy
      // links.
      Entry{"key00", object0_id, KeyPriority::LAZY, EntryId("id00")},
      Entry{"key01", object1_id, KeyPriority::EAGER, EntryId("id01")},
      Entry{"key02", object0_id, KeyPriority::EAGER, EntryId("id02")},

      Entry{"key03", object1_id, KeyPriority::LAZY, EntryId("id03")},

      // Two lazy references for the same object.
      Entry{"key04", object0_id, KeyPriority::LAZY, EntryId("id04")},
      Entry{"key05", object1_id, KeyPriority::EAGER, EntryId("id05")},
      Entry{"key06", object0_id, KeyPriority::LAZY, EntryId("id06")},

      Entry{"key07", object1_id, KeyPriority::EAGER, EntryId("id07")},

      // Two eager references for the same object, and an inlined object.
      Entry{"key08", object0_id, KeyPriority::EAGER, EntryId("id08")},
      Entry{"key09", object1_id, KeyPriority::LAZY, EntryId("id09")},
      Entry{"key10", object0_id, KeyPriority::EAGER, EntryId("id10")},
      Entry{"key11", inlined_object_id, KeyPriority::EAGER, EntryId("id11")}};

  std::unique_ptr<const TreeNode> root, child0, child1, child2;
  ASSERT_TRUE(CreateNodeFromEntries({entries[0], entries[1], entries[2]}, {}, &child0));
  ASSERT_TRUE(CreateNodeFromEntries({entries[4], entries[5], entries[6]}, {}, &child1));
  ASSERT_TRUE(
      CreateNodeFromEntries({entries[8], entries[9], entries[10], entries[11]}, {}, &child2));
  ASSERT_TRUE(CreateNodeFromEntries(
      {entries[3], entries[7]},
      {{0, child0->GetIdentifier()}, {1, child1->GetIdentifier()}, {2, child2->GetIdentifier()}},
      &root));

  const ObjectDigest digest0 = object0->GetIdentifier().object_digest();
  const ObjectDigest digest1 = object1->GetIdentifier().object_digest();

  // Check that references returned by each TreeNode are correct.
  ObjectReferencesAndPriority references;
  root->AppendReferences(&references);
  EXPECT_THAT(references, UnorderedElementsAre(
                              // Keys
                              Pair(digest1, KeyPriority::LAZY),   // key03
                              Pair(digest1, KeyPriority::EAGER),  // key07
                              // Children
                              Pair(child0->GetIdentifier().object_digest(), KeyPriority::EAGER),
                              Pair(child1->GetIdentifier().object_digest(), KeyPriority::EAGER),
                              Pair(child2->GetIdentifier().object_digest(), KeyPriority::EAGER)));
  references.clear();
  child0->AppendReferences(&references);
  EXPECT_THAT(references, UnorderedElementsAre(Pair(digest0, KeyPriority::LAZY),   // key00
                                               Pair(digest1, KeyPriority::EAGER),  // key01
                                               Pair(digest0, KeyPriority::EAGER)   // key02
                                               ));
  references.clear();
  child1->AppendReferences(&references);
  EXPECT_THAT(references, UnorderedElementsAre(Pair(digest0, KeyPriority::LAZY),  // key04 and key06
                                               Pair(digest1, KeyPriority::EAGER)  // key05
                                               ));
  references.clear();
  child2->AppendReferences(&references);
  EXPECT_THAT(references,
              UnorderedElementsAre(Pair(digest0, KeyPriority::EAGER),  // key08 and key10
                                   Pair(digest1, KeyPriority::LAZY)    // key09
                                   // No reference to key11 (points to inline object02)
                                   ));

  // Check that references have been correctly added to PageStorage during
  // object creation.
  EXPECT_THAT(fake_storage_.GetReferences(),
              // All the pieces are small enough not to get split so we know all objects
              // and can exhaustively enumerate references.
              UnorderedElementsAre(
                  // References from the root piece.
                  Pair(root->GetIdentifier().object_digest(),
                       UnorderedElementsAre(
                           // Keys
                           Pair(digest1, KeyPriority::LAZY),   // key03
                           Pair(digest1, KeyPriority::EAGER),  // key07
                           // Children
                           Pair(child0->GetIdentifier().object_digest(), KeyPriority::EAGER),
                           Pair(child1->GetIdentifier().object_digest(), KeyPriority::EAGER),
                           Pair(child2->GetIdentifier().object_digest(), KeyPriority::EAGER))),
                  // References from each child, which don't have any children
                  // themselves, but reference values.
                  Pair(child0->GetIdentifier().object_digest(),
                       UnorderedElementsAre(Pair(digest0, KeyPriority::LAZY),   // key00
                                            Pair(digest1, KeyPriority::EAGER),  // key01
                                            Pair(digest0, KeyPriority::EAGER)   // key02
                                            )),
                  Pair(child1->GetIdentifier().object_digest(),
                       UnorderedElementsAre(Pair(digest0, KeyPriority::LAZY),  // key04 and key06
                                            Pair(digest1, KeyPriority::EAGER)  // key05
                                            )),
                  Pair(child2->GetIdentifier().object_digest(),
                       UnorderedElementsAre(Pair(digest0, KeyPriority::EAGER),  // key08 and key10
                                            Pair(digest1, KeyPriority::LAZY)    // key09
                                            // No reference to key11 (points to inline object02)
                                            )),
                  // References from values, which don't have any children themselves.
                  Pair(object0->GetIdentifier().object_digest(), IsEmpty()),
                  Pair(object1->GetIdentifier().object_digest(), IsEmpty()),
                  Pair(object2->GetIdentifier().object_digest(), IsEmpty())));
}

}  // namespace
}  // namespace btree
}  // namespace storage
