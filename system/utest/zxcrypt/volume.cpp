// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <crypto/cipher.h>
#include <crypto/secret.h>
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
bool VolumeCreate(const fbl::unique_fd& fd, const crypto::Secret& key, bool fvm,
                  zx_status_t expected) {
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

    fbl::unique_fd new_fd(dup(fd.get()));
    EXPECT_EQ(Volume::Create(fbl::move(new_fd), key), expected, err);

    END_HELPER;
}

bool TestInit(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Create(kDeviceSize, kBlockSize, fvm));

    // Invalid arguments
    fbl::unique_fd bad_fd;
    fbl::unique_ptr<Volume> volume;
    EXPECT_ZX(Volume::Init(fbl::move(bad_fd), &volume), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(Volume::Init(fbl::move(device.parent()), nullptr), ZX_ERR_INVALID_ARGS);

    // Valid
    EXPECT_ZX(Volume::Init(device.parent(), &volume), ZX_OK);
    ASSERT_TRUE(!!volume);
    EXPECT_EQ(volume->reserved_blocks(), fvm ? (FVM_BLOCK_SIZE / kBlockSize) : 2u);
    EXPECT_EQ(volume->reserved_slices(), fvm ? 1u : 0u);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestInit);

bool TestCreate(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Create(kDeviceSize, kBlockSize, fvm));

    // Invalid file descriptor
    fbl::unique_fd bad_fd;
    EXPECT_ZX(Volume::Create(fbl::move(bad_fd), device.key()), ZX_ERR_INVALID_ARGS);

    // Weak key
    crypto::Secret short_key;
    ASSERT_OK(short_key.Generate(device.key().len() - 1));
    EXPECT_TRUE(VolumeCreate(device.parent(), short_key, fvm, ZX_ERR_INVALID_ARGS));

    // Valid
    EXPECT_TRUE(VolumeCreate(device.parent(), device.key(), fvm, ZX_OK));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestCreate);

bool TestUnlock(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Create(kDeviceSize, kBlockSize, fvm));

    // Invalid device
    fbl::unique_ptr<Volume> volume;
    EXPECT_ZX(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume),
              ZX_ERR_ACCESS_DENIED);

    // Bad file descriptor
    fbl::unique_fd bad_fd;
    EXPECT_ZX(Volume::Unlock(fbl::move(bad_fd), device.key(), 0, &volume), ZX_ERR_INVALID_ARGS);

    // Bad key
    ASSERT_TRUE(VolumeCreate(device.parent(), device.key(), fvm, ZX_OK));

    crypto::Secret bad_key;
    ASSERT_OK(bad_key.Generate(device.key().len()));
    EXPECT_ZX(Volume::Unlock(fbl::move(device.parent()), bad_key, 0, &volume),
              ZX_ERR_ACCESS_DENIED);

    // Bad slot
    EXPECT_ZX(Volume::Unlock(fbl::move(device.parent()), device.key(), -1, &volume),
              ZX_ERR_ACCESS_DENIED);
    EXPECT_ZX(Volume::Unlock(fbl::move(device.parent()), device.key(), 1, &volume),
              ZX_ERR_ACCESS_DENIED);

    // Valid
    EXPECT_OK(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume));

    // Corrupt the key in each block.
    fbl::unique_fd parent = device.parent();
    off_t off = 0;
    uint8_t before[kBlockSize];
    uint8_t after[sizeof(before)];
    const size_t num_blocks = volume->reserved_blocks();

    for (size_t i = 0; i < num_blocks; ++i) {
        // On FVM, the trailing reserved blocks may just be to pad to a slice, and not have any
        // metdata.  Start from the end and iterate backward to ensure the last block corrupted has
        // metadata.
        ASSERT_TRUE(device.Corrupt(num_blocks - 1 - i, 0));
        lseek(parent.get(), off, SEEK_SET);
        read(parent.get(), before, sizeof(before));

        if (i < num_blocks - 1) {
            // Volume should still be unlockable as long as one copy of the key exists
            EXPECT_OK(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume));
        } else {
            // Key should fail when last copy is corrupted.
            EXPECT_ZX(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume),
                      ZX_ERR_ACCESS_DENIED);
        }

        lseek(parent.get(), off, SEEK_SET);
        read(parent.get(), after, sizeof(after));

        // Unlock should not modify the parent
        EXPECT_EQ(memcmp(before, after, sizeof(before)), 0);
    }

    END_TEST;
}
DEFINE_EACH_DEVICE(TestUnlock);

bool TestEnroll(Volume::Version version, bool fvm) {
    BEGIN_TEST;
    TestDevice device;
    ASSERT_TRUE(device.Bind(version, fvm));

    fbl::unique_ptr<Volume> volume;
    ASSERT_OK(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume));

    // Bad key
    crypto::Secret bad_key;
    EXPECT_ZX(volume->Enroll(bad_key, 1), ZX_ERR_INVALID_ARGS);

    // Bad slot
    EXPECT_ZX(volume->Enroll(device.key(), volume->num_slots()), ZX_ERR_INVALID_ARGS);

    // Valid; new slot
    EXPECT_OK(volume->Enroll(device.key(), 1));
    EXPECT_OK(Volume::Unlock(fbl::move(device.parent()), device.key(), 1, &volume));

    // Valid; existing slot
    EXPECT_OK(volume->Enroll(device.key(), 0));
    EXPECT_OK(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestEnroll);

bool TestRevoke(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Bind(version, fvm));

    fbl::unique_ptr<Volume> volume;
    ASSERT_OK(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume));

    // Bad slot
    EXPECT_ZX(volume->Revoke(volume->num_slots()), ZX_ERR_INVALID_ARGS);

    // Valid, even if slot isn't enrolled
    EXPECT_OK(volume->Revoke(volume->num_slots() - 1));

    // Valid, even if last slot
    EXPECT_OK(volume->Revoke(0));
    EXPECT_ZX(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume),
              ZX_ERR_ACCESS_DENIED);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestRevoke);

bool TestShred(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_TRUE(device.Bind(version, fvm));

    fbl::unique_ptr<Volume> volume;
    ASSERT_OK(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume));

    // Valid
    EXPECT_OK(volume->Shred());

    // No further methods work
    EXPECT_ZX(volume->Enroll(device.key(), 0), ZX_ERR_BAD_STATE);
    EXPECT_ZX(volume->Revoke(0), ZX_ERR_BAD_STATE);
    EXPECT_ZX(Volume::Unlock(fbl::move(device.parent()), device.key(), 0, &volume),
              ZX_ERR_ACCESS_DENIED);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestShred);

BEGIN_TEST_CASE(VolumeTest)
RUN_EACH_DEVICE(TestInit)
RUN_EACH_DEVICE(TestCreate)
RUN_EACH_DEVICE(TestUnlock)
RUN_EACH_DEVICE(TestEnroll)
RUN_EACH_DEVICE(TestRevoke)
RUN_EACH_DEVICE(TestShred)
END_TEST_CASE(VolumeTest)

} // namespace
} // namespace testing
} // namespace zxcrypt
