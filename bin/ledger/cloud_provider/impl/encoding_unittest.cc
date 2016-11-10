// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/impl/encoding.h"

#include <memory>

#include "apps/ledger/src/cloud_provider/impl/timestamp_conversions.h"
#include "gtest/gtest.h"
#include "lib/ftl/time/time_delta.h"

namespace cloud_provider {
namespace {

// Allows to create correct std::strings with \0 bytes inside from C-style
// string constants.
std::string operator"" _s(const char* str, size_t size) {
  return std::string(str, size);
}

TEST(EncodingTest, Encode) {
  Commit commit(
      "some_id", "some_content",
      std::map<ObjectId, Data>{{"object_a", "data_a"}, {"object_b", "data_b"}});

  std::string encoded;
  EXPECT_TRUE(EncodeCommit(commit, &encoded));
  EXPECT_EQ(
      "{\"id\":\"some_idV\","
      "\"content\":\"some_contentV\","
      "\"objects\":{"
      "\"object_aV\":\"data_aV\","
      "\"object_bV\":\"data_bV\"},"
      "\"timestamp\":{\".sv\":\"timestamp\"}"
      "}",
      encoded);
}

TEST(EncodingTest, Decode) {
  std::string json =
      "{\"content\":\"xyzV\","
      "\"id\":\"abcV\","
      "\"objects\":{"
      "\"object_aV\":\"aV\","
      "\"object_bV\":\"bV\"},"
      "\"timestamp\":1472722368296"
      "}";

  std::unique_ptr<Record> record;
  EXPECT_TRUE(DecodeCommit(json, &record));
  EXPECT_EQ("abc", record->commit.id);
  EXPECT_EQ("xyz", record->commit.content);
  EXPECT_EQ(2u, record->commit.storage_objects.size());
  EXPECT_EQ("a", record->commit.storage_objects.at("object_a"));
  EXPECT_EQ("b", record->commit.storage_objects.at("object_b"));
  EXPECT_EQ(ServerTimestampToBytes(1472722368296), record->timestamp);
}

TEST(EncodingTest, DecodeMultiple) {
  std::string json =
      "{\"id1V\":"
      "{\"content\":\"xyzV\","
      "\"id\":\"id1V\","
      "\"objects\":{"
      "\"object_aV\":\"aV\","
      "\"object_bV\":\"bV\"},"
      "\"timestamp\":1472722368296"
      "},"
      "\"id2V\":"
      "{\"content\":\"bazingaV\","
      "\"id\":\"id2V\","
      "\"timestamp\":42"
      "}}";

  std::vector<Record> records;
  EXPECT_TRUE(DecodeMultipleCommits(json, &records));
  EXPECT_EQ(2u, records.size());

  EXPECT_EQ("id1", records[0].commit.id);
  EXPECT_EQ("xyz", records[0].commit.content);
  EXPECT_EQ(2u, records[0].commit.storage_objects.size());
  EXPECT_EQ("a", records[0].commit.storage_objects.at("object_a"));
  EXPECT_EQ("b", records[0].commit.storage_objects.at("object_b"));
  EXPECT_EQ(ServerTimestampToBytes(1472722368296), records[0].timestamp);

  EXPECT_EQ("id2", records[1].commit.id);
  EXPECT_EQ("bazinga", records[1].commit.content);
  EXPECT_EQ(0u, records[1].commit.storage_objects.size());
  EXPECT_EQ(ServerTimestampToBytes(42), records[1].timestamp);
}

// Verifies that encoding and JSON parsing we use work with zero bytes within
// strings.
TEST(EncodingTest, EncodeDecodeZeroByte) {
  Commit commit("id\0\0\0"_s, "\0"_s,
                std::map<ObjectId, Data>{{"object_a", "\0\0\0"_s},
                                         {"object_\0"_s, "\0"_s}});

  std::string encoded;
  EXPECT_TRUE(EncodeCommit(commit, &encoded));
  // Encoded commit is sent to server, which replaces the timestamp
  // placeholder with server-side timestamp. We emulate this for testing.
  std::string pattern = "{\".sv\":\"timestamp\"}";
  encoded.replace(encoded.find(pattern), pattern.size(), "42");

  std::unique_ptr<Record> output_record;
  EXPECT_TRUE(DecodeCommit(encoded, &output_record));
  EXPECT_EQ(commit, output_record->commit);
  EXPECT_EQ(ServerTimestampToBytes(42), output_record->timestamp);
}

}  // namespace
}  // namespace cloud_provider
