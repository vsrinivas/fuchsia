// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <zxcrypt/volume.h>

#include "test-device.h"

namespace zxcrypt {
namespace testing {
namespace {

// See test-device.h; the following macros allow reusing tests for each of the supported versions.
#define EACH_PARAM(OP, Test) OP(Test, Volume, AES256_XTS_SHA256)

// ZX-1948: Dump extra information if encountering an unexpected error during volume creation.
bool VolumeCreate(fbl::unique_fd fd, const crypto::Bytes& key, bool fvm, zx_status_t expected) {
    BEGIN_HELPER;

    char err[128];
    block_info_t bInfo;
    ASSERT_GE(ioctl_block_get_info(fd.get(), &bInfo), 0);
    if (fvm) {
        fvm_info_t fInfo;
        ASSERT_GE(ioctl_block_fvm_query(fd.get(), &fInfo), 0);
        snprintf(err, sizeof(err),
                 "details: block size=%" PRIu32 ", block count=%" PRIu64
                 ", slice size=%zu, slice count=%zu",
                 bInfo.block_size, bInfo.block_count, fInfo.slice_size, fInfo.vslice_count);
    } else {
        snprintf(err, sizeof(err), "details: block size=%" PRIu32 ", block count=%" PRIu64,
                 bInfo.block_size, bInfo.block_count);
    }

    EXPECT_EQ(Volume::Create(fbl::move(fd), key), expected, err);

    END_HELPER;
}

bool TestCreate(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Create(kDeviceSize, kBlockSize, fvm));

    // Invalid file descriptor
    fbl::unique_fd bad_fd;
    EXPECT_ZX(Volume::Create(fbl::move(bad_fd), device.key()), ZX_ERR_INVALID_ARGS);

    // Weak key
    crypto::Bytes short_key;
    ASSERT_OK(short_key.Copy(device.key()));
    ASSERT_OK(short_key.Resize(short_key.len() - 1));
    EXPECT_TRUE(VolumeCreate(fbl::move(device.parent()), short_key, fvm, ZX_ERR_INVALID_ARGS));

    // Valid
    EXPECT_TRUE(VolumeCreate(fbl::move(device.parent()), device.key(), fvm, ZX_OK));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestCreate);

bool TestOpen(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Create(kDeviceSize, kBlockSize, fvm));

    // Invalid device
    fbl::unique_ptr<Volume> volume;
    EXPECT_ZX(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume),
              ZX_ERR_ACCESS_DENIED);

    // Bad file descriptor
    fbl::unique_fd bad_fd;
    EXPECT_ZX(Volume::Open(fbl::move(bad_fd), device.key(), 0, &volume), ZX_ERR_INVALID_ARGS);

    // Bad key
    ASSERT_TRUE(VolumeCreate(fbl::move(device.parent()), device.key(), fvm, ZX_OK));

    crypto::Bytes mod;
    ASSERT_OK(mod.Copy(device.key()));
    mod[0] ^= 1;
    EXPECT_ZX(Volume::Open(fbl::move(device.parent()), mod, 0, &volume), ZX_ERR_ACCESS_DENIED);

    // Bad slot
    EXPECT_ZX(Volume::Open(fbl::move(device.parent()), device.key(), Volume::kNumSlots, &volume),
              ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(Volume::Open(fbl::move(device.parent()), device.key(), 1, &volume),
              ZX_ERR_ACCESS_DENIED);

    // Valid
    EXPECT_OK(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume));

    // Corrupt a byte in each block.  Volume will "self-heal" and continue to be usable.
    size_t i, off;
    for (i = 0; i < kBlockCount; ++i) {
        off = (i * kBlockSize) + (rand() % kBlockSize);
        ASSERT_TRUE(device.Corrupt(off));
        EXPECT_OK(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume));
    }

    END_TEST;
}
DEFINE_EACH_DEVICE(TestOpen);

bool TestEnroll(Volume::Version version, bool fvm) {
    BEGIN_TEST;
    TestDevice device;
    ASSERT_TRUE(device.Bind(version, fvm));

    fbl::unique_ptr<Volume> volume;
    ASSERT_OK(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume));

    // Bad key
    crypto::Bytes bad_key;
    EXPECT_ZX(volume->Enroll(bad_key, 1), ZX_ERR_INVALID_ARGS);

    // Bad slot
    EXPECT_ZX(volume->Enroll(device.key(), Volume::kNumSlots), ZX_ERR_INVALID_ARGS);

    // Valid; new slot
    EXPECT_OK(volume->Enroll(device.key(), 1));
    EXPECT_OK(Volume::Open(fbl::move(device.parent()), device.key(), 1, &volume));

    // Valid; existing slot
    EXPECT_OK(volume->Enroll(device.key(), 0));
    EXPECT_OK(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestEnroll);

bool TestRevoke(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Bind(version, fvm));

    fbl::unique_ptr<Volume> volume;
    ASSERT_OK(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume));

    // Bad slot
    EXPECT_ZX(volume->Revoke(Volume::kNumSlots), ZX_ERR_INVALID_ARGS);

    // Valid, even if slot isn't enrolled
    EXPECT_OK(volume->Revoke(Volume::kNumSlots - 1));

    // Valid, even if last slot
    EXPECT_OK(volume->Revoke(0));
    EXPECT_ZX(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume),
              ZX_ERR_ACCESS_DENIED);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestRevoke);

bool TestShred(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Bind(version, fvm));

    fbl::unique_ptr<Volume> volume;
    ASSERT_OK(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume));

    // Valid
    EXPECT_OK(volume->Shred());

    // No further methods work
    EXPECT_ZX(volume->Enroll(device.key(), 0), ZX_ERR_BAD_STATE);
    EXPECT_ZX(volume->Revoke(0), ZX_ERR_BAD_STATE);
    EXPECT_ZX(Volume::Open(fbl::move(device.parent()), device.key(), 0, &volume),
              ZX_ERR_ACCESS_DENIED);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestShred);

BEGIN_TEST_CASE(VolumeTest)
RUN_EACH_DEVICE(TestCreate)
RUN_EACH_DEVICE(TestOpen)
RUN_EACH_DEVICE(TestEnroll)
RUN_EACH_DEVICE(TestRevoke)
RUN_EACH_DEVICE(TestShred)
END_TEST_CASE(VolumeTest)

} // namespace
} // namespace testing
} // namespace zxcrypt
