// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <fbl/unique_fd.h>
#include <fvm/fvm.h>
#include <unittest/unittest.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <zxcrypt/volume.h>

#include "test-device.h"

namespace zxcrypt {
namespace testing {
namespace {

// See test-device.h; the following macros allow reusing tests for each of the supported versions.
#define EACH_PARAM(OP, Test) OP(Test, Volume, AES256_XTS_SHA256)

bool TestBind(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.GenerateKey(version));
    ASSERT_OK(device.Create(kDeviceSize, kBlockSize, fvm));
    ASSERT_OK(Volume::Create(device.parent(), device.key()));

    EXPECT_OK(device.BindZxcrypt());

    END_TEST;
}
DEFINE_EACH_DEVICE(TestBind);

// TODO(aarongreen): When ZX-1130 is resolved, add tests that check zxcrypt_rekey and zxcrypt_shred.

// Device::DdkGetSize tests
bool TestDdkGetSize(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    fbl::unique_fd parent = device.parent();
    fbl::unique_fd zxcrypt = device.zxcrypt();

    struct stat parent_buf, zxcrypt_buf;
    ASSERT_EQ(fstat(parent.get(), &parent_buf), 0, strerror(errno));
    ASSERT_EQ(fstat(zxcrypt.get(), &zxcrypt_buf), 0, strerror(errno));
    EXPECT_GT(zxcrypt_buf.st_size, 0);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestDdkGetSize);

// Device::DdkIoctl tests
bool TestBlockGetInfo(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    fbl::unique_fd parent = device.parent();
    fbl::unique_fd zxcrypt = device.zxcrypt();

    block_info_t parent_blk, zxcrypt_blk;
    EXPECT_EQ(ioctl_block_get_info(zxcrypt.get(), nullptr), ZX_ERR_INVALID_ARGS);
    EXPECT_GE(ioctl_block_get_info(parent.get(), &parent_blk), 0);
    EXPECT_GE(ioctl_block_get_info(zxcrypt.get(), &zxcrypt_blk), 0);

    EXPECT_EQ(parent_blk.block_size, kBlockSize);
    EXPECT_EQ(zxcrypt_blk.block_size, PAGE_SIZE);

    EXPECT_EQ(parent_blk.block_size * parent_blk.block_count, kDeviceSize);
    EXPECT_EQ(zxcrypt_blk.block_size * zxcrypt_blk.block_count, device.size());

    END_TEST;
}
DEFINE_EACH_DEVICE(TestBlockGetInfo);

bool TestBlockFvmQuery(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    fbl::unique_fd zxcrypt = device.zxcrypt();

    if (!fvm) {
        // Send FVM query to non-FVM device
        EXPECT_EQ(ioctl_block_fvm_query(zxcrypt.get(), nullptr), ZX_ERR_NOT_SUPPORTED);
    } else {
        // Get the zxcrypt info
        fvm_info_t fvm_info;
        EXPECT_EQ(ioctl_block_fvm_query(zxcrypt.get(), nullptr), ZX_ERR_INVALID_ARGS);
        EXPECT_GE(ioctl_block_fvm_query(zxcrypt.get(), &fvm_info), 0);
        EXPECT_EQ(fvm_info.slice_size, FVM_BLOCK_SIZE);
        EXPECT_EQ(fvm_info.vslice_count, VSLICE_MAX - Volume::kReservedSlices);
    }

    END_TEST;
}
DEFINE_EACH_DEVICE(TestBlockFvmQuery);

bool TestBlockFvmVSliceQuery(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    fbl::unique_fd zxcrypt = device.zxcrypt();

    query_request_t req;
    req.count = 1;
    req.vslice_start[0] = 0;
    query_response_t resp;

    if (!fvm) {
        // Send FVM ioctl to non-FVM device
        EXPECT_EQ(ioctl_block_fvm_vslice_query(zxcrypt.get(), &req, &resp), ZX_ERR_NOT_SUPPORTED);
    } else {
        // Query zxcrypt about the slices, which should omit those reserved
        req.vslice_start[0] = 0;
        EXPECT_GE(ioctl_block_fvm_vslice_query(zxcrypt.get(), &req, &resp), 0);
        EXPECT_EQ(resp.count, 1U);
        EXPECT_TRUE(resp.vslice_range[0].allocated);
        EXPECT_EQ(resp.vslice_range[0].count, device.size() / FVM_BLOCK_SIZE);
    }

    END_TEST;
}
DEFINE_EACH_DEVICE(TestBlockFvmVSliceQuery);

bool TestBlockFvmShrinkAndExtend(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    fbl::unique_fd zxcrypt = device.zxcrypt();

    extend_request_t mod;
    mod.offset = 1;
    mod.length = 1;

    if (!fvm) {
        // Send FVM ioctl to non-FVM device
        EXPECT_EQ(ioctl_block_fvm_shrink(zxcrypt.get(), &mod), ZX_ERR_NOT_SUPPORTED);
        EXPECT_EQ(ioctl_block_fvm_extend(zxcrypt.get(), &mod), ZX_ERR_NOT_SUPPORTED);
    } else {
        // Get the current size in slices
        query_request_t req;
        req.count = 1;
        req.vslice_start[0] = 0;
        query_response_t resp;
        EXPECT_GE(ioctl_block_fvm_vslice_query(zxcrypt.get(), &req, &resp), 0);
        EXPECT_EQ(resp.count, 1U);
        EXPECT_TRUE(resp.vslice_range[0].allocated);
        EXPECT_EQ(resp.vslice_range[0].count, device.size() / FVM_BLOCK_SIZE);

        // Shrink the FVM partition and make sure the change in size is reflected
        ASSERT_GE(ioctl_block_fvm_shrink(zxcrypt.get(), &mod), 0);
        EXPECT_GE(ioctl_block_fvm_vslice_query(zxcrypt.get(), &req, &resp), 0);
        EXPECT_EQ(resp.count, 1U);
        EXPECT_TRUE(resp.vslice_range[0].allocated);
        EXPECT_GE(resp.vslice_range[0].count, 1);

        // Extend the FVM partition and make sure the change in size is reflected
        ASSERT_GE(ioctl_block_fvm_extend(zxcrypt.get(), &mod), 0);
        EXPECT_GE(ioctl_block_fvm_vslice_query(zxcrypt.get(), &req, &resp), 0);
        EXPECT_EQ(resp.count, 1U);
        EXPECT_TRUE(resp.vslice_range[0].allocated);
        EXPECT_EQ(resp.vslice_range[0].count, device.size() / FVM_BLOCK_SIZE);
    }
    END_TEST;
}
DEFINE_EACH_DEVICE(TestBlockFvmShrinkAndExtend);

// Device::DdkIotxnQueue tests
bool TestFdZeroLength(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));

