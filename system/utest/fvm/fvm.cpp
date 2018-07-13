// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <blobfs/format.h>
#include <block-client/client.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/limits.h>
#include <fbl/new.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/vmo.h>
#include <lib/memfs/memfs.h>
#include <minfs/format.h>
#include <zircon/device/block.h>
#include <zircon/device/device.h>
#include <zircon/device/ramdisk.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>

#include <unittest/unittest.h>

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) sizeof(s) / sizeof((s)[0])

namespace {

/////////////////////// Helper functions for creating FVM:

const char kTmpfsPath[] = "/fvm-tmp";
const char kMountPath[] = "/fvm-tmp/minfs_test_mountpath";

static bool use_real_disk = false;
static char test_disk_path[PATH_MAX];
static uint64_t test_block_size;
static uint64_t test_block_count;

static int StartFVMTest(uint64_t blk_size, uint64_t blk_count, uint64_t slice_size,
                        char* disk_path_out, char* fvm_driver_out) {
    int fd;
    ssize_t r;
    disk_path_out[0] = 0;
    if (!use_real_disk) {
        if (create_ramdisk(blk_size, blk_count, disk_path_out)) {
            fprintf(stderr, "fvm: Could not create ramdisk\n");
            goto fail;
        }
    } else {
        strcpy(disk_path_out, test_disk_path);
    }

    fd = open(disk_path_out, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "fvm: Could not open ramdisk\n");
        goto fail;
    }

    if (fvm_init(fd, slice_size) != ZX_OK) {
        fprintf(stderr, "fvm: Could not initialize fvm\n");
        close(fd);
        goto fail;
    }

    r = ioctl_device_bind(fd, FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB));
    close(fd);
    if (r < 0) {
        fprintf(stderr, "fvm: Error binding to fvm driver\n");
        goto fail;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/fvm", disk_path_out);
    if (wait_for_device(path, ZX_SEC(3)) != ZX_OK) {
        fprintf(stderr, "fvm: Error waiting for fvm driver to bind\n");
        goto fail;
    }

    // TODO(security): SEC-70.  This may overflow |fvm_driver_out|.
    strcpy(fvm_driver_out, path);

    return 0;

fail:
    if (!use_real_disk && disk_path_out[0]) {
        destroy_ramdisk(disk_path_out);
    }
    return -1;
}

typedef struct {
    const char* name;
    size_t number;
} partition_entry_t;

static int FVMRebind(int fvm_fd, char* ramdisk_path, const partition_entry_t* entries,
                     size_t entry_count) {
    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    if (ramdisk_fd < 0) {
        fprintf(stderr, "fvm rebind: Could not open ramdisk\n");
        return -1;
    }

    if (ioctl_block_rr_part(ramdisk_fd) != 0) {
        fprintf(stderr, "fvm rebind: Rebind hack failed\n");
        return -1;
    }

    close(fvm_fd);
    close(ramdisk_fd);

    // Wait for the ramdisk to rebind to a block driver
    if (wait_for_device(ramdisk_path, ZX_SEC(3)) != ZX_OK) {
        fprintf(stderr, "fvm rebind: Block driver did not rebind to ramdisk\n");
        return -1;
    }

    ramdisk_fd = open(ramdisk_path, O_RDWR);
    if (ramdisk_fd < 0) {
        fprintf(stderr, "fvm rebind: Could not open ramdisk\n");
        return -1;
    }

    ssize_t r = ioctl_device_bind(ramdisk_fd, FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB));
    close(ramdisk_fd);
    if (r < 0) {
        fprintf(stderr, "fvm rebind: Could not bind fvm driver\n");
        return -1;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/fvm", ramdisk_path);
    if (wait_for_device(path, ZX_SEC(3)) != ZX_OK) {
        fprintf(stderr, "fvm rebind: Error waiting for fvm driver to bind\n");
        return -1;
    }

    for (size_t i = 0; i < entry_count; i++) {
        snprintf(path, sizeof(path), "%s/fvm/%s-p-%zu/block", ramdisk_path, entries[i].name,
                 entries[i].number);
        if (wait_for_device(path, ZX_SEC(3)) != ZX_OK) {
            fprintf(stderr, "  Failed to wait for %s\n", path);
            return -1;
        }
    }

    snprintf(path, sizeof(path), "%s/fvm", ramdisk_path);
    fvm_fd = open(path, O_RDWR);
    if (fvm_fd < 0) {
        fprintf(stderr, "fvm rebind: Failed to open fvm\n");
        return -1;
    }
    return fvm_fd;
}

static int FVMCheck(const char* fvm_path, size_t expected_slice_size) {
    int fd = open(fvm_path, O_RDWR);

    if (fd < 0) {
        fprintf(stderr, "FVMCheck: Failed to open fvm driver: %d\n", fd);
        return -1;
    }

    fvm_info_t fvm_info;
    ssize_t r;
    if ((r = ioctl_block_fvm_query(fd, &fvm_info)) <= 0) {
        fprintf(stderr, "FVMCheck: Failed to query fvm: %ld\n", r);
        close(fd);
        return -1;
    }

    close(fd);

    if (expected_slice_size != fvm_info.slice_size) {
        fprintf(stderr, "Slice size %lu did not match expected: %lu\n", fvm_info.slice_size,
                expected_slice_size);
        return -1;
    }

    return 0;
}

// Unbind FVM driver and removes the backing ramdisk device.
static int EndFVMTest(const char* ramdisk_path) {
    if (!use_real_disk) {
        return destroy_ramdisk(ramdisk_path);
    } else {
        return fvm_destroy(ramdisk_path);
    }
}

/////////////////////// Helper functions, definitions

constexpr uint8_t kTestUniqueGUID[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
constexpr uint8_t kTestUniqueGUID2[] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

// Intentionally avoid aligning these GUIDs with
// the actual system GUIDs; otherwise, limited versions
// of Fuchsia may attempt to actually mount these
// partitions automatically.

#define GUID_TEST_DATA_VALUE {                      \
    0xAA, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99, \
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17  \
}

#define GUID_TEST_BLOB_VALUE {                      \
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, \
    0xAA, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99  \
}

#define GUID_TEST_SYS_VALUE {                       \
    0xEE, 0xFF, 0xBB, 0x00, 0x33, 0x44, 0x88, 0x99, \
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17  \
}

constexpr char kTestPartName1[] = "data";
constexpr uint8_t kTestPartGUIDData[] = GUID_TEST_DATA_VALUE;

constexpr char kTestPartName2[] = "blob";
constexpr uint8_t kTestPartGUIDBlob[] = GUID_TEST_BLOB_VALUE;

constexpr char kTestPartName3[] = "system";
constexpr uint8_t kTestPartGUIDSystem[] = GUID_TEST_SYS_VALUE;

class VmoBuf;

class VmoClient : public fbl::RefCounted<VmoClient> {
public:
    static bool Create(int fd, fbl::RefPtr<VmoClient>* out);
    bool CheckWrite(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len);
    bool CheckRead(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len);
    bool Txn(block_fifo_request_t* requests, size_t count) {
        BEGIN_HELPER;
        ASSERT_EQ(block_fifo_txn(client_, &requests[0], count), ZX_OK); END_HELPER;
    }

    int fd() const { return fd_; }
    groupid_t group() { return 0; }
    ~VmoClient() {
        block_fifo_release_client(client_);
    }
private:
    int fd_;
    block_info_t info_;
    fifo_client_t* client_;
};

class VmoBuf {
public:
    static bool Create(fbl::RefPtr<VmoClient> client, size_t size,
                       fbl::unique_ptr<VmoBuf>* out) {
        BEGIN_HELPER;

        fbl::AllocChecker ac;
        fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[size]);
        ASSERT_TRUE(ac.check());

        zx::vmo vmo;
        ASSERT_EQ(zx::vmo::create(size, 0, &vmo), ZX_OK);

        zx_handle_t xfer_vmo;
        ASSERT_EQ(zx_handle_duplicate(vmo.get(), ZX_RIGHT_SAME_RIGHTS,
                                      &xfer_vmo), ZX_OK);

        vmoid_t vmoid;
        ASSERT_GT(ioctl_block_attach_vmo(client->fd(), &xfer_vmo, &vmoid), 0);

        fbl::unique_ptr<VmoBuf> vb(new (&ac) VmoBuf(fbl::move(client),
                                                     fbl::move(vmo),
                                                     fbl::move(buf),
                                                     vmoid));
        ASSERT_TRUE(ac.check());
        *out = fbl::move(vb);
        END_HELPER;
    }

    ~VmoBuf() {
        if (vmo_.is_valid()) {
            block_fifo_request_t request;
            request.group = client_->group();
            request.vmoid = vmoid_;
            request.opcode = BLOCKIO_CLOSE_VMO;
            client_->Txn(&request, 1);
        }
    }

private:
    friend VmoClient;

    VmoBuf(fbl::RefPtr<VmoClient> client, zx::vmo vmo,
           fbl::unique_ptr<uint8_t[]> buf, vmoid_t vmoid) :
        client_(fbl::move(client)), vmo_(fbl::move(vmo)),
        buf_(fbl::move(buf)), vmoid_(vmoid) {}

    fbl::RefPtr<VmoClient> client_;
    zx::vmo vmo_;
    fbl::unique_ptr<uint8_t[]> buf_;
    vmoid_t vmoid_;
};

bool VmoClient::Create(int fd, fbl::RefPtr<VmoClient>* out) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::RefPtr<VmoClient> vc = fbl::AdoptRef(new (&ac) VmoClient());
    ASSERT_TRUE(ac.check());
    zx_handle_t fifo;
    ASSERT_GT(ioctl_block_get_fifos(fd, &fifo), 0, "Failed to get FIFO");
    ASSERT_GT(ioctl_block_get_info(fd, &vc->info_), 0, "Failed to get block info");
    ASSERT_EQ(block_fifo_create_client(fifo, &vc->client_), ZX_OK);
    vc->fd_ = fd;
    *out = fbl::move(vc);
    END_HELPER;
}

bool VmoClient::CheckWrite(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len) {
    BEGIN_HELPER;
    // Write to the client-side buffer
    for (size_t i = 0; i < len; i++)
        vbuf->buf_[i + buf_off] = static_cast<uint8_t>(rand());

    // Write to the registered VMO
    ASSERT_EQ(vbuf->vmo_.write(&vbuf->buf_[buf_off], buf_off, len), ZX_OK);

    // Write to the block device
    block_fifo_request_t request;
    request.group = group();
    request.vmoid = vbuf->vmoid_;
    request.opcode = BLOCKIO_WRITE;
    ASSERT_EQ(len % info_.block_size, 0);
    ASSERT_EQ(buf_off % info_.block_size, 0);
    ASSERT_EQ(dev_off % info_.block_size, 0);
    request.length = static_cast<uint32_t>(len / info_.block_size);
    request.vmo_offset = buf_off / info_.block_size;
    request.dev_offset = dev_off / info_.block_size;
    ASSERT_TRUE(Txn(&request, 1));
    END_HELPER;
}

