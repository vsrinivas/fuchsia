// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_digest.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/encryption/primitives/hash.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace {

absl::string_view operator"" _s(const char* str, size_t size) {
  return absl::string_view(str, size);
}

// Test for object ids smaller than the inlining threshold.
using ObjectDigestSmallTest = ::testing::TestWithParam<absl::string_view>;

TEST_P(ObjectDigestSmallTest, Index) {
  ObjectDigest object_digest = ComputeObjectDigest(PieceType::INDEX, ObjectType::BLOB, GetParam());
  ASSERT_TRUE(IsDigestValid(object_digest));
  ObjectDigestInfo info = GetObjectDigestInfo(object_digest);
  EXPECT_EQ(info.piece_type, PieceType::INDEX);
  EXPECT_EQ(info.object_type, ObjectType::BLOB);
  EXPECT_EQ(info.inlined, InlinedPiece::YES);
  EXPECT_EQ(ExtractObjectDigestData(object_digest), GetParam());
}

TEST_P(ObjectDigestSmallTest, Value) {
  ObjectDigest object_digest =
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::TREE_NODE, GetParam());
  ASSERT_TRUE(IsDigestValid(object_digest));
  ObjectDigestInfo info = GetObjectDigestInfo(object_digest);
  EXPECT_EQ(info.piece_type, PieceType::CHUNK);
  EXPECT_EQ(info.inlined, InlinedPiece::YES);
  EXPECT_EQ(info.object_type, ObjectType::TREE_NODE);
  EXPECT_EQ(ExtractObjectDigestData(object_digest), GetParam());
}

INSTANTIATE_TEST_SUITE_P(ObjectDigestTest, ObjectDigestSmallTest,
                         ::testing::Values("", "hello", "world\0withzero"_s,
                                           "01234567890123456789012345678901"));

// Test for object ids bigger than the inlining threshold.
using ObjectDigestBigTest = ::testing::TestWithParam<absl::string_view>;

TEST_P(ObjectDigestBigTest, Index) {
  ObjectDigest object_digest =
      ComputeObjectDigest(PieceType::INDEX, ObjectType::TREE_NODE, GetParam());
  ASSERT_TRUE(IsDigestValid(object_digest));
  ObjectDigestInfo info = GetObjectDigestInfo(object_digest);
  EXPECT_EQ(info.piece_type, PieceType::INDEX);
  EXPECT_EQ(info.object_type, ObjectType::TREE_NODE);
  EXPECT_EQ(info.inlined, InlinedPiece::NO);
  EXPECT_EQ(ExtractObjectDigestData(object_digest), encryption::SHA256WithLengthHash(GetParam()));
}

TEST_P(ObjectDigestBigTest, Value) {
  ObjectDigest object_digest = ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, GetParam());
  ASSERT_TRUE(IsDigestValid(object_digest));
  ObjectDigestInfo info = GetObjectDigestInfo(object_digest);
  EXPECT_EQ(info.piece_type, PieceType::CHUNK);
  EXPECT_EQ(info.object_type, ObjectType::BLOB);
  EXPECT_EQ(info.inlined, InlinedPiece::NO);
  EXPECT_EQ(ExtractObjectDigestData(object_digest), encryption::SHA256WithLengthHash(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(ObjectDigestTest, ObjectDigestBigTest,
                         ::testing::Values("012345678901234567890123456789012",
                                           "012345678900123456789001234567890012"
                                           "345678900123456789001234567890012345"
                                           "67890"));
}  // namespace
}  // namespace storage
