// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <iterator>
#include <memory>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <fbl/unique_fd.h>
#include <fvm/format.h>
#include <zxcrypt/volume.h>
#include <zxtest/zxtest.h>

#include "test-device.h"

namespace zxcrypt {
namespace testing {
namespace {

// See test-device.h; the following macros allow reusing tests for each of the supported versions.
#define EACH_PARAM(OP, TestSuite, Test) OP(TestSuite, Test, Volume, AES256_XTS_SHA256)

void TestBind(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBind)

// TODO(aarongreen): When fxbug.dev/31073 is resolved, add tests that check zxcrypt_rekey and zxcrypt_shred.

// Device::DdkGetSize tests
void TestDdkGetSize(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  fbl::unique_fd parent = device.parent();
  fbl::unique_fd zxcrypt = device.zxcrypt();

  struct stat parent_buf, zxcrypt_buf;
  ASSERT_EQ(fstat(parent.get(), &parent_buf), 0, "%s", strerror(errno));
  ASSERT_EQ(fstat(zxcrypt.get(), &zxcrypt_buf), 0, "%s", strerror(errno));

  ASSERT_GT(parent_buf.st_size, zxcrypt_buf.st_size);
  EXPECT_EQ((parent_buf.st_size - zxcrypt_buf.st_size) / device.block_size(),
            device.reserved_blocks());
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestDdkGetSize)

// FIDL tests
void TestBlockGetInfo(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));

  fuchsia_hardware_block_BlockInfo parent_blk, zxcrypt_blk;
  zx_status_t status;
  EXPECT_EQ(
      fuchsia_hardware_block_BlockGetInfo(device.parent_channel()->get(), &status, &parent_blk),
      ZX_OK);
  EXPECT_EQ(status, ZX_OK);

  EXPECT_EQ(
      fuchsia_hardware_block_BlockGetInfo(device.zxcrypt_channel()->get(), &status, &zxcrypt_blk),
      ZX_OK);
  EXPECT_EQ(status, ZX_OK);