bool VmoClient::CheckRead(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len) {
    BEGIN_HELPER;

    // Create a comparison buffer
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check());
    memset(out.get(), 0, len);

    // Read from the block device
    block_fifo_request_t request;
    request.group = group();
    request.vmoid = vbuf->vmoid_;
    request.opcode = BLOCKIO_READ;
    ASSERT_EQ(len % info_.block_size, 0);
    ASSERT_EQ(buf_off % info_.block_size, 0);
    ASSERT_EQ(dev_off % info_.block_size, 0);
    request.length = static_cast<uint32_t>(len / info_.block_size);
    request.vmo_offset = buf_off / info_.block_size;
    request.dev_offset = dev_off / info_.block_size;
    ASSERT_TRUE(Txn(&request, 1));

    // Read from the registered VMO
    ASSERT_EQ(vbuf->vmo_.read(out.get(), buf_off, len), ZX_OK);

    ASSERT_EQ(memcmp(&vbuf->buf_[buf_off], out.get(), len), 0);
    END_HELPER;
}

bool CheckWrite(int fd, size_t off, size_t len, uint8_t* buf) {
    BEGIN_HELPER;
    for (size_t i = 0; i < len; i++) {
        buf[i] = static_cast<uint8_t>(rand());
    }
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
    ASSERT_EQ(write(fd, buf, len), static_cast<ssize_t>(len));
    END_HELPER;
}

bool CheckRead(int fd, size_t off, size_t len, const uint8_t* in) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check());
    memset(out.get(), 0, len);
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
    ASSERT_EQ(read(fd, out.get(), len), static_cast<ssize_t>(len));
    ASSERT_EQ(memcmp(in, out.get(), len), 0);
    END_HELPER;
}

bool CheckWriteColor(int fd, size_t off, size_t len, uint8_t color) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check());
    memset(buf.get(), color, len);
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
    ASSERT_EQ(write(fd, buf.get(), len), static_cast<ssize_t>(len));
    END_HELPER;
}

bool CheckReadColor(int fd, size_t off, size_t len, uint8_t color) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check());
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
    ASSERT_EQ(read(fd, buf.get(), len), static_cast<ssize_t>(len));
    for (size_t i = 0; i < len; i++) {
        ASSERT_EQ(buf[i], color);
    }
    END_HELPER;
}

bool CheckWriteReadBlock(int fd, size_t block, size_t count) {
    BEGIN_HELPER;
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(fd, &info), 0);
    size_t len = info.block_size * count;
    size_t off = info.block_size * block;
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> in(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check());
    ASSERT_TRUE(CheckWrite(fd, off, len, in.get()));
    ASSERT_TRUE(CheckRead(fd, off, len, in.get()));
    END_HELPER;
}

bool CheckNoAccessBlock(int fd, size_t block, size_t count) {
    BEGIN_HELPER;
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(fd, &info), 0);
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[info.block_size * count]);
    ASSERT_TRUE(ac.check());
    size_t len = info.block_size * count;
    size_t off = info.block_size * block;
    for (size_t i = 0; i < len; i++)
        buf[i] = static_cast<uint8_t>(rand());
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
    ASSERT_EQ(write(fd, buf.get(), len), -1);
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
    ASSERT_EQ(read(fd, buf.get(), len), -1);
    END_HELPER;
}

bool CheckDeadBlock(int fd) {
    BEGIN_HELPER;
    block_info_t info;
    ASSERT_LT(ioctl_block_get_info(fd, &info), 0);
    fbl::AllocChecker ac;
    constexpr size_t kBlksize = 8192;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[kBlksize]);
    ASSERT_TRUE(ac.check());
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(write(fd, buf.get(), kBlksize), -1);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, buf.get(), kBlksize), -1);
    END_HELPER;
}

/////////////////////// Actual tests:

// Test initializing the FVM on a partition that is smaller than a slice
bool TestTooSmall(void) {
    BEGIN_TEST;

    if (use_real_disk) {
        fprintf(stderr, "Test is ramdisk-exclusive; ignoring\n");
        return true;
    }

    char ramdisk_path[PATH_MAX];
    uint64_t blk_size = 512;
    uint64_t blk_count = (1 << 15);
    ASSERT_GE(create_ramdisk(blk_size, blk_count, ramdisk_path), 0);
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    size_t slice_size = blk_size * blk_count;
    ASSERT_EQ(fvm_init(fd, slice_size), ZX_ERR_NO_SPACE);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test initializing the FVM on a large partition, with metadata size > the max transfer size
bool TestLarge(void) {
    BEGIN_TEST;

    if (use_real_disk) {
        fprintf(stderr, "Test is ramdisk-exclusive; ignoring\n");
        return true;
    }

    char ramdisk_path[PATH_MAX];
    char fvm_path[PATH_MAX];
    uint64_t blk_size = 512;
    uint64_t blk_count = 8 * (1 << 20);
    ASSERT_GE(create_ramdisk(blk_size, blk_count, ramdisk_path), 0);

    fbl::unique_fd fd(open(ramdisk_path, O_RDWR));
    ASSERT_GT(fd.get(), 0);
    size_t slice_size = 16 * (1 << 10);
    size_t metadata_size = fvm::MetadataSize(blk_size * blk_count, slice_size);

    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(fd.get(), &info), 0);
    ASSERT_LT(info.max_transfer_size, metadata_size);

    ASSERT_EQ(fvm_init(fd.get(), slice_size), ZX_OK);

    ASSERT_EQ(ioctl_device_bind(fd.get(), FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)), 0);
    fd.reset();

    snprintf(fvm_path, sizeof(fvm_path), "%s/fvm", ramdisk_path);
    ASSERT_EQ(wait_for_device(fvm_path, ZX_SEC(3)), ZX_OK);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Load and unload an empty FVM
bool TestEmpty(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating a single partition
bool TestAllocateOne(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    // Allocate one VPart
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    // Check that the name matches what we provided
    char name[FVM_NAME_LEN + 1];
    ASSERT_GE(ioctl_block_get_name(vp_fd, name, sizeof(name)), 0);
    ASSERT_EQ(memcmp(name, kTestPartName1, strlen(kTestPartName1)), 0);

    // Check that we can read from / write to it.
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));

    // Try accessing the block again after closing / re-opening it.
    ASSERT_EQ(close(vp_fd), 0);
    vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating a collection of partitions
bool TestAllocateMany(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    // Test allocation of multiple VPartitions
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int data_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(data_fd, 0);

    strcpy(request.name, kTestPartName2);
    memcpy(request.type, kTestPartGUIDBlob, GUID_LEN);
    int blob_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(blob_fd, 0);

    strcpy(request.name, kTestPartName3);
    memcpy(request.type, kTestPartGUIDSystem, GUID_LEN);
    int sys_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(sys_fd, 0);

    ASSERT_TRUE(CheckWriteReadBlock(data_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(blob_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(sys_fd, 0, 1));

    ASSERT_EQ(close(data_fd), 0);
    ASSERT_EQ(close(blob_fd), 0);
    ASSERT_EQ(close(sys_fd), 0);

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the fvm driver can cope with a sudden close during read / write
// operations.
bool TestCloseDuringAccess(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    auto bg_thread = [](void* arg) {
        int vp_fd = *reinterpret_cast<int*>(arg);
        while (true) {
            uint8_t in[8192];
            memset(in, 'a', sizeof(in));
            if (write(vp_fd, in, sizeof(in)) != static_cast<ssize_t>(sizeof(in))) {
                return 0;
            }
            uint8_t out[8192];
            memset(out, 0, sizeof(out));
            lseek(vp_fd, 0, SEEK_SET);
            if (read(vp_fd, out, sizeof(out)) != static_cast<ssize_t>(sizeof(out))) {
                return 0;
            }
            // If we DID manage to read it, then the data should be valid...
            if (memcmp(in, out, sizeof(in)) != 0) {
                return -1;
            }
        }
    };

    // Launch a background thread to read from / write to the VPartition
    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, bg_thread, &vp_fd), thrd_success);
    // Let the background thread warm up a little bit...
    usleep(10000);
    // ... and close the fd from underneath it!
    //
    // Yes, this is a little unsafe (we risk the bg thread accessing an
    // unallocated fd), but no one else in this test process should be adding
    // fds, so we won't risk anyone reusing "vp_fd" within this test case.
    ASSERT_EQ(close(vp_fd), 0);

    int res;
    ASSERT_EQ(thrd_join(thread, &res), thrd_success);
    ASSERT_EQ(res, 0, "Background thread failed");

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the fvm driver can cope with a sudden release during read / write
// operations.
bool TestReleaseDuringAccess(void) {
    BEGIN_TEST;

    if (use_real_disk) {
        fprintf(stderr, "Test is ramdisk-exclusive; ignoring\n");
        return true;
    }

    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    auto bg_thread = [](void* arg) {
        int vp_fd = *reinterpret_cast<int*>(arg);
        while (true) {
            uint8_t in[8192];
            memset(in, 'a', sizeof(in));
            if (write(vp_fd, in, sizeof(in)) != static_cast<ssize_t>(sizeof(in))) {
                return 0;
            }
            uint8_t out[8192];
            memset(out, 0, sizeof(out));
            lseek(vp_fd, 0, SEEK_SET);
            if (read(vp_fd, out, sizeof(out)) != static_cast<ssize_t>(sizeof(out))) {
                return 0;
            }
            // If we DID manage to read it, then the data should be valid...
            if (memcmp(in, out, sizeof(in)) != 0) {
                return -1;
            }
        }
    };

    // Launch a background thread to read from / write to the VPartition
    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, bg_thread, &vp_fd), thrd_success);
    // Let the background thread warm up a little bit...
    usleep(10000);
    // ... and close the entire ramdisk from underneath it!
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");

    int res;
    ASSERT_EQ(thrd_join(thread, &res), thrd_success);
    ASSERT_EQ(res, 0, "Background thread failed");

    close(vp_fd);
    close(fd);
    END_TEST;
}

bool TestDestroyDuringAccess(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0,
              "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    auto bg_thread = [](void* arg) {
        int vp_fd = *reinterpret_cast<int*>(arg);
        unsigned count = 0;
        while (true) {
            if (++count % 10000 == 0) {
                printf("Run %u\n", count);
            }
            uint8_t in[8192];
            memset(in, 'a', sizeof(in));
            if (write(vp_fd, in, sizeof(in)) != static_cast<ssize_t>(sizeof(in))) {
                return 0;
            }
            uint8_t out[8192];
            memset(out, 0, sizeof(out));
            lseek(vp_fd, 0, SEEK_SET);
            if (read(vp_fd, out, sizeof(out)) != static_cast<ssize_t>(sizeof(out))) {
                return 0;
            }
            // If we DID manage to read it, then the data should be valid...
            if (memcmp(in, out, sizeof(in)) != 0) {
                return -1;
            }
        }
    };

    // Launch a background thread to read from / write to the VPartition
    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, bg_thread, &vp_fd), thrd_success);
    // Let the background thread warm up a little bit...
    usleep(10000);
    // ... and destroy the vpartition
    ASSERT_EQ(ioctl_block_fvm_destroy_partition(vp_fd), 0);

    int res;
    ASSERT_EQ(thrd_join(thread, &res), thrd_success);
    ASSERT_EQ(res, 0, "Background thread failed");

    close(vp_fd);
    close(fd);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating additional slices to a vpartition.
