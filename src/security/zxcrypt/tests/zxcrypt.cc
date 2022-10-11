// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <iterator>
#include <memory>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/security/zxcrypt/tests/test-device.h"
#include "src/security/zxcrypt/volume.h"

namespace zxcrypt {
namespace testing {
namespace {

// See test-device.h; the following macros allow reusing tests for each of the supported versions.
#define EACH_PARAM(OP, TestSuite, Test) OP(TestSuite, Test, Volume, AES256_XTS_SHA256)

void TestBind(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBind)

// TODO(aarongreen): When fxbug.dev/31073 is resolved, add tests that check zxcrypt_rekey and
// zxcrypt_shred.

// Device::DdkGetSize tests
void TestDdkGetSize(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
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
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  const fidl::WireResult parent_result = fidl::WireCall(device.parent_block())->GetInfo();
  ASSERT_OK(parent_result.status());
  const fidl::WireResponse parent_response = parent_result.value();
  ASSERT_OK(parent_response.status);

  const fidl::WireResult zxcrypt_result = fidl::WireCall(device.zxcrypt_block())->GetInfo();
  ASSERT_OK(zxcrypt_result.status());
  const fidl::WireResponse zxcrypt_response = zxcrypt_result.value();
  ASSERT_OK(zxcrypt_response.status);

  EXPECT_EQ(parent_response.info->block_size, zxcrypt_response.info->block_size);
  EXPECT_GE(parent_response.info->block_count,
            zxcrypt_response.info->block_count + device.reserved_blocks());
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBlockGetInfo)

void TestBlockFvmQuery(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  if (!fvm) {
    // Send FVM query to non-FVM device.
    const fidl::WireResult result = fidl::WireCall(device.zxcrypt_volume())->GetVolumeInfo();
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_STATUS(response.status, ZX_ERR_NOT_SUPPORTED);
  } else {
    // Get the zxcrypt info.
    const fidl::WireResult parent_result = fidl::WireCall(device.parent_volume())->GetVolumeInfo();
    ASSERT_OK(parent_result.status());
    const fidl::WireResponse parent_response = parent_result.value();
    ASSERT_OK(parent_response.status);

    const fidl::WireResult zxcrypt_result =
        fidl::WireCall(device.zxcrypt_volume())->GetVolumeInfo();
    ASSERT_OK(zxcrypt_result.status());
    const fidl::WireResponse zxcrypt_response = zxcrypt_result.value();
    ASSERT_OK(zxcrypt_response.status);

    EXPECT_EQ(parent_response.manager->slice_size, zxcrypt_response.manager->slice_size);
    EXPECT_EQ(parent_response.manager->slice_count,
              zxcrypt_response.manager->slice_count + device.reserved_slices());
  }
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBlockFvmQuery)

void QueryLeadingFvmSlice(const TestDevice& device, bool fvm) {
  uint64_t start_slices[] = {
      0,
  };

  const fidl::WireResult parent_result =
      fidl::WireCall(device.parent_volume())
          ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(start_slices));

  const fidl::WireResult zxcrypt_result =
      fidl::WireCall(device.zxcrypt_volume())
          ->QuerySlices(fidl::VectorView<uint64_t>::FromExternal(start_slices));

  if (fvm) {
    ASSERT_OK(parent_result.status());
    const fidl::WireResponse parent_response = parent_result.value();
    ASSERT_OK(parent_response.status);

    ASSERT_OK(zxcrypt_result.status());
    const fidl::WireResponse zxcrypt_response = zxcrypt_result.value();
    ASSERT_OK(zxcrypt_response.status);

    // Query zxcrypt about the slices, which should omit those reserved.
    ASSERT_EQ(parent_response.response_count, 1);
    EXPECT_TRUE(parent_response.response[0].allocated);

    ASSERT_EQ(zxcrypt_response.response_count, 1);
    EXPECT_TRUE(zxcrypt_response.response[0].allocated);

    EXPECT_EQ(parent_response.response[0].count,
              zxcrypt_response.response[0].count + device.reserved_slices());
  } else {
    // Non-FVM parent devices will close the connection upon receiving FVM requests.
    ASSERT_STATUS(parent_result.status(), ZX_ERR_PEER_CLOSED);

    // zxcrypt always supports the FVM protocol, but returns ERR_NOT_SUPPORTED if not
    // sitting atop an FVM driver.
    ASSERT_OK(zxcrypt_result.status());
    const fidl::WireResponse zxcrypt_response = zxcrypt_result.value();
    ASSERT_STATUS(zxcrypt_response.status, ZX_ERR_NOT_SUPPORTED);
  }
}

void TestBlockFvmVSliceQuery(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  ASSERT_NO_FATAL_FAILURE(QueryLeadingFvmSlice(device, fvm));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBlockFvmVSliceQuery)

void TestBlockFvmShrinkAndExtend(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  const uint64_t offset = 1;
  const uint64_t length = 1;

  const fidl::WireResult shrink_result =
      fidl::WireCall(device.zxcrypt_volume())->Shrink(offset, length);
  ASSERT_OK(shrink_result.status());
  const fidl::WireResponse shrink_response = shrink_result.value();

  const fidl::WireResult extend_result =
      fidl::WireCall(device.zxcrypt_volume())->Extend(offset, length);
  ASSERT_OK(extend_result.status());
  const fidl::WireResponse extend_response = extend_result.value();

  if (!fvm) {
    ASSERT_STATUS(shrink_response.status, ZX_ERR_NOT_SUPPORTED);
    ASSERT_STATUS(extend_response.status, ZX_ERR_NOT_SUPPORTED);
  } else {
    ASSERT_OK(shrink_response.status);
    ASSERT_OK(extend_response.status);
  }
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestBlockFvmShrinkAndExtend)

// Device::DdkIotxnQueue tests
void TestFdZeroLength(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  ASSERT_NO_FATAL_FAILURE(device.WriteFd(0, 0));
  ASSERT_NO_FATAL_FAILURE(device.ReadFd(0, 0));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdZeroLength)

void TestFdFirstBlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t one = device.block_size();

