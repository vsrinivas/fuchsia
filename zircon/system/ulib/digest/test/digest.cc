// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <utility>

#include <digest/digest.h>
#include <fbl/string.h>
#include <zxtest/zxtest.h>

// These unit tests are for the Digest object in ulib/digest.

namespace digest {
namespace testing {

// echo -n | sha256sum
const char* kZeroDigest = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
// echo -n | sha256sum | cut -c1-64 | tr -d '\n' | xxd -p -r | sha256sum
const char* kDoubleZeroDigest = "5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456";

TEST(DigestTestCase, Strings) {
  Digest actual;
  size_t len = strlen(kZeroDigest);
  // Incorrect length
  EXPECT_STATUS(actual.Parse(kZeroDigest, len - 1), ZX_ERR_INVALID_ARGS);
  EXPECT_STATUS(actual.Parse(kZeroDigest, len + 1), ZX_ERR_INVALID_ARGS);
  // Not hex
  char bad[kSha256HexLength + 1];
  snprintf(bad, sizeof(bad), "%s", kZeroDigest);
  bad[0] = 'g';
  EXPECT_STATUS(actual.Parse(bad), ZX_ERR_INVALID_ARGS);
  // Explicit length
  EXPECT_OK(actual.Parse(kZeroDigest, len));
  EXPECT_STR_EQ(kZeroDigest, actual.ToString().c_str());
  // Implicit length
  EXPECT_OK(actual.Parse(kDoubleZeroDigest));
  EXPECT_STR_EQ(kDoubleZeroDigest, actual.ToString().c_str());
  // fbl::String
  EXPECT_OK(actual.Parse(fbl::String(kZeroDigest)));
  EXPECT_STR_EQ(kZeroDigest, actual.ToString().c_str());
}

TEST(DigestTestCase, Zero) {
  Digest actual, expected;
  ASSERT_OK(expected.Parse(kZeroDigest));
  actual.Hash(nullptr, 0);
  EXPECT_BYTES_EQ(actual.get(), expected.get(), kSha256Length);
}

TEST(DigestTestCase, Self) {
  Digest actual, expected;
  ASSERT_OK(expected.Parse(kDoubleZeroDigest));
  ASSERT_OK(actual.Parse(kZeroDigest));
  uint8_t buf[kSha256Length];
  actual.CopyTo(buf, sizeof(buf));
  actual.Hash(buf, kSha256Length);
  EXPECT_BYTES_EQ(actual.get(), expected.get(), kSha256Length);
}

TEST(DigestTestCase, Split) {
  Digest actual, expected;
  actual.Init();
  size_t n = strlen(kZeroDigest);
  expected.Hash(kZeroDigest, n);
  for (size_t i = 1; i < n; ++i) {
    actual.Init();
    actual.Update(kZeroDigest, i);
    actual.Update(kZeroDigest + i, n - i);
    actual.Final();
    EXPECT_BYTES_EQ(actual.get(), expected.get(), kSha256Length);
  }
}

TEST(DigestTestCase, Equality) {
  Digest actual, expected;
  ASSERT_OK(expected.Parse(kZeroDigest));
  ASSERT_OK(actual.Parse(kZeroDigest));
  EXPECT_FALSE(actual.Equals(nullptr, actual.len()), "Does not equal NULL");
  EXPECT_FALSE(actual.Equals(actual.get(), actual.len() - 1), "Does not equal length-1");
  EXPECT_TRUE(actual.Equals(actual.get(), actual.len()), "Equals self");
  EXPECT_TRUE(actual.Equals(expected.get(), expected.len()), "Equals expected");
  EXPECT_TRUE(actual == actual, "Equals self");
  EXPECT_TRUE(actual == expected, "Equals expected");
  EXPECT_FALSE(actual != actual, "Doesn't not equal self");
  EXPECT_FALSE(actual != expected, "Doesn't not equal expected");
}

TEST(DigestTestCase, CopyTo) {
  Digest actual;
  uint8_t buf[kSha256Length * 2];
  memset(buf, 1, sizeof(buf));
  ASSERT_OK(actual.Parse(kZeroDigest));

  // CopyTo uses ZX_DEBUG_ASSERT and won't crash in release builds.  This test should
  // only run when ZX_DEBUG_ASSERT is implemented.
  if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
    ASSERT_DEATH(([&actual, &buf]() { actual.CopyTo(buf, kSha256Length - 1); }),
                 "Disallow truncation");
  }

  for (size_t len = 0; len < sizeof(buf); ++len) {
    actual.CopyTruncatedTo(buf, len);

    // First bytes match digest.
    EXPECT_EQ(memcmp(buf, actual.get(), std::min(len, kSha256Length)), 0);

    // Pad with zeros up to |len|.
    for (size_t i = kSha256Length; i < len; ++i) {
      EXPECT_EQ(buf[i], 0);
    }

    // Remaining bytes are untouched.
    for (size_t i = len; i < sizeof(buf); ++i) {
      EXPECT_EQ(buf[i], 1);
    }
  }
}

TEST(DigestTestCase, Move) {
  const Digest uninitialized_digest;
  Digest digest1;

  {
    // Verify that digest1 is not valid, and that it's current digest value
    // is all zeros.  Verify that when move digest1 into digest2, that
    // both retain this property (not valid, digest full of zeros)
    EXPECT_EQ(digest1, uninitialized_digest);

    Digest digest2(std::move(digest1));
    EXPECT_BYTES_EQ(digest1.get(), uninitialized_digest.get(), kSha256Length);
    EXPECT_BYTES_EQ(digest2.get(), uninitialized_digest.get(), kSha256Length);
  }

  // Start a hash operation in digest1, verify that this does not update the
  // initial hash value.
  digest1.Init();
  EXPECT_BYTES_EQ(digest1.get(), uninitialized_digest.get(), kSha256Length);

  // Hash some nothing into the hash.  Again verify the digest is still
  // valid, but that the internal result is still full of nothing.
  digest1.Update(nullptr, 0);
  EXPECT_BYTES_EQ(digest1.get(), uninitialized_digest.get(), kSha256Length);

  // Move the hash into digest2.  Verify that the context goes with the move
  // operation.
  Digest digest2(std::move(digest1));
  EXPECT_BYTES_EQ(digest1.get(), uninitialized_digest.get(), kSha256Length);

  // Finish the hash operation started in digest1 which was moved into
  // digest2.  Verify that digest2 is no longer valid, but that the result is
  // what we had expected.
  Digest zero_digest;
  ASSERT_OK(zero_digest.Parse(kZeroDigest));
  digest2.Final();
  EXPECT_BYTES_EQ(digest2.get(), zero_digest.get(), kSha256Length);

  // Move the result of the hash into a new digest3.  Verify that neither is
  // valid, but that the result was properly moved.
  Digest digest3(std::move(digest2));
  EXPECT_BYTES_EQ(digest2.get(), uninitialized_digest.get(), kSha256Length);
  EXPECT_BYTES_EQ(digest3.get(), zero_digest.get(), kSha256Length);
}

}  // namespace testing
}  // namespace digest
