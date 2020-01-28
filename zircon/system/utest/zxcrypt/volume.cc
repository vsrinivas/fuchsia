// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <inttypes.h>
#include <lib/fzl/fdio.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <crypto/cipher.h>
#include <crypto/secret.h>
#include <kms-stateless/kms-stateless.h>
#include <unittest/unittest.h>
#include <zxcrypt/fdio-volume.h>
#include <zxcrypt/volume.h>

#include "test-device.h"

namespace zxcrypt {
namespace testing {
namespace {

// See test-device.h; the following macros allow reusing tests for each of the supported versions.
#define EACH_PARAM(OP, Test) OP(Test, Volume, AES256_XTS_SHA256)

// ZX-1948: Dump extra information if encountering an unexpected error during volume creation.
bool VolumeCreate(const fbl::unique_fd& fd, const fbl::unique_fd& devfs_root,
                  const crypto::Secret& key, bool fvm, zx_status_t expected) {
  BEGIN_HELPER;

  char err[128];
  fzl::UnownedFdioCaller caller(fd.get());
  fuchsia_hardware_block_BlockInfo block_info;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &status, &block_info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  if (fvm) {
    fuchsia_hardware_block_volume_VolumeInfo fvm_info;
    ASSERT_OK(
        fuchsia_hardware_block_volume_VolumeQuery(caller.borrow_channel(), &status, &fvm_info));
    ASSERT_OK(status);

    snprintf(
        err, sizeof(err),
        "details: block size=%" PRIu32 ", block count=%" PRIu64 ", slice size=%zu, slice count=%zu",
        block_info.block_size, block_info.block_count, fvm_info.slice_size, fvm_info.vslice_count);
  } else {
    snprintf(err, sizeof(err), "details: block size=%" PRIu32 ", block count=%" PRIu64,
             block_info.block_size, block_info.block_count);
  }

  fbl::unique_fd new_fd(dup(fd.get()));
  fbl::unique_fd devfs_root_copy(dup(devfs_root.get()));
  EXPECT_EQ(FdioVolume::Create(std::move(new_fd), std::move(devfs_root_copy), key), expected, err);

  END_HELPER;
}

bool TestInit(Volume::Version version, bool fvm) {
  BEGIN_TEST;

  TestDevice device;
  ASSERT_TRUE(device.SetupDevmgr());
  ASSERT_TRUE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  // Invalid arguments
  fbl::unique_fd bad_fd;
  fbl::unique_fd bad_fd2;
  std::unique_ptr<FdioVolume> volume;
  EXPECT_ZX(FdioVolume::Init(std::move(bad_fd), device.devfs_root(), &volume), ZX_ERR_INVALID_ARGS);
  EXPECT_ZX(FdioVolume::Init(device.parent(), std::move(bad_fd2), &volume), ZX_ERR_INVALID_ARGS);
  EXPECT_ZX(FdioVolume::Init(device.parent(), device.devfs_root(), nullptr), ZX_ERR_INVALID_ARGS);

  // Valid
  EXPECT_ZX(FdioVolume::Init(device.parent(), device.devfs_root(), &volume), ZX_OK);
  ASSERT_TRUE(!!volume);
  EXPECT_EQ(volume->reserved_blocks(), fvm ? (fvm::kBlockSize / kBlockSize) : 2u);
  EXPECT_EQ(volume->reserved_slices(), fvm ? 1u : 0u);

  END_TEST;
}
DEFINE_EACH_DEVICE(TestInit)

bool TestCreate(Volume::Version version, bool fvm) {
  BEGIN_TEST;

  TestDevice device;
  ASSERT_TRUE(device.SetupDevmgr());
  ASSERT_TRUE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  // Invalid file descriptor
  fbl::unique_fd bad_fd;
  EXPECT_ZX(FdioVolume::Create(std::move(bad_fd), device.devfs_root(), device.key()),
            ZX_ERR_INVALID_ARGS);

  // Weak key
  crypto::Secret short_key;
  ASSERT_OK(short_key.Generate(device.key().len() - 1));
  EXPECT_TRUE(
      VolumeCreate(device.parent(), device.devfs_root(), short_key, fvm, ZX_ERR_INVALID_ARGS));

  // Valid
  EXPECT_TRUE(VolumeCreate(device.parent(), device.devfs_root(), device.key(), fvm, ZX_OK));

  END_TEST;
}
DEFINE_EACH_DEVICE(TestCreate)

bool TestUnlock(Volume::Version version, bool fvm) {
  BEGIN_TEST;

  TestDevice device;
  ASSERT_TRUE(device.SetupDevmgr());
  ASSERT_TRUE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  // Invalid device
  std::unique_ptr<FdioVolume> volume;
  EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);

