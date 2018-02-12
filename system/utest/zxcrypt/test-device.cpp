// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fdio/debug.h>
#include <fdio/watcher.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm.h>
#include <unittest/unittest.h>
#include <zircon/types.h>
#include <zx/time.h>
#include <zxcrypt/volume.h>

#include "test-device.h"

#define ZXDEBUG 0

namespace zxcrypt {
namespace testing {
namespace {

// Request to block watcher
struct BlockDeviceRequest {
    explicit BlockDeviceRequest(char* path) : driver(path), out_fd(-1) {}
    ~BlockDeviceRequest() {}

    const char* driver;
    int out_fd;
};

// Path to zxcrypt driver
const char* kZxcryptLib = "/boot/driver/zxcrypt.so";

// Translates a topological device tree path for a block device to its driver.
char* GetBlockDriver(char* path) {
    char* block = strrchr(path, '/');
    if (!block || strcmp(block, "/block") != 0 || block == path) {
        xprintf("unable to find block driver in '%s'\n", path);
        return nullptr;
    }
    *block = 0;
    char* driver = strrchr(path, '/');
    *block = '/';
    if (!driver) {
        xprintf("unable to find block driver in '%s'\n", path);
        return nullptr;
    }
    return driver + 1;
}

// Routine to watch directory in the device tree for a new block device.
zx_status_t BlockWatcher(int dirfd, int event, const char* filename, void* cookie) {
    BlockDeviceRequest* req = static_cast<BlockDeviceRequest*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE) {
        return ZX_OK;
    }

    fbl::unique_fd devfd(openat(dirfd, filename, O_RDWR));
    if (!devfd) {
        xprintf("openat(%d, '%s', O_RDWR) failed: %s\n", dirfd, filename, strerror(errno));
        return ZX_OK;
    }

    char path[PATH_MAX] = {0};
    ssize_t res;
    if ((res = ioctl_device_get_topo_path(devfd.get(), path, PATH_MAX - 1)) < 0) {
        xprintf("ioctl_device_get_topo_path(%d, %p, %zu) failed: %s\n", devfd.get(), path,
                sizeof(path), zx_status_get_string(static_cast<zx_status_t>(res)));
        return ZX_OK;
    }
    xprintf("%s added\n", path);

    // Trim trailing /block
    const char* driver = GetBlockDriver(path);
    if (!driver) {
        return ZX_ERR_INTERNAL;
    }
    if (strcmp(driver, req->driver) != 0) {
        xprintf("found '%s', want '.../%s'\n", path, req->driver);
        return ZX_OK;
    }

