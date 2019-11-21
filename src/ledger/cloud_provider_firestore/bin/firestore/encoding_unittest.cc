// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/cloud_provider_firestore/bin/firestore/encoding.h"

#include <string>

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>

#include "src/ledger/cloud_provider_firestore/bin/firestore/testing/encoding.h"
#include "src/ledger/lib/convert/convert.h"

namespace cloud_provider_firestore {

namespace {

// Creates correct std::strings with \0 bytes inside from C-style string
// constants.
std::string operator"" _s(const char* str, size_t size) { return std::string(str, size); }

using StringEncodingTest = ::testing::TestWithParam<std::string>;

TEST_P(StringEncodingTest, BackAndForth) {
  std::string data = GetParam();
  std::string encoded;
  std::string decoded;

  encoded = EncodeKey(data);
  EXPECT_EQ('+', encoded.back());
  EXPECT_TRUE(DecodeKey(encoded, &decoded));
  EXPECT_EQ(data, decoded);
}

INSTANTIATE_TEST_SUITE_P(ExampleData, StringEncodingTest,
                         ::testing::Values(""_s, "abcdef"_s, "\x02\x7F"_s, "~!@#$%^&*()_+-="_s,
                                           "\0"_s, "bazinga\0\0\0"_s));

TEST(BatchEncodingTest, Empty) {
  cloud_provider::CommitPack commits;
  ASSERT_TRUE(cloud_provider::EncodeCommitPack({}, &commits));
  google::firestore::v1beta1::Document document;
  ASSERT_TRUE(EncodeCommitBatch(commits, &document));

  std::vector<cloud_provider::CommitPackEntry> entries;
  std::string timestamp;
  ASSERT_TRUE(DecodeCommitBatch(document, &entries, &timestamp));
  EXPECT_EQ(0u, entries.size());
}

TEST(BatchEncodingTest, TwoCommits) {
  cloud_provider::CommitPack commits;
  ASSERT_TRUE(cloud_provider::EncodeCommitPack({{"id0", "data0"}, {"id1", "data1"}}, &commits));
  google::firestore::v1beta1::Document document;
  ASSERT_TRUE(EncodeCommitBatch(commits, &document));

  std::vector<cloud_provider::CommitPackEntry> entries;
  std::string timestamp;
  ASSERT_TRUE(DecodeCommitBatch(document, &entries, &timestamp));
  EXPECT_EQ(2u, entries.size());
  EXPECT_EQ("id0", entries[0].id);
  EXPECT_EQ("data0", entries[0].data);
  EXPECT_EQ("id1", entries[1].id);
  EXPECT_EQ("data1", entries[1].data);
}

TEST(BatchEncodingTest, Timestamp) {
  cloud_provider::CommitPack commits;
  ASSERT_TRUE(cloud_provider::EncodeCommitPack({{"id0", "data0"}}, &commits));
  google::firestore::v1beta1::Document document;
  google::protobuf::Timestamp protobuf_timestamp;
  ASSERT_TRUE(google::protobuf::util::TimeUtil::FromString("2018-06-26T14:39:22+00:00",
                                                           &protobuf_timestamp));
  std::string original_timestamp;
  ASSERT_TRUE(protobuf_timestamp.SerializeToString(&original_timestamp));
  EncodeCommitBatchWithTimestamp(commits, original_timestamp, &document);

  std::vector<cloud_provider::CommitPackEntry> entries;
  std::string decoded_timestamp;
  EXPECT_TRUE(DecodeCommitBatch(document, &entries, &decoded_timestamp));
  EXPECT_EQ(original_timestamp, decoded_timestamp);
}

TEST(BatchEncodingTest, DecodingErrors) {
  std::vector<cloud_provider::CommitPackEntry> entries;
  std::string timestamp;

  {
    // Empty document.
    google::firestore::v1beta1::Document document;
    EXPECT_FALSE(DecodeCommitBatch(document, &entries, &timestamp));
  }

  {
    // Non-empty but the commit key is missing.
    google::firestore::v1beta1::Document document;
    (*document.mutable_fields())["some_field"].set_integer_value(3);
    EXPECT_FALSE(DecodeCommitBatch(document, &entries, &timestamp));
  }

  {
    // Commits field is not an array.
    google::firestore::v1beta1::Document document;
    (*document.mutable_fields())["commits"].set_integer_value(3);
    EXPECT_FALSE(DecodeCommitBatch(document, &entries, &timestamp));
  }

  {
    // Commits contains a commit that is not a map.
    google::firestore::v1beta1::Document document;
    google::firestore::v1beta1::ArrayValue* commit_array =
        (*document.mutable_fields())["commits"].mutable_array_value();
    commit_array->add_values()->set_integer_value(3);
    EXPECT_FALSE(DecodeCommitBatch(document, &entries, &timestamp));
  }

  {
    // Commits contains a commit that misses the "data" field.
    google::firestore::v1beta1::Document document;
    google::firestore::v1beta1::ArrayValue* commit_array =
        (*document.mutable_fields())["commits"].mutable_array_value();
    google::firestore::v1beta1::MapValue* commit_value =
        commit_array->add_values()->mutable_map_value();
    *((*commit_value->mutable_fields())["id"].mutable_bytes_value()) = "some_id";
    EXPECT_FALSE(DecodeCommitBatch(document, &entries, &timestamp));
  }

  {
    // Commits contains a commit that misses the "id" field.
    google::firestore::v1beta1::Document document;
    google::firestore::v1beta1::ArrayValue* commit_array =
        (*document.mutable_fields())["commits"].mutable_array_value();
    google::firestore::v1beta1::MapValue* commit_value =
        commit_array->add_values()->mutable_map_value();
    *((*commit_value->mutable_fields())["data"].mutable_bytes_value()) = "some_data";
    EXPECT_FALSE(DecodeCommitBatch(document, &entries, &timestamp));
  }

  {
    // Correct batch (sanity check).
    google::firestore::v1beta1::Document document;
    google::firestore::v1beta1::ArrayValue* commit_array =
        (*document.mutable_fields())["commits"].mutable_array_value();
    google::firestore::v1beta1::MapValue* commit_value =
        commit_array->add_values()->mutable_map_value();
    *((*commit_value->mutable_fields())["id"].mutable_bytes_value()) = "some_id";
    *((*commit_value->mutable_fields())["data"].mutable_bytes_value()) = "some_data";
    EXPECT_TRUE(DecodeCommitBatch(document, &entries, &timestamp));
  }
}

}  // namespace

}  // namespace cloud_provider_firestore
