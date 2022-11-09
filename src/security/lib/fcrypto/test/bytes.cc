// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/lib/fcrypto/bytes.h"

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "src/security/lib/fcrypto/test/utils.h"

namespace crypto {
namespace testing {
namespace {

const size_t kSize = 1024;

// This test only checks that the routine basically functions; it does NOT assure anything about the
// quality of the entropy.  That topic is beyond the scope of a deterministic unit test.
TEST(Bytes, Randomize) {
  Bytes bytes;

  ASSERT_OK(bytes.Resize(kSize));
  ASSERT_TRUE(AllEqual(bytes, 0, 0, kSize));

  EXPECT_OK(bytes.Randomize(kSize));
  EXPECT_FALSE(AllEqual(bytes, 0, 0, kSize));
}

TEST(Bytes, Resize) {
  Bytes bytes;
  EXPECT_OK(bytes.Resize(kSize, 0xff));
  EXPECT_EQ(bytes.len(), kSize);
  EXPECT_NOT_NULL(bytes.get());

  EXPECT_OK(bytes.Resize(kSize));
  EXPECT_EQ(bytes.len(), kSize);
  EXPECT_NOT_NULL(bytes.get());
  EXPECT_TRUE(AllEqual(bytes, 0xff, 0, kSize));

  EXPECT_OK(bytes.Resize(kSize / 2));
  EXPECT_EQ(bytes.len(), kSize / 2);
  EXPECT_NOT_NULL(bytes.get());
  EXPECT_TRUE(AllEqual(bytes, 0xff, 0, kSize / 2));

  EXPECT_OK(bytes.Resize(kSize));
  EXPECT_EQ(bytes.len(), kSize);
  EXPECT_NOT_NULL(bytes.get());
  EXPECT_TRUE(AllEqual(bytes, 0xff, 0, kSize / 2));
  EXPECT_TRUE(AllEqual(bytes, 0, kSize / 2, kSize / 2));

  EXPECT_OK(bytes.Resize(0));
  EXPECT_EQ(bytes.len(), 0U);
  EXPECT_NULL(bytes.get());
}

TEST(Bytes, Copy) {
  Bytes bytes, copy;
  ASSERT_OK(bytes.Resize(kSize));

  uint8_t buf[kSize];
  memset(buf, 2, kSize);
  EXPECT_STATUS(bytes.Copy(nullptr, kSize, kSize), ZX_ERR_INVALID_ARGS);
  EXPECT_OK(bytes.Copy(buf, 0, kSize * 10));
  EXPECT_EQ(bytes.len(), kSize);
  EXPECT_TRUE(AllEqual(bytes, 0, 0, kSize));

  EXPECT_OK(bytes.Copy(buf, kSize, kSize));
  EXPECT_TRUE(AllEqual(bytes, 0, 0, kSize));
  EXPECT_TRUE(AllEqual(bytes, 2, kSize, kSize));

  memset(buf, 1, kSize);
  EXPECT_OK(bytes.Copy(buf, kSize / 2, kSize / 2));
  EXPECT_TRUE(AllEqual(bytes, 0, 0, kSize / 2));
  EXPECT_TRUE(AllEqual(bytes, 1, kSize / 2, kSize / 2));
  EXPECT_TRUE(AllEqual(bytes, 2, kSize, kSize));

  ASSERT_OK(bytes.Resize(0));
  EXPECT_OK(bytes.Copy(buf, kSize));
  EXPECT_EQ(bytes.len(), kSize);
  EXPECT_TRUE(AllEqual(bytes, 1, 0, kSize));

  EXPECT_OK(copy.Copy(bytes));
  EXPECT_TRUE(AllEqual(copy, 1, 0, kSize));

  EXPECT_OK(copy.Copy(bytes, kSize));
  EXPECT_TRUE(AllEqual(copy, 1, 0, kSize * 2));
}

TEST(Bytes, ArrayAccess) {
  Bytes bytes;
  ASSERT_OK(bytes.Resize(kSize, 1));
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(bytes[i], 1);
    bytes[i] = 2;
  }
  EXPECT_TRUE(AllEqual(bytes, 2, 0, kSize));
}

TEST(Bytes, Comparison) {
  Bytes bytes1, bytes2;
  ASSERT_OK(bytes1.Randomize(kSize));
  ASSERT_OK(bytes2.Copy(bytes1.get(), bytes1.len()));
  EXPECT_TRUE(bytes1 == bytes1);
  EXPECT_TRUE(bytes2 == bytes2);
  EXPECT_FALSE(bytes1 != bytes1);
  EXPECT_FALSE(bytes2 != bytes2);
  EXPECT_TRUE(bytes1 == bytes2);
  EXPECT_TRUE(bytes2 == bytes1);
  EXPECT_FALSE(bytes1 != bytes2);
  EXPECT_FALSE(bytes2 != bytes1);

  ASSERT_OK(bytes2.Randomize(kSize));
  EXPECT_TRUE(bytes1 == bytes1);
  EXPECT_TRUE(bytes2 == bytes2);
  EXPECT_FALSE(bytes1 != bytes1);
  EXPECT_FALSE(bytes2 != bytes2);
  EXPECT_FALSE(bytes1 == bytes2);
  EXPECT_FALSE(bytes2 == bytes1);
  EXPECT_TRUE(bytes1 != bytes2);
  EXPECT_TRUE(bytes2 != bytes1);
}

TEST(Bytes, Clear) {
  Bytes bytes;
  bytes.Clear();

  EXPECT_OK(bytes.Randomize(kSize));
  EXPECT_EQ(bytes.len(), kSize);
  EXPECT_NOT_NULL(bytes.get());

  bytes.Clear();
  EXPECT_EQ(bytes.len(), 0);
  EXPECT_NULL(bytes.get());

  bytes.Clear();
}

TEST(Bytes, MoveDestructive) {
  Bytes src;
  ASSERT_OK(src.Randomize(kSize));
  uint8_t buf[kSize];
  memcpy(buf, src.get(), kSize);

  Bytes dst(std::move(src));

  EXPECT_BYTES_EQ(buf, dst.get(), kSize);
  EXPECT_EQ(src.len(), 0);
  EXPECT_EQ(src.get(), nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace crypto