    req->out_fd = devfd.release();
    return ZX_ERR_STOP;
}

zx_status_t WaitForBlockDevice(char* driver, fbl::unique_fd* out_fd) {
    zx_status_t rc;

    static const char* kBlockDevPath = "/dev/class/block";
    DIR* dir = opendir(kBlockDevPath);
    if (!dir) {
        xprintf("opendir('%s') failed: %s\n", kBlockDevPath, strerror(errno));
        return ZX_ERR_IO;
    }
    auto cleanup = fbl::MakeAutoCall([&] { closedir(dir); });

    zx_time_t deadline = zx_deadline_after(ZX_SEC(3));
    BlockDeviceRequest req(driver);
    if ((rc = fdio_watch_directory(dirfd(dir), BlockWatcher, deadline, &req)) != ZX_ERR_STOP) {
        xprintf("fdio_watch_directory(%d, %p, %" PRIu64 ", %p) failed: %s\n", dirfd(dir),
                BlockWatcher, deadline, &req, zx_status_get_string(rc));
        return rc;
    }
    out_fd->reset(req.out_fd);
    return ZX_OK;
}

} // namespace

TestDevice::TestDevice() : has_ramdisk_(false), block_size_(0) {
    Reset();
}

TestDevice::~TestDevice() {
    Reset();
}

bool TestDevice::GenerateKey(Volume::Version version) {
    BEGIN_HELPER;

// TODO(aarongreen): See ZX-1130. The code below should be enabled when that bug is fixed.
#if 0
    crypto::digest::Algorithm digest;
    switch (version) {
    case Volume::kAES256_XTS_SHA256:
        digest = crypto::digest::kSHA256;
        break;
    default:
        digest = crypto::digest::kUninitialized;
        break;
    }

    size_t digest_len;
    key_.Reset();
    if ((rc = crypto::digest::GetDigestLen(digest, &digest_len)) != ZX_OK ||
        (rc = key_.Randomize(digest_len)) != ZX_OK) {
        return rc;
    }
#else
    key_.Reset();
    ASSERT_OK(key_.InitZero(kZx1130KeyLen));
#endif

    END_HELPER;
}

bool TestDevice::Create(size_t device_size, size_t block_size, bool fvm) {
    BEGIN_HELPER;

    ASSERT_LT(device_size, SSIZE_MAX);
    if (fvm) {
        ASSERT_TRUE(CreateFvmPart(device_size, block_size));
    } else {
        ASSERT_TRUE(CreateRamdisk(device_size, block_size));
    }

    END_HELPER;
}

bool TestDevice::BindZxcrypt() {
    BEGIN_HELPER;

    if (zxcrypt_) {
        fvm_part_.reset();
        ASSERT_GE(ioctl_block_rr_part(ramdisk_.get()), 0);
        ramdisk_.reset();
        ASSERT_OK(WaitForBlockDevice(GetBlockDriver(ramdisk_path_), &ramdisk_));
        if (strlen(fvm_part_path_) != 0) {
            ASSERT_OK(WaitForBlockDevice(GetBlockDriver(fvm_part_path_), &fvm_part_));
        }
    }

    fbl::unique_fd parent = this->parent();
    ASSERT_GE(ioctl_device_bind(parent.get(), kZxcryptLib, strlen(kZxcryptLib)), 0);

    char zxcrypt_relpath[PATH_MAX];
    snprintf(zxcrypt_relpath, PATH_MAX, "zxcrypt/block");
    ASSERT_OK(WaitForBlockDevice(zxcrypt_relpath, &zxcrypt_));

    block_info_t blk;
    ASSERT_GE(ioctl_block_get_info(zxcrypt_.get(), &blk), 0);
    block_size_ = blk.block_size;
    block_count_ = blk.block_count;

    zx_handle_t fifo;
    ASSERT_GE(ioctl_block_get_fifos(zxcrypt_.get(), &fifo), 0);
    ASSERT_GE(ioctl_block_alloc_txn(zxcrypt_.get(), &req_.txnid), 0);
    ASSERT_OK(block_fifo_create_client(fifo, &client_));

    // Create the vmo and get a transferable handle to give to the block server
    ASSERT_OK(zx::vmo::create(size(), 0, &vmo_));
    zx_handle_t xfer;
    ASSERT_OK(zx_handle_duplicate(vmo_.get(), ZX_RIGHT_SAME_RIGHTS, &xfer));
    ASSERT_GE(ioctl_block_attach_vmo(zxcrypt_.get(), &xfer, &req_.vmoid), 0);

    END_HELPER;
}

bool TestDevice::DefaultInit(Volume::Version version, bool fvm) {
    BEGIN_HELPER;
    ASSERT_TRUE(GenerateKey(version));
    ASSERT_TRUE(Create(kDeviceSize, kBlockSize, fvm));
    ASSERT_OK(Volume::Create(parent(), key_));
    ASSERT_TRUE(BindZxcrypt());
    END_HELPER;
}

bool TestDevice::ReadFd(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    EXPECT_GE(lseek(off), 0);
    EXPECT_GE(read(off, len), 0);
    EXPECT_EQ(memcmp(as_read_.get() + off, to_write_.get() + off, len), 0);
    END_HELPER;
}

bool TestDevice::WriteFd(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    EXPECT_GE(lseek(off), 0);
    EXPECT_GE(write(off, len), 0);
    END_HELPER;
}

bool TestDevice::ReadVmo(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    EXPECT_OK(block_fifo_txn(BLOCKIO_READ, off, len));
    off *= block_size_;
    len *= block_size_;
    EXPECT_OK(vmo_read(off, len));
    EXPECT_EQ(memcmp(as_read_.get() + off, to_write_.get() + off, len), 0);
    END_HELPER;
}

bool TestDevice::WriteVmo(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    EXPECT_OK(vmo_write(off * block_size_, len * block_size_));
    EXPECT_OK(block_fifo_txn(BLOCKIO_WRITE, off, len));
    END_HELPER;
}

bool TestDevice::Corrupt(zx_off_t offset) {
    BEGIN_HELPER;
    uint8_t block[block_size_];
    zx_off_t block_off = offset % block_size_;
    offset -= block_off;

    ASSERT_GE(::lseek(ramdisk_.get(), offset, SEEK_SET), 0);
    ASSERT_GE(::read(ramdisk_.get(), block, block_size_), 0);

    int bit = rand() % 8;
    uint8_t flip = static_cast<uint8_t>(1U << bit);
    block[block_off] ^= flip;

    ASSERT_GE(::lseek(ramdisk_.get(), offset, SEEK_SET), 0);
    ASSERT_GE(::write(ramdisk_.get(), block, block_size_), 0);
    END_HELPER;
}

// Private methods

bool TestDevice::CreateRamdisk(size_t device_size, size_t block_size) {
    BEGIN_HELPER;

    Reset();
    auto cleanup = fbl::MakeAutoCall([&] { Reset(); });

    fbl::AllocChecker ac;
    size_t count = fbl::round_up(device_size, block_size) / block_size;
    to_write_.reset(new (&ac) uint8_t[device_size]);
    ASSERT_TRUE(ac.check());
    for (size_t i = 0; i < device_size; ++i) {
        to_write_[i] = static_cast<uint8_t>(rand());
    }

    as_read_.reset(new (&ac) uint8_t[device_size]);
    ASSERT_TRUE(ac.check());
    memset(as_read_.get(), 0, block_size);

    ASSERT_GE(create_ramdisk(block_size, count, ramdisk_path_), 0);
    has_ramdisk_ = true;

    ASSERT_OK(WaitForBlockDevice(GetBlockDriver(ramdisk_path_), &ramdisk_));
    block_size_ = block_size;
    block_count_ = count;
    cleanup.cancel();

    END_HELPER;
}

// Creates a ramdisk, formats it, and binds to it.
bool TestDevice::CreateFvmPart(size_t device_size, size_t block_size) {
    BEGIN_HELPER;

    // Calculate total size of data + metadata.
    device_size = fbl::round_up(device_size, FVM_BLOCK_SIZE);
    size_t old_meta = fvm::MetadataSize(device_size, FVM_BLOCK_SIZE);
    size_t new_meta = fvm::MetadataSize(old_meta + device_size, FVM_BLOCK_SIZE);
    while (old_meta != new_meta) {
        old_meta = new_meta;
        new_meta = fvm::MetadataSize(old_meta + device_size, FVM_BLOCK_SIZE);
    }
    ASSERT_TRUE(CreateRamdisk(device_size + (new_meta * 2), block_size));

    // Format the ramdisk as FVM and bind to it
    static const char* kFvmLib = "/boot/driver/fvm.so";
    ASSERT_OK(fvm_init(ramdisk_.get(), FVM_BLOCK_SIZE));
    ASSERT_GE(ioctl_device_bind(ramdisk_.get(), kFvmLib, strlen(kFvmLib)), 0);

    // Wait and open the target device
    ASSERT_GE(wait_for_driver_bind(ramdisk_path_, "fvm"), 0);
    char fvm_path[PATH_MAX];
    snprintf(fvm_path, PATH_MAX, "%s/fvm", ramdisk_path_);
    fbl::unique_fd fvm_fd(open(fvm_path, O_RDWR));
    ASSERT_TRUE(fvm_fd);

    // Allocate a FVM partition with the last slice unallocated.
    alloc_req_t req;
    memset(&req, 0, sizeof(alloc_req_t));
    req.slice_count = (kDeviceSize / FVM_BLOCK_SIZE) - 1;
    memcpy(req.type, kTypeGuid, GUID_LEN);
    for (uint8_t i = 0; i < GUID_LEN; ++i) {
        req.guid[i] = i;
    }
    snprintf(req.name, NAME_LEN, "data");
    fvm_part_.reset(fvm_allocate_partition(fvm_fd.get(), &req));
    ASSERT_TRUE(fvm_part_);

    // Save the topological path for rebinding
    ASSERT_GE(ioctl_device_get_topo_path(fvm_part_.get(), fvm_part_path_, sizeof(fvm_part_path_)),
              0);

    END_HELPER;
}

void TestDevice::Reset() {
    zxcrypt_.reset();
    fvm_part_.reset();
    ramdisk_.reset();
    if (has_ramdisk_) {
        destroy_ramdisk(ramdisk_path_);
    }
    memset(ramdisk_path_, 0, sizeof(ramdisk_path_));
    memset(fvm_part_path_, 0, sizeof(fvm_part_path_));
    has_ramdisk_ = false;
}

} // namespace testing
} // namespace zxcrypt
