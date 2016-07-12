// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/hash.h>
#include <unittest.h>

namespace crypto {

namespace {
bool instantiate(void*) {
    BEGIN_TEST;

    { Hash256 hash(); }

    END_TEST;
}

// Test an implementation detail of the Hash256 class: that it is a valid
// SHA256.
bool compute_hashes(void*) {
    BEGIN_TEST;

    // Test vectors from
    // http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/SHA256.pdf

    static const char kTestVector1[] = "abc";
    static const int kTestSize1 = 3;
    static const uint8_t kExpected1[Hash256::kHashSize] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
        0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
        0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

    {
        Hash256 hash1;
        hash1.Update(kTestVector1, kTestSize1);
        hash1.Final();

        EXPECT_EQ(0, memcmp(kExpected1, hash1.digest(), sizeof(kExpected1)),
                  "invalid hash1");
    }

    {
        Hash256 hash1;
        hash1.Update(kTestVector1, 1);
        hash1.Update(kTestVector1 + 1, kTestSize1 - 1);
        hash1.Final();

        EXPECT_EQ(0, memcmp(kExpected1, hash1.digest(), sizeof(kExpected1)),
                  "invalid hash1");
    }

    {
        const Hash256 hash1(kTestVector1, kTestSize1);
        EXPECT_EQ(0, memcmp(kExpected1, hash1.digest(), sizeof(kExpected1)),
                  "invalid hash1");
    }

    static const char kTestVector2[] = "";
    static const uint8_t kExpected2[Hash256::kHashSize] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
        0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
        0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};

    {
        const Hash256 hash2(kTestVector2, 0);
        EXPECT_EQ(0, memcmp(kExpected2, hash2.digest(), sizeof(kExpected2)),
                  "invalid hash2");
    }

    static const char kTestVector3[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const int kTestSize3 = 448 / 8;
    static const uint8_t kExpected3[Hash256::kHashSize] = {
        0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26,
        0x93, 0x0c, 0x3e, 0x60, 0x39, 0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff,
        0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1

    };

    {
        const Hash256 hash3(kTestVector3, kTestSize3);
        EXPECT_EQ(0, memcmp(kExpected3, hash3.digest(), sizeof(kExpected3)),
                  "invalid hash3");
    }

    END_TEST;
}

} // namespace

UNITTEST_START_TESTCASE(hash256_tests)
UNITTEST("Instantiate", instantiate)
UNITTEST("Compute", compute_hashes)
UNITTEST_END_TESTCASE(hash256_tests, "sha256", "Test SHA256 implementation Tests",
                      NULL, NULL);

} // namespace crypto
