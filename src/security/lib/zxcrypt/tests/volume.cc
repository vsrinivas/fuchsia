// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/lib/zxcrypt/volume.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <inttypes.h>
#include <lib/fdio/cpp/caller.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <utility>

#include <zxtest/zxtest.h>

#include "src/security/lib/fcrypto/secret.h"
#include "src/security/lib/zxcrypt/client.h"
#include "src/security/lib/zxcrypt/fdio-volume.h"
#include "src/security/lib/zxcrypt/tests/test-device.h"

namespace zxcrypt {
namespace testing {
namespace {

// See test-device.h; the following macros allow reusing tests for each of the supported versions.
#define EACH_PARAM(OP, TestSuite, Test) OP(TestSuite, Test, Volume, AES256_XTS_SHA256)

// fxbug.dev/31814: Dump extra information if encountering an unexpected error during volume
// creation.
void VolumeCreate(const fbl::unique_fd& fd, const fbl::unique_fd& devfs_root,
                  const crypto::Secret& key, bool fvm, zx_status_t expected) {
  char err[128];
  fdio_cpp::UnownedFdioCaller caller(fd);
  const fidl::WireResult result =
      fidl::WireCall(caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);
  const fuchsia_hardware_block::wire::BlockInfo& block_info = *response.info;

  if (fvm) {
    const fidl::WireResult result =
        fidl::WireCall(caller.borrow_as<fuchsia_hardware_block_volume::Volume>())->GetVolumeInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
    const fuchsia_hardware_block_volume::wire::VolumeManagerInfo& manager_info = *response.manager;

    snprintf(err, sizeof(err),
             "details: block size=%" PRIu32 ", block count=%" PRIu64
             ", slice size=%zu, slice count=%zu",
             block_info.block_size, block_info.block_count, manager_info.slice_size,
             manager_info.slice_count);
  } else {
    snprintf(err, sizeof(err), "details: block size=%" PRIu32 ", block count=%" PRIu64,
             block_info.block_size, block_info.block_count);
  }

  fbl::unique_fd new_fd(dup(fd.get()));
  EXPECT_EQ(FdioVolume::Create(std::move(new_fd), key), expected, "%s", err);
}

void TestInit(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  // Invalid arguments
  fbl::unique_fd bad_fd;
  fbl::unique_fd bad_fd2;
  std::unique_ptr<FdioVolume> volume;
  EXPECT_ZX(FdioVolume::Init(std::move(bad_fd), &volume), ZX_ERR_INVALID_ARGS);
  EXPECT_ZX(FdioVolume::Init(device.parent().duplicate(), nullptr), ZX_ERR_INVALID_ARGS);

  // Valid
  EXPECT_ZX(FdioVolume::Init(device.parent().duplicate(), &volume), ZX_OK);
  ASSERT_TRUE(!!volume);
  EXPECT_EQ(volume->reserved_blocks(), fvm ? (fvm::kBlockSize / kBlockSize) : 2u);
  EXPECT_EQ(volume->reserved_slices(), fvm ? 1u : 0u);
}
DEFINE_EACH_DEVICE(VolumeTest, TestInit)

void TestCreate(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  // Invalid file descriptor
  fbl::unique_fd bad_fd;
  EXPECT_ZX(FdioVolume::Create(std::move(bad_fd), device.key()), ZX_ERR_INVALID_ARGS);

  // Weak key
  crypto::Secret short_key;
  ASSERT_OK(short_key.Generate(device.key().len() - 1));
  VolumeCreate(device.parent(), device.devfs_root(), short_key, fvm, ZX_ERR_INVALID_ARGS);

  // Valid
  VolumeCreate(device.parent(), device.devfs_root(), device.key(), fvm, ZX_OK);
}
DEFINE_EACH_DEVICE(VolumeTest, TestCreate)

void TestUnlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  // Invalid device
  std::unique_ptr<FdioVolume> volume;
  EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);