  EXPECT_EQ(parent_blk.block_size, zxcrypt_blk.block_size);
  EXPECT_GE(parent_blk.block_count, zxcrypt_blk.block_count + device.reserved_blocks());
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBlockGetInfo)

void TestBlockFvmQuery(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  auto parent = device.parent_channel();
  auto zxcrypt = device.zxcrypt_channel();

  zx_status_t status;
  fuchsia_hardware_block_volume_VolumeInfo parent_fvm, zxcrypt_fvm;
  if (!fvm) {
    // Send FVM query to non-FVM device.
    EXPECT_EQ(fuchsia_hardware_block_volume_VolumeQuery(zxcrypt->get(), &status, &zxcrypt_fvm),
              ZX_OK);
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
  } else {
    // Get the zxcrypt info.
    EXPECT_EQ(fuchsia_hardware_block_volume_VolumeQuery(parent->get(), &status, &parent_fvm),
              ZX_OK);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(fuchsia_hardware_block_volume_VolumeQuery(zxcrypt->get(), &status, &zxcrypt_fvm),
              ZX_OK);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(parent_fvm.slice_size, zxcrypt_fvm.slice_size);
    EXPECT_EQ(parent_fvm.vslice_count, zxcrypt_fvm.vslice_count + device.reserved_slices());
  }
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBlockFvmQuery)

void QueryLeadingFvmSlice(const TestDevice& device, bool fvm) {
  auto parent = device.parent_channel();
  auto zxcrypt = device.zxcrypt_channel();

  uint64_t start_slices[1];
  start_slices[0] = 0;

  fuchsia_hardware_block_volume_VsliceRange
      parent_ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  fuchsia_hardware_block_volume_VsliceRange
      zxcrypt_ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  size_t actual_parent_ranges_count, actual_zxcrypt_ranges_count;
  zx_status_t parent_io_status, zxcrypt_io_status, parent_status, zxcrypt_status;
  parent_io_status = fuchsia_hardware_block_volume_VolumeQuerySlices(
      parent->get(), start_slices, std::size(start_slices), &parent_status, parent_ranges,
      &actual_parent_ranges_count);
  zxcrypt_io_status = fuchsia_hardware_block_volume_VolumeQuerySlices(
      zxcrypt->get(), start_slices, std::size(start_slices), &zxcrypt_status, zxcrypt_ranges,
      &actual_zxcrypt_ranges_count);

  if (fvm) {
    ASSERT_EQ(parent_io_status, ZX_OK);
    ASSERT_EQ(parent_status, ZX_OK);
    ASSERT_EQ(zxcrypt_io_status, ZX_OK);
    ASSERT_EQ(zxcrypt_status, ZX_OK);

    // Query zxcrypt about the slices, which should omit those reserved.
    ASSERT_EQ(actual_parent_ranges_count, 1U);
    EXPECT_TRUE(parent_ranges[0].allocated);

    ASSERT_EQ(actual_zxcrypt_ranges_count, 1U);
    EXPECT_TRUE(zxcrypt_ranges[0].allocated);

    EXPECT_EQ(parent_ranges[0].count, zxcrypt_ranges[0].count + device.reserved_slices());
  } else {
    // Non-FVM parent devices will close the connection upon receiving FVM requests.
    ASSERT_EQ(parent_io_status, ZX_ERR_PEER_CLOSED);

    // zxcrypt always supports the FVM protocol, but returns ERR_NOT_SUPPORTED if not
    // sitting atop an FVM driver.
    ASSERT_EQ(zxcrypt_io_status, ZX_OK);
    ASSERT_EQ(zxcrypt_status, ZX_ERR_NOT_SUPPORTED);
  }
}

void TestBlockFvmVSliceQuery(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  ASSERT_NO_FATAL_FAILURES(QueryLeadingFvmSlice(device, fvm));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBlockFvmVSliceQuery)

void TestBlockFvmShrinkAndExtend(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  auto zxcrypt = device.zxcrypt_channel();

  uint64_t offset = 1;
  uint64_t length = 1;
  zx_status_t status;

  if (!fvm) {
    // Send FVM message to non-FVM device.
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(zxcrypt->get(), offset, length, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(zxcrypt->get(), offset, length, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);
  } else {
    // Shrink the FVM partition and make sure the change in size is reflected
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeShrink(zxcrypt->get(), offset, length, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_NO_FATAL_FAILURES(QueryLeadingFvmSlice(device, fvm));

    // Extend the FVM partition and make sure the change in size is reflected
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(zxcrypt->get(), offset, length, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_NO_FATAL_FAILURES(QueryLeadingFvmSlice(device, fvm));
  }
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBlockFvmShrinkAndExtend)

// Device::DdkIotxnQueue tests
void TestFdZeroLength(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));

  ASSERT_NO_FATAL_FAILURES(device.WriteFd(0, 0));
  ASSERT_NO_FATAL_FAILURES(device.ReadFd(0, 0));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdZeroLength)

void TestFdFirstBlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t one = device.block_size();

  ASSERT_NO_FATAL_FAILURES(device.WriteFd(0, one));
  ASSERT_NO_FATAL_FAILURES(device.ReadFd(0, one));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdFirstBlock)

void TestFdLastBlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.size();
  size_t one = device.block_size();

  ASSERT_NO_FATAL_FAILURES(device.WriteFd(n - one, one));
  ASSERT_NO_FATAL_FAILURES(device.ReadFd(n - one, one));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdLastBlock)

void TestFdAllBlocks(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.size();

  ASSERT_NO_FATAL_FAILURES(device.WriteFd(0, n));
  ASSERT_NO_FATAL_FAILURES(device.ReadFd(0, n));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdAllBlocks)

