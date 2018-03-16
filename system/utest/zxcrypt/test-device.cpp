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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fdio/debug.h>
#include <fdio/watcher.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm.h>
#include <unittest/unittest.h>
#include <zircon/assert.h>
#include <zircon/types.h>
#include <lib/zx/time.h>
#include <zxcrypt/volume.h>

#include "test-device.h"

#define ZXDEBUG 0

namespace zxcrypt {
namespace testing {
namespace {

// Takes a given |result|, e.g. from an ioctl, and translates into a zx_status_t.
zx_status_t ToStatus(ssize_t result) {
    return result < 0 ? static_cast<zx_status_t>(result) : ZX_OK;
}

// Helper function to build error messages
char* Error(const char* fmt, ...) {
    static char err[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, sizeof(err), fmt, ap);
    va_end(ap);
    return err;
}

// Waits for the given |path| to be opened, opens it, and returns the file descriptor via |out|.
bool WaitAndOpen(char* path, fbl::unique_fd* out) {
    BEGIN_HELPER;

    // Recursively wait for parent directories to exist
    char* parent = path;
    char* sep = strrchr(path, '/');
    char* child = sep + 1;
    ASSERT_NONNULL(child);
    *sep = '\0';
    ASSERT_GT(strlen(parent), 0);
    struct stat buf;
    if (stat(parent, &buf) != 0) {
        ASSERT_TRUE(WaitAndOpen(parent, nullptr), Error("failed to open %s", parent));
    }
    ASSERT_EQ(wait_for_driver_bind(parent, child), 0,
              Error("failed while waiting to bind %s to %s", child, parent));
    *sep = '/';
    fbl::unique_fd fd(open(path, O_RDWR));
    ASSERT_TRUE(fd, Error("failed to open %s", path));
    if (out) {
        out->swap(fd);
    }

    END_HELPER;
}

// Binds a given |driver| to a |parent| device, waits for the |child| device to show up in the
// device tree, opens it, and returns the file descriptor via |out|.
bool BindAndOpen(const fbl::unique_fd& parent, const char* child, const char* driver,
                 fbl::unique_fd* out) {
    BEGIN_HELPER;
    char path[PATH_MAX];
    ASSERT_OK(ToStatus(ioctl_device_bind(parent.get(), driver, strlen(driver))));
    ASSERT_OK(ToStatus(ioctl_device_get_topo_path(parent.get(), path, sizeof(path))));
    ASSERT_GE(snprintf(path, sizeof(path), "%s/%s", path, child), 0);
    ASSERT_TRUE(WaitAndOpen(path, out), Error("failed to open %s", path));
    END_HELPER;
}

} // namespace

TestDevice::TestDevice() : block_count_(0), block_size_(0), client_(nullptr) {
    memset(ramdisk_path_, 0, sizeof(ramdisk_path_));
    memset(fvm_part_path_, 0, sizeof(fvm_part_path_));
    memset(&req_, 0, sizeof(req_));
}

TestDevice::~TestDevice() {
    Disconnect();
    ramdisk_.reset();
    if (strlen(ramdisk_path_) != 0) {
        destroy_ramdisk(ramdisk_path_);
    }
}

bool TestDevice::Create(size_t device_size, size_t block_size, bool fvm) {
    BEGIN_HELPER;

    ASSERT_LT(device_size, SSIZE_MAX);
    if (fvm) {
        ASSERT_TRUE(CreateFvmPart(device_size, block_size));
    } else {
        ASSERT_TRUE(CreateRamdisk(device_size, block_size));
    }

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

bool TestDevice::Bind(Volume::Version version, bool fvm) {
    BEGIN_HELPER;
    ASSERT_TRUE(Create(kDeviceSize, kBlockSize, fvm));
    ASSERT_OK(Volume::Create(parent(), key_));
    ASSERT_TRUE(Connect());
    END_HELPER;
}

bool TestDevice::Rebind() {
    BEGIN_HELPER;
    ASSERT_TRUE(Disconnect());

    ASSERT_OK(ToStatus(ioctl_block_rr_part(ramdisk_.get())));
    zxcrypt_.reset();
    fvm_part_.reset();
    ramdisk_.reset();
    ASSERT_TRUE(WaitAndOpen(ramdisk_path_, &ramdisk_), Error("failed to open %s", ramdisk_path_));
    if (strlen(fvm_part_path_) != 0) {
        ASSERT_TRUE(WaitAndOpen(fvm_part_path_, &fvm_part_),
                    Error("failed to open %s", fvm_part_path_));
    }

    ASSERT_TRUE(Connect());
    END_HELPER;
}

bool TestDevice::ReadFd(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    ASSERT_OK(ToStatus(lseek(off)));
    ASSERT_OK(ToStatus(read(off, len)));
    ASSERT_EQ(memcmp(as_read_.get() + off, to_write_.get() + off, len), 0);
    END_HELPER;
}

bool TestDevice::WriteFd(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    ASSERT_OK(ToStatus(lseek(off)));
    ASSERT_OK(ToStatus(write(off, len)));
    END_HELPER;
}

bool TestDevice::ReadVmo(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    ASSERT_OK(block_fifo_txn(BLOCKIO_READ, off, len));
    off *= block_size_;
    len *= block_size_;
    ASSERT_OK(vmo_read(off, len));
    ASSERT_EQ(memcmp(as_read_.get() + off, to_write_.get() + off, len), 0);
    END_HELPER;
}

bool TestDevice::WriteVmo(zx_off_t off, size_t len) {
    BEGIN_HELPER;
    ASSERT_OK(vmo_write(off * block_size_, len * block_size_));
    ASSERT_OK(block_fifo_txn(BLOCKIO_WRITE, off, len));
    END_HELPER;
}

bool TestDevice::Corrupt(zx_off_t offset) {
    BEGIN_HELPER;
    uint8_t block[block_size_];
    zx_off_t block_off = offset % block_size_;
    offset -= block_off;

    ASSERT_OK(ToStatus(::lseek(ramdisk_.get(), offset, SEEK_SET)));
    ASSERT_OK(ToStatus(::read(ramdisk_.get(), block, block_size_)));

    int bit = rand() % 8;
    uint8_t flip = static_cast<uint8_t>(1U << bit);
    block[block_off] ^= flip;

    ASSERT_OK(ToStatus(::lseek(ramdisk_.get(), offset, SEEK_SET)));
    ASSERT_OK(ToStatus(::write(ramdisk_.get(), block, block_size_)));
    END_HELPER;
}

// Private methods

bool TestDevice::CreateRamdisk(size_t device_size, size_t block_size) {
    BEGIN_HELPER;

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

    ASSERT_EQ(create_ramdisk(block_size, count, ramdisk_path_), 0);
    ramdisk_.reset(open(ramdisk_path_, O_RDWR));
    ASSERT_TRUE(ramdisk_, Error("failed to open %s", ramdisk_path_));

    block_size_ = block_size;
    block_count_ = count;

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
    fbl::unique_fd fvm_fd;
    ASSERT_OK(fvm_init(ramdisk_.get(), FVM_BLOCK_SIZE));
    ASSERT_TRUE(BindAndOpen(ramdisk_, "fvm", "/boot/driver/fvm.so", &fvm_fd),
                Error("failed to bind and open fvm"));

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
    ASSERT_OK(ToStatus(
        ioctl_device_get_topo_path(fvm_part_.get(), fvm_part_path_, sizeof(fvm_part_path_))));

    END_HELPER;
}

bool TestDevice::Connect() {
    BEGIN_HELPER;
    ZX_DEBUG_ASSERT(!zxcrypt_);

    ASSERT_TRUE(BindAndOpen(parent(), "zxcrypt/block", "/boot/driver/zxcrypt.so", &zxcrypt_),
                Error("failed to bind and open zxcrypt"));

    block_info_t blk;
    ASSERT_OK(ToStatus(ioctl_block_get_info(zxcrypt_.get(), &blk)));
    block_size_ = blk.block_size;
    block_count_ = blk.block_count;

    zx_handle_t fifo;
    ASSERT_OK(ToStatus(ioctl_block_get_fifos(zxcrypt_.get(), &fifo)));
    ASSERT_OK(ToStatus(ioctl_block_alloc_txn(zxcrypt_.get(), &req_.txnid)));
    ASSERT_OK(block_fifo_create_client(fifo, &client_));

    // Create the vmo and get a transferable handle to give to the block server
    ASSERT_OK(zx::vmo::create(size(), 0, &vmo_));
    zx_handle_t xfer;
    ASSERT_OK(zx_handle_duplicate(vmo_.get(), ZX_RIGHT_SAME_RIGHTS, &xfer));
    ASSERT_OK(ToStatus(ioctl_block_attach_vmo(zxcrypt_.get(), &xfer, &req_.vmoid)));

    END_HELPER;
}

bool TestDevice::Disconnect() {
    BEGIN_HELPER;
    if (client_) {
        ASSERT_OK(ToStatus(ioctl_block_free_txn(zxcrypt_.get(), &req_.txnid)));
        memset(&req_, 0, sizeof(req_));
        block_fifo_release_client(client_);
        client_ = nullptr;
    }
    zxcrypt_.reset();
    block_size_ = 0;
    block_count_ = 0;
    vmo_.reset();
    END_HELPER;
}

} // namespace testing
} // namespace zxcrypt
