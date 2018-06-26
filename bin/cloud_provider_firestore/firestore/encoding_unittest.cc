// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/firestore/encoding.h"

#include <string>

#include <google/protobuf/util/time_util.h>
#include <gtest/gtest.h>

#include "peridot/bin/cloud_provider_firestore/firestore/testing/encoding.h"
#include "peridot/lib/convert/convert.h"

namespace cloud_provider_firestore {

namespace {

// Creates correct std::strings with \0 bytes inside from C-style string
// constants.
std::string operator"" _s(const char* str, size_t size) {
  return std::string(str, size);
}

class StringEncodingTest : public ::testing::TestWithParam<std::string> {};

TEST_P(StringEncodingTest, BackAndForth) {
  std::string data = GetParam();
  std::string encoded;
  std::string decoded;

  encoded = EncodeKey(data);
  EXPECT_EQ('+', encoded.back());
  EXPECT_TRUE(DecodeKey(encoded, &decoded));
  EXPECT_EQ(data, decoded);
}

INSTANTIATE_TEST_CASE_P(ExampleData, StringEncodingTest,
                        ::testing::Values(""_s, "abcdef"_s, "\x02\x7F"_s,
                                          "~!@#$%^&*()_+-="_s, "\0"_s,
                                          "bazinga\0\0\0"_s));

TEST(BatchEncodingTest, Empty) {
  fidl::VectorPtr<cloud_provider::Commit> empty(static_cast<size_t>(0u));
  google::firestore::v1beta1::Document document;
  EncodeCommitBatch(empty, &document);

  fidl::VectorPtr<cloud_provider::Commit> result;
  std::string timestamp;
  EXPECT_TRUE(DecodeCommitBatch(document, &result, &timestamp));
  EXPECT_EQ(0u, result->size());
}

TEST(BatchEncodingTest, TwoCommits) {
  fidl::VectorPtr<cloud_provider::Commit> original;
  {
    cloud_provider::Commit commit;
    commit.id = convert::ToArray("id0");
    commit.data = convert::ToArray("data0");
    original.push_back(std::move(commit));
  }
  {
    cloud_provider::Commit commit;
    commit.id = convert::ToArray("id1");
    commit.data = convert::ToArray("data1");
    original.push_back(std::move(commit));
  }
  google::firestore::v1beta1::Document document;
  EncodeCommitBatch(original, &document);

  fidl::VectorPtr<cloud_provider::Commit> result;
  std::string timestamp;
  EXPECT_TRUE(DecodeCommitBatch(document, &result, &timestamp));
  EXPECT_EQ(2u, result->size());
  EXPECT_EQ("id0", convert::ToString(result.get()[0].id));
  EXPECT_EQ("data0", convert::ToString(result.get()[0].data));
  EXPECT_EQ("id1", convert::ToString(result.get()[1].id));
  EXPECT_EQ("data1", convert::ToString(result.get()[1].data));
}

TEST(BatchEncodingTest, Timestamp) {
  fidl::VectorPtr<cloud_provider::Commit> commits;
  cloud_provider::Commit commit;
  commit.id = convert::ToArray("id0");
  commit.data = convert::ToArray("data0");
  commits.push_back(std::move(commit));
  google::firestore::v1beta1::Document document;
  google::protobuf::Timestamp protobuf_timestamp;
  ASSERT_TRUE(google::protobuf::util::TimeUtil::FromString(
      "2018-06-26T14:39:22+00:00", &protobuf_timestamp));
  std::string original_timestamp;
  ASSERT_TRUE(protobuf_timestamp.SerializeToString(&original_timestamp));
  EncodeCommitBatchWithTimestamp(commits, original_timestamp, &document);

  fidl::VectorPtr<cloud_provider::Commit> result;
  std::string decoded_timestamp;
  EXPECT_TRUE(DecodeCommitBatch(document, &result, &decoded_timestamp));
  EXPECT_EQ(original_timestamp, decoded_timestamp);
}

TEST(BatchEncodingTest, DecodingErrors) {
  fidl::VectorPtr<cloud_provider::Commit> result;
  std::string timestamp;

  {
    // Empty document.
    google::firestore::v1beta1::Document document;
    EXPECT_FALSE(DecodeCommitBatch(document, &result, &timestamp));
  }

  {
    // Non-empty but the commit key is missing.
    google::firestore::v1beta1::Document document;
    (*document.mutable_fields())["some_field"].set_integer_value(3);
    EXPECT_FALSE(DecodeCommitBatch(document, &result, &timestamp));
  }

  {
    // Commits field is not an array.
    google::firestore::v1beta1::Document document;
    (*document.mutable_fields())["commits"].set_integer_value(3);
    EXPECT_FALSE(DecodeCommitBatch(document, &result, &timestamp));
  }

  {
    // Commits contains a commit that is not a map.
    google::firestore::v1beta1::Document document;
    google::firestore::v1beta1::ArrayValue* commit_array =
        (*document.mutable_fields())["commits"].mutable_array_value();
    commit_array->add_values()->set_integer_value(3);
    EXPECT_FALSE(DecodeCommitBatch(document, &result, &timestamp));
  }

  {
    // Commits contains a commit that misses the "data" field.
    google::firestore::v1beta1::Document document;
    google::firestore::v1beta1::ArrayValue* commit_array =
        (*document.mutable_fields())["commits"].mutable_array_value();
    google::firestore::v1beta1::MapValue* commit_value =
        commit_array->add_values()->mutable_map_value();
    *((*commit_value->mutable_fields())["id"].mutable_bytes_value()) =
        "some_id";
    EXPECT_FALSE(DecodeCommitBatch(document, &result, &timestamp));
  }

  {
    // Commits contains a commit that misses the "id" field.
    google::firestore::v1beta1::Document document;
    google::firestore::v1beta1::ArrayValue* commit_array =
        (*document.mutable_fields())["commits"].mutable_array_value();
    google::firestore::v1beta1::MapValue* commit_value =
        commit_array->add_values()->mutable_map_value();
    *((*commit_value->mutable_fields())["data"].mutable_bytes_value()) =
        "some_data";
    EXPECT_FALSE(DecodeCommitBatch(document, &result, &timestamp));
  }

  {
    // Correct batch (sanity check).
    google::firestore::v1beta1::Document document;
    google::firestore::v1beta1::ArrayValue* commit_array =
        (*document.mutable_fields())["commits"].mutable_array_value();
    google::firestore::v1beta1::MapValue* commit_value =
        commit_array->add_values()->mutable_map_value();
    *((*commit_value->mutable_fields())["id"].mutable_bytes_value()) =
        "some_id";
    *((*commit_value->mutable_fields())["data"].mutable_bytes_value()) =
        "some_data";
    EXPECT_TRUE(DecodeCommitBatch(document, &result, &timestamp));
  }
}

}  // namespace

}  // namespace cloud_provider_firestore