bool TestVPartitionExtend(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    const size_t kDiskSize = use_real_disk ? test_block_size * test_block_count : 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;
    size_t slices_left = fvm::UsableSlicesCount(kDiskSize, slice_size);

    // Allocate one VPart
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    size_t slice_count = 1;
    request.slice_count = slice_count;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    slices_left--;

    // Confirm that the disk reports the correct number of slices
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);

    extend_request_t erequest;

    // Try re-allocating an already allocated vslice
    erequest.offset = 0;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);

    // Try again with a portion of the request which is unallocated
    erequest.length = 2;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);

    // Allocate OBSCENELY too many slices
    erequest.offset = slice_count;
    erequest.length = fbl::numeric_limits<size_t>::max();
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Allocate slices at a too-large offset
    erequest.offset = fbl::numeric_limits<size_t>::max();
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Attempt to allocate slightly too many slices
    erequest.offset = slice_count;
    erequest.length = slices_left + 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Allocate exactly the remaining number of slices
    erequest.offset = slice_count;
    erequest.length = slices_left;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0);
    slice_count += slices_left;
    slices_left = 0;

    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);

    // We can't allocate any more to this VPartition
    erequest.offset = slice_count;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // We can't allocate a new VPartition
    strcpy(request.name, kTestPartName2);
    memcpy(request.type, kTestPartGUIDBlob, GUID_LEN);
    ASSERT_LT(ioctl_block_fvm_alloc_partition(fd, &request), 0, "Couldn't allocate VPart");

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating very sparse VPartition
bool TestVPartitionExtendSparse(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    uint64_t blk_size = use_real_disk ? test_block_size : 512;
    uint64_t blk_count = use_real_disk ? test_block_size : 1 << 20;
    uint64_t slice_size = 16 * blk_size;
    ASSERT_EQ(StartFVMTest(blk_size, blk_count, slice_size, ramdisk_path,
                           fvm_driver), 0, "error mounting FVM");

    size_t slices_left = fvm::UsableSlicesCount(blk_size * blk_count, slice_size);
    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    slices_left--;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));

    // Double check that we can access a block at this vslice address
    // (this isn't always possible; for certain slice sizes, blocks may be
    // allocatable / freeable, but not addressable).
    size_t bno = (VSLICE_MAX - 1) * (slice_size / blk_size);
    ASSERT_EQ(bno / (slice_size / blk_size), (VSLICE_MAX - 1), "bno overflowed");
    ASSERT_EQ((bno * blk_size) / blk_size, bno, "block access will overflow");

    extend_request_t erequest;

    // Try allocating at a location that's slightly too large
    erequest.offset = VSLICE_MAX;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Try allocating at the largest offset
    erequest.offset = VSLICE_MAX - 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0);
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, bno, 1));

    // Try freeing beyond largest offset
    erequest.offset = VSLICE_MAX;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "Expected request failure");
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, bno, 1));

    // Try freeing at the largest offset
    erequest.offset = VSLICE_MAX - 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &erequest), 0);
    ASSERT_TRUE(CheckNoAccessBlock(vp_fd, bno, 1));

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, slice_size), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test removing slices from a VPartition.
bool TestVPartitionShrink(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    const size_t kDiskSize = use_real_disk ? test_block_size * test_block_count : 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;
    size_t slices_left = fvm::UsableSlicesCount(kDiskSize, slice_size);

    // Allocate one VPart
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    size_t slice_count = 1;
    request.slice_count = slice_count;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    slices_left--;

    // Confirm that the disk reports the correct number of slices
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, (slice_size / info.block_size) - 1, 1));
    ASSERT_TRUE(CheckNoAccessBlock(vp_fd, (slice_size / info.block_size) - 1, 2));

    extend_request_t erequest;

    // Try shrinking the 0th vslice
    erequest.offset = 0;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "Expected request failure (0th offset)");

    // Try no-op requests
    erequest.offset = 1;
    erequest.length = 0;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Zero Length request should be no-op");
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "Zero Length request should be no-op");
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);

    // Try again with a portion of the request which is unallocated
    erequest.length = 2;
    ASSERT_LT(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "Expected request failure");
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);

    // Allocate exactly the remaining number of slices
    erequest.offset = slice_count;
    erequest.length = slices_left;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0);
    slice_count += slices_left;
    slices_left = 0;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, (slice_size / info.block_size) - 1, 1));
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, (slice_size / info.block_size) - 1, 2));

    // We can't allocate any more to this VPartition
    erequest.offset = slice_count;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Try to shrink off the end (okay, since SOME of the slices are allocated)
    erequest.offset = 1;
    erequest.length = slice_count + 3;
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &erequest), 0);

    // The same request to shrink should now fail (NONE of the slices are
    // allocated)
    erequest.offset = 1;
    erequest.length = slice_count - 1;
    ASSERT_LT(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "Expected request failure");

    // ... unless we re-allocate and try again.
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0);
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &erequest), 0);

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test splitting a contiguous slice extent into multiple parts
bool TestVPartitionSplit(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    size_t disk_size = 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;
    size_t slices_left = fvm::UsableSlicesCount(disk_size, slice_size);

    // Allocate one VPart
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    size_t slice_count = 5;
    request.slice_count = slice_count;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    slices_left--;

    // Confirm that the disk reports the correct number of slices
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count);

    extend_request_t reset_erequest;
    reset_erequest.offset = 1;
    reset_erequest.length = slice_count - 1;
    extend_request_t mid_erequest;
    mid_erequest.offset = 2;
    mid_erequest.length = 1;
    extend_request_t start_erequest;
    start_erequest.offset = 1;
    start_erequest.length = 1;
    extend_request_t end_erequest;
    end_erequest.offset = 3;
    end_erequest.length = slice_count - 3;


    auto verifyExtents = [=](bool start, bool mid, bool end) {
        if (start) {
            ASSERT_TRUE(CheckWriteReadBlock(vp_fd, start_erequest.offset  * (slice_size / info.block_size), 1));
        } else {
            ASSERT_TRUE(CheckNoAccessBlock(vp_fd, start_erequest.offset  * (slice_size / info.block_size), 1));
        }
        if (mid) {
            ASSERT_TRUE(CheckWriteReadBlock(vp_fd, mid_erequest.offset  * (slice_size / info.block_size), 1));
        } else {
            ASSERT_TRUE(CheckNoAccessBlock(vp_fd, mid_erequest.offset  * (slice_size / info.block_size), 1));
        }
        if (end) {
            ASSERT_TRUE(CheckWriteReadBlock(vp_fd, end_erequest.offset  * (slice_size / info.block_size), 1));
        } else {
            ASSERT_TRUE(CheckNoAccessBlock(vp_fd, end_erequest.offset  * (slice_size / info.block_size), 1));
        }
        return true;
    };

    // We should be able to split the extent.
    ASSERT_TRUE(verifyExtents(true, true, true));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &mid_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, false, true));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &start_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, false, true));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &end_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, false, false));

    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &reset_erequest), 0);

    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &start_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, true, true));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &mid_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, false, true));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &end_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, false, false));

    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &reset_erequest), 0);

    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &end_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, true, false));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &mid_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, false, false));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &start_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, false, false));

    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &reset_erequest), 0);

    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &end_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, true, false));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &start_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, true, false));
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &mid_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, false, false));

    // We should also be able to combine extents
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &mid_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, true, false));
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &start_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, true, false));
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &end_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, true, true));

    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &reset_erequest), 0);

    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &end_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, false, true));
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &mid_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, true, true));
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &start_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, true, true));

    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &reset_erequest), 0);

    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &end_erequest), 0);
    ASSERT_TRUE(verifyExtents(false, false, true));
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &start_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, false, true));
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &mid_erequest), 0);
    ASSERT_TRUE(verifyExtents(true, true, true));

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test removing VPartitions within an FVM
bool TestVPartitionDestroy(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    // Test allocation of multiple VPartitions
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int data_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(data_fd, 0);
    strcpy(request.name, kTestPartName2);
    memcpy(request.type, kTestPartGUIDBlob, GUID_LEN);
    int blob_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(blob_fd, 0);
    strcpy(request.name, kTestPartName3);
    memcpy(request.type, kTestPartGUIDSystem, GUID_LEN);
    int sys_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(sys_fd, 0);

    // We can access all three...
    ASSERT_TRUE(CheckWriteReadBlock(data_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(blob_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(sys_fd, 0, 1));

    // But not after we destroy the blob partition.
    ASSERT_EQ(ioctl_block_fvm_destroy_partition(blob_fd), 0);
    ASSERT_TRUE(CheckWriteReadBlock(data_fd, 0, 1));
    ASSERT_TRUE(CheckDeadBlock(blob_fd));
    ASSERT_TRUE(CheckWriteReadBlock(sys_fd, 0, 1));

    // We also can't re-destroy the blob partition.
    ASSERT_LT(ioctl_block_fvm_destroy_partition(blob_fd), 0);

    // We also can't allocate slices to the destroyed blob partition.
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(blob_fd, &erequest), 0);

    // Destroy the other two VPartitions.
    ASSERT_EQ(ioctl_block_fvm_destroy_partition(data_fd), 0);
    ASSERT_TRUE(CheckDeadBlock(data_fd));
    ASSERT_TRUE(CheckDeadBlock(blob_fd));
    ASSERT_TRUE(CheckWriteReadBlock(sys_fd, 0, 1));

    ASSERT_EQ(ioctl_block_fvm_destroy_partition(sys_fd), 0);
    ASSERT_TRUE(CheckDeadBlock(data_fd));
    ASSERT_TRUE(CheckDeadBlock(blob_fd));
    ASSERT_TRUE(CheckDeadBlock(sys_fd));

    ASSERT_EQ(close(data_fd), 0);
    ASSERT_EQ(close(blob_fd), 0);
    ASSERT_EQ(close(sys_fd), 0);
    ASSERT_EQ(close(fd), 0);

    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

bool TestVPartitionQuery(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    size_t slice_count = 64;
    size_t block_count = 512;
    size_t block_size = 1 << 20;
    size_t slice_size = (block_count * block_size) / slice_count;
    ASSERT_EQ(StartFVMTest(block_count, block_size, slice_size, ramdisk_path, fvm_driver),
                           0, "error mounting FVM");
    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    // Allocate partition
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 10;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int part_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(part_fd, 0);

    // Create non-contiguous extent
    extend_request_t extend_request;
    extend_request.offset = 20;
    extend_request.length = 10;
    ASSERT_EQ(ioctl_block_fvm_extend(part_fd, &extend_request), 0);

    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);

    // Query various vslice ranges
    query_request_t query_request;
    query_request.count = 6;
    query_request.vslice_start[0] = 0;
    query_request.vslice_start[1] = 10;
    query_request.vslice_start[2] = 20;
    query_request.vslice_start[3] = 50;
    query_request.vslice_start[4] = 25;
    query_request.vslice_start[5] = 15;

    // Check response from partition query
    query_response_t query_response;
    ASSERT_EQ(ioctl_block_fvm_vslice_query(part_fd, &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_response.count, query_request.count);
    ASSERT_TRUE(query_response.vslice_range[0].allocated);
    ASSERT_EQ(query_response.vslice_range[0].count, 10);
    ASSERT_FALSE(query_response.vslice_range[1].allocated);
    ASSERT_EQ(query_response.vslice_range[1].count, 10);
    ASSERT_TRUE(query_response.vslice_range[2].allocated);
    ASSERT_EQ(query_response.vslice_range[2].count, 10);
    ASSERT_FALSE(query_response.vslice_range[3].allocated);
    ASSERT_EQ(query_response.vslice_range[3].count, fvm_info.vslice_count - 50);
    ASSERT_TRUE(query_response.vslice_range[4].allocated);
    ASSERT_EQ(query_response.vslice_range[4].count, 5);
    ASSERT_FALSE(query_response.vslice_range[5].allocated);
    ASSERT_EQ(query_response.vslice_range[5].count, 5);

    // Merge the extents!
    extend_request.offset = 10;
    extend_request.length = 10;
    ASSERT_EQ(ioctl_block_fvm_extend(part_fd, &extend_request), 0);

    // Check partition query response again after extend
    ASSERT_EQ(ioctl_block_fvm_vslice_query(part_fd, &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_response.count, query_request.count);
    ASSERT_TRUE(query_response.vslice_range[0].allocated);
    ASSERT_EQ(query_response.vslice_range[0].count, 30);
    ASSERT_TRUE(query_response.vslice_range[1].allocated);
    ASSERT_EQ(query_response.vslice_range[1].count, 20);
    ASSERT_TRUE(query_response.vslice_range[2].allocated);
    ASSERT_EQ(query_response.vslice_range[2].count, 10);
    ASSERT_FALSE(query_response.vslice_range[3].allocated);
    ASSERT_EQ(query_response.vslice_range[3].count, fvm_info.vslice_count - 50);
    ASSERT_TRUE(query_response.vslice_range[4].allocated);
    ASSERT_EQ(query_response.vslice_range[4].count, 5);
    ASSERT_TRUE(query_response.vslice_range[5].allocated);
    ASSERT_EQ(query_response.vslice_range[5].count, 15);

    query_request.vslice_start[0] = fvm_info.vslice_count + 1;
    ASSERT_EQ(ioctl_block_fvm_vslice_query(part_fd, &query_request, &query_response),
              ZX_ERR_OUT_OF_RANGE);

    // Check that request count is valid
    query_request.count = MAX_FVM_VSLICE_REQUESTS + 1;
    ASSERT_EQ(ioctl_block_fvm_vslice_query(part_fd, &query_request, &query_response),
              ZX_ERR_BUFFER_TOO_SMALL);

    ASSERT_EQ(close(part_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, slice_size), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating and accessing slices which are allocated contiguously.
bool TestSliceAccessContiguous(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);

    // This is the last 'accessible' block.
    size_t last_block = (slice_size / info.block_size) - 1;

    {
        fbl::RefPtr<VmoClient> vc;
        ASSERT_TRUE(VmoClient::Create(vp_fd, &vc));
        fbl::unique_ptr<VmoBuf> vb;
        ASSERT_TRUE(VmoBuf::Create(vc, info.block_size * 2, &vb));
        ASSERT_TRUE(vc->CheckWrite(vb.get(), 0, info.block_size * last_block, info.block_size));
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, info.block_size * last_block, info.block_size));

        // Try writing out of bounds -- check that we don't have access.
        ASSERT_TRUE(CheckNoAccessBlock(vp_fd, (slice_size / info.block_size) - 1, 2));
        ASSERT_TRUE(CheckNoAccessBlock(vp_fd, slice_size / info.block_size, 1));

        // Attempt to access the next contiguous slice
        extend_request_t erequest;
        erequest.offset = 1;
        erequest.length = 1;
        ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Couldn't extend VPartition");

        // Now we can access the next slice...
        ASSERT_TRUE(vc->CheckWrite(vb.get(), info.block_size,
                                   info.block_size * (last_block + 1), info.block_size));
        ASSERT_TRUE(vc->CheckRead(vb.get(), info.block_size,
                                  info.block_size * (last_block + 1), info.block_size));
        // ... We can still access the previous slice...
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, info.block_size * last_block,
                                  info.block_size));
        // ... And we can cross slices
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, info.block_size * last_block,
                                  info.block_size * 2));
    }

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating and accessing multiple (3+) slices at once.
bool TestSliceAccessMany(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    // The size of a slice must be carefully constructed for this test
    // so that we can hold multiple slices in memory without worrying
    // about hitting resource limits.
    const size_t kBlockSize = use_real_disk ? test_block_size : 512;
    const size_t kBlocksPerSlice = 256;
    const size_t kSliceSize = kBlocksPerSlice * kBlockSize;
    ASSERT_EQ(StartFVMTest(kBlockSize, (1 << 20), kSliceSize, ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    ASSERT_EQ(fvm_info.slice_size, kSliceSize);

    // Allocate one VPart
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_size, kBlockSize);

    {
        fbl::RefPtr<VmoClient> vc;
        ASSERT_TRUE(VmoClient::Create(vp_fd, &vc));
        fbl::unique_ptr<VmoBuf> vb;
        ASSERT_TRUE(VmoBuf::Create(vc, kSliceSize * 3, &vb));

        // Access the first slice
        ASSERT_TRUE(vc->CheckWrite(vb.get(), 0, 0, kSliceSize));
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, 0, kSliceSize));

        // Try writing out of bounds -- check that we don't have access.
        ASSERT_TRUE(CheckNoAccessBlock(vp_fd, kBlocksPerSlice - 1, 2));
        ASSERT_TRUE(CheckNoAccessBlock(vp_fd, kBlocksPerSlice, 1));

        // Attempt to access the next contiguous slices
        extend_request_t erequest;
        erequest.offset = 1;
        erequest.length = 2;
        ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Couldn't extend VPartition");

        // Now we can access the next slices...
        ASSERT_TRUE(vc->CheckWrite(vb.get(), kSliceSize, kSliceSize, 2 * kSliceSize));
        ASSERT_TRUE(vc->CheckRead(vb.get(), kSliceSize, kSliceSize, 2 * kSliceSize));
        // ... We can still access the previous slice...
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, 0, kSliceSize));
        // ... And we can cross slices for reading.
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, 0, 3 * kSliceSize));

        // Also, we can cross slices for writing.
        ASSERT_TRUE(vc->CheckWrite(vb.get(), 0, 0, 3 * kSliceSize));
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, 0, 3 * kSliceSize));

        // Additionally, we can access "parts" of slices in a multi-slice
        // operation. Here, read one block into the first slice, and read
        // up to the last block in the final slice.
        ASSERT_TRUE(vc->CheckWrite(vb.get(), 0, kBlockSize, 3 * kSliceSize - 2 * kBlockSize));
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, kBlockSize, 3 * kSliceSize - 2 * kBlockSize));
    }

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, kSliceSize), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating and accessing slices which are allocated
// virtually contiguously (they appear sequential to the client) but are
// actually noncontiguous on the FVM partition.
bool TestSliceAccessNonContiguousPhysical(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];

    // This takes 130sec on a fast desktop, target x86 non-kvm qemu.
    // On the bots for arm it times out after 200sec.
    // For now just disable the timeout. An alternative is to make it
    // a large test, but then it won't get run for CQ/CI.
    unittest_cancel_timeout();

    ASSERT_EQ(StartFVMTest(512, 1 << 20, 8lu * (1 << 20), ramdisk_path, fvm_driver), 0,
              "error mounting FVM");
    const size_t kDiskSize = use_real_disk ? test_block_size * test_block_count : 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);

    constexpr size_t kNumVParts = 3;
    typedef struct vdata {
        int fd;
        uint8_t guid[GUID_LEN];
        char name[32];
        size_t slices_used;
    } vdata_t;

    vdata_t vparts[kNumVParts] = {
        {0, GUID_TEST_DATA_VALUE, "data", request.slice_count},
        {0, GUID_TEST_BLOB_VALUE, "blob", request.slice_count},
        {0, GUID_TEST_SYS_VALUE, "sys", request.slice_count},
    };

    for (size_t i = 0; i < countof(vparts); i++) {
        strcpy(request.name, vparts[i].name);
        memcpy(request.type, vparts[i].guid, GUID_LEN);
        vparts[i].fd = fvm_allocate_partition(fd, &request);
        ASSERT_GT(vparts[i].fd, 0);
    }

    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vparts[0].fd, &info), 0);

    size_t usable_slices_per_vpart = fvm::UsableSlicesCount(kDiskSize, slice_size) / kNumVParts;
    size_t i = 0;
    while (vparts[i].slices_used < usable_slices_per_vpart) {
        int vfd = vparts[i].fd;
        // This is the last 'accessible' block.
        size_t last_block = (vparts[i].slices_used * (slice_size / info.block_size)) - 1;
        fbl::RefPtr<VmoClient> vc;
        ASSERT_TRUE(VmoClient::Create(vfd, &vc));
        fbl::unique_ptr<VmoBuf> vb;
        ASSERT_TRUE(VmoBuf::Create(vc, info.block_size * 2, &vb));

        ASSERT_TRUE(vc->CheckWrite(vb.get(), 0, info.block_size * last_block, info.block_size));
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, info.block_size * last_block, info.block_size));

        // Try writing out of bounds -- check that we don't have access.
        ASSERT_TRUE(CheckNoAccessBlock(vfd, last_block, 2));
        ASSERT_TRUE(CheckNoAccessBlock(vfd, last_block + 1, 1));

        // Attempt to access the next contiguous slice
        extend_request_t erequest;
        erequest.offset = vparts[i].slices_used;
        erequest.length = 1;
        ASSERT_EQ(ioctl_block_fvm_extend(vfd, &erequest), 0, "Couldn't extend VPartition");

        // Now we can access the next slice...
        ASSERT_TRUE(vc->CheckWrite(vb.get(), info.block_size, info.block_size *
                                   (last_block + 1), info.block_size));
        ASSERT_TRUE(vc->CheckRead(vb.get(), info.block_size, info.block_size *
                                  (last_block + 1), info.block_size));
        // ... We can still access the previous slice...
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, info.block_size * last_block,
                                  info.block_size));
        // ... And we can cross slices
        ASSERT_TRUE(vc->CheckRead(vb.get(), 0, info.block_size * last_block,
                                  info.block_size * 2));

        vparts[i].slices_used++;
        i = (i + 1) % kNumVParts;
    }

    for (size_t i = 0; i < kNumVParts; i++) {
        printf("Testing multi-slice operations on vslice %lu\n", i);

        // We need at least five slices, so we can access three "middle"
        // slices and jitter to test off-by-one errors.
        ASSERT_GE(vparts[i].slices_used, 5);

        {
            fbl::RefPtr<VmoClient> vc;
            ASSERT_TRUE(VmoClient::Create(vparts[i].fd, &vc));
            fbl::unique_ptr<VmoBuf> vb;
            ASSERT_TRUE(VmoBuf::Create(vc, slice_size * 4, &vb));

            // Try accessing 3 noncontiguous slices at once, with the
            // addition of "off by one block".
            size_t dev_off_start = slice_size - info.block_size;
            size_t dev_off_end = slice_size + info.block_size;
            size_t len_start = slice_size * 3 - info.block_size;
            size_t len_end = slice_size * 3 + info.block_size;

            // Test a variety of:
            // Starting device offsets,
            size_t bsz = info.block_size;
            for (size_t dev_off = dev_off_start; dev_off <= dev_off_end; dev_off += bsz) {
                printf("  Testing non-contiguous write/read starting at offset: %zu\n", dev_off);
                // Operation lengths,
                for (size_t len = len_start; len <= len_end; len += bsz) {
                    printf("    Testing operation of length: %zu\n", len);
                    // and starting VMO offsets
                    for (size_t vmo_off = 0; vmo_off < 3 * bsz; vmo_off += bsz) {
                        // Try writing & reading the entire section (multiple
                        // slices) at once.
                        ASSERT_TRUE(vc->CheckWrite(vb.get(), vmo_off, dev_off, len));
                        ASSERT_TRUE(vc->CheckRead(vb.get(), vmo_off, dev_off, len));

                        // Try reading the section one slice at a time.
                        // The results should be the same.
                        size_t sub_off = 0;
                        size_t sub_len = slice_size - (dev_off % slice_size);
                        while (sub_off < len) {
                            ASSERT_TRUE(vc->CheckRead(vb.get(), vmo_off + sub_off,
                                                      dev_off + sub_off, sub_len));
                            sub_off += sub_len;
                            sub_len = fbl::min(slice_size, len - sub_off);
                        }
                    }
                }
            }
        }
        ASSERT_EQ(close(vparts[i].fd), 0);
    }

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, slice_size), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating and accessing slices which are
// allocated noncontiguously from the client's perspective.
bool TestSliceAccessNonContiguousVirtual(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    const size_t kDiskSize = 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);

    constexpr size_t kNumVParts = 3;
    typedef struct vdata {
        int fd;
        uint8_t guid[GUID_LEN];
        char name[32];
        size_t slices_used;
        size_t last_slice;
    } vdata_t;

    vdata_t vparts[kNumVParts] = {
        {0, GUID_TEST_DATA_VALUE, "data", request.slice_count, request.slice_count},
        {0, GUID_TEST_BLOB_VALUE, "blob", request.slice_count, request.slice_count},
        {0, GUID_TEST_SYS_VALUE, "sys", request.slice_count, request.slice_count},
    };

    for (size_t i = 0; i < countof(vparts); i++) {
        strcpy(request.name, vparts[i].name);
        memcpy(request.type, vparts[i].guid, GUID_LEN);
        vparts[i].fd = fvm_allocate_partition(fd, &request);
        ASSERT_GT(vparts[i].fd, 0);
    }

    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vparts[0].fd, &info), 0);

    size_t usable_slices_per_vpart = fvm::UsableSlicesCount(kDiskSize, slice_size) / kNumVParts;
    size_t i = 0;
    while (vparts[i].slices_used < usable_slices_per_vpart) {
        int vfd = vparts[i].fd;
        // This is the last 'accessible' block.
        size_t last_block = (vparts[i].last_slice * (slice_size / info.block_size)) - 1;
        ASSERT_TRUE(CheckWriteReadBlock(vfd, last_block, 1));

        // Try writing out of bounds -- check that we don't have access.
        ASSERT_TRUE(CheckNoAccessBlock(vfd, last_block, 2));
        ASSERT_TRUE(CheckNoAccessBlock(vfd, last_block + 1, 1));

        // Attempt to access a non-contiguous slice
        extend_request_t erequest;
        erequest.offset = vparts[i].last_slice + 2;
        erequest.length = 1;
        ASSERT_EQ(ioctl_block_fvm_extend(vfd, &erequest), 0, "Couldn't extend VPartition");

        // We still don't have access to the next slice...
        ASSERT_TRUE(CheckNoAccessBlock(vfd, last_block, 2));
        ASSERT_TRUE(CheckNoAccessBlock(vfd, last_block + 1, 1));

        // But we have access to the slice we asked for!
        size_t requested_block = (erequest.offset * slice_size) / info.block_size;
        ASSERT_TRUE(CheckWriteReadBlock(vfd, requested_block, 1));

        vparts[i].slices_used++;
        vparts[i].last_slice = erequest.offset;
        i = (i + 1) % kNumVParts;
    }

    for (size_t i = 0; i < kNumVParts; i++) {
        ASSERT_EQ(close(vparts[i].fd), 0);
    }

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver,slice_size), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the FVM driver actually persists updates.
bool TestPersistenceSimple(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    constexpr uint64_t kBlkSize = 512;
    constexpr uint64_t kBlkCount = 1 << 20;
    constexpr uint64_t kSliceSize = 64 * (1 << 20);
    ASSERT_EQ(StartFVMTest(kBlkSize, kBlkCount, kSliceSize, ramdisk_path,
                           fvm_driver), 0, "error mounting FVM");

    constexpr uint64_t kDiskSize = kBlkSize * kBlkCount;
    size_t slices_left = fvm::UsableSlicesCount(kDiskSize, kSliceSize);
    const uint64_t kSliceCount = slices_left;

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    slices_left--;

    // Check that the name matches what we provided
    char name[FVM_NAME_LEN + 1];
    ASSERT_GE(ioctl_block_get_name(vp_fd, name, sizeof(name)), 0);
    ASSERT_EQ(memcmp(name, kTestPartName1, strlen(kTestPartName1)), 0);
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[info.block_size * 2]);
    ASSERT_TRUE(ac.check());

    // Check that we can read from / write to it
    ASSERT_TRUE(CheckWrite(vp_fd, 0, info.block_size, buf.get()));
    ASSERT_TRUE(CheckRead(vp_fd, 0, info.block_size, buf.get()));
    ASSERT_EQ(close(vp_fd), 0);

    // Check that it still exists after rebinding the driver
    const partition_entry_t entries[] = {
        {kTestPartName1, 1},
    };
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");
    ASSERT_TRUE(CheckRead(vp_fd, 0, info.block_size, buf.get()));

    // Try extending the vpartition, and checking that the extension persists.
    // This is the last 'accessible' block.
    size_t last_block = (slice_size / info.block_size) - 1;
    ASSERT_TRUE(CheckWrite(vp_fd, info.block_size * last_block, info.block_size, &buf[0]));
    ASSERT_TRUE(CheckRead(vp_fd, info.block_size * last_block, info.block_size, &buf[0]));

    // Try writing out of bounds -- check that we don't have access.
    ASSERT_TRUE(CheckNoAccessBlock(vp_fd, (slice_size / info.block_size) - 1, 2));
    ASSERT_TRUE(CheckNoAccessBlock(vp_fd, slice_size / info.block_size, 1));
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Couldn't extend VPartition");
    slices_left--;

    // Rebind the FVM driver, check the extension has succeeded.
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    // Now we can access the next slice...
    ASSERT_TRUE(CheckWrite(vp_fd, info.block_size * (last_block + 1),
                           info.block_size, &buf[info.block_size]));
    ASSERT_TRUE(CheckRead(vp_fd, info.block_size * (last_block + 1),
                          info.block_size, &buf[info.block_size]));
    // ... We can still access the previous slice...
    ASSERT_TRUE(CheckRead(vp_fd, info.block_size * last_block,
                          info.block_size, &buf[0]));
    // ... And we can cross slices
    ASSERT_TRUE(CheckRead(vp_fd, info.block_size * last_block,
                          info.block_size * 2, &buf[0]));

    // Try allocating the rest of the slices, rebinding, and ensuring
    // that the size stays updated.
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, kSliceSize * 2);
    erequest.offset = 2;
    erequest.length = slices_left;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Couldn't extend VPartition");
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, kSliceSize * kSliceCount);

    ASSERT_EQ(close(vp_fd), 0);
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");

    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, kSliceSize * kSliceCount);

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