  // Bad file descriptor
  fbl::unique_fd bad_fd;
  EXPECT_ZX(FdioVolume::Unlock(std::move(bad_fd), device.key(), 0, &volume), ZX_ERR_INVALID_ARGS);

  // Bad key
  ASSERT_NO_FATAL_FAILURE(
      VolumeCreate(device.parent(), device.devfs_root(), device.key(), fvm, ZX_OK));

  crypto::Secret bad_key;
  ASSERT_OK(bad_key.Generate(device.key().len()));
  EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), bad_key, 0, &volume),
            ZX_ERR_ACCESS_DENIED);

  // Bad slot
  EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), device.key(), -1, &volume),
            ZX_ERR_ACCESS_DENIED);
  EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 1, &volume),
            ZX_ERR_ACCESS_DENIED);

  // Valid
  EXPECT_OK(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume));

  // Corrupt the key in each block.
  off_t off = 0;
  uint8_t before[kBlockSize];
  uint8_t after[sizeof(before)];
  const size_t num_blocks = volume->reserved_blocks();

  for (size_t i = 0; i < num_blocks; ++i) {
    // On FVM, the trailing reserved blocks may just be to pad to a slice, and not have any
    // metdata.  Start from the end and iterate backward to ensure the last block corrupted has
    // metadata.
    ASSERT_NO_FATAL_FAILURE(device.Corrupt(num_blocks - 1 - i, 0));
    lseek(device.parent().get(), off, SEEK_SET);
    read(device.parent().get(), before, sizeof(before));

    if (i < num_blocks - 1) {
      // Volume should still be unlockable as long as one copy of the key exists
      EXPECT_OK(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume));
    } else {
      // Key should fail when last copy is corrupted.
      EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume),
                ZX_ERR_ACCESS_DENIED);
    }

    lseek(device.parent().get(), off, SEEK_SET);
    read(device.parent().get(), after, sizeof(after));

    // Unlock should not modify the parent
    EXPECT_EQ(memcmp(before, after, sizeof(before)), 0);
  }
}
DEFINE_EACH_DEVICE(VolumeTest, TestUnlock)

void TestEnroll(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  std::unique_ptr<FdioVolume> volume;
  ASSERT_OK(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume));

  // Bad key
  crypto::Secret bad_key;
  EXPECT_ZX(volume->Enroll(bad_key, 1), ZX_ERR_INVALID_ARGS);

  // Bad slot
  EXPECT_ZX(volume->Enroll(device.key(), volume->num_slots()), ZX_ERR_INVALID_ARGS);

  // Valid; new slot
  EXPECT_OK(volume->Enroll(device.key(), 1));
  EXPECT_OK(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 1, &volume));

  // Valid; existing slot
  EXPECT_OK(volume->Enroll(device.key(), 0));
  EXPECT_OK(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume));
}
DEFINE_EACH_DEVICE(VolumeTest, TestEnroll)

void TestRevoke(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  std::unique_ptr<FdioVolume> volume;
  ASSERT_OK(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume));

  // Bad slot
  EXPECT_ZX(volume->Revoke(volume->num_slots()), ZX_ERR_INVALID_ARGS);

  // Valid, even if slot isn't enrolled
  EXPECT_OK(volume->Revoke(volume->num_slots() - 1));

  // Valid, even if last slot
  EXPECT_OK(volume->Revoke(0));
  EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);
}
DEFINE_EACH_DEVICE(VolumeTest, TestRevoke)

void TestShred(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  std::unique_ptr<FdioVolume> volume;
  ASSERT_OK(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume));

  // Valid
  EXPECT_OK(volume->Shred());

  // No further methods work
  EXPECT_ZX(volume->Enroll(device.key(), 0), ZX_ERR_BAD_STATE);
  EXPECT_ZX(volume->Revoke(0), ZX_ERR_BAD_STATE);
  EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);
}
DEFINE_EACH_DEVICE(VolumeTest, TestShred)

