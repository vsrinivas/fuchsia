// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <crypto/bytes.h>
#include <crypto/digest.h>
#include <crypto/hkdf.h>
#include <unittest/unittest.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "utils.h"

namespace crypto {
namespace testing {
namespace {

bool TestInit(void) {
    BEGIN_TEST;
    size_t md_size;
    ASSERT_OK(digest::GetDigestLen(digest::kSHA256, &md_size));

    Bytes ikm, salt;
    ASSERT_OK(ikm.InitRandom(md_size));
    ASSERT_OK(salt.InitRandom(GUID_LEN));

    // Bad version
    HKDF hkdf;
    EXPECT_ZX(hkdf.Init(digest::kUninitialized, ikm, salt), ZX_ERR_INVALID_ARGS);

    // Bad input key material
    ASSERT_OK(ikm.Resize(md_size - 1));
    EXPECT_ZX(hkdf.Init(digest::kSHA256, ikm, salt), ZX_ERR_INVALID_ARGS);
    ASSERT_OK(ikm.InitRandom(md_size));

    // Salt is optional
    salt.Reset();
    EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt));
    ASSERT_OK(salt.InitRandom(GUID_LEN));

    // Invalid flags
    EXPECT_ZX(hkdf.Init(digest::kSHA256, ikm, salt, 0x8000), ZX_ERR_INVALID_ARGS);

    // Valid
    EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt));
    END_TEST;
}

bool TestDerive(void) {
    BEGIN_TEST;
    size_t md_size;
    ASSERT_OK(digest::GetDigestLen(digest::kSHA256, &md_size));

    HKDF hkdf;
    Bytes ikm, salt, key1, key2, key3;
    ASSERT_OK(ikm.InitRandom(md_size));
    ASSERT_OK(salt.InitRandom(GUID_LEN));
    ASSERT_OK(key1.Resize(md_size));
    ASSERT_OK(key2.Resize(md_size));
    ASSERT_OK(key3.Resize(md_size));

    // Uninitialized
    EXPECT_ZX(hkdf.Derive("init", &key1), ZX_ERR_INVALID_ARGS);
    ASSERT_OK(hkdf.Init(digest::kSHA256, ikm, salt));

    // Label is optional
    EXPECT_OK(hkdf.Derive(nullptr, &key1));
    EXPECT_OK(hkdf.Derive("", &key1));

    // Bad key
    key1.Reset();
    EXPECT_ZX(hkdf.Derive("init", &key1), ZX_ERR_INVALID_ARGS);
    ASSERT_OK(key1.Resize(md_size));

    // Same label, same key
    EXPECT_OK(hkdf.Derive("same", &key1));
    EXPECT_OK(hkdf.Derive("same", &key2));
    EXPECT_TRUE(key1 == key2);

    // Different label, different key.
    EXPECT_OK(hkdf.Derive("diff", &key3));
    EXPECT_TRUE(key1 != key3);
    END_TEST;
}

// Based on RFC 5869, Appendix A.2: Test with SHA-256 and longer inputs/outputs
bool TestRfc5869_TC1(void) {
    BEGIN_TEST;
    HKDF hkdf;
    Bytes ikm, salt, okm;
    ASSERT_OK(ikm.Resize(22, 0x0b));
    ASSERT_OK(salt.Resize(13));
    for (uint8_t i = 0; i < salt.len(); ++i) {
        salt[i] = i;
    }
    const char* info = "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9";
    uint8_t expected[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36,
        0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56,
        0xec, 0xc4, 0xc5, 0xbf, 0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65,
    };
    ASSERT_OK(okm.Resize(sizeof(expected)));

    EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt, HKDF::ALLOW_WEAK_KEY));
    EXPECT_OK(hkdf.Derive(info, &okm));
    EXPECT_EQ(memcmp(okm.get(), expected, sizeof(expected)), 0);
    END_TEST;
}