  // Bad file descriptor
  fbl::unique_fd bad_fd;
  EXPECT_ZX(FdioVolume::Unlock(std::move(bad_fd), device.devfs_root(), device.key(), 0, &volume),
            ZX_ERR_INVALID_ARGS);

  // Bad key
  ASSERT_TRUE(VolumeCreate(device.parent(), device.devfs_root(), device.key(), fvm, ZX_OK));

  crypto::Secret bad_key;
  ASSERT_OK(bad_key.Generate(device.key().len()));
  EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), bad_key, 0, &volume),
            ZX_ERR_ACCESS_DENIED);

  // Bad slot
  EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), -1, &volume),
            ZX_ERR_ACCESS_DENIED);
  EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 1, &volume),
            ZX_ERR_ACCESS_DENIED);

  // Valid
  EXPECT_OK(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume));

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
      EXPECT_OK(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume));
    } else {
      // Key should fail when last copy is corrupted.
      EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume),
                ZX_ERR_ACCESS_DENIED);
    }

    lseek(parent.get(), off, SEEK_SET);
    read(parent.get(), after, sizeof(after));

    // Unlock should not modify the parent
    EXPECT_EQ(memcmp(before, after, sizeof(before)), 0);
  }

  END_TEST;
}
DEFINE_EACH_DEVICE(TestUnlock)

bool TestEnroll(Volume::Version version, bool fvm) {
  BEGIN_TEST;
  TestDevice device;
  ASSERT_TRUE(device.SetupDevmgr());
  ASSERT_TRUE(device.Bind(version, fvm));

  std::unique_ptr<FdioVolume> volume;
  ASSERT_OK(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume));

  // Bad key
  crypto::Secret bad_key;
  EXPECT_ZX(volume->Enroll(bad_key, 1), ZX_ERR_INVALID_ARGS);

  // Bad slot
  EXPECT_ZX(volume->Enroll(device.key(), volume->num_slots()), ZX_ERR_INVALID_ARGS);

  // Valid; new slot
  EXPECT_OK(volume->Enroll(device.key(), 1));
  EXPECT_OK(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 1, &volume));

  // Valid; existing slot
  EXPECT_OK(volume->Enroll(device.key(), 0));
  EXPECT_OK(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume));

  END_TEST;
}
DEFINE_EACH_DEVICE(TestEnroll)

bool TestRevoke(Volume::Version version, bool fvm) {
  BEGIN_TEST;

  TestDevice device;
  ASSERT_TRUE(device.SetupDevmgr());
  ASSERT_TRUE(device.Bind(version, fvm));

  std::unique_ptr<FdioVolume> volume;
  ASSERT_OK(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume));

  // Bad slot
  EXPECT_ZX(volume->Revoke(volume->num_slots()), ZX_ERR_INVALID_ARGS);

  // Valid, even if slot isn't enrolled
  EXPECT_OK(volume->Revoke(volume->num_slots() - 1));

  // Valid, even if last slot
  EXPECT_OK(volume->Revoke(0));
  EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);

  END_TEST;
}
DEFINE_EACH_DEVICE(TestRevoke)

bool TestShred(Volume::Version version, bool fvm) {
  BEGIN_TEST;

  TestDevice device;
  ASSERT_TRUE(device.SetupDevmgr());
  ASSERT_TRUE(device.Bind(version, fvm));

  std::unique_ptr<FdioVolume> volume;
  ASSERT_OK(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume));

  // Valid
  EXPECT_OK(volume->Shred());

  // No further methods work
  EXPECT_ZX(volume->Enroll(device.key(), 0), ZX_ERR_BAD_STATE);
  EXPECT_ZX(volume->Revoke(0), ZX_ERR_BAD_STATE);
  EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);

  END_TEST;
}
DEFINE_EACH_DEVICE(TestShred)