  ASSERT_NO_FATAL_FAILURE(device.WriteFd(0, one));
  ASSERT_NO_FATAL_FAILURE(device.ReadFd(0, one));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdFirstBlock)

void TestFdLastBlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.size();
  size_t one = device.block_size();

  ASSERT_NO_FATAL_FAILURE(device.WriteFd(n - one, one));
  ASSERT_NO_FATAL_FAILURE(device.ReadFd(n - one, one));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdLastBlock)

void TestFdAllBlocks(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.size();

  ASSERT_NO_FATAL_FAILURE(device.WriteFd(0, n));
  ASSERT_NO_FATAL_FAILURE(device.ReadFd(0, n));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdAllBlocks)

void TestFdUnaligned(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t one = device.block_size();
  ssize_t one_s = static_cast<ssize_t>(one);

  ASSERT_NO_FATAL_FAILURE(device.WriteFd(one, one));
  ASSERT_NO_FATAL_FAILURE(device.ReadFd(one, one));

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
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.size();
  ssize_t n_s = static_cast<ssize_t>(n);

  size_t one = device.block_size();
  ssize_t one_s = static_cast<ssize_t>(one);

  size_t two = one + one;
  ssize_t two_s = static_cast<ssize_t>(two);

  ASSERT_NO_FATAL_FAILURE(device.WriteFd(0, one));

  EXPECT_EQ(device.lseek(n), n_s);
  EXPECT_NE(device.write(n, one), one_s);

  EXPECT_EQ(device.lseek(n - one), n_s - one_s);
  EXPECT_NE(device.write(n - one, two), two_s);

  EXPECT_EQ(device.lseek(two), two_s);
  EXPECT_NE(device.write(two, n - one), n_s - one_s);

  EXPECT_EQ(device.lseek(one), one_s);
  EXPECT_NE(device.write(one, n), n_s);

  ASSERT_NO_FATAL_FAILURE(device.ReadFd(0, one));

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
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.size();
  size_t one = device.block_size();

  ASSERT_NO_FATAL_FAILURE(device.WriteFd(0, n));
  ASSERT_NO_FATAL_FAILURE(device.Rebind());

  for (size_t off = 0; off < n; off += one) {
    ASSERT_NO_FATAL_FAILURE(device.ReadFd(off, one));
  }
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdOneToMany)

void TestFdManyToOne(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.size();
  size_t one = device.block_size();

  for (size_t off = 0; off < n; off += one) {
    ASSERT_NO_FATAL_FAILURE(device.WriteFd(off, one));
  }

  ASSERT_NO_FATAL_FAILURE(device.Rebind());
  ASSERT_NO_FATAL_FAILURE(device.ReadFd(0, n));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestFdManyToOne)

// Device::BlockWrite and Device::BlockRead tests
void TestVmoZeroLength(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  // Zero length is illegal for the block fifo
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, 0, 0), ZX_ERR_INVALID_ARGS);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, 0, 0), ZX_ERR_INVALID_ARGS);
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoZeroLength)

void TestVmoFirstBlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  ASSERT_NO_FATAL_FAILURE(device.WriteVmo(0, 1));
  ASSERT_NO_FATAL_FAILURE(device.ReadVmo(0, 1));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoFirstBlock)