// Based on RFC 5869, Appendix A.2: Basic test case with SHA-256
bool TestRfc5869_TC2(void) {
    BEGIN_TEST;
    HKDF hkdf;
    Bytes ikm, salt, okm;
    ASSERT_OK(ikm.Resize(80));
    for (uint8_t i = 0; i < ikm.len(); ++i) {
        ikm[i] = i;
    }
    ASSERT_OK(salt.Resize(80));
    for (uint8_t i = 0; i < salt.len(); ++i) {
        salt[i] = static_cast<uint8_t>(0x60 + i);
    }
    const char* info = "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
                       "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
                       "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
                       "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
                       "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff";
    uint8_t expected[82] = {
        0xb1, 0x1e, 0x39, 0x8d, 0xc8, 0x03, 0x27, 0xa1, 0xc8, 0xe7, 0xf7, 0x8c, 0x59, 0x6a,
        0x49, 0x34, 0x4f, 0x01, 0x2e, 0xda, 0x2d, 0x4e, 0xfa, 0xd8, 0xa0, 0x50, 0xcc, 0x4c,
        0x19, 0xaf, 0xa9, 0x7c, 0x59, 0x04, 0x5a, 0x99, 0xca, 0xc7, 0x82, 0x72, 0x71, 0xcb,
        0x41, 0xc6, 0x5e, 0x59, 0x0e, 0x09, 0xda, 0x32, 0x75, 0x60, 0x0c, 0x2f, 0x09, 0xb8,
        0x36, 0x77, 0x93, 0xa9, 0xac, 0xa3, 0xdb, 0x71, 0xcc, 0x30, 0xc5, 0x81, 0x79, 0xec,
        0x3e, 0x87, 0xc1, 0x4c, 0x01, 0xd5, 0xc1, 0xf3, 0x43, 0x4f, 0x1d, 0x87,
    };
    ASSERT_OK(okm.Resize(sizeof(expected)));

    EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt));
    EXPECT_OK(hkdf.Derive(info, &okm));
    EXPECT_EQ(memcmp(okm.get(), expected, sizeof(expected)), 0);
    END_TEST;
}

// Based on RFC 5869, Appendix A.3: Test with SHA-256 and zero-length salt/info
bool TestRfc5869_TC3(void) {
    BEGIN_TEST;
    HKDF hkdf;
    Bytes ikm, salt, okm;
    ASSERT_OK(ikm.Resize(22, 0x0b));
    const char* info = "";
    uint8_t expected[42] = {
        0x8d, 0xa4, 0xe7, 0x75, 0xa5, 0x63, 0xc1, 0x8f, 0x71, 0x5f, 0x80, 0x2a, 0x06, 0x3c,
        0x5a, 0x31, 0xb8, 0xa1, 0x1f, 0x5c, 0x5e, 0xe1, 0x87, 0x9e, 0xc3, 0x45, 0x4e, 0x5f,
        0x3c, 0x73, 0x8d, 0x2d, 0x9d, 0x20, 0x13, 0x95, 0xfa, 0xa4, 0xb6, 0x1a, 0x96, 0xc8,
    };
    ASSERT_OK(okm.Resize(sizeof(expected)));

    EXPECT_OK(hkdf.Init(digest::kSHA256, ikm, salt, HKDF::ALLOW_WEAK_KEY));
    EXPECT_OK(hkdf.Derive(info, &okm));
    EXPECT_EQ(memcmp(okm.get(), expected, sizeof(expected)), 0);
    END_TEST;
}

BEGIN_TEST_CASE(KdfTest)
RUN_TEST(TestInit)
RUN_TEST(TestDerive)
RUN_TEST(TestRfc5869_TC1)
RUN_TEST(TestRfc5869_TC2)
RUN_TEST(TestRfc5869_TC3)
END_TEST_CASE(KdfTest)

} // namespace
} // namespace testing
} // namespace crypto