bool TestShredThroughDriver(Volume::Version version, bool fvm) {
  BEGIN_TEST;

  TestDevice device;
  ASSERT_TRUE(device.SetupDevmgr());
  ASSERT_TRUE(device.Bind(version, fvm));

  std::unique_ptr<FdioVolume> volume;
  ASSERT_OK(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume));

  zx::channel driver_chan;
  ASSERT_OK(volume->OpenManager(zx::duration::infinite(), driver_chan.reset_and_get_address()));
  FdioVolumeManager zxc_manager(std::move(driver_chan));
  EXPECT_OK(zxc_manager.Shred());
  EXPECT_OK(zxc_manager.Seal());

  // Key should no longer work
  EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);

  END_TEST;
}
DEFINE_EACH_DEVICE(TestShredThroughDriver)

bool TestShredThroughDriverLocked(Volume::Version version, bool fvm) {
  BEGIN_TEST;

  TestDevice device;
  ASSERT_TRUE(device.SetupDevmgr());
  ASSERT_TRUE(device.Bind(version, fvm));

  std::unique_ptr<FdioVolume> volume;
  ASSERT_OK(FdioVolume::Init(device.parent(), device.devfs_root(), &volume));

  zx::channel driver_chan;
  ASSERT_OK(volume->OpenManager(zx::duration::infinite(), driver_chan.reset_and_get_address()));
  FdioVolumeManager zxc_manager(std::move(driver_chan));
  EXPECT_OK(zxc_manager.Shred());

  // Key should no longer work
  EXPECT_ZX(FdioVolume::Unlock(device.parent(), device.devfs_root(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);

  END_TEST;
}
DEFINE_EACH_DEVICE(TestShredThroughDriverLocked)


BEGIN_TEST_CASE(VolumeTest)
RUN_EACH_DEVICE(TestInit)
RUN_EACH_DEVICE(TestCreate)
RUN_EACH_DEVICE(TestUnlock)
RUN_EACH_DEVICE(TestEnroll)
RUN_EACH_DEVICE(TestRevoke)
RUN_EACH_DEVICE(TestShred)
RUN_EACH_DEVICE(TestShredThroughDriver)
RUN_EACH_DEVICE(TestShredThroughDriverLocked)
END_TEST_CASE(VolumeTest)

bool CheckOneCreatePolicy(KeySourcePolicy policy, fbl::Vector<KeySource> expected) {
  BEGIN_HELPER;
  fbl::Vector<KeySource> actual = ComputeEffectiveCreatePolicy(policy);
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < actual.size(); i++) {
    ASSERT_EQ(actual[i], expected[i]);
  }
  END_HELPER;
}

bool TestCreatePolicy() {
  BEGIN_TEST;

  EXPECT_TRUE(CheckOneCreatePolicy(NullSource, {kNullSource}));
  EXPECT_TRUE(CheckOneCreatePolicy(TeeRequiredSource, {kTeeSource}));
  EXPECT_TRUE(CheckOneCreatePolicy(TeeTransitionalSource, {kTeeSource}));
  EXPECT_TRUE(CheckOneCreatePolicy(TeeOpportunisticSource, {kTeeSource, kNullSource}));

  END_TEST;
}

bool CheckOneUnsealPolicy(KeySourcePolicy policy, fbl::Vector<KeySource> expected) {
  BEGIN_HELPER;
  fbl::Vector<KeySource> actual = ComputeEffectiveUnsealPolicy(policy);
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < actual.size(); i++) {
    ASSERT_EQ(actual[i], expected[i]);
  }
  END_HELPER;
}

bool TestUnsealPolicy() {
  BEGIN_TEST;

  EXPECT_TRUE(CheckOneUnsealPolicy(NullSource, {kNullSource}));
  EXPECT_TRUE(CheckOneUnsealPolicy(TeeRequiredSource, {kTeeSource}));
  EXPECT_TRUE(CheckOneUnsealPolicy(TeeTransitionalSource, {kTeeSource, kNullSource}));
  EXPECT_TRUE(CheckOneUnsealPolicy(TeeOpportunisticSource, {kTeeSource, kNullSource}));

  END_TEST;
}

BEGIN_TEST_CASE(PolicyTest)
RUN_TEST(TestCreatePolicy)
RUN_TEST(TestUnsealPolicy)
END_TEST_CASE(PolicyTest)

}  // namespace
}  // namespace testing
}  // namespace zxcrypt
