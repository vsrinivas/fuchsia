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
#include <fdio/debug.h>
#include <fdio/watcher.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm.h>
#include <zircon/types.h>
#include <zx/time.h>
#include <zxcrypt/superblock.h>

#include "test-device.h"

#define MXDEBUG 0

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
const size_t kZxcryptLibLen = strlen(kZxcryptLib);

// Translates a topological device tree path for a block device to its driver.
char* GetBlockDriver(char* path) {
    char* block = strrchr(path, '/');
    if (!block || strcmp(block, "/block") != 0 || block == path) {
        xprintf("%s: unable to find block driver in '%s'\n", __PRETTY_FUNCTION__, path);
        return nullptr;
    }
    *block = 0;
    char* driver = strrchr(path, '/');
    *block = '/';
    if (!driver) {
        xprintf("%s: unable to find block driver in '%s'\n", __PRETTY_FUNCTION__, path);
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
        xprintf("%s: openat(%d, '%s', O_RDWR) failed: %s\n", __PRETTY_FUNCTION__, dirfd, filename,
                strerror(errno));
        return ZX_OK;
    }

    char path[PATH_MAX] = {0};
    ssize_t res;
    if ((res = ioctl_device_get_topo_path(devfd.get(), path, PATH_MAX - 1)) < 0) {
        xprintf("%s: ioctl_device_get_topo_path(%d, %p, %zu) failed: %s\n", __PRETTY_FUNCTION__,
                devfd.get(), path, sizeof(path),
                zx_status_get_string(static_cast<zx_status_t>(res)));
        return ZX_OK;
    }
    xprintf("%s: %s added\n", __PRETTY_FUNCTION__, path);

    // Trim trailing /block
    const char* driver = GetBlockDriver(path);
    if (!driver) {
        return ZX_ERR_INTERNAL;
    }
    if (strcmp(driver, req->driver) != 0) {
        xprintf("%s: found '%s', want '.../%s'\n", __PRETTY_FUNCTION__, path, req->driver);
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
        xprintf("%s: opendir('%s') failed: %s\n", __PRETTY_FUNCTION__, kBlockDevPath,
                strerror(errno));
        return ZX_ERR_IO;
    }
    auto cleanup = fbl::MakeAutoCall([&] { closedir(dir); });

    zx_time_t deadline = zx::deadline_after(ZX_SEC(3));
    BlockDeviceRequest req(driver);
    if ((rc = fdio_watch_directory(dirfd(dir), BlockWatcher, deadline, &req)) != ZX_ERR_STOP) {
        xprintf("%s: fdio_watch_directory(%d, %p, %" PRIu64 ", %p) failed: %s\n",
                __PRETTY_FUNCTION__, dirfd(dir), BlockWatcher, deadline, &req,
                zx_status_get_string(rc));
        return rc;
    }
    out_fd->reset(req.out_fd);
    return ZX_OK;
}

} // namespace

TestDevice::TestDevice() : block_size_(0) {
    Reset();
}

TestDevice::~TestDevice() {
    Reset();
}

