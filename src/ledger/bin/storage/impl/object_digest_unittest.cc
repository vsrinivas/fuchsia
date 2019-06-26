// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/object_digest.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/encryption/primitives/hash.h"

namespace storage {
namespace {

fxl::StringView operator"" _s(const char* str, size_t size) { return fxl::StringView(str, size); }

// Test for object ids smaller than the inlining threshold.
using ObjectDigestSmallTest = ::testing::TestWithParam<fxl::StringView>;

TEST_P(ObjectDigestSmallTest, Index) {
  ObjectDigest object_digest = ComputeObjectDigest(PieceType::INDEX, ObjectType::BLOB, GetParam());
  ASSERT_TRUE(IsDigestValid(object_digest));
  ObjectDigestInfo info = GetObjectDigestInfo(object_digest);
  EXPECT_EQ(PieceType::INDEX, info.piece_type);
  EXPECT_EQ(ObjectType::BLOB, info.object_type);
  EXPECT_EQ(InlinedPiece::YES, info.inlined);
  EXPECT_EQ(GetParam(), ExtractObjectDigestData(object_digest));
}

TEST_P(ObjectDigestSmallTest, Value) {
  ObjectDigest object_digest =
      ComputeObjectDigest(PieceType::CHUNK, ObjectType::TREE_NODE, GetParam());
  ASSERT_TRUE(IsDigestValid(object_digest));
  ObjectDigestInfo info = GetObjectDigestInfo(object_digest);
  EXPECT_EQ(PieceType::CHUNK, info.piece_type);
  EXPECT_EQ(InlinedPiece::YES, info.inlined);
  EXPECT_EQ(ObjectType::TREE_NODE, info.object_type);
  EXPECT_EQ(GetParam(), ExtractObjectDigestData(object_digest));
}

INSTANTIATE_TEST_SUITE_P(ObjectDigestTest, ObjectDigestSmallTest,
                         ::testing::Values("", "hello", "world\0withzero"_s,
                                           "01234567890123456789012345678901"));

// Test for object ids bigger than the inlining threshold.
using ObjectDigestBigTest = ::testing::TestWithParam<fxl::StringView>;

TEST_P(ObjectDigestBigTest, Index) {
  ObjectDigest object_digest =
      ComputeObjectDigest(PieceType::INDEX, ObjectType::TREE_NODE, GetParam());
  ASSERT_TRUE(IsDigestValid(object_digest));
  ObjectDigestInfo info = GetObjectDigestInfo(object_digest);
  EXPECT_EQ(PieceType::INDEX, info.piece_type);
  EXPECT_EQ(ObjectType::TREE_NODE, info.object_type);
  EXPECT_EQ(InlinedPiece::NO, info.inlined);
  EXPECT_EQ(encryption::SHA256WithLengthHash(GetParam()), ExtractObjectDigestData(object_digest));
}

TEST_P(ObjectDigestBigTest, Value) {
  ObjectDigest object_digest = ComputeObjectDigest(PieceType::CHUNK, ObjectType::BLOB, GetParam());
  ASSERT_TRUE(IsDigestValid(object_digest));
  ObjectDigestInfo info = GetObjectDigestInfo(object_digest);
  EXPECT_EQ(PieceType::CHUNK, info.piece_type);
  EXPECT_EQ(ObjectType::BLOB, info.object_type);
  EXPECT_EQ(InlinedPiece::NO, info.inlined);
  EXPECT_EQ(encryption::SHA256WithLengthHash(GetParam()), ExtractObjectDigestData(object_digest));
}

INSTANTIATE_TEST_SUITE_P(ObjectDigestTest, ObjectDigestBigTest,
                         ::testing::Values("012345678901234567890123456789012",
                                           "012345678900123456789001234567890012"
                                           "345678900123456789001234567890012345"
                                           "67890"));
}  // namespace
}  // namespace storage