    EXPECT_OK(device.WriteFd(0, 0));
    EXPECT_OK(device.ReadFd(0, 0));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestFdZeroLength);

bool TestFdFirstBlock(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t one = device.block_size();

    EXPECT_OK(device.WriteFd(0, one));
    EXPECT_OK(device.ReadFd(0, one));
    EXPECT_TRUE(device.CheckMatch(0, one));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestFdFirstBlock);

bool TestFdLastBlock(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.size();
    size_t one = device.block_size();

    EXPECT_OK(device.WriteFd(n - one, one));
    EXPECT_OK(device.ReadFd(n - one, one));
    EXPECT_TRUE(device.CheckMatch(n - one, one));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestFdLastBlock);

bool TestFdAllBlocks(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.size();

    EXPECT_OK(device.WriteFd(0, n));
    EXPECT_OK(device.ReadFd(0, n));
    EXPECT_TRUE(device.CheckMatch(0, n));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestFdAllBlocks);

bool TestFdUnaligned(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t one = device.block_size();

    ASSERT_OK(device.WriteFd(0, one));

    EXPECT_ZX(device.WriteFd(one - 1, one), ZX_ERR_IO);
    EXPECT_ZX(device.WriteFd(one + 1, one), ZX_ERR_IO);
    EXPECT_ZX(device.WriteFd(one, one - 1), ZX_ERR_IO);
    EXPECT_ZX(device.WriteFd(one, one + 1), ZX_ERR_IO);

    ASSERT_OK(device.ReadFd(0, one));

    EXPECT_ZX(device.ReadFd(one - 1, one), ZX_ERR_IO);
    EXPECT_ZX(device.ReadFd(one + 1, one), ZX_ERR_IO);
    EXPECT_ZX(device.ReadFd(one, one - 1), ZX_ERR_IO);
    EXPECT_ZX(device.ReadFd(one, one + 1), ZX_ERR_IO);
    END_TEST;
}
DEFINE_EACH_DEVICE(TestFdUnaligned);

