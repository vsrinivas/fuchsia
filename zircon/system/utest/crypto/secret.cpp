// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <crypto/secret.h>
#include <stddef.h>
#include <stdint.h>
#include <unittest/unittest.h>
#include <zircon/types.h>

#include "utils.h"

namespace crypto {
namespace testing {
namespace {

const size_t kSize = 1024;

bool TestAllocate(void) {
    BEGIN_TEST;
    Secret secret;
    uint8_t* buf = nullptr;
    uint8_t tmp[kSize] = {0};

    // Pre-allocation
    EXPECT_EQ(secret.len(), 0U);
    EXPECT_NULL(secret.get());

    // Invalid args
    ASSERT_DEATH(
        [](void* arg) {
            uint8_t* buf;
            static_cast<Secret*>(arg)->Allocate(0, &buf);
        },
        &secret, "zero length");
    ASSERT_DEATH([](void* arg) { static_cast<Secret*>(arg)->Allocate(kSize, nullptr); }, &secret,
                 "null buffer");

    // Valid
    EXPECT_OK(secret.Allocate(kSize, &buf));
    EXPECT_EQ(secret.len(), kSize);
    ASSERT_NONNULL(secret.get());
    EXPECT_EQ(memcmp(secret.get(), tmp, kSize), 0);

    // Fill with data
    EXPECT_NONNULL(buf);
    memset(buf, 1, kSize);
    memset(tmp, 1, kSize);
    EXPECT_EQ(memcmp(secret.get(), tmp, kSize), 0);

    // Ensure memory is reinitialized on reallocation
    EXPECT_OK(secret.Allocate(kSize, &buf));
    memset(tmp, 0, kSize);
    EXPECT_EQ(memcmp(secret.get(), tmp, kSize), 0);

    END_TEST;
}

// This test only checks that the routine basically functions; it does NOT assure anything about the
// quality of the entropy.  That topic is beyond the scope of a deterministic unit test.
bool TestGenerate(void) {
    BEGIN_TEST;

    Secret secret;
    uint8_t tmp[kSize] = {0};

    // Invalid args
    ASSERT_DEATH([](void* arg) { static_cast<Secret*>(arg)->Generate(0); }, &secret, "zero length");

    // Valid
    EXPECT_OK(secret.Generate(kSize));
    EXPECT_EQ(secret.len(), kSize);
    ASSERT_NONNULL(secret.get());
    EXPECT_NE(memcmp(secret.get(), tmp, kSize), 0);
    memcpy(tmp, secret.get(), kSize);

    // Ensure different results on regeneration
    EXPECT_OK(secret.Generate(kSize));
    EXPECT_NE(memcmp(secret.get(), tmp, kSize), 0);

    END_TEST;
}

bool TestClear(void) {
    BEGIN_TEST;

    Secret secret;
    secret.Clear();

    EXPECT_OK(secret.Generate(kSize));
    EXPECT_EQ(secret.len(), kSize);
    EXPECT_NONNULL(secret.get());

    secret.Clear();
    EXPECT_EQ(secret.len(), 0);
    EXPECT_NULL(secret.get());

    secret.Clear();

    END_TEST;
}

BEGIN_TEST_CASE(SecretTest)
RUN_TEST(TestAllocate)
RUN_TEST(TestGenerate)
RUN_TEST(TestClear)
END_TEST_CASE(SecretTest)

} // namespace
} // namespace testing
} // namespace crypto