void TestFormatThroughDriver(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  zxcrypt::VolumeManager manager(device.parent().duplicate(), device.devfs_root().duplicate());
  zx::channel chan;
  ASSERT_OK(manager.OpenClient(zx::duration::infinite(), chan));

  zxcrypt::EncryptedVolumeClient zxc_client(std::move(chan));
  uint8_t slot = 0;
  auto& key = device.key();
  ASSERT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_OK);
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_OK);
  EXPECT_OK(zxc_client.Seal());

  // Verify that we can also format the device with a slot other than slot 0
  slot = 1;
  ASSERT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_OK);
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_OK);
  EXPECT_OK(zxc_client.Seal());
}
DEFINE_EACH_DEVICE(VolumeTest, TestFormatThroughDriver)

void TestShredThroughDriver(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  zxcrypt::VolumeManager manager(device.parent().duplicate(), device.devfs_root().duplicate());
  zx::channel chan;
  ASSERT_OK(manager.OpenClient(zx::duration::infinite(), chan));
  zxcrypt::EncryptedVolumeClient zxc_client(std::move(chan));

  EXPECT_OK(zxc_client.Shred());
  EXPECT_OK(zxc_client.Seal());

  // Key should no longer work
  std::unique_ptr<FdioVolume> volume;
  EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);
}
DEFINE_EACH_DEVICE(VolumeTest, TestShredThroughDriver)

void TestShredThroughDriverLocked(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  zxcrypt::VolumeManager manager(device.parent().duplicate(), device.devfs_root().duplicate());
  zx::channel chan;
  ASSERT_OK(manager.OpenClient(zx::duration::infinite(), chan));
  zxcrypt::EncryptedVolumeClient zxc_client(std::move(chan));
  EXPECT_OK(zxc_client.Shred());

  // Key should no longer work
  std::unique_ptr<FdioVolume> volume;
  EXPECT_ZX(FdioVolume::Unlock(device.parent().duplicate(), device.key(), 0, &volume),
            ZX_ERR_ACCESS_DENIED);
}
DEFINE_EACH_DEVICE(VolumeTest, TestShredThroughDriverLocked)

void TestFormatAfterShredThroughDriverWhileSealed(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  zxcrypt::VolumeManager manager(device.parent().duplicate(), device.devfs_root().duplicate());
  zx::channel chan;
  ASSERT_OK(manager.OpenClient(zx::duration::infinite(), chan));

  zxcrypt::EncryptedVolumeClient zxc_client(std::move(chan));
  uint8_t slot = 0;
  auto& key = device.key();

  EXPECT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_OK);

  // We can unseal before Shred
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_OK);
  EXPECT_EQ(zxc_client.Seal(), ZX_OK);

  // We cannot unseal after Shred
  EXPECT_EQ(zxc_client.Shred(), ZX_OK);
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_ERR_ACCESS_DENIED);

  // We can format after Shred
  EXPECT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_OK);

  // We can unseal after format after Shred
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_OK);
}
DEFINE_EACH_DEVICE(VolumeTest, TestFormatAfterShredThroughDriverWhileSealed)

void TestFormatAfterShredThroughDriverWhileUnsealed(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  zxcrypt::VolumeManager manager(device.parent().duplicate(), device.devfs_root().duplicate());
  zx::channel chan;
  ASSERT_OK(manager.OpenClient(zx::duration::infinite(), chan));

  zxcrypt::EncryptedVolumeClient zxc_client(std::move(chan));
  uint8_t slot = 0;
  auto& key = device.key();

  EXPECT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_OK);

  // We can Unseal before Shred
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_OK);

  // We can Shred while unsealed
  EXPECT_EQ(zxc_client.Shred(), ZX_OK);

  // We cannot format while still unsealed & shredded
  EXPECT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_ERR_BAD_STATE);

  // We can seal, but cannot unseal
  EXPECT_EQ(zxc_client.Seal(), ZX_OK);
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_ERR_ACCESS_DENIED);

  // We can format after sealing
  EXPECT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_OK);

  // We can unseal after format
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_OK);
}
DEFINE_EACH_DEVICE(VolumeTest, TestFormatAfterShredThroughDriverWhileUnsealed)

