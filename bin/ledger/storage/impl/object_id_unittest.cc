// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/object_id.h"

#include "peridot/bin/ledger/glue/crypto/hash.h"
#include "gtest/gtest.h"

namespace storage {
namespace {

fxl::StringView operator"" _s(const char* str, size_t size) {
  return fxl::StringView(str, size);
}

// Test for object ids smaller than the inlining threshold.
class ObjectIdSmallTest : public ::testing::TestWithParam<fxl::StringView> {};

TEST_P(ObjectIdSmallTest, Index) {
  ObjectId object_id = ComputeObjectId(ObjectType::INDEX, GetParam());
  EXPECT_EQ(ObjectIdType::INDEX_HASH, GetObjectIdType(object_id));
  EXPECT_EQ(glue::SHA256Hash(GetParam()), ExtractObjectIdData(object_id));
}

TEST_P(ObjectIdSmallTest, Value) {
  ObjectId object_id = ComputeObjectId(ObjectType::VALUE, GetParam());
  EXPECT_EQ(ObjectIdType::INLINE, GetObjectIdType(object_id));
  EXPECT_EQ(GetParam(), ExtractObjectIdData(object_id));
}

INSTANTIATE_TEST_CASE_P(ObjectIdTest,
                        ObjectIdSmallTest,
                        ::testing::Values("",
                                          "hello",
                                          "world\0withzero"_s,
                                          "01234567890123456789012345678901"));

// Test for object ids bigger than the inlining threshold.
class ObjectIdBigTest : public ::testing::TestWithParam<fxl::StringView> {};

TEST_P(ObjectIdBigTest, Index) {
  ObjectId object_id = ComputeObjectId(ObjectType::INDEX, GetParam());
  EXPECT_EQ(ObjectIdType::INDEX_HASH, GetObjectIdType(object_id));
  EXPECT_EQ(glue::SHA256Hash(GetParam()), ExtractObjectIdData(object_id));
}

TEST_P(ObjectIdBigTest, Value) {
  ObjectId object_id = ComputeObjectId(ObjectType::VALUE, GetParam());
  EXPECT_EQ(ObjectIdType::VALUE_HASH, GetObjectIdType(object_id));
  EXPECT_EQ(glue::SHA256Hash(GetParam()), ExtractObjectIdData(object_id));
}

INSTANTIATE_TEST_CASE_P(ObjectIdTest,
                        ObjectIdBigTest,
                        ::testing::Values("012345678901234567890123456789012",
                                          "012345678900123456789001234567890012"
                                          "345678900123456789001234567890012345"
                                          "67890"));
}  // namespace
}  // namespace storage
