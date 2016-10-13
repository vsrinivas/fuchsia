// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/store/encoding.h"

#include "apps/ledger/storage/public/constants.h"
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
  std::vector<ObjectId> children;

  std::string bytes = EncodeNode(entries, children);

  std::vector<Entry> res_entries;
  std::vector<ObjectId> res_children;
  EXPECT_TRUE(DecodeNode(bytes, &res_entries, &res_children));
  EXPECT_EQ(entries, res_entries);
  EXPECT_EQ(children, res_children);
}

TEST(EncodingTest, SingleEntry) {
  std::vector<Entry> entries = {
      {"key", MakeObjectId("blob_id"), KeyPriority::EAGER}};
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

TEST(EncodingTest, Errors) {
  std::vector<Entry> res_entries;
  std::vector<ObjectId> res_children;
  EXPECT_FALSE(DecodeNode("[]", &res_entries, &res_children));
  EXPECT_FALSE(DecodeNode("{}", &res_entries, &res_children));
  EXPECT_FALSE(DecodeNode("{\"entries\":[]}", &res_entries, &res_children));
  EXPECT_FALSE(DecodeNode("{\"children\":[]}", &res_entries, &res_children));
  EXPECT_TRUE(DecodeNode("{\"entries\":[],\"children\":[]}", &res_entries,
                         &res_children));
}

}  // namespace
}  // namespace storage