void TestFdUnaligned(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t one = device.block_size();
  ssize_t one_s = static_cast<ssize_t>(one);

  ASSERT_NO_FATAL_FAILURES(device.WriteFd(one, one));
  ASSERT_NO_FATAL_FAILURES(device.ReadFd(one, one));

  EXPECT_EQ(device.lseek(one - 1), one_s - 1);
  EXPECT_LT(device.write(one, one), 0);
  EXPECT_LT(device.read(one, one), 0);

  EXPECT_EQ(device.lseek(one + 1), one_s + 1);
  EXPECT_LT(device.write(one, one), 0);
  EXPECT_LT(device.read(one, one), 0);

  EXPECT_EQ(device.lseek(one), one_s);
  EXPECT_LT(device.write(one, one - 1), 0);
  EXPECT_LT(device.read(one, one - 1), 0);

  EXPECT_EQ(device.lseek(one), one_s);
  EXPECT_LT(device.write(one, one + 1), 0);
  EXPECT_LT(device.read(one, one + 1), 0);
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdUnaligned)

void TestFdOutOfBounds(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.size();
  ssize_t n_s = static_cast<ssize_t>(n);

  size_t one = device.block_size();
  ssize_t one_s = static_cast<ssize_t>(one);

  size_t two = one + one;
  ssize_t two_s = static_cast<ssize_t>(two);

  ASSERT_NO_FATAL_FAILURES(device.WriteFd(0, one));

  EXPECT_EQ(device.lseek(n), n_s);
  EXPECT_NE(device.write(n, one), one_s);

  EXPECT_EQ(device.lseek(n - one), n_s - one_s);
  EXPECT_NE(device.write(n - one, two), two_s);

  EXPECT_EQ(device.lseek(two), two_s);
  EXPECT_NE(device.write(two, n - one), n_s - one_s);

  EXPECT_EQ(device.lseek(one), one_s);
  EXPECT_NE(device.write(one, n), n_s);

  ASSERT_NO_FATAL_FAILURES(device.ReadFd(0, one));

  EXPECT_EQ(device.lseek(n), n_s);
  EXPECT_NE(device.read(n, one), one_s);

  EXPECT_EQ(device.lseek(n - one), n_s - one_s);
  EXPECT_NE(device.read(n - one, two), two_s);

  EXPECT_EQ(device.lseek(two), two_s);
  EXPECT_NE(device.read(two, n - one), n_s - one_s);

  EXPECT_EQ(device.lseek(one), one_s);
  EXPECT_NE(device.read(one, n), n_s);
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdOutOfBounds)

void TestFdOneToMany(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.size();
  size_t one = device.block_size();

  ASSERT_NO_FATAL_FAILURES(device.WriteFd(0, n));
  ASSERT_NO_FATAL_FAILURES(device.Rebind());

  for (size_t off = 0; off < n; off += one) {
    ASSERT_NO_FATAL_FAILURES(device.ReadFd(off, one));
  }
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdOneToMany)

void TestFdManyToOne(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.size();
  size_t one = device.block_size();

  for (size_t off = 0; off < n; off += one) {
    ASSERT_NO_FATAL_FAILURES(device.WriteFd(off, one));
  }

  ASSERT_NO_FATAL_FAILURES(device.Rebind());
  ASSERT_NO_FATAL_FAILURES(device.ReadFd(0, n));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdManyToOne)

// Device::BlockWrite and Device::BlockRead tests
void TestVmoZeroLength(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));

  // Zero length is illegal for the block fifo
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, 0, 0), ZX_ERR_INVALID_ARGS);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, 0, 0), ZX_ERR_INVALID_ARGS);
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoZeroLength)

void TestVmoFirstBlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));

  ASSERT_NO_FATAL_FAILURES(device.WriteVmo(0, 1));
  ASSERT_NO_FATAL_FAILURES(device.ReadVmo(0, 1));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoFirstBlock)

void TestVmoLastBlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.block_count();

  ASSERT_NO_FATAL_FAILURES(device.WriteVmo(n - 1, 1));
  ASSERT_NO_FATAL_FAILURES(device.ReadVmo(n - 1, 1));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoLastBlock)

void TestVmoAllBlocks(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.block_count();

  ASSERT_NO_FATAL_FAILURES(device.WriteVmo(0, n));
  ASSERT_NO_FATAL_FAILURES(device.ReadVmo(0, n));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoAllBlocks)

