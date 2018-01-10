// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <zxcrypt/superblock.h>

#include "test-device.h"

namespace zxcrypt {
namespace testing {
namespace {

// See test-device.h; the following macros allow reusing tests for each of the supported versions.
#define EACH_PARAM(OP, Test) OP(Test, Superblock, AES256_XTS_SHA256)

bool TestCreate(Superblock::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.GenerateKey(version));
    // These expected failures aren't possible on FVM, because fvm_init checks for them and fails.
    if (!fvm) {
        // Small device
        ASSERT_OK(device.Create(kBlockSize, kBlockSize, fvm));
        EXPECT_ZX(Superblock::Create(fbl::move(device.parent()), device.key()), ZX_ERR_NO_SPACE);
    }

    // Invalid file descriptor
    fbl::unique_fd bad_fd;
    EXPECT_ZX(Superblock::Create(fbl::move(bad_fd), device.key()), ZX_ERR_INVALID_ARGS);

    // Weak key
    ASSERT_OK(device.Create(kDeviceSize, kBlockSize, fvm));
    crypto::Bytes short_key;
    ASSERT_OK(short_key.Copy(device.key()));
    ASSERT_OK(short_key.Resize(short_key.len() - 1));
    EXPECT_ZX(Superblock::Create(fbl::move(device.parent()), short_key), ZX_ERR_INVALID_ARGS);

    // Valid
    ASSERT_OK(device.GenerateKey(version));
    EXPECT_OK(Superblock::Create(fbl::move(device.parent()), device.key()));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestCreate);

bool TestOpen(Superblock::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.Create(kDeviceSize, kBlockSize, fvm));

    // Invalid device
    fbl::unique_ptr<Superblock> superblock;
    EXPECT_ZX(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock),
              ZX_ERR_ACCESS_DENIED);

    // Bad file descriptor
    fbl::unique_fd bad_fd;
    EXPECT_ZX(Superblock::Open(fbl::move(bad_fd), device.key(), 0, &superblock),
              ZX_ERR_INVALID_ARGS);

    // Bad key
    ASSERT_OK(device.GenerateKey(version));
    ASSERT_OK(Superblock::Create(fbl::move(device.parent()), device.key()));

    crypto::Bytes mod;
    ASSERT_OK(mod.Copy(device.key()));
    mod[0] ^= 1;
    EXPECT_ZX(Superblock::Open(fbl::move(device.parent()), mod, 0, &superblock),
              ZX_ERR_ACCESS_DENIED);

    // Bad slot
    EXPECT_ZX(Superblock::Open(fbl::move(device.parent()), device.key(), Superblock::kNumSlots,
                               &superblock),
              ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(Superblock::Open(fbl::move(device.parent()), device.key(), 1, &superblock),
              ZX_ERR_ACCESS_DENIED);

    // Valid
    EXPECT_OK(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock));

    // Corrupt a byte in each block.  Superblock will "self-heal" and continue to be usable.
    size_t i, off;
    for (i = 0; i < kBlockCount; ++i) {
        off = (i * kBlockSize) + (rand() % kBlockSize);
        ASSERT_OK(device.Corrupt(off));
        EXPECT_OK(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock));
    }

    END_TEST;
}
DEFINE_EACH_DEVICE(TestOpen);

bool TestEnroll(Superblock::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));

    fbl::unique_ptr<Superblock> superblock;
    ASSERT_OK(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock));

    // Bad key
    crypto::Bytes bad_key;
    EXPECT_ZX(superblock->Enroll(bad_key, 1), ZX_ERR_INVALID_ARGS);

    ASSERT_OK(device.GenerateKey(version));

    // Bad slot
    EXPECT_ZX(superblock->Enroll(device.key(), Superblock::kNumSlots), ZX_ERR_INVALID_ARGS);

    // Valid; new slot
    EXPECT_OK(superblock->Enroll(device.key(), 1));
    EXPECT_OK(Superblock::Open(fbl::move(device.parent()), device.key(), 1, &superblock));

    // Valid; existing slot
    ASSERT_OK(device.GenerateKey(version));
    EXPECT_OK(superblock->Enroll(device.key(), 0));
    EXPECT_OK(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestEnroll);

bool TestRevoke(Superblock::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));

    fbl::unique_ptr<Superblock> superblock;
    ASSERT_OK(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock));

    // Bad slot
    EXPECT_ZX(superblock->Revoke(Superblock::kNumSlots), ZX_ERR_INVALID_ARGS);

    // Valid, even if slot isn't enrolled
    EXPECT_OK(superblock->Revoke(Superblock::kNumSlots - 1));

    // Valid, even if last slot
    EXPECT_OK(superblock->Revoke(0));
    EXPECT_ZX(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock),
              ZX_ERR_ACCESS_DENIED);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestRevoke);

bool TestShred(Superblock::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));

    fbl::unique_ptr<Superblock> superblock;
    ASSERT_OK(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock));

    // Valid
    EXPECT_OK(superblock->Shred());

    // No further methods work
    EXPECT_ZX(superblock->Enroll(device.key(), 0), ZX_ERR_BAD_STATE);
    EXPECT_ZX(superblock->Revoke(0), ZX_ERR_BAD_STATE);
    EXPECT_ZX(Superblock::Open(fbl::move(device.parent()), device.key(), 0, &superblock),
              ZX_ERR_ACCESS_DENIED);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestShred);

BEGIN_TEST_CASE(SuperblockTest)
RUN_EACH_DEVICE(TestCreate)
RUN_EACH_DEVICE(TestOpen)
RUN_EACH_DEVICE(TestEnroll)
RUN_EACH_DEVICE(TestRevoke)
RUN_EACH_DEVICE(TestShred)
END_TEST_CASE(SuperblockTest)

} // namespace
} // namespace testing
} // namespace zxcrypt