void TestDriverStateTransitions(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Create(kDeviceSize, kBlockSize, fvm, version));

  zxcrypt::VolumeManager manager(device.parent().duplicate(), device.devfs_root().duplicate());
  zx::channel chan;
  ASSERT_OK(manager.OpenClient(zx::duration::infinite(), chan));

  zxcrypt::EncryptedVolumeClient zxc_client(std::move(chan));
  uint8_t slot = 0;
  auto& key = device.key();

  // We believe we are in state kSealed.
  // We cannot Seal.
  EXPECT_EQ(zxc_client.Seal(), ZX_ERR_BAD_STATE);
  // We can Shred(), which does not transition our state.
  EXPECT_EQ(zxc_client.Shred(), ZX_OK);
  // We can Format() again, which does not transition our state
  EXPECT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_OK);
  // We can Unseal, which takes us to state kUnsealed.
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_OK);

  // We believe we are in state kUnsealed.
  // We cannot Unseal() again; we are already unsealed.
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_ERR_BAD_STATE);
  // We cannot Format().
  EXPECT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_ERR_BAD_STATE);
  // We can Seal() once, returning us to kSealed, but then can't Seal() again.
  EXPECT_EQ(zxc_client.Seal(), ZX_OK);
  EXPECT_EQ(zxc_client.Seal(), ZX_ERR_BAD_STATE);
  // Return to kUnsealed once more by calling Unseal().
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_OK);
  // We can Shred(), which takes us to kUnsealedShredded
  EXPECT_EQ(zxc_client.Shred(), ZX_OK);

  // We believe we are in state kUnsealedShredded.
  // We cannot Format() from kUnsealedShredded.
  EXPECT_EQ(zxc_client.Format(key.get(), key.len(), slot), ZX_ERR_BAD_STATE);
  // We cannot Unseal() from kUnsealedShredded.
  EXPECT_EQ(zxc_client.Unseal(key.get(), key.len(), slot), ZX_ERR_BAD_STATE);
  // We can Shred() again from kUnsealedShredded, which leaves us in the same
  // state.
  EXPECT_EQ(zxc_client.Shred(), ZX_OK);
  // We can Seal(), which takes us back to kSealed()
  EXPECT_EQ(zxc_client.Seal(), ZX_OK);
}
DEFINE_EACH_DEVICE(VolumeTest, TestDriverStateTransitions)

constexpr uint64_t kFakeVolumeSize = 1 << 24;
class TestVolume : public zxcrypt::Volume {
 public:
  zx_status_t DoInit() {
    // Init is protected, so we make a public method here to reach it.
    return Init();
  }
  zx_status_t GetBlockInfo(BlockInfo* out) override {
    // Expect a large virtual address space.
    out->block_count = kFakeVolumeSize;
    out->block_size = 8192;
    return ZX_OK;
  }

  zx_status_t GetFvmSliceSize(uint64_t* out) override {
    // Just an example slice size from Astro.
    *out = 1048576;
    return ZX_OK;
  }

  zx_status_t DoBlockFvmVsliceQuery(uint64_t vslice_start, SliceRegion ranges[MAX_SLICE_REGIONS],
                                    uint64_t* slice_count) override = 0;

  zx_status_t DoBlockFvmExtend(uint64_t start_slice, uint64_t slice_count) override {
    extend_calls_++;
    last_extend_start_slice_ = start_slice;
    last_extend_slice_count_ = slice_count;
    return ZX_OK;
  }