zx_status_t TestDevice::GenerateKey(Superblock::Version version) {
    zx_status_t rc;

    // TODO(aarongreen): See ZX-1130. The code below should be enabled when that bug is fixed.
#if 0
    crypto::digest::Algorithm digest;
    switch (version) {
    case Superblock::kAES256_XTS_SHA256:
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
    if ((rc = key_.InitZero(kZx1130KeyLen)) != ZX_OK) {
        return rc;
    }
#endif

    return ZX_OK;
}

zx_status_t TestDevice::Create(size_t device_size, size_t block_size, bool fvm) {
    if (fvm) {
        return CreateFvmPart(device_size, block_size);
    } else {
        return CreateRamdisk(device_size, block_size);
    }
}

zx_status_t TestDevice::DefaultInit(Superblock::Version version, bool fvm) {
    zx_status_t rc;

    if ((rc = GenerateKey(version)) != ZX_OK ||
        (rc = Create(kDeviceSize, kBlockSize, fvm)) != ZX_OK ||
        (rc = Superblock::Create(parent(), key_)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t TestDevice::Corrupt(zx_off_t offset) {
    zx_status_t rc;

    uint8_t block[block_size_];
    zx_off_t block_off = offset % block_size_;
    offset -= block_off;
    if ((rc = Read(ramdisk_, block, offset, block_size_)) != ZX_OK) {
        return rc;
    }
    int bit = rand() % 8;
    uint8_t flip = static_cast<uint8_t>(1U << bit);
    block[block_off] ^= flip;
    if ((rc = Write(ramdisk_, block, offset, block_size_)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

// Private methods

zx_status_t TestDevice::CreateRamdisk(size_t device_size, size_t block_size) {
    zx_status_t rc;

    Reset();
    auto cleanup = fbl::MakeAutoCall([&] { Reset(); });

    fbl::AllocChecker ac;
    size_t count = fbl::round_up(device_size, block_size) / block_size;
    to_write_.reset(new (&ac) uint8_t[device_size]);
    if (!ac.check()) {
        xprintf("%s: allocation failed: %zu bytes\n", __PRETTY_FUNCTION__, device_size);
        return ZX_ERR_NO_MEMORY;
    }
    for (size_t i = 0; i < device_size; ++i) {
        to_write_[i] = static_cast<uint8_t>(rand());
    }

    as_read_.reset(new (&ac) uint8_t[device_size]);
    if (!ac.check()) {
        xprintf("%s: allocation failed: %zu bytes\n", __PRETTY_FUNCTION__, device_size);
        return ZX_ERR_NO_MEMORY;
    }
    memset(as_read_.get(), 0, block_size);

    if (create_ramdisk(block_size, count, ramdisk_path_) < 0) {
        xprintf("%s: create_ramdisk(%zu, %zu, %p) failed\n", __PRETTY_FUNCTION__, block_size, count,
                ramdisk_path_);
        return ZX_ERR_IO;
    }
    if ((rc = WaitForBlockDevice(GetBlockDriver(ramdisk_path_), &ramdisk_)) != ZX_OK) {
        return rc;
    }

    block_size_ = block_size;
    block_count_ = count;
    cleanup.cancel();
    return ZX_OK;
}

// Creates a ramdisk, formats it, and binds to it.
zx_status_t TestDevice::CreateFvmPart(size_t device_size, size_t block_size) {
    zx_status_t rc;

    // Calculate total size of data + metadata.
    device_size = fbl::round_up(device_size, FVM_BLOCK_SIZE);
    size_t old_meta = fvm::MetadataSize(device_size, FVM_BLOCK_SIZE);
    size_t new_meta = fvm::MetadataSize(old_meta + device_size, FVM_BLOCK_SIZE);
    while (old_meta != new_meta) {
        old_meta = new_meta;
        new_meta = fvm::MetadataSize(old_meta + device_size, FVM_BLOCK_SIZE);
    }
    if ((rc = CreateRamdisk(device_size + (new_meta * 2), block_size)) != ZX_OK) {
        return rc;
    }

    // Format the ramdisk as FVM and bind to it
    if ((rc = fvm_init(ramdisk_.get(), FVM_BLOCK_SIZE)) != ZX_OK) {
        xprintf("%s: fvm_init(%d, %lu) failed: %s\n", __PRETTY_FUNCTION__, ramdisk_.get(),
                FVM_BLOCK_SIZE, zx_status_get_string(rc));
        return rc;
    }

    static const char* kFvmLib = "/boot/driver/fvm.so";
    static const size_t kFvmLibLen = strlen(kFvmLib);
    ssize_t res;
    if ((res = ioctl_device_bind(ramdisk_.get(), kFvmLib, kFvmLibLen)) < 0) {
        rc = static_cast<zx_status_t>(res);
        xprintf("%s: ioctl_device_bind(%d, %s, %zu) failed: %s\n", __PRETTY_FUNCTION__,
                ramdisk_.get(), kFvmLib, kFvmLibLen, zx_status_get_string(rc));
        return rc;
    }

    // Wait and open the target device
    if (wait_for_driver_bind(ramdisk_path_, "fvm") < 0) {
        xprintf("%s: wait_for_driver_bind('%s', 'fvm') failed\n", __PRETTY_FUNCTION__,
                ramdisk_path_);
        return ZX_ERR_NOT_FOUND;
    }

    char fvm_path[PATH_MAX];
    snprintf(fvm_path, PATH_MAX, "%s/fvm", ramdisk_path_);
    fbl::unique_fd fvm_fd(open(fvm_path, O_RDWR));
    if (!fvm_fd) {
        xprintf("%s: open('%s', O_RDWR) failed: %s\n", __PRETTY_FUNCTION__, fvm_path,
                strerror(errno));
        return ZX_ERR_NOT_FOUND;
    }

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
    if (!fvm_part_) {
        xprintf("%s: fvm_allocate_partition(%d, %p) failed\n", __PRETTY_FUNCTION__, fvm_fd.get(),
                &req);
        return ZX_ERR_IO;
    }

    // Save the topological path for rebinding
    if ((res = ioctl_device_get_topo_path(fvm_part_.get(), fvm_part_path_,
                                          sizeof(fvm_part_path_))) < 0) {
        rc = static_cast<zx_status_t>(res);
        xprintf("%s: ioctl_device_get_topo_path(%d, %p, %zu) failed: %s\n", __PRETTY_FUNCTION__,
                fvm_part_.get(), fvm_part_path_, sizeof(fvm_part_path_), zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

zx_status_t TestDevice::Write(const fbl::unique_fd& fd, const uint8_t* buf, zx_off_t off,
                              size_t len) {
    if (lseek(fd.get(), off, SEEK_SET) < 0) {
        xprintf("%s: lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", __PRETTY_FUNCTION__, fd.get(),
                off, strerror(errno));
        return ZX_ERR_IO;
    }

    ssize_t actual;
    if ((actual = write(fd.get(), to_write_.get() + off, len)) < 0) {
        xprintf("%s: write(%d, %p, %zu) failed (off=%" PRIu64 "): %s\n", __PRETTY_FUNCTION__,
                fd.get(), to_write_.get(), len, off, strerror(errno));
        return ZX_ERR_IO;
    }

    if (static_cast<size_t>(actual) < len) {
        xprintf("%s: short write: have %zd, need %zu\n", __PRETTY_FUNCTION__, actual, len);
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t TestDevice::Read(const fbl::unique_fd& fd, uint8_t* buf, zx_off_t off, size_t len) {
    if (lseek(fd.get(), off, SEEK_SET) < 0) {
        xprintf("%s: lseek(%d, %" PRIu64 ", SEEK_SET) failed: %s\n", __PRETTY_FUNCTION__, fd.get(),
                off, strerror(errno));
        return ZX_ERR_IO;
    }

    ssize_t actual;
    if ((actual = read(fd.get(), as_read_.get() + off, len)) < 0) {
        xprintf("%s: read(%d, %p, %zu) failed (off=%" PRIu64 "): %s\n", __PRETTY_FUNCTION__,
                fd.get(), as_read_.get(), len, off, strerror(errno));
        return ZX_ERR_IO;
    }

    if (static_cast<size_t>(actual) < len) {
        xprintf("%s: short read: have %zd, need %zu\n", __PRETTY_FUNCTION__, actual, len);
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

void TestDevice::Reset() {
    fvm_part_.reset();
    ramdisk_.reset();
    if (strnlen(ramdisk_path_, PATH_MAX) != 0) {
        destroy_ramdisk(ramdisk_path_);
    }
    memset(ramdisk_path_, 0, sizeof(ramdisk_path_));
    memset(fvm_part_path_, 0, sizeof(fvm_part_path_));
}

} // namespace testing
} // namespace zxcrypt
