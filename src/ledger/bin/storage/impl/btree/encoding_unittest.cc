// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/btree/encoding.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/storage/impl/btree/tree_node_generated.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/impl/object_identifier_factory_impl.h"
#include "src/ledger/bin/storage/impl/storage_test_utils.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"

namespace storage {
namespace btree {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

// Allows to create correct std::strings with \0 bytes inside from C-style
// string constants.
std::string operator"" _s(const char* str, size_t size) { return std::string(str, size); }

TEST(EncodingTest, EmptyData) {
  uint8_t level = 0u;
  std::vector<Entry> entries;
  std::map<size_t, ObjectIdentifier> children;

  std::string bytes = EncodeNode(level, entries, children);

  ObjectIdentifierFactoryImpl factory;
  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &factory, &res_level, &res_entries, &res_children));
  EXPECT_EQ(res_level, level);
  EXPECT_EQ(res_entries, entries);
  EXPECT_EQ(res_children, children);
}

TEST(EncodingTest, SingleEntry) {
  uint8_t level = 1u;
  std::vector<Entry> entries = {
      {"key", MakeObjectIdentifier("object_digest"), KeyPriority::EAGER, EntryId("id_1")}};
  std::map<size_t, ObjectIdentifier> children = {{0u, MakeObjectIdentifier("child_1")},
                                                 {1u, MakeObjectIdentifier("child_2")}};

  std::string bytes = EncodeNode(level, entries, children);

  ObjectIdentifierFactoryImpl factory;
  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &factory, &res_level, &res_entries, &res_children));
  EXPECT_EQ(res_level, level);
  EXPECT_EQ(res_entries, entries);
  EXPECT_EQ(res_children, children);
}

TEST(EncodingTest, MoreEntries) {
  uint8_t level = 5;
  std::vector<Entry> entries = {
      {"key1", MakeObjectIdentifier("abc"), KeyPriority::EAGER, EntryId("id_1")},
      {"key2", MakeObjectIdentifier("def"), KeyPriority::LAZY, EntryId("id_2")},
      {"key3", MakeObjectIdentifier("geh"), KeyPriority::EAGER, EntryId("id_3")},
      {"key4", MakeObjectIdentifier("ijk"), KeyPriority::LAZY, EntryId("id_4")}};
  std::map<size_t, ObjectIdentifier> children = {{0, MakeObjectIdentifier("child_1")},
                                                 {1, MakeObjectIdentifier("child_2")},
                                                 {2, MakeObjectIdentifier("child_3")},
                                                 {3, MakeObjectIdentifier("child_4")},
                                                 {4, MakeObjectIdentifier("child_5")}};

  std::string bytes = EncodeNode(level, entries, children);

  ObjectIdentifierFactoryImpl factory;
  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &factory, &res_level, &res_entries, &res_children));
  EXPECT_EQ(res_level, level);
  EXPECT_EQ(res_entries, entries);
  EXPECT_EQ(res_children, children);
}

// TODO(LE-823): Remove when we break compatibility with nodes not holding an entry_id.
TEST(EncodingTest, BackwardCompatibilityWithoutEntryId) {
  // In old versions, nodes will have |nullptr| for entry_id. Build an "old" version node and make
  // sure it is decoded correctly.
  flatbuffers::FlatBufferBuilder builder;

  auto entries_offsets = builder.CreateVector(
      1u, static_cast<std::function<flatbuffers::Offset<EntryStorage>(size_t)>>([&builder](
                                                                                    size_t /*i*/) {
        // By not explicitly initializing entry_id we default initialize it to |nullptr|.
        return CreateEntryStorage(builder, convert::ToFlatBufferVector(&builder, "key1"),
                                  ToObjectIdentifierStorage(&builder, MakeObjectIdentifier("abc")),
                                  KeyPriorityStorage_EAGER);
      }));
  auto children_offsets = builder.CreateVector(
      0u, static_cast<std::function<flatbuffers::Offset<ChildStorage>(size_t)>>([&builder](
                                                                                    size_t /*i*/) {
        EXPECT_TRUE(false);
        return CreateChildStorage(builder, /*index*/ -1,
                                  ToObjectIdentifierStorage(&builder, MakeObjectIdentifier("")));
      }));

  builder.Finish(CreateTreeNodeStorage(builder, entries_offsets, children_offsets, /*level*/ 1));

  std::string bytes = convert::ToString(builder);

  ObjectIdentifierFactoryImpl factory;
  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &factory, &res_level, &res_entries, &res_children));
  EXPECT_EQ(res_level, 1);
  EXPECT_THAT(res_entries, SizeIs(1));
  EXPECT_THAT(res_entries[0].entry_id, Not(IsEmpty()));
  EXPECT_THAT(res_children, IsEmpty());
}

