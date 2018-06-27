// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/encoding.h"

#include <lib/fit/function.h>

#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/storage/impl/btree/tree_node_generated.h"
#include "peridot/bin/ledger/storage/impl/object_identifier_encoding.h"
#include "peridot/bin/ledger/storage/impl/storage_test_utils.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace storage {
namespace btree {
namespace {

// Allows to create correct std::strings with \0 bytes inside from C-style
// string constants.
std::string operator"" _s(const char* str, size_t size) {
  return std::string(str, size);
}

TEST(EncodingTest, EmptyData) {
  uint8_t level = 0u;
  std::vector<Entry> entries;
  std::map<size_t, ObjectIdentifier> children;

  std::string bytes = EncodeNode(level, entries, children);

  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_level, &res_entries, &res_children));
  EXPECT_EQ(level, res_level);
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, SingleEntry) {
  uint8_t level = 1u;
  std::vector<Entry> entries = {
      {"key", MakeObjectIdentifier("object_digest"), KeyPriority::EAGER}};
  std::map<size_t, ObjectIdentifier> children = {
      {0u, MakeObjectIdentifier("child_1")},
      {1u, MakeObjectIdentifier("child_2")}};

  std::string bytes = EncodeNode(level, entries, children);

  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_level, &res_entries, &res_children));
  EXPECT_EQ(level, res_level);
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, MoreEntries) {
  uint8_t level = 5;
  std::vector<Entry> entries = {
      {"key1", MakeObjectIdentifier("abc"), KeyPriority::EAGER},
      {"key2", MakeObjectIdentifier("def"), KeyPriority::LAZY},
      {"key3", MakeObjectIdentifier("geh"), KeyPriority::EAGER},
      {"key4", MakeObjectIdentifier("ijk"), KeyPriority::LAZY}};
  std::map<size_t, ObjectIdentifier> children = {
      {0, MakeObjectIdentifier("child_1")},
      {1, MakeObjectIdentifier("child_2")},
      {2, MakeObjectIdentifier("child_3")},
      {3, MakeObjectIdentifier("child_4")},
      {4, MakeObjectIdentifier("child_5")}};

  std::string bytes = EncodeNode(level, entries, children);

  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_level, &res_entries, &res_children));
  EXPECT_EQ(level, res_level);
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, SparsedEntriesWithBeginAndEnd) {
  uint8_t level = 5;
  std::vector<Entry> entries = {
      {"key1", MakeObjectIdentifier("abc"), KeyPriority::EAGER},
      {"key2", MakeObjectIdentifier("def"), KeyPriority::LAZY},
      {"key3", MakeObjectIdentifier("geh"), KeyPriority::EAGER},
      {"key4", MakeObjectIdentifier("ijk"), KeyPriority::LAZY}};
  std::map<size_t, ObjectIdentifier> children = {
      {0, MakeObjectIdentifier("child_1")},
      {2, MakeObjectIdentifier("child_2")},
      {4, MakeObjectIdentifier("child_3")}};

  std::string bytes = EncodeNode(level, entries, children);

  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_level, &res_entries, &res_children));
  EXPECT_EQ(level, res_level);
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, SparsedEntriesWithoutBeginAndEnd) {
  uint8_t level = 5;
  std::vector<Entry> entries = {
      {"key1", MakeObjectIdentifier("abc"), KeyPriority::EAGER},
      {"key2", MakeObjectIdentifier("def"), KeyPriority::LAZY},
      {"key3", MakeObjectIdentifier("geh"), KeyPriority::EAGER},
      {"key4", MakeObjectIdentifier("ijk"), KeyPriority::LAZY}};
  std::map<size_t, ObjectIdentifier> children = {
      {1, MakeObjectIdentifier("child_1")},
      {3, MakeObjectIdentifier("child_2")}};

  std::string bytes = EncodeNode(level, entries, children);

  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_level, &res_entries, &res_children));
  EXPECT_EQ(level, res_level);
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, ZeroByte) {
  uint8_t level = 13;
  std::vector<Entry> entries = {
      {"k\0ey"_s, MakeObjectIdentifier("\0a\0\0"_s), KeyPriority::EAGER}};
  std::map<size_t, ObjectIdentifier> children = {
      {0u, MakeObjectIdentifier("ch\0ld_1"_s)},
      {1u, MakeObjectIdentifier("child_\0"_s)}};

  std::string bytes = EncodeNode(level, entries, children);

  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_level, &res_entries, &res_children));
  EXPECT_EQ(level, res_level);
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

std::string ToString(flatbuffers::FlatBufferBuilder* builder) {
  return std::string(reinterpret_cast<const char*>(builder->GetBufferPointer()),
                     builder->GetSize());
}

TEST(EncodingTest, Errors) {
  flatbuffers::FlatBufferBuilder builder;

  auto create_children = [&builder](size_t size) {
    std::vector<flatbuffers::Offset<ChildStorage>> children;

    for (size_t i = 0; i < size; ++i) {
      children.push_back(CreateChildStorage(
          builder, 1,
          ToObjectIdentifierStorage(
              &builder, MakeObjectIdentifier(fxl::StringPrintf("c%lu", i)))));
    }
    return builder.CreateVector(children);
  };

  // An empty string is not a valid serialization.
  EXPECT_FALSE(CheckValidTreeNodeSerialization(""));

  // 2 children without entries is not a valid serialization.
  builder.Finish(CreateTreeNodeStorage(
      builder,
      builder.CreateVector(std::vector<flatbuffers::Offset<EntryStorage>>()),
      create_children(2)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(ToString(&builder)));

  // A single child with index 1 is not a valid serialization.
  builder.Clear();
  builder.Finish(CreateTreeNodeStorage(
      builder,
      builder.CreateVector(std::vector<flatbuffers::Offset<EntryStorage>>()),
      create_children(1)));
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
                    builder, convert::ToFlatBufferVector(&builder, "hello"),
                    ToObjectIdentifierStorage(&builder,
                                              MakeObjectIdentifier("world")),
                    KeyPriorityStorage::KeyPriorityStorage_EAGER);
              })),
      create_children(2)));
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
                    builder, convert::ToFlatBufferVector(&builder, "hello"),
                    ToObjectIdentifierStorage(&builder,
                                              MakeObjectIdentifier("world")),
                    KeyPriorityStorage::KeyPriorityStorage_EAGER);
              })),
      create_children(0)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(ToString(&builder)));
}

}  // namespace
}  // namespace btree
}  // namespace storage
