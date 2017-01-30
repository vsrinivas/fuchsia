// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/encoding.h"

#include "apps/ledger/src/storage/impl/btree/tree_node_generated.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "gtest/gtest.h"

namespace storage {
namespace {
// Creates the object id for testing from the given str.
ObjectId MakeObjectId(std::string str) {
  // Resize id to the required size, adding trailing underscores if needed.
  str.resize(kObjectIdSize, '_');
  return str;
}

// Allows to create correct std::strings with \0 bytes inside from C-style
// string constants.
std::string operator"" _s(const char* str, size_t size) {
  return std::string(str, size);
}

TEST(EncodingTest, EmptyData) {
  std::vector<Entry> entries;
  std::vector<ObjectId> children{""};

  std::string bytes = EncodeNode(entries, children);

  std::vector<Entry> res_entries;
  std::vector<ObjectId> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_entries, &res_children));
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, SingleEntry) {
  std::vector<Entry> entries = {
      {"key", MakeObjectId("object_id"), KeyPriority::EAGER}};
  std::vector<ObjectId> children = {MakeObjectId("child_1"),
                                    MakeObjectId("child_2")};

  std::string bytes = EncodeNode(entries, children);

  std::vector<Entry> res_entries;
  std::vector<ObjectId> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_entries, &res_children));
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, MoreEntries) {
  std::vector<Entry> entries = {
      {"key1", MakeObjectId("abc"), KeyPriority::EAGER},
      {"key2", MakeObjectId("def"), KeyPriority::LAZY},
      {"key3", MakeObjectId("geh"), KeyPriority::EAGER},
      {"key4", MakeObjectId("ijk"), KeyPriority::LAZY}};
  std::vector<ObjectId> children = {
      MakeObjectId("child_1"), MakeObjectId("child_2"), MakeObjectId("child_3"),
      MakeObjectId("child_4"), MakeObjectId("child_5")};

  std::string bytes = EncodeNode(entries, children);

  std::vector<Entry> res_entries;
  std::vector<ObjectId> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_entries, &res_children));
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, ZeroByte) {
  std::vector<Entry> entries = {
      {"k\0ey"_s, MakeObjectId("\0a\0\0"_s), KeyPriority::EAGER}};
  std::vector<ObjectId> children = {MakeObjectId("ch\0ld_1"_s),
                                    MakeObjectId("child_\0"_s)};

  std::string bytes = EncodeNode(entries, children);

  std::vector<Entry> res_entries;
  std::vector<ObjectId> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_entries, &res_children));
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

std::string ToString(flatbuffers::FlatBufferBuilder* builder) {
  return std::string(reinterpret_cast<const char*>(builder->GetBufferPointer()),
                     builder->GetSize());
}

TEST(EncodingTest, Errors) {
  flatbuffers::FlatBufferBuilder builder;

  ChildStorage children[2];
  children[0].mutate_index(1);
  children[0].mutable_object_id() = *convert::ToIdStorage(MakeObjectId("c1"));
  children[1].mutate_index(1);
  children[1].mutable_object_id() = *convert::ToIdStorage(MakeObjectId("c2"));

  // An empty string is not a valid serialization.
  EXPECT_FALSE(CheckValidTreeNodeSerialization(""));

  // 2 children without entries is not a valid serialization.
  builder.Finish(CreateTreeNodeStorage(
      builder,
      builder.CreateVector(std::vector<flatbuffers::Offset<EntryStorage>>()),
      builder.CreateVectorOfStructs(children, 2)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(ToString(&builder)));

  // A single child with index 1 is not a valid serialization.
  builder.Clear();
  builder.Finish(CreateTreeNodeStorage(
      builder,
      builder.CreateVector(std::vector<flatbuffers::Offset<EntryStorage>>()),
      builder.CreateVectorOfStructs(children, 1)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(ToString(&builder)));

  // 2 children with the same index is not a valid serialization.
  builder.Clear();
  builder.Finish(CreateTreeNodeStorage(
      builder,
      builder.CreateVector(
          1,
          static_cast<std::function<flatbuffers::Offset<EntryStorage>(size_t)>>(
              [&](size_t i) {
                return CreateEntryStorage(
                    builder, convert::ToByteStorage(&builder, "hello"),
                    convert::ToIdStorage(MakeObjectId("world")),
                    KeyPriorityStorage::KeyPriorityStorage_EAGER);
              })),
      builder.CreateVectorOfStructs(children, 2)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(ToString(&builder)));

  // 2 entries not sorted.
  builder.Clear();
  builder.Finish(CreateTreeNodeStorage(
      builder,
      builder.CreateVector(
          2,
          static_cast<std::function<flatbuffers::Offset<EntryStorage>(size_t)>>(
              [&](size_t i) {
                return CreateEntryStorage(
                    builder, convert::ToByteStorage(&builder, "hello"),
                    convert::ToIdStorage(MakeObjectId("world")),
                    KeyPriorityStorage::KeyPriorityStorage_EAGER);
              })),
      builder.CreateVectorOfStructs(children, 0)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(ToString(&builder)));
}

}  // namespace
}  // namespace storage
