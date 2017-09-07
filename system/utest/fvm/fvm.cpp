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

#include <block-client/client.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm.h>
#include <gpt/gpt.h>
#include <magenta/device/block.h>
#include <magenta/device/device.h>
#include <magenta/device/ramdisk.h>
#include <magenta/device/vfs.h>
#include <magenta/syscalls.h>
#include <mx/vmo.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/limits.h>
#include <fbl/new.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>

/////////////////////// Helper functions for creating FVM:

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) sizeof(s) / sizeof((s)[0])

// Creates a ramdisk, formats it, and binds to it.
static int StartFVMTest(uint64_t blk_size, uint64_t blk_count, uint64_t slice_size,
                        char* ramdisk_path_out, char* fvm_driver_out) {
    if (create_ramdisk(blk_size, blk_count, ramdisk_path_out)) {
        fprintf(stderr, "fvm: Could not create ramdisk\n");
        return -1;
    }

    int fd = open(ramdisk_path_out, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "fvm: Could not open ramdisk\n");
        destroy_ramdisk(ramdisk_path_out);
        return -1;
    }

    mx_status_t status = fvm_init(fd, slice_size);
    if (status != MX_OK) {
        fprintf(stderr, "fvm: Could not initialize fvm\n");
        destroy_ramdisk(ramdisk_path_out);
        close(fd);
        return -1;
    }

    ssize_t r = ioctl_device_bind(fd, FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB));
    close(fd);
    if (r < 0) {
        fprintf(stderr, "fvm: Error binding to fvm driver\n");
        destroy_ramdisk(ramdisk_path_out);
        return -1;
    }

    if (wait_for_driver_bind(ramdisk_path_out, "fvm")) {
        fprintf(stderr, "fvm: Error waiting for fvm driver to bind\n");
        return -1;
    }
    strcpy(fvm_driver_out, ramdisk_path_out);
    strcat(fvm_driver_out, "/fvm");

    return 0;
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
    char* s = strrchr(ramdisk_path, '/');
    *s = '\0';
    if (wait_for_driver_bind(ramdisk_path, "block")) {
        fprintf(stderr, "fvm rebind: Block driver did not rebind to ramdisk\n");
        return -1;
    }
    *s = '/';

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
    if (wait_for_driver_bind(ramdisk_path, "fvm")) {
        fprintf(stderr, "fvm rebind: Error waiting for fvm driver to bind\n");
        return -1;
    }

    char path[PATH_MAX];
    strcpy(path, ramdisk_path);
    strcat(path, "/fvm");

    size_t path_len = strlen(path);
    for (size_t i = 0; i < entry_count; i++) {
        char vpart_driver[256];
        snprintf(vpart_driver, sizeof(vpart_driver), "%s-p-%zu",
                 entries[i].name, entries[i].number);
        if (wait_for_driver_bind(path, vpart_driver)) {
            return -1;
        }
        strcat(path, "/");
        strcat(path, vpart_driver);
        if (wait_for_driver_bind(path, "block")) {
            return -1;
        }
        path[path_len] = '\0';
    }

    fvm_fd = open(path, O_RDWR);
    if (fvm_fd < 0) {
        fprintf(stderr, "fvm rebind: Failed to open fvm\n");
        return -1;
    }
    return fvm_fd;
}

// Unbind FVM driver and removes the backing ramdisk device.
static int EndFVMTest(const char* ramdisk_path) {
    return destroy_ramdisk(ramdisk_path);
}

/////////////////////// Helper functions, definitions