bool TestFdOutOfBounds(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.size();
    size_t one = device.block_size();
    size_t two = one + one;

    ASSERT_OK(device.WriteFd(0, one));

    EXPECT_ZX(device.WriteFd(n, one), ZX_ERR_IO);
    EXPECT_ZX(device.WriteFd(n - one, two), ZX_ERR_IO);
    EXPECT_ZX(device.WriteFd(two, n - one), ZX_ERR_IO);
    EXPECT_ZX(device.WriteFd(one, n), ZX_ERR_IO);

    ASSERT_OK(device.ReadFd(0, one));

    EXPECT_ZX(device.ReadFd(n, one), ZX_ERR_IO);
    EXPECT_ZX(device.ReadFd(n - one, two), ZX_ERR_IO);
    EXPECT_ZX(device.ReadFd(two, n - one), ZX_ERR_IO);
    EXPECT_ZX(device.ReadFd(one, n), ZX_ERR_IO);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestFdOutOfBounds);

bool TestFdOneToMany(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.size();
    size_t one = device.block_size();

    ASSERT_OK(device.WriteFd(0, n));
    ASSERT_OK(device.BindZxcrypt());

    for (size_t off = 0; off < n; off += one) {
        EXPECT_OK(device.ReadFd(off, one));
    }

    EXPECT_TRUE(device.CheckMatch(0, n));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestFdOneToMany);

bool TestFdManyToOne(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.size();
    size_t one = device.block_size();

    for (size_t off = 0; off < n; off += one) {
        EXPECT_OK(device.WriteFd(off, one));
    }

    ASSERT_OK(device.BindZxcrypt());
    EXPECT_OK(device.ReadFd(0, n));

    EXPECT_TRUE(device.CheckMatch(0, n));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestFdManyToOne);

// Device::BlockWrite and Device::BlockRead tests
bool TestVmoZeroLength(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));

    // Zero length is illegal for the block fifo
    EXPECT_ZX(device.WriteVmo(0, 0), ZX_ERR_INVALID_ARGS);
    EXPECT_ZX(device.ReadVmo(0, 0), ZX_ERR_INVALID_ARGS);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestVmoZeroLength);

bool TestVmoFirstBlock(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t one = device.block_size();

    EXPECT_OK(device.WriteVmo(0, 1));
    EXPECT_OK(device.ReadVmo(0, 1));
    EXPECT_TRUE(device.CheckMatch(0, one));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestVmoFirstBlock);

bool TestVmoLastBlock(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.block_count();
    size_t one = device.block_size();

    EXPECT_OK(device.WriteVmo(n - 1, 1));
    EXPECT_OK(device.ReadVmo(n - 1, 1));
    EXPECT_TRUE(device.CheckMatch(device.size() - one, one));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestVmoLastBlock);

bool TestVmoAllBlocks(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.block_count();

    EXPECT_OK(device.WriteVmo(0, n));
    EXPECT_OK(device.ReadVmo(0, n));
    EXPECT_TRUE(device.CheckMatch(0, device.size()));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestVmoAllBlocks);