bool CorruptMountHelper(const char* partition_path, disk_format_t disk_format,
                        const query_request_t& query_request) {
    BEGIN_HELPER;

    // Format the VPart as |disk_format|.
    ASSERT_EQ(mkfs(partition_path, disk_format, launch_stdio_sync,
                   &default_mkfs_options),
              ZX_OK);

    int vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0);

    // Check initial slice allocation.
    query_response_t query_response;
    ASSERT_EQ(ioctl_block_fvm_vslice_query(vp_fd, &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_request.count, query_response.count);

    for (unsigned i = 0; i < query_request.count; i++) {
        ASSERT_TRUE(query_response.vslice_range[i].allocated);
        ASSERT_EQ(query_response.vslice_range[i].count, 1);
    }

    // Manually shrink slices so FVM will differ from the partition.
    extend_request_t extend_request;
    extend_request.length = 1;
    extend_request.offset = query_request.vslice_start[0];
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &extend_request), 0);

    // Check slice allocation after manual grow/shrink
    ASSERT_EQ(ioctl_block_fvm_vslice_query(vp_fd, &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_FALSE(query_response.vslice_range[0].allocated);
    ASSERT_EQ(query_response.vslice_range[0].count,
              query_request.vslice_start[1] - query_request.vslice_start[0]);

    // Try to mount the VPart.
    ASSERT_NE(mount(vp_fd, kMountPath, disk_format, &default_mount_options,
                    launch_stdio_async), ZX_OK);

    vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0);

    // Grow back the slice we shrunk earlier.
    extend_request.length = 1;
    extend_request.offset = query_request.vslice_start[0];
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &extend_request), 0);

    // Verify grow was successful.
    ASSERT_EQ(ioctl_block_fvm_vslice_query(vp_fd, &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_request.count, query_response.count);
    ASSERT_TRUE(query_response.vslice_range[0].allocated);
    ASSERT_EQ(query_response.vslice_range[0].count, 1);

    // Now extend all extents by some number of additional slices.
    for (unsigned i = 0; i < query_request.count; i++) {
        extend_request_t extend_request;
        extend_request.length = query_request.count - i;
        extend_request.offset = query_request.vslice_start[i] + 1;
        ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &extend_request), 0);
    }

    // Verify that the extensions were successful.
    ASSERT_EQ(ioctl_block_fvm_vslice_query(vp_fd, &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_request.count, query_response.count);
    for (unsigned i = 0; i < query_request.count; i++) {
        ASSERT_TRUE(query_response.vslice_range[i].allocated);
        ASSERT_EQ(query_response.vslice_range[i].count, 1 + query_request.count - i);
    }

    // Try mount again.
    ASSERT_EQ(mount(vp_fd, kMountPath, disk_format, &default_mount_options,
                    launch_stdio_async), ZX_OK);
    ASSERT_EQ(umount(kMountPath), ZX_OK);

    vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0);

    // Verify that slices were fixed on mount.
    ASSERT_EQ(ioctl_block_fvm_vslice_query(vp_fd, &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_request.count, query_response.count);

    for (unsigned i = 0; i < query_request.count; i++) {
        ASSERT_TRUE(query_response.vslice_range[i].allocated);
        ASSERT_EQ(query_response.vslice_range[i].count, 1);
    }

    END_HELPER;
}