constexpr uint8_t kTestUniqueGUID[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr char kTestPartName1[] = "data";
constexpr uint8_t kTestPartGUIDData[] = GUID_DATA_VALUE;
constexpr char kTestPartName2[] = "blob";
constexpr uint8_t kTestPartGUIDBlob[] = GUID_BLOBFS_VALUE;
constexpr char kTestPartName3[] = "system";
constexpr uint8_t kTestPartGUIDSys[] = GUID_SYSTEM_VALUE;

class VmoBuf;

class VmoClient : public fbl::RefCounted<VmoClient> {
public:
    static bool Create(int fd, fbl::RefPtr<VmoClient>* out);
    bool CheckWrite(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len);
    bool CheckRead(VmoBuf* vbuf, size_t buf_off, size_t dev_off, size_t len);
    bool Txn(block_fifo_request_t* requests, size_t count) {
        BEGIN_HELPER;
        ASSERT_EQ(block_fifo_txn(client_, &requests[0], count), MX_OK); END_HELPER;
    }

    int fd() const { return fd_; }
    txnid_t txnid() { return txnid_; }
    ~VmoClient() {
        block_fifo_release_client(client_);
    }
private:
    int fd_;
    txnid_t txnid_;
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

        mx::vmo vmo;
        ASSERT_EQ(mx::vmo::create(size, 0, &vmo), MX_OK);

        mx_handle_t xfer_vmo;
        ASSERT_EQ(mx_handle_duplicate(vmo.get(), MX_RIGHT_SAME_RIGHTS,
                                      &xfer_vmo), MX_OK);

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
            request.txnid = client_->txnid();
            request.vmoid = vmoid_;
            request.opcode = BLOCKIO_CLOSE_VMO;
            client_->Txn(&request, 1);
        }
    }

private:
    friend VmoClient;

    VmoBuf(fbl::RefPtr<VmoClient> client, mx::vmo vmo,
           fbl::unique_ptr<uint8_t[]> buf, vmoid_t vmoid) :
        client_(fbl::move(client)), vmo_(fbl::move(vmo)),
        buf_(fbl::move(buf)), vmoid_(vmoid) {}

    fbl::RefPtr<VmoClient> client_;
    mx::vmo vmo_;
    fbl::unique_ptr<uint8_t[]> buf_;
    vmoid_t vmoid_;
};

bool VmoClient::Create(int fd, fbl::RefPtr<VmoClient>* out) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::RefPtr<VmoClient> vc = fbl::AdoptRef(new (&ac) VmoClient());
    ASSERT_TRUE(ac.check());
    mx_handle_t fifo;
    ASSERT_GT(ioctl_block_get_fifos(fd, &fifo), 0, "Failed to get FIFO");
    ASSERT_GT(ioctl_block_alloc_txn(fd, &vc->txnid_), 0, "Failed to alloc txn");
    ASSERT_EQ(block_fifo_create_client(fifo, &vc->client_), MX_OK);
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
    size_t actual;
    ASSERT_EQ(vbuf->vmo_.write(&vbuf->buf_[buf_off], buf_off, len, &actual), MX_OK);
    ASSERT_EQ(len, actual);

    // Write to the block device
    block_fifo_request_t request;
    request.txnid = txnid_;
    request.vmoid = vbuf->vmoid_;
    request.opcode = BLOCKIO_WRITE;
    request.length = len;
    request.vmo_offset = buf_off;
    request.dev_offset = dev_off;
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
    request.txnid = txnid_;
    request.vmoid = vbuf->vmoid_;
    request.opcode = BLOCKIO_READ;
    request.length = len;
    request.vmo_offset = buf_off;
    request.dev_offset = dev_off;
    ASSERT_TRUE(Txn(&request, 1));

    // Read from the registered VMO
    size_t actual;
    ASSERT_EQ(vbuf->vmo_.read(out.get(), buf_off, len, &actual), MX_OK);
    ASSERT_EQ(len, actual);

    ASSERT_EQ(memcmp(&vbuf->buf_[buf_off], out.get(), len), 0);
    END_HELPER;
}

static bool CheckWrite(int fd, size_t off, size_t len, uint8_t* buf) {
    BEGIN_HELPER;
    for (size_t i = 0; i < len; i++) {
        buf[i] = static_cast<uint8_t>(rand());
    }
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
    ASSERT_EQ(write(fd, buf, len), static_cast<ssize_t>(len));
    END_HELPER;
}

static bool CheckRead(int fd, size_t off, size_t len, const uint8_t* in) {
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

static bool CheckWriteColor(int fd, size_t off, size_t len, uint8_t color) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check());
    memset(buf.get(), color, len);
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off));
    ASSERT_EQ(write(fd, buf.get(), len), static_cast<ssize_t>(len));
    END_HELPER;
}