bool TestVmoOutOfBounds(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.block_count();

    ASSERT_OK(device.WriteVmo(0, 1));
    EXPECT_ZX(device.WriteVmo(n, 1), ZX_ERR_OUT_OF_RANGE);
    EXPECT_ZX(device.WriteVmo(n - 1, 2), ZX_ERR_OUT_OF_RANGE);
    EXPECT_ZX(device.WriteVmo(2, n - 1), ZX_ERR_OUT_OF_RANGE);
    EXPECT_ZX(device.WriteVmo(1, n), ZX_ERR_OUT_OF_RANGE);

    ASSERT_OK(device.ReadVmo(0, 1));
    EXPECT_ZX(device.ReadVmo(n, 1), ZX_ERR_OUT_OF_RANGE);
    EXPECT_ZX(device.ReadVmo(n - 1, 2), ZX_ERR_OUT_OF_RANGE);
    EXPECT_ZX(device.ReadVmo(2, n - 1), ZX_ERR_OUT_OF_RANGE);
    EXPECT_ZX(device.ReadVmo(1, n), ZX_ERR_OUT_OF_RANGE);

    END_TEST;
}
DEFINE_EACH_DEVICE(TestVmoOutOfBounds);

bool TestVmoOneToMany(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.block_count();

    EXPECT_OK(device.WriteVmo(0, n));
    ASSERT_OK(device.BindZxcrypt());
    for (size_t off = 0; off < n; ++off) {
        EXPECT_OK(device.ReadVmo(off, 1));
    }
    EXPECT_TRUE(device.CheckMatch(0, device.size()));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestVmoOneToMany);

bool TestVmoManyToOne(Volume::Version version, bool fvm) {
    BEGIN_TEST;

    TestDevice device;
    ASSERT_OK(device.DefaultInit(version, fvm));
    size_t n = device.block_count();

    for (size_t off = 0; off < n; ++off) {
        EXPECT_OK(device.WriteVmo(off, 1));
    }

    ASSERT_OK(device.BindZxcrypt());
    EXPECT_OK(device.ReadVmo(0, n));
    EXPECT_TRUE(device.CheckMatch(0, device.size()));

    END_TEST;
}
DEFINE_EACH_DEVICE(TestVmoManyToOne);

// TODO(aarongreen): Currently, we're using XTS, which provides no data integrity.  When possible,
// we should switch to an AEAD, which would allow us to detect data corruption when doing I/O.
// bool TestBadData(void) {
//     BEGIN_TEST;
//     END_TEST;
// }

BEGIN_TEST_CASE(ZxcryptTest)
RUN_EACH_DEVICE(TestBind)
RUN_EACH_DEVICE(TestDdkGetSize)
RUN_EACH_DEVICE(TestBlockGetInfo)
RUN_EACH_DEVICE(TestBlockFvmQuery)
RUN_EACH_DEVICE(TestBlockFvmVSliceQuery)
RUN_EACH_DEVICE(TestBlockFvmShrinkAndExtend)
RUN_EACH_DEVICE(TestFdZeroLength)
RUN_EACH_DEVICE(TestFdFirstBlock)
RUN_EACH_DEVICE(TestFdLastBlock)
RUN_EACH_DEVICE(TestFdAllBlocks)
RUN_EACH_DEVICE(TestFdUnaligned)
RUN_EACH_DEVICE(TestFdOutOfBounds)
RUN_EACH_DEVICE(TestFdOneToMany)
RUN_EACH_DEVICE(TestFdManyToOne)
RUN_EACH_DEVICE(TestVmoZeroLength)
RUN_EACH_DEVICE(TestVmoFirstBlock)
RUN_EACH_DEVICE(TestVmoLastBlock)
RUN_EACH_DEVICE(TestVmoAllBlocks)
RUN_EACH_DEVICE(TestVmoOutOfBounds)
RUN_EACH_DEVICE(TestVmoOneToMany)
RUN_EACH_DEVICE(TestVmoManyToOne)
END_TEST_CASE(ZxcryptTest)

} // namespace
} // namespace testing
} // namespace zxcrypt