  zx_status_t Read() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Write() override { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Flush() override { return ZX_ERR_NOT_SUPPORTED; }

  int extend_calls_ = 0;
  uint64_t last_extend_start_slice_ = 0;
  uint64_t last_extend_slice_count_ = 0;
};

TEST(VolumeTest, TestFvmUsageNewImage) {
  // Verify that when we start out with a single FVM slice, we'll attempt to
  // allocate a second one for the inner volume when we call Init().
  class TestVolumeNewImage : public TestVolume {
    zx_status_t DoBlockFvmVsliceQuery(uint64_t vslice_start, SliceRegion ranges[MAX_SLICE_REGIONS],
                                      uint64_t* slice_count) override {
      if (vslice_start == 0) {
        if (extend_calls_ > 0) {
          ranges[0].allocated = true;
          ranges[0].count = 2;
          ranges[1].allocated = false;
          ranges[1].count = kFakeVolumeSize - 2;
          *slice_count = 2;
          return ZX_OK;
        }
        ranges[0].allocated = true;
        ranges[0].count = 1;
        ranges[1].allocated = false;
        ranges[1].count = kFakeVolumeSize - 1;
        *slice_count = 2;
        return ZX_OK;

        return ZX_OK;
      }
      if (vslice_start == 1) {
        if (extend_calls_ > 0) {
          ranges[0].allocated = true;
          ranges[0].count = 1;
          ranges[1].allocated = true;
          ranges[1].count = kFakeVolumeSize - 2;
          *slice_count = 2;
          return ZX_OK;
        }
        ranges[0].allocated = false;
        ranges[0].count = kFakeVolumeSize - 1;
        *slice_count = 1;
        return ZX_OK;
      }

      // Should be unreachable.
      return ZX_ERR_NOT_SUPPORTED;
    }
  };
  TestVolumeNewImage volume;
  EXPECT_OK(volume.DoInit());
  EXPECT_EQ(volume.extend_calls_, 1);
  EXPECT_EQ(volume.last_extend_start_slice_, 1);
  EXPECT_EQ(volume.last_extend_slice_count_, 1);
}

TEST(VolumeTest, TestFvmUsageAlreadyAllocated) {
  // Verify that when we start out with two FVM slices allocated, we don't
  // attempt to allocate any more when calling Init().
  class TestVolumeAllocatedImage : public TestVolume {
    zx_status_t DoBlockFvmVsliceQuery(uint64_t vslice_start, SliceRegion ranges[MAX_SLICE_REGIONS],
                                      uint64_t* slice_count) override {
      ranges[0].allocated = true;
      ranges[0].count = 2;
      ranges[1].allocated = false;
      ranges[1].count = kFakeVolumeSize - 2;
      *slice_count = 2;
      return ZX_OK;
    }
  };
  TestVolumeAllocatedImage volume;
  EXPECT_OK(volume.DoInit());
  EXPECT_EQ(volume.extend_calls_, 0);
}

void CheckOneCreatePolicy(KeySourcePolicy policy, fbl::Vector<KeySource> expected) {
  fbl::Vector<KeySource> actual = ComputeEffectiveCreatePolicy(policy);
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < actual.size(); i++) {
    ASSERT_EQ(actual[i], expected[i]);
  }
}

TEST(PolicyTest, TestCreatePolicy) {
  CheckOneCreatePolicy(NullSource, {kNullSource});
  CheckOneCreatePolicy(TeeRequiredSource, {kTeeSource});
  CheckOneCreatePolicy(TeeTransitionalSource, {kTeeSource});
  CheckOneCreatePolicy(TeeOpportunisticSource, {kTeeSource, kNullSource});
}

void CheckOneUnsealPolicy(KeySourcePolicy policy, fbl::Vector<KeySource> expected) {
  fbl::Vector<KeySource> actual = ComputeEffectiveUnsealPolicy(policy);
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < actual.size(); i++) {
    ASSERT_EQ(actual[i], expected[i]);
  }
}

TEST(PolicyTest, TestUnsealPolicy) {
  CheckOneUnsealPolicy(NullSource, {kNullSource});
  CheckOneUnsealPolicy(TeeRequiredSource, {kTeeSource});
  CheckOneUnsealPolicy(TeeTransitionalSource, {kTeeSource, kNullSource});
  CheckOneUnsealPolicy(TeeOpportunisticSource, {kTeeSource, kNullSource});
}

}  // namespace
}  // namespace testing
}  // namespace zxcrypt