bool TestCorruptMount(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    size_t slice_size = 1 << 20;
    ASSERT_EQ(StartFVMTest(512, 1 << 20, slice_size, ramdisk_path, fvm_driver), 0,
              "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    ASSERT_EQ(slice_size, fvm_info.slice_size);

    // Allocate one VPart
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    ASSERT_EQ(close(vp_fd), 0);

    ASSERT_EQ(mkdir(kMountPath, 0666), 0);

    char partition_path[PATH_MAX];
    snprintf(partition_path, sizeof(partition_path), "%s/%s-p-1/block",
             fvm_driver, kTestPartName1);

    size_t kMinfsBlocksPerSlice = slice_size / minfs::kMinfsBlockSize;
    query_request_t query_request;
    query_request.count = 4;
    query_request.vslice_start[0] = minfs::kFVMBlockInodeBmStart / kMinfsBlocksPerSlice;
    query_request.vslice_start[1] = minfs::kFVMBlockDataBmStart / kMinfsBlocksPerSlice;
    query_request.vslice_start[2] = minfs::kFVMBlockInodeStart / kMinfsBlocksPerSlice;
    query_request.vslice_start[3] = minfs::kFVMBlockDataStart / kMinfsBlocksPerSlice;

    // Run the test for Minfs.
    ASSERT_TRUE(CorruptMountHelper(partition_path, DISK_FORMAT_MINFS, query_request));

    size_t kBlobfsBlocksPerSlice = slice_size / blobfs::kBlobfsBlockSize;
    query_request.count = 3;
    query_request.vslice_start[0] = blobfs::kFVMBlockMapStart / kBlobfsBlocksPerSlice;
    query_request.vslice_start[1] = blobfs::kFVMNodeMapStart / kBlobfsBlocksPerSlice;
    query_request.vslice_start[2] = blobfs::kFVMDataStart / kBlobfsBlocksPerSlice;

    // Run the test for Blobfs.
    ASSERT_TRUE(CorruptMountHelper(partition_path, DISK_FORMAT_BLOBFS, query_request));

    // Clean up
    ASSERT_EQ(rmdir(kMountPath), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

bool TestVPartitionUpgrade(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    constexpr uint64_t kBlkSize = 512;
    constexpr uint64_t kBlkCount = 1 << 20;
    constexpr uint64_t kSliceSize = 64 * (1 << 20);
    ASSERT_EQ(StartFVMTest(kBlkSize, kBlkCount, kSliceSize, ramdisk_path,
                           fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    // Short-hand for asking if we can open a partition.
    auto openable = [](const uint8_t* instanceGUID, const uint8_t* typeGUID) {
        int fd = open_partition(instanceGUID, typeGUID, 0, nullptr);
        if (fd < 0) {
            return false;
        }
        ASSERT_EQ(close(fd), 0);
        return true;
    };

    // Allocate two VParts, one active, and one inactive.
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.flags = fvm::kVPartFlagInactive;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    ASSERT_EQ(close(vp_fd), 0);

    request.flags = 0;
    memcpy(request.guid, kTestUniqueGUID2, GUID_LEN);
    strcpy(request.name, kTestPartName2);
    vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);
    ASSERT_EQ(close(vp_fd), 0);

    const partition_entry_t entries[] = {
        {kTestPartName2, 2},
    };
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    // We shouldn't be able to re-open the inactive partition...
    ASSERT_FALSE(openable(kTestUniqueGUID, kTestPartGUIDData));
    // ... but we SHOULD be able to re-open the active partition.
    ASSERT_TRUE(openable(kTestUniqueGUID2, kTestPartGUIDData));

    // Try to upgrade the partition (from GUID2 --> GUID)
    request.flags = fvm::kVPartFlagInactive;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    int new_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(new_fd, 0);
    ASSERT_EQ(close(new_fd), 0);

    upgrade_req_t upgrade;
    memcpy(upgrade.old_guid, kTestUniqueGUID2, GUID_LEN);
    memcpy(upgrade.new_guid, kTestUniqueGUID, GUID_LEN);
    ASSERT_EQ(ioctl_block_fvm_upgrade(fd, &upgrade), ZX_OK);

    // After upgrading, we should be able to open both partitions
    ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
    ASSERT_TRUE(openable(kTestUniqueGUID2, kTestPartGUIDData));

    // Rebind the FVM driver, check the upgrade has succeeded.
    // The original (GUID2) should be deleted, and the new partition (GUID)
    // should exist.
    const partition_entry_t upgraded_entries[] = {
        {kTestPartName1, 1},
    };
    fd = FVMRebind(fd, ramdisk_path, upgraded_entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
    ASSERT_FALSE(openable(kTestUniqueGUID2, kTestPartGUIDData));

    // Try upgrading when the "new" version doesn't exist.
    // (It should return an error and have no noticable effect).
    memcpy(upgrade.old_guid, kTestUniqueGUID, GUID_LEN);
    memcpy(upgrade.new_guid, kTestUniqueGUID2, GUID_LEN);
    ASSERT_EQ(ioctl_block_fvm_upgrade(fd, &upgrade), ZX_ERR_NOT_FOUND);

    fd = FVMRebind(fd, ramdisk_path, upgraded_entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
    ASSERT_FALSE(openable(kTestUniqueGUID2, kTestPartGUIDData));

    // Try upgrading when the "old" version doesn't exist.
    request.flags = fvm::kVPartFlagInactive;
    memcpy(request.guid, kTestUniqueGUID2, GUID_LEN);
    strcpy(request.name, kTestPartName2);
    new_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(new_fd, 0);
    ASSERT_EQ(close(new_fd), 0);

    char fake_guid[GUID_LEN];
    memset(fake_guid, 0, GUID_LEN);
    memcpy(upgrade.old_guid, fake_guid, GUID_LEN);
    memcpy(upgrade.new_guid, kTestUniqueGUID2, GUID_LEN);
    ASSERT_EQ(ioctl_block_fvm_upgrade(fd, &upgrade), ZX_OK);

    const partition_entry_t upgraded_entries_both[] = {
        {kTestPartName1, 1},
        {kTestPartName2, 2},
    };
    fd = FVMRebind(fd, ramdisk_path, upgraded_entries_both, 2);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    // We should be able to open both partitions again.
    ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
    ASSERT_TRUE(openable(kTestUniqueGUID2, kTestPartGUIDData));

    // Destroy and reallocate the first partition as inactive.
    vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0);
    ASSERT_EQ(ioctl_block_fvm_destroy_partition(vp_fd), 0);
    ASSERT_EQ(close(vp_fd), 0);
    request.flags = fvm::kVPartFlagInactive;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    new_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(new_fd, 0);
    ASSERT_EQ(close(new_fd), 0);

    // Upgrade the partition with old_guid == new_guid.
    // This should activate the partition.
    memcpy(upgrade.old_guid, kTestUniqueGUID, GUID_LEN);
    memcpy(upgrade.new_guid, kTestUniqueGUID, GUID_LEN);
    ASSERT_EQ(ioctl_block_fvm_upgrade(fd, &upgrade), ZX_OK);

    fd = FVMRebind(fd, ramdisk_path, upgraded_entries_both, 2);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    // We should be able to open both partitions again.
    ASSERT_TRUE(openable(kTestUniqueGUID, kTestPartGUIDData));
    ASSERT_TRUE(openable(kTestUniqueGUID2, kTestPartGUIDData));

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the FVM driver can mount filesystems.
bool TestMounting(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    // Format the VPart as minfs
    char partition_path[PATH_MAX];
    snprintf(partition_path, sizeof(partition_path), "%s/%s-p-1/block",
             fvm_driver, kTestPartName1);
    ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync,
                   &default_mkfs_options),
              ZX_OK);

    // Mount the VPart
    ASSERT_EQ(mkdir(kMountPath, 0666), 0);
    ASSERT_EQ(mount(vp_fd, kMountPath, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              ZX_OK);

    // Verify that the mount was successful
    int rootfd = open(kMountPath, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(rootfd, 0);
    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* out = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t r = ioctl_vfs_query_fs(rootfd, out, sizeof(buf));
    ASSERT_EQ(r, static_cast<ssize_t>(sizeof(vfs_query_info_t) + strlen("minfs")),
              "Failed to query filesystem");
    out->name[r - sizeof(vfs_query_info_t)] = '\0';
    ASSERT_EQ(strcmp("minfs", out->name), 0, "Unexpected filesystem mounted");

    // Verify that MinFS does not try to use more of the VPartition than
    // was originally allocated.
    ASSERT_LE(out->total_bytes, slice_size * request.slice_count);

    // Clean up
    ASSERT_EQ(close(rootfd), 0);
    ASSERT_EQ(umount(kMountPath), ZX_OK);
    ASSERT_EQ(rmdir(kMountPath), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that FVM-aware filesystem can be reformatted.
bool TestMkfs(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver),
              0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart.
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    // Format the VPart as minfs.
    char partition_path[PATH_MAX];
    snprintf(partition_path, sizeof(partition_path), "%s/%s-p-1/block",
             fvm_driver, kTestPartName1);
    ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync,
                   &default_mkfs_options), ZX_OK);

    // Format it as MinFS again, even though it is already formatted.
    ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync,
                   &default_mkfs_options), ZX_OK);

    // Now try reformatting as blobfs.
    ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_BLOBFS, launch_stdio_sync,
                   &default_mkfs_options), ZX_OK);

    // Demonstrate that mounting as minfs will fail, but mounting as blobfs
    // is successful.
    ASSERT_EQ(mkdir(kMountPath, 0666), 0);
    ASSERT_NE(mount(vp_fd, kMountPath, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_sync), ZX_OK);
    vp_fd = open(partition_path, O_RDWR);
    ASSERT_GE(vp_fd, 0);
    ASSERT_EQ(mount(vp_fd, kMountPath, DISK_FORMAT_BLOBFS, &default_mount_options,
                    launch_stdio_async), ZX_OK);
    ASSERT_EQ(umount(kMountPath), ZX_OK);

    // ... and reformat back to MinFS again.
    ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync,
                   &default_mkfs_options), ZX_OK);

    // Mount the VPart.
    vp_fd = open(partition_path, O_RDWR);
    ASSERT_GE(vp_fd, 0);
    ASSERT_EQ(mount(vp_fd, kMountPath, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async), ZX_OK);

    // Verify that the mount was successful.
    int rootfd = open(kMountPath, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(rootfd, 0);
    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* out = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t r = ioctl_vfs_query_fs(rootfd, out, sizeof(buf));
    ASSERT_EQ(r, static_cast<ssize_t>(sizeof(vfs_query_info_t) + strlen("minfs")),
              "Failed to query filesystem");
    out->name[r - sizeof(vfs_query_info_t)] = '\0';
    ASSERT_EQ(strcmp("minfs", out->name), 0, "Unexpected filesystem mounted");

    // Verify that MinFS does not try to use more of the VPartition than
    // was originally allocated.
    ASSERT_LE(out->total_bytes, slice_size * request.slice_count);

    // Clean up.
    ASSERT_EQ(close(rootfd), 0);
    ASSERT_EQ(umount(kMountPath), ZX_OK);
    ASSERT_EQ(rmdir(kMountPath), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the FVM can recover when one copy of
// metadata becomes corrupt.
bool TestCorruptionOk(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];

    size_t kDiskSize = use_real_disk ? test_block_size * test_block_count : 512 * (1 << 20);
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0,
              "error mounting FVM");

    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdisk_fd, 0);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart (writes to backup)
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    // Extend the vpart (writes to primary)
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0);
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * 2);

    // Initial slice access
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));
    // Extended slice access
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, slice_size / info.block_size, 1));

    ASSERT_EQ(close(vp_fd), 0);

    // Corrupt the (backup) metadata and rebind.
    // The 'primary' was the last one written, so it'll be used.
    off_t off = fvm::BackupStart(kDiskSize, slice_size);
    uint8_t buf[FVM_BLOCK_SIZE];
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off);
    ASSERT_EQ(read(ramdisk_fd, buf, sizeof(buf)), sizeof(buf));
    // Modify an arbitrary byte (not the magic bits; we still want it to mount!)
    buf[128]++;
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off);
    ASSERT_EQ(write(ramdisk_fd, buf, sizeof(buf)), sizeof(buf));

    const partition_entry_t entries[] = {
        {kTestPartName1, 1},
    };

    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");

    // The slice extension is still accessible.
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, slice_size / info.block_size, 1));

    // Clean up
    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(ramdisk_fd), 0);

    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

