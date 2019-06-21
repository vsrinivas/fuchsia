// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <digest/digest.h>

#include <stdlib.h>

#include <zircon/status.h>
#include <unittest/unittest.h>

#include <utility>

// These unit tests are for the Digest object in ulib/digest.

namespace {

////////////////
// Test support.

using digest::Digest;

// echo -n | sha256sum
const char* kZeroDigest =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
// echo -n | sha256sum | cut -c1-64 | tr -d '\n' | xxd -p -r | sha256sum
const char* kDoubleZeroDigest =
    "5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456";

////////////////
// Test cases

bool DigestStrings(void) {
    BEGIN_TEST;
    Digest actual;
    zx_status_t rc = actual.Parse(kZeroDigest, strlen(kZeroDigest));
    char buf[(Digest::kLength * 2) + 1];
    rc = actual.ToString(buf, sizeof(buf));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    ASSERT_EQ(strncmp(kZeroDigest, buf, sizeof(buf)), 0, __FUNCTION__);
    END_TEST;
}

bool DigestZero(void) {
    BEGIN_TEST;
    Digest actual, expected;
    zx_status_t rc = expected.Parse(kZeroDigest, strlen(kZeroDigest));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    actual.Hash(nullptr, 0);
    ASSERT_TRUE(actual == expected, __FUNCTION__);
    END_TEST;
}

bool DigestSelf(void) {
    BEGIN_TEST;
    Digest actual, expected;
    zx_status_t rc =
        expected.Parse(kDoubleZeroDigest, strlen(kDoubleZeroDigest));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    rc = actual.Parse(kZeroDigest, strlen(kZeroDigest));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    uint8_t buf[Digest::kLength];
    rc = actual.CopyTo(buf, sizeof(buf));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    actual.Hash(buf, Digest::kLength);
    ASSERT_TRUE(actual == expected, __FUNCTION__);
    END_TEST;
}

bool DigestSplit(void) {
    BEGIN_TEST;
    Digest actual, expected;
    actual.Init();
    size_t n = strlen(kZeroDigest);
    expected.Hash(kZeroDigest, n);
    for (size_t i = 1; i < n; ++i) {
        actual.Init();
        actual.Update(kZeroDigest, i);
        actual.Update(kZeroDigest + i, n - i);
        actual.Final();
        ASSERT_TRUE(actual == expected, __FUNCTION__);
    }
    END_TEST;
}

bool DigestCWrappers(void) {
    BEGIN_TEST;
    uint8_t buf[Digest::kLength];
    zx_status_t rc = digest_hash(nullptr, 0, buf, sizeof(buf) - 1);
    ASSERT_EQ(rc, ZX_ERR_BUFFER_TOO_SMALL, "Small buffer should be rejected");
    rc = digest_hash(nullptr, 0, buf, sizeof(buf));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    Digest expected;
    rc = expected.Parse(kZeroDigest, strlen(kZeroDigest));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    ASSERT_TRUE(expected == buf, __FUNCTION__);
    digest_t* digest = nullptr;
    rc = digest_init(&digest);
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    expected.Hash(buf, sizeof(buf));
    digest_update(digest, buf, sizeof(buf));
    rc = digest_final(digest, buf, sizeof(buf));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    ASSERT_TRUE(expected == buf, __FUNCTION__);
    END_TEST;
}

bool DigestEquality(void) {
    BEGIN_TEST;
    Digest actual, expected;
    zx_status_t rc = expected.Parse(kZeroDigest, strlen(kZeroDigest));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    rc = actual.Parse(kZeroDigest, strlen(kZeroDigest));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    ASSERT_FALSE(actual == nullptr, "Does not equal NULL");
    ASSERT_TRUE(actual == actual, "Equals self");
    const uint8_t* actual_bytes = actual.AcquireBytes();
    const uint8_t* expected_bytes = expected.AcquireBytes();
    ASSERT_TRUE(actual == actual_bytes, "Equals self.bytes_");
    ASSERT_TRUE(actual == expected, "Equals expected");
    ASSERT_TRUE(actual == expected_bytes, "Equals expected.bytes_");
    ASSERT_TRUE(actual != nullptr, "Doesn't equal NULL");
    ASSERT_FALSE(actual != actual, "Doesn't not equal self");
    ASSERT_FALSE(actual != actual_bytes, "Doesn't not equal self.bytes_");
    ASSERT_FALSE(actual != expected, "Doesn't not equal expected");
    ASSERT_FALSE(actual != expected_bytes, "Doesn't not equal expected.bytes_");
    expected.ReleaseBytes();
    actual.ReleaseBytes();
    END_TEST;
}

bool DigestMove(void) {
    BEGIN_TEST;
    const Digest uninitialized_digest;
    Digest digest1;

    {
        // Verify that digest1 is not valid, and that it's current digest value
        // is all zeros.  Verify that when move digest1 into digest2, that
        // both retain this property (not valid, digest full of zeros)
        ASSERT_TRUE(digest1 == uninitialized_digest);
        ASSERT_FALSE(digest1.is_valid());

        Digest digest2(std::move(digest1));
        ASSERT_TRUE(digest1 == uninitialized_digest);
        ASSERT_FALSE(digest1.is_valid());
        ASSERT_TRUE(digest2 == uninitialized_digest);
        ASSERT_FALSE(digest2.is_valid());
    }

    // Start a hash operation in digest1, verify that this does not update the
    // initial hash value.
    zx_status_t rc = digest1.Init();
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    ASSERT_TRUE(digest1 == uninitialized_digest);
    ASSERT_TRUE(digest1.is_valid());

    // Hash some nothing into the hash.  Again veryify the digest is still
    // valid, but that the internal result is still full of nothing.
    digest1.Hash(nullptr, 0);
    ASSERT_TRUE(digest1 == uninitialized_digest);
    ASSERT_TRUE(digest1.is_valid());

    // Move the hash into digest2.  Verify that the context goes with the move
    // operation.
    Digest digest2(std::move(digest1));
    ASSERT_TRUE(digest1 == uninitialized_digest);
    ASSERT_FALSE(digest1.is_valid());
    ASSERT_TRUE(digest1 == uninitialized_digest);
    ASSERT_TRUE(digest2.is_valid());

    // Finish the hash operation started in digest1 which was moved into
    // digest2.  Verify that digest2 is no longer valid, but that the result is
    // what we had expected.
    Digest zero_digest;
    rc = zero_digest.Parse(kZeroDigest, strlen(kZeroDigest));
    ASSERT_EQ(rc, ZX_OK, zx_status_get_string(rc));
    digest2.Final();
    ASSERT_FALSE(digest2.is_valid());
    ASSERT_TRUE(digest2 == zero_digest);

    // Move the result of the hash into a new digest3.  Verify that neither is
    // valid, but that the result was properly moved.
    Digest digest3(std::move(digest2));
    ASSERT_FALSE(digest2.is_valid());
    ASSERT_FALSE(digest3.is_valid());
    ASSERT_TRUE(digest2 == uninitialized_digest);
    ASSERT_TRUE(digest3 == zero_digest);

    END_TEST;
}

} // namespace
BEGIN_TEST_CASE(DigestTests)
RUN_TEST(DigestStrings)
RUN_TEST(DigestZero)
RUN_TEST(DigestSelf)
RUN_TEST(DigestSplit)
RUN_TEST(DigestCWrappers)
RUN_TEST(DigestEquality)
END_TEST_CASE(DigestTests)