static bool CheckReadColor(int fd, size_t off, size_t len, uint8_t color) {
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

static bool CheckWriteReadBlock(int fd, size_t block, size_t count) {
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

static bool CheckNoAccessBlock(int fd, size_t block, size_t count) {
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

static bool CheckDeadBlock(int fd) {
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
static bool TestTooSmall(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    uint64_t blk_size = 512;
    uint64_t blk_count = (1 << 15);
    ASSERT_GE(create_ramdisk(blk_size, blk_count, ramdisk_path), 0);
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0);
    size_t slice_size = blk_size * blk_count;
    ASSERT_EQ(fvm_init(fd, slice_size), MX_ERR_NO_SPACE);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Load and unload an empty FVM
static bool TestEmpty(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating a single partition
static bool TestAllocateOne(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    // Allocate one VPart
    alloc_req_t request;
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
    vp_fd = fvm_open_partition(kTestUniqueGUID, kTestPartGUIDData, nullptr);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating a collection of partitions
static bool TestAllocateMany(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    // Test allocation of multiple VPartitions
    alloc_req_t request;
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
    memcpy(request.type, kTestPartGUIDSys, GUID_LEN);
    int sys_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(sys_fd, 0);

    ASSERT_TRUE(CheckWriteReadBlock(data_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(blob_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(sys_fd, 0, 1));

    ASSERT_EQ(close(data_fd), 0);
    ASSERT_EQ(close(blob_fd), 0);
    ASSERT_EQ(close(sys_fd), 0);

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the fvm driver can cope with a sudden close during read / write
// operations.
static bool TestCloseDuringAccess(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

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
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the fvm driver can cope with a sudden release during read / write
// operations.
static bool TestReleaseDuringAccess(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

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
    // ... and close the entire ramdisk from undearneath it!
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");

    int res;
    ASSERT_EQ(thrd_join(thread, &res), thrd_success);
    ASSERT_EQ(res, 0, "Background thread failed");

    close(vp_fd);
    close(fd);
    END_TEST;
}

// Test allocating additional slices to a vpartition.
static bool TestVPartitionExtend(void) {
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
    size_t slices_left = fvm::UsableSlicesCount(kDiskSize, slice_size);

    // Allocate one VPart
    alloc_req_t request;
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
    ASSERT_LT(ioctl_block_fvm_alloc(fd, &request), 0, "Couldn't allocate VPart");

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating very sparse VPartition
static bool TestVPartitionExtendSparse(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    uint64_t blk_size = 512;
    uint64_t blk_count = 1 << 20;
    uint64_t slice_size = 16 * blk_size;
    ASSERT_EQ(StartFVMTest(blk_size, blk_count, slice_size, ramdisk_path,
                           fvm_driver),
              0, "error mounting FVM");

    size_t slices_left = fvm::UsableSlicesCount(blk_size * blk_count, slice_size);
    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    alloc_req_t request;
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
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test removing slices from a VPartition.
static bool TestVPartitionShrink(void) {
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
    size_t slices_left = fvm::UsableSlicesCount(kDiskSize, slice_size);

    // Allocate one VPart
    alloc_req_t request;
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
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test splitting a contiguous slice extent into multiple parts
static bool TestVPartitionSplit(void) {
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
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test removing VPartitions within an FVM
static bool TestVPartitionDestroy(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    // Test allocation of multiple VPartitions
    alloc_req_t request;
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
    memcpy(request.type, kTestPartGUIDSys, GUID_LEN);
    int sys_fd = fvm_allocate_partition(fd, &request);
    ASSERT_GT(sys_fd, 0);

    // We can access all three...
    ASSERT_TRUE(CheckWriteReadBlock(data_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(blob_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(sys_fd, 0, 1));

    // But not after we destroy the blob partition.
    ASSERT_EQ(ioctl_block_fvm_destroy(blob_fd), 0);
    ASSERT_TRUE(CheckWriteReadBlock(data_fd, 0, 1));
    ASSERT_TRUE(CheckDeadBlock(blob_fd));
    ASSERT_TRUE(CheckWriteReadBlock(sys_fd, 0, 1));

    // We also can't re-destroy the blob partition.
    ASSERT_LT(ioctl_block_fvm_destroy(blob_fd), 0);

    // We also can't allocate slices to the destroyed blob partition.
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(blob_fd, &erequest), 0);

    // Destroy the other two VPartitions.
    ASSERT_EQ(ioctl_block_fvm_destroy(data_fd), 0);
    ASSERT_TRUE(CheckDeadBlock(data_fd));
    ASSERT_TRUE(CheckDeadBlock(blob_fd));
    ASSERT_TRUE(CheckWriteReadBlock(sys_fd, 0, 1));

    ASSERT_EQ(ioctl_block_fvm_destroy(sys_fd), 0);
    ASSERT_TRUE(CheckDeadBlock(data_fd));
    ASSERT_TRUE(CheckDeadBlock(blob_fd));
    ASSERT_TRUE(CheckDeadBlock(sys_fd));

    ASSERT_EQ(close(data_fd), 0);
    ASSERT_EQ(close(blob_fd), 0);
    ASSERT_EQ(close(sys_fd), 0);

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating and accessing slices which are allocated contiguously.
static bool TestSliceAccessContiguous(void) {
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
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating and accessing multiple (3+) slices at once.
static bool TestSliceAccessMany(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    // The size of a slice must be carefully constructed for this test
    // so that we can hold multiple slices in memory without worrying
    // about hitting resource limits.
    const size_t kBlockSize = 512;
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
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating and accessing slices which are allocated
// virtually contiguously (they appear sequential to the client) but are
// actually noncontiguous on the FVM partition.
static bool TestSliceAccessNonContiguousPhysical(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 8lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    const size_t kDiskSize = 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    alloc_req_t request;
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
        {0, GUID_DATA_VALUE, "data", request.slice_count},
        {0, GUID_BLOBFS_VALUE, "blob", request.slice_count},
        {0, GUID_SYSTEM_VALUE, "sys", request.slice_count},
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
        ASSERT_GE(vparts[i].slices_used, 5, "");

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

            for (size_t dev_off = dev_off_start; dev_off <= dev_off_end; dev_off += info.block_size) {
                for (size_t len = len_start; len <= len_end; len += info.block_size) {
                    ASSERT_TRUE(vc->CheckWrite(vb.get(), 0, dev_off, len));
                    ASSERT_TRUE(vc->CheckRead(vb.get(), 0, dev_off, len));
                }
            }
        }
        ASSERT_EQ(close(vparts[i].fd), 0);
    }

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test allocating and accessing slices which are
// allocated noncontiguously from the client's perspective.
static bool TestSliceAccessNonContiguousVirtual(void) {
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
        {0, GUID_DATA_VALUE, "data", request.slice_count, request.slice_count},
        {0, GUID_BLOBFS_VALUE, "blob", request.slice_count, request.slice_count},
        {0, GUID_SYSTEM_VALUE, "sys", request.slice_count, request.slice_count},
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
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the FVM driver actually persists updates.
static bool TestPersistenceSimple(void) {
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

    vp_fd = fvm_open_partition(kTestUniqueGUID, kTestPartGUIDData, nullptr);
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

    // Rebind the FVM driver, check the extension has succeeded.
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    // Now we can access the next slice...
    ASSERT_TRUE(CheckWrite(vp_fd, info.block_size * (last_block + 1),
                           info.block_size, &buf[info.block_size]),
                "");
    ASSERT_TRUE(CheckRead(vp_fd, info.block_size * (last_block + 1),
                          info.block_size, &buf[info.block_size]),
                "");
    // ... We can still access the previous slice...
    ASSERT_TRUE(CheckRead(vp_fd, info.block_size * last_block,
                          info.block_size, &buf[0]),
                "");
    // ... And we can cross slices
    ASSERT_TRUE(CheckRead(vp_fd, info.block_size * last_block,
                          info.block_size * 2, &buf[0]),
                "");

    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the FVM driver can mount filesystems.
static bool TestMounting(void) {
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
              MX_OK);

    // Mount the VPart
    const char* mount_path = "/tmp/minfs_test_mountpath";
    ASSERT_EQ(mkdir(mount_path, 0666), 0);
    ASSERT_EQ(mount(vp_fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              MX_OK);

    // Verify that the mount was successful
    int rootfd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(rootfd, 0);
    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* out = reinterpret_cast<vfs_query_info_t*>(buf);
    ASSERT_EQ(ioctl_vfs_query_fs(rootfd, out, sizeof(buf) - 1),
              static_cast<ssize_t>(sizeof(vfs_query_info_t) + strlen("minfs")),
              "Failed to query filesystem");
    ASSERT_EQ(strcmp("minfs", out->name), 0, "Unexpected filesystem mounted");

    // Verify that MinFS does not try to use more of the VPartition than
    // was originally allocated.
    ASSERT_LE(out->total_bytes, slice_size * request.slice_count);

    // Clean up
    ASSERT_EQ(close(rootfd), 0);
    ASSERT_EQ(umount(mount_path), MX_OK);
    ASSERT_EQ(rmdir(mount_path), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test that the FVM can recover when one copy of
// metadata becomes corrupt.
static bool TestCorruptionOk(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    const size_t kDiskSize = 512 * (1 << 20);
    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdisk_fd, 0);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart (writes to backup)
    alloc_req_t request;
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
    vp_fd = fvm_open_partition(kTestUniqueGUID, kTestPartGUIDData, nullptr);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");

    // The slice extension is still accessible.
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, slice_size / info.block_size, 1));

    // Clean up
    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(ramdisk_fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

static bool TestCorruptionRegression(void) {
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
    vp_fd = fvm_open_partition(kTestUniqueGUID, kTestPartGUIDData, nullptr);
    ASSERT_GT(vp_fd, 0);

    // The slice extension is no longer accessible
    ASSERT_TRUE(CheckWriteReadBlock(vp_fd, 0, 1));
    ASSERT_TRUE(CheckNoAccessBlock(vp_fd, slice_size / info.block_size, 1));

    // Clean up
    ASSERT_EQ(close(vp_fd), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(ramdisk_fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

static bool TestCorruptionUnrecoverable(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    const size_t kDiskSize = 512 * (1 << 20);
    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdisk_fd, 0);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0);
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart (writes to backup)
    alloc_req_t request;
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
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
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
    size_t slices_left;
    size_t thread_count;
};

template <size_t ThreadCount>
int random_access_thread(void* arg) {
    auto st = static_cast<fvm_test_state_t<ThreadCount>*>(arg);
    unsigned int seed = static_cast<unsigned int>(mx_ticks_get());
    unittest_printf("random_access_thread using seed: %u\n", seed);

    uint8_t color = 0;
    fvm_thread_state_t* self;
    {
        fbl::AutoLock al(&st->lock);
        self = &st->thread_states[st->thread_count];
        color = (uint8_t)st->thread_count;
        st->thread_count++;
    }

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
                                           len / st->block_size),
                        "");
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
                                           len / st->block_size),
                        "");
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
                                           len / st->block_size),
                        "");

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
                                           len / st->block_size),
                        "");
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
static bool TestRandomOpMultithreaded(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    const size_t kBlockSize = 512;
    const size_t kBlockCount = 1 << 20;
    const size_t kBlocksPerSlice = 256;
    const size_t kSliceSize = kBlocksPerSlice * kBlockSize;
    ASSERT_EQ(StartFVMTest(kBlockSize, kBlockCount, kSliceSize, ramdisk_path,
                           fvm_driver),
              0, "error mounting FVM");

    const size_t kDiskSize = kBlockSize * kBlockCount;
    const size_t kSlicesCount = fvm::UsableSlicesCount(kDiskSize, kSliceSize);

    static_assert(kSlicesCount > ThreadCount * 2,
                  "Not enough slices to distribute between threads");

    fvm_test_state_t<ThreadCount> s{};
    s.block_size = kBlockSize;
    s.slice_size = kSliceSize;
    s.slices_left = kSlicesCount;
    s.slices_total = kSlicesCount;

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0);

    alloc_req_t request;
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

    s.slices_left -= ThreadCount;

    // Initialize and launch all threads
    for (size_t i = 0; i < ThreadCount; i++) {
        EXPECT_EQ(s.thread_states[i].extents.size(), 0);
        fvm_extent_t extent;
        extent.start = 0;
        extent.len = 1;
        fbl::AllocChecker ac;
        s.thread_states[i].extents.push_back(fbl::move(extent), &ac);
        EXPECT_TRUE(ac.check());
        EXPECT_TRUE(CheckWriteReadBlock(s.thread_states[i].vp_fd, 0, kBlocksPerSlice));
        EXPECT_EQ(thrd_create(&s.thread_states[i].thr,
                              random_access_thread<ThreadCount>, &s),
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
        s.thread_count = 0;

        // Re-open all partitions, re-launch the worker threads
        for (size_t i = 0; i < ThreadCount; i++) {
            request.guid[0] = static_cast<uint8_t>(i);
            int vp_fd = fvm_open_partition(request.guid, request.type, nullptr);
            ASSERT_GT(vp_fd, 0);
            s.thread_states[i].vp_fd = vp_fd;
            EXPECT_EQ(thrd_create(&s.thread_states[i].thr,
                                  random_access_thread<ThreadCount>, &s),
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
        EXPECT_EQ(ioctl_block_fvm_destroy(s.thread_states[i].vp_fd), 0);
        EXPECT_EQ(close(s.thread_states[i].vp_fd), 0);
    }

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

BEGIN_TEST_CASE(fvm_tests)
RUN_TEST_MEDIUM(TestTooSmall)
RUN_TEST_MEDIUM(TestEmpty)
RUN_TEST_MEDIUM(TestAllocateOne)
RUN_TEST_MEDIUM(TestAllocateMany)
RUN_TEST_MEDIUM(TestCloseDuringAccess)
RUN_TEST_MEDIUM(TestReleaseDuringAccess)
RUN_TEST_MEDIUM(TestVPartitionExtend)
RUN_TEST_MEDIUM(TestVPartitionExtendSparse)
RUN_TEST_MEDIUM(TestVPartitionShrink)
RUN_TEST_MEDIUM(TestVPartitionSplit)
RUN_TEST_MEDIUM(TestVPartitionDestroy)
RUN_TEST_MEDIUM(TestSliceAccessContiguous)
RUN_TEST_MEDIUM(TestSliceAccessMany)
RUN_TEST_MEDIUM(TestSliceAccessNonContiguousPhysical)
RUN_TEST_MEDIUM(TestSliceAccessNonContiguousVirtual)
RUN_TEST_MEDIUM(TestPersistenceSimple)
RUN_TEST_MEDIUM(TestMounting)
RUN_TEST_MEDIUM(TestCorruptionOk)
RUN_TEST_MEDIUM(TestCorruptionRegression)
RUN_TEST_MEDIUM(TestCorruptionUnrecoverable)
RUN_TEST_MEDIUM((TestRandomOpMultithreaded<1, /* persistent= */ false>))
RUN_TEST_MEDIUM((TestRandomOpMultithreaded<3, /* persistent= */ false>))
RUN_TEST_MEDIUM((TestRandomOpMultithreaded<5, /* persistent= */ false>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<10, /* persistent= */ false>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<25, /* persistent= */ false>))
RUN_TEST_MEDIUM((TestRandomOpMultithreaded<1, /* persistent= */ true>))
RUN_TEST_MEDIUM((TestRandomOpMultithreaded<3, /* persistent= */ true>))
RUN_TEST_MEDIUM((TestRandomOpMultithreaded<5, /* persistent= */ true>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<10, /* persistent= */ true>))
RUN_TEST_LARGE((TestRandomOpMultithreaded<25, /* persistent= */ true>))
END_TEST_CASE(fvm_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
