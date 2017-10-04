// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_digest.h"

#include "gtest/gtest.h"
#include "peridot/bin/ledger/glue/crypto/hash.h"

namespace storage {
namespace {

fxl::StringView operator"" _s(const char* str, size_t size) {
  return fxl::StringView(str, size);
}

// Test for object ids smaller than the inlining threshold.
class ObjectDigestSmallTest : public ::testing::TestWithParam<fxl::StringView> {
};

TEST_P(ObjectDigestSmallTest, Index) {
  ObjectDigest object_digest =
      ComputeObjectDigest(ObjectType::INDEX, GetParam());
  EXPECT_EQ(ObjectDigestType::INDEX_HASH, GetObjectDigestType(object_digest));
  EXPECT_EQ(glue::SHA256WithLengthHash(GetParam()),
            ExtractObjectDigestData(object_digest));
}

TEST_P(ObjectDigestSmallTest, Value) {
  ObjectDigest object_digest =
      ComputeObjectDigest(ObjectType::VALUE, GetParam());
  EXPECT_EQ(ObjectDigestType::INLINE, GetObjectDigestType(object_digest));
  EXPECT_EQ(GetParam(), ExtractObjectDigestData(object_digest));
}

INSTANTIATE_TEST_CASE_P(ObjectDigestTest,
                        ObjectDigestSmallTest,
                        ::testing::Values("",
                                          "hello",
                                          "world\0withzero"_s,
                                          "01234567890123456789012345678901"));

// Test for object ids bigger than the inlining threshold.
class ObjectDigestBigTest : public ::testing::TestWithParam<fxl::StringView> {};

TEST_P(ObjectDigestBigTest, Index) {
  ObjectDigest object_digest =
      ComputeObjectDigest(ObjectType::INDEX, GetParam());
  EXPECT_EQ(ObjectDigestType::INDEX_HASH, GetObjectDigestType(object_digest));
  EXPECT_EQ(glue::SHA256WithLengthHash(GetParam()),
            ExtractObjectDigestData(object_digest));
}

TEST_P(ObjectDigestBigTest, Value) {
  ObjectDigest object_digest =
      ComputeObjectDigest(ObjectType::VALUE, GetParam());
  EXPECT_EQ(ObjectDigestType::VALUE_HASH, GetObjectDigestType(object_digest));
  EXPECT_EQ(glue::SHA256WithLengthHash(GetParam()),
            ExtractObjectDigestData(object_digest));
}

INSTANTIATE_TEST_CASE_P(ObjectDigestTest,
                        ObjectDigestBigTest,
                        ::testing::Values("012345678901234567890123456789012",
                                          "012345678900123456789001234567890012"
                                          "345678900123456789001234567890012345"
                                          "67890"));
}  // namespace
}  // namespace storage