bool TestCorruptionRegression(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdisk_fd, 0);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart (writes to backup)
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    // Extend the vpart (writes to primary)
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0);
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * 2);

    // Initial slice access
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));
    // Extended slice access
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, slice_size / info.block_size, 1));

    ASSERT_EQ(close(vp_fd), 0);

    // Corrupt the (primary) metadata and rebind.
    // The 'primary' was the last one written, so the backup will be used.
    off_t off = 0;
    uint8_t buf[FVM_BLOCK_SIZE];
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off);
    ASSERT_EQ(read(ramdisk_fd, buf, sizeof(buf)), sizeof(buf));
    buf[128]++;
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off);
    ASSERT_EQ(write(ramdisk_fd, buf, sizeof(buf)), sizeof(buf));

    const partition_entry_t entries[] = {
        {kTestPartName1, 1},
    };
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");
    vp_fd = open_partition(kTestUniqueGUID, kTestPartGUIDData, 0, nullptr);
    ASSERT_GT(vp_fd, 0);

    // The slice extension is no longer accessible
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));
    ASSERT_TRUE(CheckNoAccessBlock(vp_fd, slice_size / info.block_size, 1));

    // Clean up
    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(ramdisk_fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, 64lu * (1 << 20)), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

bool TestCorruptionUnrecoverable(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    const size_t kDiskSize = use_real_disk ? test_block_size * test_block_count : 512 * (1 << 20);
    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdisk_fd, 0);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart (writes to backup)
    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(vp_fd, 0);

    // Extend the vpart (writes to primary)
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0);
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0);
    ASSERT_EQ(info.block_count * info.block_size, slice_size * 2);

    // Initial slice access
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));
    // Extended slice access
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, slice_size / info.block_size, 1));

    ASSERT_EQ(close(vp_fd), 0);

    // Corrupt both copies of the metadata.
    // The 'primary' was the last one written, so the backup will be used.
    off_t off = 0;
    uint8_t buf[FVM_BLOCK_SIZE];
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off);
    ASSERT_EQ(read(ramdisk_fd, buf, sizeof(buf)), sizeof(buf));
    buf[128]++;
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off);
    ASSERT_EQ(write(ramdisk_fd, buf, sizeof(buf)), sizeof(buf));
    off = fvm::BackupStart(kDiskSize, slice_size);
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off);
    ASSERT_EQ(read(ramdisk_fd, buf, sizeof(buf)), sizeof(buf));
    buf[128]++;
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off);
    ASSERT_EQ(write(ramdisk_fd, buf, sizeof(buf)), sizeof(buf));

    const partition_entry_t entries[] = {
        {kTestPartName1, 1},
    };
    ASSERT_LT(FVMRebind(fd, ramdisk_path, entries, 1), 0, "FVM Should have failed to rebind");

    // Clean up
    ASSERT_EQ(close(ramdisk_fd), 0);

    // FVM is no longer valid - only need to remove if using ramdisk
    if (!use_real_disk) {
        ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    } else {
        fvm_overwrite(ramdisk_path, slice_size);
    }
    END_TEST;
}

