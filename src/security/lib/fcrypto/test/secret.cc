// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/lib/fcrypto/secret.h"

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "src/security/lib/fcrypto/test/utils.h"

namespace crypto {
namespace testing {
namespace {

const size_t kSize = 1024;

TEST(Secret, Allocate) {
  Secret secret;
  uint8_t tmp[kSize] = {0};
  uint8_t *buf;

  // Invalid args
  ASSERT_DEATH(([&secret, &buf] { secret.Allocate(0, &buf); }), "zero length");
  ASSERT_DEATH(([&secret] { secret.Allocate(kSize, nullptr); }), "null buffer");

  // Pre-allocation
  EXPECT_EQ(secret.len(), 0U);
  EXPECT_NULL(secret.get());

  // Valid
  EXPECT_OK(secret.Allocate(kSize, &buf));
  EXPECT_EQ(secret.len(), kSize);
  ASSERT_NOT_NULL(secret.get());
  EXPECT_EQ(memcmp(secret.get(), tmp, kSize), 0);

  // Fill with data
  EXPECT_NOT_NULL(buf);
  memset(buf, 1, kSize);
  memset(tmp, 1, kSize);
  EXPECT_EQ(memcmp(secret.get(), tmp, kSize), 0);

  // Ensure memory is reinitialized on reallocation
  EXPECT_OK(secret.Allocate(kSize, &buf));
  memset(tmp, 0, kSize);
  EXPECT_EQ(memcmp(secret.get(), tmp, kSize), 0);
}

// This test only checks that the routine basically functions; it does NOT assure anything about the
// quality of the entropy.  That topic is beyond the scope of a deterministic unit test.
TEST(Secret, Generate) {
  Secret secret;
  uint8_t tmp[kSize] = {0};

  // Invalid args
  ASSERT_DEATH(([&secret] { secret.Generate(0); }), "zero length");

  // Valid
  EXPECT_OK(secret.Generate(kSize));
  EXPECT_EQ(secret.len(), kSize);
  ASSERT_NOT_NULL(secret.get());
  EXPECT_NE(memcmp(secret.get(), tmp, kSize), 0);
  memcpy(tmp, secret.get(), kSize);

  // Ensure different results on regeneration
  EXPECT_OK(secret.Generate(kSize));
  EXPECT_NE(memcmp(secret.get(), tmp, kSize), 0);
}

TEST(Secret, Clear) {
  Secret secret;
  secret.Clear();

  EXPECT_OK(secret.Generate(kSize));
  EXPECT_EQ(secret.len(), kSize);
  EXPECT_NOT_NULL(secret.get());

  secret.Clear();
  EXPECT_EQ(secret.len(), 0);
  EXPECT_NULL(secret.get());

  secret.Clear();
}

TEST(Secret, MoveDestructive) {
  Secret src;
  ASSERT_OK(src.Generate(kSize));
  uint8_t buf[kSize];
  memcpy(buf, src.get(), kSize);

  Secret dst(std::move(src));

  EXPECT_BYTES_EQ(buf, dst.get(), kSize);
  EXPECT_EQ(src.len(), 0);
  EXPECT_EQ(src.get(), nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace crypto
