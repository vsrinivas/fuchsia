// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <crypto/bytes.h>
#include <unittest/unittest.h>
#include <zircon/types.h>

#include "utils.h"

namespace crypto {
namespace testing {
namespace {

const size_t kSize = 1024;


bool AllEqual(const void* buf, uint8_t val, zx_off_t off, size_t len) {
    BEGIN_HELPER;
    const uint8_t* u8 = static_cast<const uint8_t*>(buf);
    size_t end;
    ASSERT_FALSE(add_overflow(off, len, &end));
    for (size_t i = off; i < end; ++i) {
        if(u8[i] != val) {
            return false;
        }
    }
    END_HELPER;
}

// This test only checks that the routine basically functions; it does NOT assure anything about the
// quality of the entropy.  That topic is beyond the scope of a deterministic unit test.
bool TestRandomize(void) {
    BEGIN_TEST;
    Bytes bytes;

    ASSERT_OK(bytes.Resize(kSize));
    ASSERT_TRUE(AllEqual(bytes.get(), 0, 0, kSize));

    EXPECT_OK(bytes.Randomize(kSize));
    EXPECT_FALSE(AllEqual(bytes.get(), 0, 0, kSize));

    END_TEST;
}

bool TestResize(void) {
    BEGIN_TEST;
    Bytes bytes;
    EXPECT_OK(bytes.Resize(kSize, 0xff));
    EXPECT_EQ(bytes.len(), kSize);
    EXPECT_NONNULL(bytes.get());

#if !__has_feature(address_sanitizer)
    // The ASan allocator reports errors for unreasonable allocation sizes.
    EXPECT_ZX(bytes.Resize(size_t(-1)), ZX_ERR_NO_MEMORY);
    EXPECT_EQ(bytes.len(), kSize);
    EXPECT_NONNULL(bytes.get());
    EXPECT_TRUE(AllEqual(bytes.get(), 0xff, 0, kSize));
#endif

    EXPECT_OK(bytes.Resize(kSize));
    EXPECT_EQ(bytes.len(), kSize);
    EXPECT_NONNULL(bytes.get());
    EXPECT_TRUE(AllEqual(bytes.get(), 0xff, 0, kSize));

    EXPECT_OK(bytes.Resize(kSize / 2));
    EXPECT_EQ(bytes.len(), kSize / 2);
    EXPECT_NONNULL(bytes.get());
    EXPECT_TRUE(AllEqual(bytes.get(), 0xff, 0, kSize / 2));

    EXPECT_OK(bytes.Resize(kSize));
    EXPECT_EQ(bytes.len(), kSize);
    EXPECT_NONNULL(bytes.get());
    EXPECT_TRUE(AllEqual(bytes.get(), 0xff, 0, kSize / 2));
    EXPECT_TRUE(AllEqual(bytes.get(), 0, kSize / 2, kSize / 2));

    EXPECT_OK(bytes.Resize(0));
    EXPECT_EQ(bytes.len(), 0U);
    EXPECT_NULL(bytes.get());
    END_TEST;
}

bool TestCopy(void) {
    BEGIN_TEST;
    Bytes bytes, copy;
    ASSERT_OK(bytes.Resize(kSize));

    uint8_t buf[kSize];
    memset(buf, 2, kSize);
    EXPECT_ZX(bytes.Copy(nullptr, kSize, kSize), ZX_ERR_INVALID_ARGS);
    EXPECT_OK(bytes.Copy(buf, 0, kSize * 10));
    EXPECT_EQ(bytes.len(), kSize);
    EXPECT_TRUE(AllEqual(bytes.get(), 0, 0, kSize));

    EXPECT_OK(bytes.Copy(buf, kSize, kSize));
    EXPECT_TRUE(AllEqual(bytes.get(), 0, 0, kSize));
    EXPECT_TRUE(AllEqual(bytes.get(), 2, kSize, kSize));

    memset(buf, 1, kSize);
    EXPECT_OK(bytes.Copy(buf, kSize / 2, kSize / 2));
    EXPECT_TRUE(AllEqual(bytes.get(), 0, 0, kSize / 2));
    EXPECT_TRUE(AllEqual(bytes.get(), 1, kSize / 2, kSize / 2));
    EXPECT_TRUE(AllEqual(bytes.get(), 2, kSize, kSize));

    ASSERT_OK(bytes.Resize(0));
    EXPECT_OK(bytes.Copy(buf, kSize));
    EXPECT_EQ(bytes.len(), kSize);
    EXPECT_TRUE(AllEqual(bytes.get(), 1, 0, kSize));

    EXPECT_OK(copy.Copy(bytes));
    EXPECT_TRUE(AllEqual(copy.get(), 1, 0, kSize));

    EXPECT_OK(copy.Copy(bytes, kSize));
    EXPECT_TRUE(AllEqual(copy.get(), 1, 0, kSize * 2));

    END_TEST;
}

bool TestArrayAccess(void) {
    BEGIN_TEST;
    Bytes bytes;
    ASSERT_OK(bytes.Resize(kSize, 1));
    for (size_t i = 0; i < kSize; ++i) {
        EXPECT_EQ(bytes[i], 1);
        bytes[i] = 2;
    }
    EXPECT_TRUE(AllEqual(bytes.get(), 2, 0, kSize));
    END_TEST;
}

bool TestComparison(void) {
    BEGIN_TEST;
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
    END_TEST;
}

BEGIN_TEST_CASE(BytesTest)
RUN_TEST(TestRandomize)
RUN_TEST(TestResize)
RUN_TEST(TestCopy)
RUN_TEST(TestArrayAccess)
RUN_TEST(TestComparison)
END_TEST_CASE(BytesTest)

} // namespace
} // namespace testing
} // namespace crypto