typedef struct {
    // Both in units of "slice"
    size_t start;
    size_t len;
} fvm_extent_t;

typedef struct {
    int vp_fd;
    fbl::Vector<fvm_extent_t> extents;
    thrd_t thr;
} fvm_thread_state_t;

template <size_t ThreadCount>
struct fvm_test_state_t {
    size_t block_size;
    size_t slice_size;
    size_t slices_total;
    fvm_thread_state_t thread_states[ThreadCount];

    fbl::Mutex lock;
    size_t slices_left TA_GUARDED(lock);
};

template <size_t ThreadCount>
struct thrd_args_t {
    size_t tid;
    fvm_test_state_t<ThreadCount>* st;
};

template <size_t ThreadCount>
int random_access_thread(void* arg) {
    auto ta = static_cast<thrd_args_t<ThreadCount>*>(arg);
    uint8_t color = static_cast<uint8_t>(ta->tid);
    auto st = ta->st;
    auto self = &st->thread_states[color];

    unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    unittest_printf("random_access_thread using seed: %u\n", seed);

    // Before we begin, color our first slice.
    // We'll identify our own slices by the "color", which
    // is distinct between threads.
    ASSERT_TRUE(CheckWriteColor(self->vp_fd, 0, st->slice_size, color));
    ASSERT_TRUE(CheckReadColor(self->vp_fd, 0, st->slice_size, color));

    size_t num_ops = 100;
    for (size_t i = 0; i < num_ops; ++i) {
        switch (rand_r(&seed) % 5) {
        case 0: {
            // Extend and color slice, if possible
            size_t extent_index = rand_r(&seed) % self->extents.size();
            size_t extension_length = 0;
            {
                fbl::AutoLock al(&st->lock);
                if (!st->slices_left) {
                    continue;
                }
                extension_length = fbl::min((rand_r(&seed) % st->slices_left) + 1, 5lu);
                st->slices_left -= extension_length;
            }
            extend_request_t erequest;
            erequest.offset = self->extents[extent_index].start + self->extents[extent_index].len;
            erequest.length = extension_length;
            size_t off = erequest.offset * st->slice_size;
            size_t len = extension_length * st->slice_size;
            ASSERT_TRUE(CheckNoAccessBlock(self->vp_fd, off / st->block_size,
                                           len / st->block_size));
            ASSERT_EQ(ioctl_block_fvm_extend(self->vp_fd, &erequest), 0);
            self->extents[extent_index].len += extension_length;

            ASSERT_TRUE(CheckWriteColor(self->vp_fd, off, len, color));
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            break;
        }
        case 1: {
            // Allocate a new slice, if possible
            fvm_extent_t extent;
            // Space out the starting offsets far enough that there
            // is no risk of collision between fvm extents
            extent.start = (self->extents.end() - 1)->start + st->slices_total;
            {
                fbl::AutoLock al(&st->lock);
                if (!st->slices_left) {
                    continue;
                }
                extent.len = fbl::min((rand_r(&seed) % st->slices_left) + 1, 5lu);
                st->slices_left -= extent.len;
            }
            extend_request_t erequest;
            erequest.offset = extent.start;
            erequest.length = extent.len;
            size_t off = erequest.offset * st->slice_size;
            size_t len = extent.len * st->slice_size;
            ASSERT_TRUE(CheckNoAccessBlock(self->vp_fd, off / st->block_size,
                                           len / st->block_size));
            ASSERT_EQ(ioctl_block_fvm_extend(self->vp_fd, &erequest), 0);
            ASSERT_TRUE(CheckWriteColor(self->vp_fd, off, len, color));
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            fbl::AllocChecker ac;
            self->extents.push_back(fbl::move(extent), &ac);
            ASSERT_TRUE(ac.check());
            break;
        }
        case 2: {
            // Shrink slice, if possible
            size_t extent_index = rand_r(&seed) % self->extents.size();
            if (self->extents[extent_index].len == 1) {
                continue;
            }
            size_t shrink_length = (rand_r(&seed) % (self->extents[extent_index].len - 1)) + 1;

            extend_request_t erequest;
            erequest.offset = self->extents[extent_index].start +
                              self->extents[extent_index].len - shrink_length;
            erequest.length = shrink_length;
            size_t off = self->extents[extent_index].start * st->slice_size;
            size_t len = self->extents[extent_index].len * st->slice_size;
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            ASSERT_EQ(ioctl_block_fvm_shrink(self->vp_fd, &erequest), 0);
            self->extents[extent_index].len -= shrink_length;
            len = self->extents[extent_index].len * st->slice_size;
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            {
                fbl::AutoLock al(&st->lock);
                st->slices_left += shrink_length;
            }
            break;
        }
        case 3: {
            // Split slice, if possible
            size_t extent_index = rand_r(&seed) % self->extents.size();
            if (self->extents[extent_index].len < 3) {
                continue;
            }
            size_t shrink_length = (rand_r(&seed) % (self->extents[extent_index].len - 2)) + 1;
            extend_request_t erequest;
            erequest.offset = self->extents[extent_index].start + 1;
            erequest.length = shrink_length;
            size_t off = self->extents[extent_index].start * st->slice_size;
            size_t len = self->extents[extent_index].len * st->slice_size;
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            ASSERT_EQ(ioctl_block_fvm_shrink(self->vp_fd, &erequest), 0);

            // We can read the slice before...
            off = self->extents[extent_index].start * st->slice_size;
            len = st->slice_size;
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            // ... and the slices after...
            off = (self->extents[extent_index].start + 1 + shrink_length) * st->slice_size;
            len = (self->extents[extent_index].len - shrink_length - 1) * st->slice_size;
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            // ... but not in the middle.
            off = (self->extents[extent_index].start + 1) * st->slice_size;
            len = (shrink_length) * st->slice_size;
            ASSERT_TRUE(CheckNoAccessBlock(self->vp_fd, off / st->block_size,
                                           len / st->block_size));

            // To avoid collisions between test extents, let's remove the
            // trailing extent.
            erequest.offset = self->extents[extent_index].start + 1 + shrink_length;
            erequest.length = self->extents[extent_index].len - shrink_length - 1;
            ASSERT_EQ(ioctl_block_fvm_shrink(self->vp_fd, &erequest), 0);

            self->extents[extent_index].len = 1;
            off = self->extents[extent_index].start * st->slice_size;
            len = self->extents[extent_index].len * st->slice_size;
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            {
                fbl::AutoLock al(&st->lock);
                st->slices_left += shrink_length;
            }
            break;
        }
        case 4: {
            // Deallocate a slice
            size_t extent_index = rand_r(&seed) % self->extents.size();
            if (extent_index == 0) {
                // We must keep the 0th slice
                continue;
            }
            extend_request_t erequest;
            erequest.offset = self->extents[extent_index].start;
            erequest.length = self->extents[extent_index].len;
            size_t off = self->extents[extent_index].start * st->slice_size;
            size_t len = self->extents[extent_index].len * st->slice_size;
            ASSERT_TRUE(CheckReadColor(self->vp_fd, off, len, color));
            ASSERT_EQ(ioctl_block_fvm_shrink(self->vp_fd, &erequest), 0);
            ASSERT_TRUE(CheckNoAccessBlock(self->vp_fd, off / st->block_size,
                                           len / st->block_size));
            {
                fbl::AutoLock al(&st->lock);
                st->slices_left += self->extents[extent_index].len;
            }
            for (size_t i = extent_index; i < self->extents.size() - 1; i++) {
                self->extents[i] = fbl::move(self->extents[i + 1]);
            }
            self->extents.pop_back();
            break;
        }
        }
    }
    return 0;
}