void TestVmoLastBlock(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.block_count();

  ASSERT_NO_FATAL_FAILURE(device.WriteVmo(n - 1, 1));
  ASSERT_NO_FATAL_FAILURE(device.ReadVmo(n - 1, 1));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoLastBlock)

void TestVmoAllBlocks(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.block_count();

  ASSERT_NO_FATAL_FAILURE(device.WriteVmo(0, n));
  ASSERT_NO_FATAL_FAILURE(device.ReadVmo(0, n));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoAllBlocks)

void TestVmoOutOfBounds(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.block_count();

  ASSERT_NO_FATAL_FAILURE(device.WriteVmo(0, 1));

  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, n, 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, n - 1, 2), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, 2, n - 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_WRITE, 1, n), ZX_ERR_OUT_OF_RANGE);

  ASSERT_NO_FATAL_FAILURE(device.ReadVmo(0, 1));

  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, n, 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, n - 1, 2), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, 2, n - 1), ZX_ERR_OUT_OF_RANGE);
  EXPECT_ZX(device.block_fifo_txn(BLOCKIO_READ, 1, n), ZX_ERR_OUT_OF_RANGE);
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoOutOfBounds)

void TestVmoOneToMany(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.block_count();

  ASSERT_NO_FATAL_FAILURE(device.WriteVmo(0, n));
  ASSERT_NO_FATAL_FAILURE(device.Rebind());
  for (size_t off = 0; off < n; ++off) {
    ASSERT_NO_FATAL_FAILURE(device.ReadVmo(off, 1));
  }
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoOneToMany)

void TestVmoManyToOne(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));
  size_t n = device.block_count();

  for (size_t off = 0; off < n; ++off) {
    ASSERT_NO_FATAL_FAILURE(device.WriteVmo(off, 1));
  }

  ASSERT_NO_FATAL_FAILURE(device.Rebind());
  ASSERT_NO_FATAL_FAILURE(device.ReadVmo(0, n));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestVmoManyToOne)

// Disabled due to flakiness (see fxbug.dev/31974).
void DISABLED_TestVmoStall(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  // The device can have up to 4 * max_transfer_size bytes in flight before it begins queuing them
  // internally.
  //
  // TODO(https://fxbug.dev/31974): the result of this call is unused. Why?
  const fidl::WireResult result = fidl::WireCall(device.zxcrypt_block())->GetInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);

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

  ASSERT_NO_FATAL_FAILURE(device.SleepUntil(max, true /* defer transactions */));
  EXPECT_EQ(device.block_fifo_txn(requests.get(), num), ZX_OK);
  ASSERT_NO_FATAL_FAILURE(device.WakeUp());
}
DEFINE_EACH_DEVICE(ZxcryptTest, DISABLED_TestVmoStall)

void TestWriteAfterFvmExtend(Volume::Version version) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, true));

  size_t n = device.size();
  ssize_t n_s = static_cast<ssize_t>(n);

  size_t one = device.block_size();
  ssize_t one_s = static_cast<ssize_t>(one);

  EXPECT_EQ(device.lseek(n), n_s);
  EXPECT_NE(device.write(n, one), one_s);

  const fidl::WireResult result = fidl::WireCall(device.zxcrypt_volume())->GetVolumeInfo();
  ASSERT_OK(result.status());
  const fidl::WireResponse response = result.value();
  ASSERT_OK(response.status);

  uint64_t offset = device.size() / response.manager->slice_size;
  uint64_t length = 1;

  {
    const fidl::WireResult result = fidl::WireCall(device.zxcrypt_volume())->Extend(offset, length);
    ASSERT_OK(result.status());
    const fidl::WireResponse response = result.value();
    ASSERT_OK(response.status);
  }

  EXPECT_EQ(device.lseek(n), n_s);
  EXPECT_EQ(device.write(n, one), one_s);
}
DEFINE_EACH(ZxcryptTest, TestWriteAfterFvmExtend)

void TestUnalignedVmoOffset(Volume::Version version, bool fvm) {
  TestDevice device;
  ASSERT_NO_FATAL_FAILURE(device.SetupDevmgr());
  ASSERT_NO_FATAL_FAILURE(device.Bind(version, fvm));

  block_fifo_request_t request{
      .opcode = BLOCKIO_READ,
      .length = 2,
      .vmo_offset = 1,
      .dev_offset = 0,
  };

  ASSERT_OK(device.block_fifo_txn(&request, 1));
}
DEFINE_EACH_DEVICE(ZxcryptTest, TestUnalignedVmoOffset)

// TODO(aarongreen): Currently, we're using XTS, which provides no data integrity.  When possible,
// we should switch to an AEAD, which would allow us to detect data corruption when doing I/O.
// void TestBadData(void) {
// }

}  // namespace
}  // namespace testing
}  // namespace zxcrypt