void TestVmoOutOfBounds(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.block_count();

  ASSERT_NO_FATAL_FAILURES(device.WriteVmo(0, 1));

  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, n, 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, n - 1, 2), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, 2, n - 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, 1, n), ZX_ERR_OUT_OF_RANGE);

  ASSERT_NO_FATAL_FAILURES(device.ReadVmo(0, 1));

  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, n, 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, n - 1, 2), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, 2, n - 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, 1, n), ZX_ERR_OUT_OF_RANGE);
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoOutOfBounds)

void TestVmoOneToMany(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.block_count();

  ASSERT_NO_FATAL_FAILURES(device.WriteVmo(0, n));
  ASSERT_NO_FATAL_FAILURES(device.Rebind());
  for (size_t off = 0; off < n; ++off) {
    ASSERT_NO_FATAL_FAILURES(device.ReadVmo(off, 1));
  }
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoOneToMany)

void TestVmoManyToOne(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  size_t n = device.block_count();

  for (size_t off = 0; off < n; ++off) {
    ASSERT_NO_FATAL_FAILURES(device.WriteVmo(off, 1));
  }

  ASSERT_NO_FATAL_FAILURES(device.Rebind());
  ASSERT_NO_FATAL_FAILURES(device.ReadVmo(0, n));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoManyToOne)

// Disabled due to flakiness (see fxbug.dev/31974).
void DISABLED_TestVmoStall(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, fvm));
  auto zxcrypt = device.zxcrypt_channel();

  // The device can have up to 4 * max_transfer_size bytes in flight before it begins queuing them
  // internally.
  fuchsia_hardware_block_BlockInfo zxcrypt_blk;
  zx_status_t status;
  ASSERT_EQ(fuchsia_hardware_block_BlockGetInfo(zxcrypt->get(), &status, &zxcrypt_blk), ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  size_t blks_per_req = 4;
  size_t max = Volume::kBufferSize / (device.block_size() * blks_per_req);
  size_t num = max + 1;
  fbl::AllocChecker ac;
  std::unique_ptr<block_fifo_request_t[]> requests(new (&ac) block_fifo_request_t[num]);
  ASSERT_TRUE(ac.check());
  for (size_t i = 0; i < num; ++i) {
    requests[i].opcode = (i % 2 == 0 ? BLOCKIO_WRITE : BLOCKIO_READ);
    requests[i].length = static_cast<uint32_t>(blks_per_req);
    requests[i].dev_offset = 0;
    requests[i].vmo_offset = 0;
  }

  ASSERT_NO_FATAL_FAILURES(device.SleepUntil(max, true /* defer transactions */));
  EXPECT_EQ(device.block_fifo_txn(requests.get(), num), ZX_OK);
  ASSERT_NO_FATAL_FAILURES(device.WakeUp());
}
DEFINE_EACH_DEVICE(ZxcryptTest, DISABLED_TestVmoStall)

void TestWriteAfterFvmExtend(Volume::Version version) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURES(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURES(device.Bind(version, true));
  auto zxcrypt = device.zxcrypt_channel();

  size_t n = device.size();
  ssize_t n_s = static_cast<ssize_t>(n);

  size_t one = device.block_size();
  ssize_t one_s = static_cast<ssize_t>(one);

  EXPECT_EQ(device.lseek(n), n_s);
  EXPECT_NE(device.write(n, one), one_s);

  zx_status_t status;
  fuchsia_hardware_block_volume_VolumeInfo info;
  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeQuery(zxcrypt->get(), &status, &info), ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  uint64_t offset = device.size() / info.slice_size;
  uint64_t length = 1;

  ASSERT_EQ(fuchsia_hardware_block_volume_VolumeExtend(zxcrypt->get(), offset, length, &status),
            ZX_OK);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(device.lseek(n), n_s);
  EXPECT_EQ(device.write(n, one), one_s);
}
DEFINE_EACH(ZxcryptTest, TestWriteAfterFvmExtend)

// TODO(aarongreen): Currently, we're using XTS, which provides no data integrity.  When possible,
// we should switch to an AEAD, which would allow us to detect data corruption when doing I/O.
// void TestBadData(void) {
// }

}  // namespace
}  // namespace testing
}  // namespace zxcrypt