template <size_t ThreadCount, bool Persistence>
bool TestRandomOpMultithreaded(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    const size_t kBlockSize = use_real_disk ? test_block_size : 512;
    const size_t kBlockCount = use_real_disk ? test_block_count : 1 << 20;
    const size_t kBlocksPerSlice = 256;
    const size_t kSliceSize = kBlocksPerSlice * kBlockSize;
    ASSERT_EQ(StartFVMTest(kBlockSize, kBlockCount, kSliceSize, ramdisk_path,
                           fvm_driver),
              0, "error mounting FVM");

    const size_t kDiskSize = kBlockSize * kBlockCount;
    const size_t kSlicesCount = fvm::UsableSlicesCount(kDiskSize, kSliceSize);

    if (use_real_disk && kSlicesCount <= ThreadCount * 2) {
        printf("Not enough slices to distribute between threads: ignoring test\n");
        return true;
    }

    ASSERT_GT(kSlicesCount, ThreadCount * 2, "Not enough slices to distribute between threads");

    fvm_test_state_t<ThreadCount> s{};
    s.block_size = kBlockSize;
    s.slice_size = kSliceSize;
    {
        fbl::AutoLock al(&s.lock);
        s.slices_left = kSlicesCount - ThreadCount;
        s.slices_total = kSlicesCount;
    }

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    alloc_req_t request;
    memset(&request, 0, sizeof(request));
    size_t slice_count = 1;
    request.slice_count = slice_count;
    strcpy(request.name, "TestPartition");
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);

    for (size_t i = 0; i < ThreadCount; i++) {
        // Change the GUID enough to be distinct for each thread
        request.guid[0] = static_cast<uint8_t>(i);
        s.thread_states[i].vp_fd = fvm_allocate_partition(fd, &request);
        ASSERT_GT(s.thread_states[i].vp_fd, 0);
    }

    thrd_args_t<ThreadCount> ta[ThreadCount];

    // Initialize and launch all threads
    for (size_t i = 0; i < ThreadCount; i++) {
        ta[i].tid = i;
        ta[i].st = &s;

        EXPECT_EQ(s.thread_states[i].extents.size(), 0);
        fvm_extent_t extent;
        extent.start = 0;
        extent.len = 1;
        fbl::AllocChecker ac;
        s.thread_states[i].extents.push_back(fbl::move(extent), &ac);
        EXPECT_TRUE(ac.check());
        EXPECT_TRUE(CheckWriteReadBlock(s.thread_states[i].vp_fd, 0, kBlocksPerSlice));
        EXPECT_EQ(thrd_create(&s.thread_states[i].thr,
                              random_access_thread<ThreadCount>, &ta[i]),
                  thrd_success);
    }

    if (Persistence) {
        partition_entry_t entries[ThreadCount];

        // Join all threads
        for (size_t i = 0; i < ThreadCount; i++) {
            int r;
            EXPECT_EQ(thrd_join(s.thread_states[i].thr, &r), thrd_success);
            EXPECT_EQ(r, 0);
            EXPECT_EQ(close(s.thread_states[i].vp_fd), 0);
            entries[i].name = request.name;
            entries[i].number = i + 1;
        }

        // Rebind the FVM (simulating rebooting)
        fd = FVMRebind(fd, ramdisk_path, entries, fbl::count_of(entries));
        ASSERT_GT(fd, 0);

        // Re-open all partitions, re-launch the worker threads
        for (size_t i = 0; i < ThreadCount; i++) {
            request.guid[0] = static_cast<uint8_t>(i);
            int vp_fd = open_partition(request.guid, request.type, 0, nullptr);
            ASSERT_GT(vp_fd, 0);
            s.thread_states[i].vp_fd = vp_fd;
            EXPECT_EQ(thrd_create(&s.thread_states[i].thr,
                                  random_access_thread<ThreadCount>, &ta[i]),
                      thrd_success);
        }
    }

    // Join all the threads, verify their initial block is still valid, and
    // destroy them.
    for (size_t i = 0; i < ThreadCount; i++) {
        int r;
        EXPECT_EQ(thrd_join(s.thread_states[i].thr, &r), thrd_success);
        EXPECT_EQ(r, 0);
        EXPECT_TRUE(CheckWriteReadBlock(s.thread_states[i].vp_fd, 0, kBlocksPerSlice));
        EXPECT_EQ(ioctl_block_fvm_destroy_partition(s.thread_states[i].vp_fd), 0);
        EXPECT_EQ(close(s.thread_states[i].vp_fd), 0);
    }

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(FVMCheck(fvm_driver, kSliceSize), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(fvm_tests)
RUN_TEST_MEDIUM(TestTooSmall)
RUN_TEST_MEDIUM(TestLarge)
RUN_TEST_MEDIUM(TestEmpty)
RUN_TEST_MEDIUM(TestAllocateOne)
RUN_TEST_MEDIUM(TestAllocateMany)
RUN_TEST_MEDIUM(TestCloseDuringAccess)
RUN_TEST_MEDIUM(TestReleaseDuringAccess)
RUN_TEST_MEDIUM(TestDestroyDuringAccess)
RUN_TEST_MEDIUM(TestVPartitionExtend)
RUN_TEST_MEDIUM(TestVPartitionExtendSparse)
RUN_TEST_MEDIUM(TestVPartitionShrink)
RUN_TEST_MEDIUM(TestVPartitionSplit)
RUN_TEST_MEDIUM(TestVPartitionDestroy)
RUN_TEST_MEDIUM(TestVPartitionQuery)
RUN_TEST_MEDIUM(TestSliceAccessContiguous)
RUN_TEST_MEDIUM(TestSliceAccessMany)
RUN_TEST_MEDIUM(TestSliceAccessNonContiguousPhysical)
RUN_TEST_MEDIUM(TestSliceAccessNonContiguousVirtual)
RUN_TEST_MEDIUM(TestPersistenceSimple)
RUN_TEST_LARGE(TestVPartitionUpgrade)
RUN_TEST_LARGE(TestMounting)
RUN_TEST_LARGE(TestMkfs)
RUN_TEST_MEDIUM(TestCorruptionOk)
RUN_TEST_MEDIUM(TestCorruptionRegression)
RUN_TEST_MEDIUM(TestCorruptionUnrecoverable)
RUN_TEST_LARGE((TestRandomOpMultithreaded<1, /* persistent= */ false>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<3, /* persistent= */ false>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<5, /* persistent= */ false>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<10, /* persistent= */ false>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<25, /* persistent= */ false>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<1, /* persistent= */ true>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<3, /* persistent= */ true>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<5, /* persistent= */ true>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<10, /* persistent= */ true>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<25, /* persistent= */ true>))
RUN_TEST_MEDIUM(TestCorruptMount)
END_TEST_CASE(fvm_tests)

int main(int argc, char** argv) {
    int i = 1;
    while (i < argc - 1) {
        if (!strcmp(argv[i], "-d")) {
            if (strnlen(argv[i + 1], PATH_MAX) > 0) {
                int fd = open(argv[i + 1], O_RDWR);

                if (fd < 0) {
                    fprintf(stderr, "[fs] Could not open block device\n");
                    return -1;
                } else if (ioctl_device_get_topo_path(fd, test_disk_path, PATH_MAX) < 0) {
                    fprintf(stderr, "[fs] Could not acquire topological path of block device\n");
                    return -1;
                }

                block_info_t block_info;
                ssize_t rc = ioctl_block_get_info(fd, &block_info);

                if (rc < 0 || rc != sizeof(block_info)) {
                    fprintf(stderr, "[fs] Could not query block device info\n");
                    return -1;
                }

                // If there is already an FVM on this partition, remove it
                fvm_destroy(test_disk_path);

                use_real_disk = true;
                test_block_size = block_info.block_size;
                test_block_count = block_info.block_count;
                close(fd);
                break;
            }
        }
        i += 1;
    }

    // Initialize tmpfs.
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    if (loop.StartThread() != ZX_OK) {
        fprintf(stderr, "Error: Cannot initialize local tmpfs loop\n");
        return -1;
    }
    if (memfs_install_at(loop.dispatcher(), kTmpfsPath) != ZX_OK) {
        fprintf(stderr, "Error: Cannot install local tmpfs\n");
        return -1;
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