TEST(EncodingTest, SparsedEntriesWithBeginAndEnd) {
  uint8_t level = 5;
  std::vector<Entry> entries = {
      {"key1", MakeObjectIdentifier("abc"), KeyPriority::EAGER, EntryId("id_1")},
      {"key2", MakeObjectIdentifier("def"), KeyPriority::LAZY, EntryId("id_2")},
      {"key3", MakeObjectIdentifier("geh"), KeyPriority::EAGER, EntryId("id_3")},
      {"key4", MakeObjectIdentifier("ijk"), KeyPriority::LAZY, EntryId("id_4")}};
  std::map<size_t, ObjectIdentifier> children = {{0, MakeObjectIdentifier("child_1")},
                                                 {2, MakeObjectIdentifier("child_2")},
                                                 {4, MakeObjectIdentifier("child_3")}};

  std::string bytes = EncodeNode(level, entries, children);

  ObjectIdentifierFactoryImpl factory;
  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &factory, &res_level, &res_entries, &res_children));
  EXPECT_EQ(res_level, level);
  EXPECT_EQ(res_entries, entries);
  EXPECT_EQ(res_children, children);
}

TEST(EncodingTest, SparsedEntriesWithoutBeginAndEnd) {
  uint8_t level = 5;
  std::vector<Entry> entries = {
      {"key1", MakeObjectIdentifier("abc"), KeyPriority::EAGER, EntryId("id_1")},
      {"key2", MakeObjectIdentifier("def"), KeyPriority::LAZY, EntryId("id_2")},
      {"key3", MakeObjectIdentifier("geh"), KeyPriority::EAGER, EntryId("id_3")},
      {"key4", MakeObjectIdentifier("ijk"), KeyPriority::LAZY, EntryId("id_4")}};
  std::map<size_t, ObjectIdentifier> children = {{1, MakeObjectIdentifier("child_1")},
                                                 {3, MakeObjectIdentifier("child_2")}};

  std::string bytes = EncodeNode(level, entries, children);

  ObjectIdentifierFactoryImpl factory;
  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &factory, &res_level, &res_entries, &res_children));
  EXPECT_EQ(res_level, level);
  EXPECT_EQ(res_entries, entries);
  EXPECT_EQ(res_children, children);
}

TEST(EncodingTest, ZeroByte) {
  uint8_t level = 13;
  std::vector<Entry> entries = {
      {"k\0ey"_s, MakeObjectIdentifier("\0a\0\0"_s), KeyPriority::EAGER, EntryId("id_1")}};
  std::map<size_t, ObjectIdentifier> children = {{0u, MakeObjectIdentifier("ch\0ld_1"_s)},
                                                 {1u, MakeObjectIdentifier("child_\0"_s)}};

  std::string bytes = EncodeNode(level, entries, children);

  ObjectIdentifierFactoryImpl factory;
  uint8_t res_level;
  std::vector<Entry> res_entries;
  std::map<size_t, ObjectIdentifier> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &factory, &res_level, &res_entries, &res_children));
  EXPECT_EQ(res_level, level);
  EXPECT_EQ(res_entries, entries);
  EXPECT_EQ(res_children, children);
}

TEST(EncodingTest, Errors) {
  flatbuffers::FlatBufferBuilder builder;

  auto create_children = [&builder](size_t size) {
    std::vector<flatbuffers::Offset<ChildStorage>> children;

    for (size_t i = 0; i < size; ++i) {
      children.push_back(CreateChildStorage(
          builder, 1,
          ToObjectIdentifierStorage(&builder, MakeObjectIdentifier(absl::StrFormat("c%lu", i)))));
    }
    return builder.CreateVector(children);
  };

  // An empty string is not a valid serialization.
  EXPECT_FALSE(CheckValidTreeNodeSerialization(""));

  // 2 children without entries is not a valid serialization.
  builder.Finish(CreateTreeNodeStorage(
      builder, builder.CreateVector(std::vector<flatbuffers::Offset<EntryStorage>>()),
      create_children(2)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(convert::ToString(builder)));

  // A single child with index 1 is not a valid serialization.
  builder.Clear();
  builder.Finish(CreateTreeNodeStorage(
      builder, builder.CreateVector(std::vector<flatbuffers::Offset<EntryStorage>>()),
      create_children(1)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(convert::ToString(builder)));

  // 2 children with the same index is not a valid serialization.
  builder.Clear();
  builder.Finish(CreateTreeNodeStorage(
      builder,
      builder.CreateVector(
          1, static_cast<std::function<flatbuffers::Offset<EntryStorage>(size_t)>>([&](size_t i) {
            return CreateEntryStorage(
                builder, convert::ToFlatBufferVector(&builder, "hello"),
                ToObjectIdentifierStorage(&builder, MakeObjectIdentifier("world")),
                KeyPriorityStorage::KeyPriorityStorage_EAGER);
          })),
      create_children(2)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(convert::ToString(builder)));

  // 2 entries not sorted.
  builder.Clear();
  builder.Finish(CreateTreeNodeStorage(
      builder,
      builder.CreateVector(
          2, static_cast<std::function<flatbuffers::Offset<EntryStorage>(size_t)>>([&](size_t i) {
            return CreateEntryStorage(
                builder, convert::ToFlatBufferVector(&builder, "hello"),
                ToObjectIdentifierStorage(&builder, MakeObjectIdentifier("world")),
                KeyPriorityStorage::KeyPriorityStorage_EAGER);
          })),
      create_children(0)));
  EXPECT_FALSE(CheckValidTreeNodeSerialization(convert::ToString(builder)));
}

}  // namespace
}  // namespace btree
}  // namespace storage
