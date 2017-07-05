// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
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

#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm.h>
#include <gpt/gpt.h>
#include <magenta/device/block.h>
#include <magenta/device/device.h>
#include <magenta/device/ramdisk.h>
#include <magenta/device/vfs.h>
#include <magenta/syscalls.h>
#include <mxio/watcher.h>
#include <mxtl/algorithm.h>
#include <mxtl/limits.h>
#include <mxtl/new.h>
#include <mxtl/unique_ptr.h>
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
constexpr char kBlockDevPath[] = "/dev/class/block";

// Helper function to identify if fd points to partition
static bool isPartition(int fd, const uint8_t* uniqueGUID, const uint8_t* typeGUID) {
    uint8_t buf[GUID_LEN];
    if (fd < 0) {
        return false;
    } else if (ioctl_block_get_type_guid(fd, buf, sizeof(buf)) < 0) {
        return false;
    } else if (memcmp(buf, typeGUID, GUID_LEN) != 0) {
        return false;
    } else if (ioctl_block_get_partition_guid(fd, buf, sizeof(buf)) < 0) {
        return false;
    } else if (memcmp(buf, uniqueGUID, GUID_LEN) != 0) {
        return false;
    }
    return true;
}

// Helper function to allocate, find, and open VPartition.
static bool fvmAllocHelper(int fvm_fd, const alloc_req_t* request, int* out_partition) {
    ASSERT_EQ(ioctl_block_fvm_alloc(fvm_fd, request), 0, "Couldn't allocate VPart");

    typedef struct {
        const alloc_req_t* request;
        int* out_partition;
    } alloc_helper_info_t;

    alloc_helper_info_t info;
    info.request = request;
    info.out_partition = out_partition;

    auto cb = [](int dirfd, int event, const char* fn, void* cookie) {
        if (event != WATCH_EVENT_ADD_FILE) {
            return MX_OK;
        }
        auto info = static_cast<alloc_helper_info_t*>(cookie);
        int devfd = openat(dirfd, fn, O_RDWR);
        if (devfd < 0) {
            return MX_OK;
        }
        if (isPartition(devfd, info->request->guid, info->request->type)) {
            *(info->out_partition) = devfd;
            return MX_ERR_STOP;
        }
        close(devfd);
        return MX_OK;
    };

    DIR* dir = opendir(kBlockDevPath);
    ASSERT_NONNULL(dir, "");

    mx_time_t deadline = mx_deadline_after(MX_SEC(2));
    ASSERT_EQ(mxio_watch_directory(dirfd(dir), cb, deadline, &info), MX_ERR_STOP, "");
    closedir(dir);
    return true;
}

// Helper function to find a VPartition which already exists.
static int openVPartition(const uint8_t* uniqueGUID, const uint8_t* typeGUID) {
    DIR* dir = opendir(kBlockDevPath);
    if (dir == nullptr) {
        return -1;
    }
    struct dirent* de;
    int result_fd = -1;
    while ((de = readdir(dir)) != NULL) {
        if ((strcmp(de->d_name, ".") == 0) || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        int devfd = openat(dirfd(dir), de->d_name, O_RDWR);
        if (devfd < 0) {
            continue;
        } else if (!isPartition(devfd, uniqueGUID, typeGUID)) {
            close(devfd);
            continue;
        }
        result_fd = devfd;
        break;
    }

    closedir(dir);
    return result_fd;
}

static bool checkWrite(int fd, size_t off, size_t len, uint8_t* buf) {
    for (size_t i = 0; i < len; i++)
        buf[i] = static_cast<uint8_t>(rand());
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off), "");
    ASSERT_EQ(write(fd, buf, len), static_cast<ssize_t>(len), "");
    return true;
}

static bool checkRead(int fd, size_t off, size_t len, const uint8_t* in) {
    mxtl::AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check(), "");
    memset(out.get(), 0, len);
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off), "");
    ASSERT_EQ(read(fd, out.get(), len), static_cast<ssize_t>(len), "");
    ASSERT_EQ(memcmp(in, out.get(), len), 0, "");
    return true;
}

static bool checkWriteReadBlock(int fd, size_t block, size_t count) {
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(fd, &info), 0, "");
    mxtl::AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[info.block_size * count]);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_TRUE(checkWrite(fd, info.block_size * block, info.block_size * count, buf.get()), "");
    ASSERT_TRUE(checkRead(fd, info.block_size * block, info.block_size * count, buf.get()), "");
    return true;
}

static bool checkNoAccessBlock(int fd, size_t block, size_t count) {
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(fd, &info), 0, "");
    mxtl::AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[info.block_size * count]);
    ASSERT_TRUE(ac.check(), "");
    size_t len = info.block_size * count;
    size_t off = info.block_size * block;
    for (size_t i = 0; i < len; i++)
        buf[i] = static_cast<uint8_t>(rand());
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off), "");
    ASSERT_EQ(write(fd, buf.get(), len), -1, "");
    ASSERT_EQ(lseek(fd, off, SEEK_SET), static_cast<ssize_t>(off), "");
    ASSERT_EQ(read(fd, buf.get(), len), -1, "");
    return true;
}

static bool checkDeadBlock(int fd) {
    block_info_t info;
    ASSERT_LT(ioctl_block_get_info(fd, &info), 0, "");
    mxtl::AllocChecker ac;
    constexpr size_t kBlksize = 8192;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[kBlksize]);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_EQ(write(fd, buf.get(), kBlksize), -1, "");
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_EQ(read(fd, buf.get(), kBlksize), -1, "");
    return true;
}

/////////////////////// Actual tests:

// Test initializing the FVM on a partition that is smaller than a slice
static bool TestTooSmall(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    uint64_t blk_size = 512;
    uint64_t blk_count = (1 << 15);
    ASSERT_GE(create_ramdisk(blk_size, blk_count, ramdisk_path), 0, "");
    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(fd, 0, "");
    size_t slice_size = blk_size * blk_count;
    ASSERT_EQ(fvm_init(fd, slice_size), MX_ERR_NO_SPACE, "");
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
    ASSERT_GT(fd, 0, "");

    // Allocate one VPart
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");

    // Check that the name matches what we provided
    char name[FVM_NAME_LEN + 1];
    ASSERT_GE(ioctl_block_get_name(vp_fd, name, sizeof(name)), 0, "");
    ASSERT_EQ(memcmp(name, kTestPartName1, strlen(kTestPartName1)), 0, "");

    // Check that we can read from / write to it.
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, 0, 1), "");

    // Try accessing the block again after closing / re-opening it.
    ASSERT_EQ(close(vp_fd), 0, "");
    vp_fd = openVPartition(kTestUniqueGUID, kTestPartGUIDData);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, 0, 1), "");

    ASSERT_EQ(close(vp_fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_GT(fd, 0, "");

    // Test allocation of multiple VPartitions
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int data_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &data_fd), "");
    strcpy(request.name, kTestPartName2);
    memcpy(request.type, kTestPartGUIDBlob, GUID_LEN);
    int blob_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &blob_fd), "");
    strcpy(request.name, kTestPartName3);
    memcpy(request.type, kTestPartGUIDSys, GUID_LEN);
    int sys_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &sys_fd), "");

    ASSERT_TRUE(checkWriteReadBlock(data_fd, 0, 1), "");
    ASSERT_TRUE(checkWriteReadBlock(blob_fd, 0, 1), "");
    ASSERT_TRUE(checkWriteReadBlock(sys_fd, 0, 1), "");

    ASSERT_EQ(close(data_fd), 0, "");
    ASSERT_EQ(close(blob_fd), 0, "");
    ASSERT_EQ(close(sys_fd), 0, "");

    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_GT(fd, 0, "");

    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");

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
    ASSERT_EQ(thrd_create(&thread, bg_thread, &vp_fd), thrd_success, "");
    // Let the background thread warm up a little bit...
    usleep(10000);
    // ... and close the fd from underneath it!
    //
    // Yes, this is a little unsafe (we risk the bg thread accessing an
    // unallocated fd), but no one else in this test process should be adding
    // fds, so we won't risk anyone reusing "vp_fd" within this test case.
    ASSERT_EQ(close(vp_fd), 0, "");

    int res;
    ASSERT_EQ(thrd_join(thread, &res), thrd_success, "");
    ASSERT_EQ(res, 0, "Background thread failed");

    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_GT(fd, 0, "");

    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");

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
    ASSERT_EQ(thrd_create(&thread, bg_thread, &vp_fd), thrd_success, "");
    // Let the background thread warm up a little bit...
    usleep(10000);
    // ... and close the entire ramdisk from undearneath it!
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");

    int res;
    ASSERT_EQ(thrd_join(thread, &res), thrd_success, "");
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
    size_t disk_size = 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    size_t slice_size = fvm_info.slice_size;
    size_t slices_left = fvm::UsableSlicesCount(disk_size, slice_size);

    // Allocate one VPart
    alloc_req_t request;
    size_t slice_count = 1;
    request.slice_count = slice_count;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");
    slices_left--;

    // Confirm that the disk reports the correct number of slices
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count, "");

    extend_request_t erequest;

    // Try re-allocating an already allocated vslice
    erequest.offset = 0;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count, "");

    // Try again with a portion of the request which is unallocated
    erequest.length = 2;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count, "");

    // Allocate OBSCENELY too many slices
    erequest.offset = slice_count;
    erequest.length = mxtl::numeric_limits<size_t>::max();
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Allocate slices at a too-large offset
    erequest.offset = mxtl::numeric_limits<size_t>::max();
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Attempt to allocate slightly too many slices
    erequest.offset = slice_count;
    erequest.length = slices_left + 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Allocate exactly the remaining number of slices
    erequest.offset = slice_count;
    erequest.length = slices_left;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "");
    slice_count += slices_left;
    slices_left = 0;

    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count, "");

    // We can't allocate any more to this VPartition
    erequest.offset = slice_count;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // We can't allocate a new VPartition
    strcpy(request.name, kTestPartName2);
    memcpy(request.type, kTestPartGUIDBlob, GUID_LEN);
    ASSERT_LT(ioctl_block_fvm_alloc(fd, &request), 0, "Couldn't allocate VPart");

    ASSERT_EQ(close(vp_fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

// Test removing slices from a VPartition.
static bool TestVPartitionShrink(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    size_t disk_size = 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    size_t slice_size = fvm_info.slice_size;
    size_t slices_left = fvm::UsableSlicesCount(disk_size, slice_size);

    // Allocate one VPart
    alloc_req_t request;
    size_t slice_count = 1;
    request.slice_count = slice_count;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");
    slices_left--;

    // Confirm that the disk reports the correct number of slices
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count, "");
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, (slice_size / info.block_size) - 1, 1), "");
    ASSERT_TRUE(checkNoAccessBlock(vp_fd, (slice_size / info.block_size) - 1, 2), "");

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
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count, "");

    // Try again with a portion of the request which is unallocated
    erequest.length = 2;
    ASSERT_LT(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "Expected request failure");
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count, "");

    // Allocate exactly the remaining number of slices
    erequest.offset = slice_count;
    erequest.length = slices_left;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "");
    slice_count += slices_left;
    slices_left = 0;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * slice_count, "");
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, (slice_size / info.block_size) - 1, 1), "");
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, (slice_size / info.block_size) - 1, 2), "");

    // We can't allocate any more to this VPartition
    erequest.offset = slice_count;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Expected request failure");

    // Try to shrink off the end (okay, since SOME of the slices are allocated)
    erequest.offset = 1;
    erequest.length = slice_count + 3;
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "Expected request failure");

    // The same request to shrink should now fail (NONE of the slices are
    // allocated)
    erequest.offset = 1;
    erequest.length = slice_count - 1;
    ASSERT_LT(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "Expected request failure");

    // ... unless we re-allocate and try again.
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "");
    ASSERT_EQ(ioctl_block_fvm_shrink(vp_fd, &erequest), 0, "");

    ASSERT_EQ(close(vp_fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_GT(fd, 0, "");

    // Test allocation of multiple VPartitions
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int data_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &data_fd), "");
    strcpy(request.name, kTestPartName2);
    memcpy(request.type, kTestPartGUIDBlob, GUID_LEN);
    int blob_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &blob_fd), "");
    strcpy(request.name, kTestPartName3);
    memcpy(request.type, kTestPartGUIDSys, GUID_LEN);
    int sys_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &sys_fd), "");

    // We can access all three...
    ASSERT_TRUE(checkWriteReadBlock(data_fd, 0, 1), "");
    ASSERT_TRUE(checkWriteReadBlock(blob_fd, 0, 1), "");
    ASSERT_TRUE(checkWriteReadBlock(sys_fd, 0, 1), "");

    // But not after we destroy the blob partition.
    ASSERT_EQ(ioctl_block_fvm_destroy(blob_fd), 0, "");
    ASSERT_TRUE(checkWriteReadBlock(data_fd, 0, 1), "");
    ASSERT_TRUE(checkDeadBlock(blob_fd), "");
    ASSERT_TRUE(checkWriteReadBlock(sys_fd, 0, 1), "");

    // We also can't re-destroy the blob partition.
    ASSERT_LT(ioctl_block_fvm_destroy(blob_fd), 0, "");

    // We also can't allocate slices to the destroyed blob partition.
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_LT(ioctl_block_fvm_extend(blob_fd, &erequest), 0, "");

    // Destroy the other two VPartitions.
    ASSERT_EQ(ioctl_block_fvm_destroy(data_fd), 0, "");
    ASSERT_TRUE(checkDeadBlock(data_fd), "");
    ASSERT_TRUE(checkDeadBlock(blob_fd), "");
    ASSERT_TRUE(checkWriteReadBlock(sys_fd, 0, 1), "");

    ASSERT_EQ(ioctl_block_fvm_destroy(sys_fd), 0, "");
    ASSERT_TRUE(checkDeadBlock(data_fd), "");
    ASSERT_TRUE(checkDeadBlock(blob_fd), "");
    ASSERT_TRUE(checkDeadBlock(sys_fd), "");

    ASSERT_EQ(close(data_fd), 0, "");
    ASSERT_EQ(close(blob_fd), 0, "");
    ASSERT_EQ(close(sys_fd), 0, "");

    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");

    // This is the last 'accessible' block.
    size_t last_block = (slice_size / info.block_size) - 1;
    mxtl::AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[info.block_size * 2]);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_TRUE(checkWrite(vp_fd, info.block_size * last_block, info.block_size, &buf[0]), "");
    ASSERT_TRUE(checkRead(vp_fd, info.block_size * last_block, info.block_size, &buf[0]), "");

    // Try writing out of bounds -- check that we don't have access.
    ASSERT_TRUE(checkNoAccessBlock(vp_fd, (slice_size / info.block_size) - 1, 2), "");
    ASSERT_TRUE(checkNoAccessBlock(vp_fd, slice_size / info.block_size, 1), "");

    // Attempt to access the next contiguous slice
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Couldn't extend VPartition");

    // Now we can access the next slice...
    ASSERT_TRUE(checkWrite(vp_fd, info.block_size * (last_block + 1),
                           info.block_size, &buf[info.block_size]),
                "");
    ASSERT_TRUE(checkRead(vp_fd, info.block_size * (last_block + 1),
                          info.block_size, &buf[info.block_size]),
                "");
    // ... We can still access the previous slice...
    ASSERT_TRUE(checkRead(vp_fd, info.block_size * last_block,
                          info.block_size, &buf[0]),
                "");
    // ... And we can cross slices
    ASSERT_TRUE(checkRead(vp_fd, info.block_size * last_block,
                          info.block_size * 2, &buf[0]),
                "");

    ASSERT_EQ(close(vp_fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    ASSERT_EQ(fvm_info.slice_size, kSliceSize);

    // Allocate one VPart
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_size, kBlockSize);

    // Access the first slice
    mxtl::AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[kSliceSize * 3]);
    ASSERT_TRUE(ac.check(), "");
    ASSERT_TRUE(checkWrite(vp_fd, 0, kSliceSize, &buf[0]), "");
    ASSERT_TRUE(checkRead(vp_fd, 0, kSliceSize, &buf[0]), "");

    // Try writing out of bounds -- check that we don't have access.
    ASSERT_TRUE(checkNoAccessBlock(vp_fd, kBlocksPerSlice - 1, 2), "");
    ASSERT_TRUE(checkNoAccessBlock(vp_fd, kBlocksPerSlice, 1), "");

    // Attempt to access the next contiguous slices
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 2;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Couldn't extend VPartition");

    // Now we can access the next slices...
    ASSERT_TRUE(checkWrite(vp_fd, kSliceSize, 2 * kSliceSize, &buf[kSliceSize]), "");
    ASSERT_TRUE(checkRead(vp_fd, kSliceSize, 2 * kSliceSize, &buf[kSliceSize]), "");
    // ... We can still access the previous slice...
    ASSERT_TRUE(checkRead(vp_fd, 0, kSliceSize, &buf[0]), "");
    // ... And we can cross slices for reading.
    ASSERT_TRUE(checkRead(vp_fd, 0, 3 * kSliceSize, &buf[0]), "");

    // Also, we can cross slices for writing.
    ASSERT_TRUE(checkWrite(vp_fd, 0, 3 * kSliceSize, &buf[0]), "");
    ASSERT_TRUE(checkRead(vp_fd, 0, 3 * kSliceSize, &buf[0]), "");

    // Additionally, we can access "parts" of slices in a multi-slice
    // operation. Here, read one block into the first slice, and read
    // up to the last block in the final slice.
    ASSERT_TRUE(checkWrite(vp_fd, kBlockSize, 3 * kSliceSize - 2 * kBlockSize, &buf[0]), "");
    ASSERT_TRUE(checkRead(vp_fd, kBlockSize, 3 * kSliceSize - 2 * kBlockSize, &buf[0]), "");

    ASSERT_EQ(close(vp_fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    size_t disk_size = 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
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
        ASSERT_TRUE(fvmAllocHelper(fd, &request, &vparts[i].fd), "");
    }

    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vparts[0].fd, &info), 0, "");

    size_t usable_slices_per_vpart = fvm::UsableSlicesCount(disk_size, slice_size) / kNumVParts;
    size_t i = 0;
    while (vparts[i].slices_used < usable_slices_per_vpart) {
        int vfd = vparts[i].fd;
        // This is the last 'accessible' block.
        size_t last_block = (vparts[i].slices_used * (slice_size / info.block_size)) - 1;
        mxtl::AllocChecker ac;
        mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[info.block_size * 2]);
        ASSERT_TRUE(ac.check(), "");
        ASSERT_TRUE(checkWrite(vfd, info.block_size * last_block, info.block_size, &buf[0]), "");
        ASSERT_TRUE(checkRead(vfd, info.block_size * last_block, info.block_size, &buf[0]), "");

        // Try writing out of bounds -- check that we don't have access.
        ASSERT_TRUE(checkNoAccessBlock(vfd, last_block, 2), "");
        ASSERT_TRUE(checkNoAccessBlock(vfd, last_block + 1, 1), "");

        // Attempt to access the next contiguous slice
        extend_request_t erequest;
        erequest.offset = vparts[i].slices_used;
        erequest.length = 1;
        ASSERT_EQ(ioctl_block_fvm_extend(vfd, &erequest), 0, "Couldn't extend VPartition");

        // Now we can access the next slice...
        ASSERT_TRUE(checkWrite(vfd, info.block_size * (last_block + 1),
                               info.block_size, &buf[info.block_size]),
                    "");
        ASSERT_TRUE(checkRead(vfd, info.block_size * (last_block + 1),
                              info.block_size, &buf[info.block_size]),
                    "");
        // ... We can still access the previous slice...
        ASSERT_TRUE(checkRead(vfd, info.block_size * last_block,
                              info.block_size, &buf[0]),
                    "");
        // ... And we can cross slices
        ASSERT_TRUE(checkRead(vfd, info.block_size * last_block,
                              info.block_size * 2, &buf[0]),
                    "");
        vparts[i].slices_used++;
        i = (i + 1) % kNumVParts;
    }

    for (size_t i = 0; i < kNumVParts; i++) {
        ASSERT_EQ(close(vparts[i].fd), 0, "");
    }

    ASSERT_EQ(close(fd), 0, "");
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
    size_t disk_size = 512 * (1 << 20);

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
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
        ASSERT_TRUE(fvmAllocHelper(fd, &request, &vparts[i].fd), "");
    }

    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vparts[0].fd, &info), 0, "");

    size_t usable_slices_per_vpart = fvm::UsableSlicesCount(disk_size, slice_size) / kNumVParts;
    size_t i = 0;
    while (vparts[i].slices_used < usable_slices_per_vpart) {
        int vfd = vparts[i].fd;
        // This is the last 'accessible' block.
        size_t last_block = (vparts[i].last_slice * (slice_size / info.block_size)) - 1;
        ASSERT_TRUE(checkWriteReadBlock(vfd, last_block, 1), "");

        // Try writing out of bounds -- check that we don't have access.
        ASSERT_TRUE(checkNoAccessBlock(vfd, last_block, 2), "");
        ASSERT_TRUE(checkNoAccessBlock(vfd, last_block + 1, 1), "");

        // Attempt to access a non-contiguous slice
        extend_request_t erequest;
        erequest.offset = vparts[i].last_slice + 2;
        erequest.length = 1;
        ASSERT_EQ(ioctl_block_fvm_extend(vfd, &erequest), 0, "Couldn't extend VPartition");

        // We still don't have access to the next slice...
        ASSERT_TRUE(checkNoAccessBlock(vfd, last_block, 2), "");
        ASSERT_TRUE(checkNoAccessBlock(vfd, last_block + 1, 1), "");

        // But we have access to the slice we asked for!
        size_t requested_block = (erequest.offset * slice_size) / info.block_size;
        ASSERT_TRUE(checkWriteReadBlock(vfd, requested_block, 1), "");

        vparts[i].slices_used++;
        vparts[i].last_slice = erequest.offset;
        i = (i + 1) % kNumVParts;
    }

    for (size_t i = 0; i < kNumVParts; i++) {
        ASSERT_EQ(close(vparts[i].fd), 0, "");
    }

    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");

    // Check that the name matches what we provided
    char name[FVM_NAME_LEN + 1];
    ASSERT_GE(ioctl_block_get_name(vp_fd, name, sizeof(name)), 0, "");
    ASSERT_EQ(memcmp(name, kTestPartName1, strlen(kTestPartName1)), 0, "");
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    mxtl::AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[info.block_size * 2]);
    ASSERT_TRUE(ac.check(), "");

    // Check that we can read from / write to it
    ASSERT_TRUE(checkWrite(vp_fd, 0, info.block_size, buf.get()), "");
    ASSERT_TRUE(checkRead(vp_fd, 0, info.block_size, buf.get()), "");
    ASSERT_EQ(close(vp_fd), 0, "");

    // Check that it still exists after rebinding the driver
    const partition_entry_t entries[] = {
        {kTestPartName1, 1},
    };
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    vp_fd = openVPartition(kTestUniqueGUID, kTestPartGUIDData);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");
    ASSERT_TRUE(checkRead(vp_fd, 0, info.block_size, buf.get()), "");

    // Try extending the vpartition, and checking that the extension persists.
    // This is the last 'accessible' block.
    size_t last_block = (slice_size / info.block_size) - 1;
    ASSERT_TRUE(checkWrite(vp_fd, info.block_size * last_block, info.block_size, &buf[0]), "");
    ASSERT_TRUE(checkRead(vp_fd, info.block_size * last_block, info.block_size, &buf[0]), "");

    // Try writing out of bounds -- check that we don't have access.
    ASSERT_TRUE(checkNoAccessBlock(vp_fd, (slice_size / info.block_size) - 1, 2), "");
    ASSERT_TRUE(checkNoAccessBlock(vp_fd, slice_size / info.block_size, 1), "");
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "Couldn't extend VPartition");

    // Rebind the FVM driver, check the extension has succeeded.
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");

    // Now we can access the next slice...
    ASSERT_TRUE(checkWrite(vp_fd, info.block_size * (last_block + 1),
                           info.block_size, &buf[info.block_size]),
                "");
    ASSERT_TRUE(checkRead(vp_fd, info.block_size * (last_block + 1),
                          info.block_size, &buf[info.block_size]),
                "");
    // ... We can still access the previous slice...
    ASSERT_TRUE(checkRead(vp_fd, info.block_size * last_block,
                          info.block_size, &buf[0]),
                "");
    // ... And we can cross slices
    ASSERT_TRUE(checkRead(vp_fd, info.block_size * last_block,
                          info.block_size * 2, &buf[0]),
                "");

    ASSERT_EQ(close(vp_fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
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
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart
    alloc_req_t request;
    request.slice_count = 6;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");

    // Format the VPart as minfs
    char partition_path[PATH_MAX];
    snprintf(partition_path, sizeof(partition_path), "%s/%s-p-1/block",
             fvm_driver, kTestPartName1);
    ASSERT_EQ(mkfs(partition_path, DISK_FORMAT_MINFS, launch_stdio_sync,
                   &default_mkfs_options),
              MX_OK, "");

    // Mount the VPart
    const char* mount_path = "/tmp/minfs_test_mountpath";
    ASSERT_EQ(mkdir(mount_path, 0666), 0, "");
    ASSERT_EQ(mount(vp_fd, mount_path, DISK_FORMAT_MINFS, &default_mount_options,
                    launch_stdio_async),
              MX_OK, "");

    // Verify that the mount was successful
    int rootfd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(rootfd, 0, "");
    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* out = reinterpret_cast<vfs_query_info_t*>(buf);
    ASSERT_EQ(ioctl_vfs_query_fs(rootfd, out, sizeof(buf) - 1),
              static_cast<ssize_t>(sizeof(vfs_query_info_t) + strlen("minfs")),
              "Failed to query filesystem");
    ASSERT_EQ(strcmp("minfs", out->name), 0, "Unexpected filesystem mounted");

    // Verify that MinFS does not try to use more of the VPartition than
    // was originally allocated.
    ASSERT_LE(out->total_bytes, slice_size * request.slice_count, "");

    // Clean up
    ASSERT_EQ(close(rootfd), 0, "");
    ASSERT_EQ(umount(mount_path), MX_OK, "");
    ASSERT_EQ(rmdir(mount_path), 0, "");
    ASSERT_EQ(close(fd), 0, "");
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
    size_t disk_size = 512 * (1 << 20);
    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdisk_fd, 0, "");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart (writes to backup)
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");

    // Extend the vpart (writes to primary)
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "");
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * 2, "");

    // Initial slice access
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, 0, 1), "");
    // Extended slice access
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, slice_size / info.block_size, 1), "");

    ASSERT_EQ(close(vp_fd), 0, "");

    // Corrupt the (backup) metadata and rebind.
    // The 'primary' was the last one written, so it'll be used.
    off_t off = fvm::BackupStart(disk_size, slice_size);
    uint8_t buf[FVM_BLOCK_SIZE];
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off, "");
    ASSERT_EQ(read(ramdisk_fd, buf, sizeof(buf)), sizeof(buf), "");
    // Modify an arbitrary byte (not the magic bits; we still want it to mount!)
    buf[128]++;
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off, "");
    ASSERT_EQ(write(ramdisk_fd, buf, sizeof(buf)), sizeof(buf), "");

    const partition_entry_t entries[] = {
        {kTestPartName1, 1},
    };
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");
    vp_fd = openVPartition(kTestUniqueGUID, kTestPartGUIDData);
    ASSERT_GT(vp_fd, 0, "Couldn't re-open Data VPart");

    // The slice extension is still accessible.
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, 0, 1), "");
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, slice_size / info.block_size, 1), "");

    // Clean up
    ASSERT_EQ(close(vp_fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(ramdisk_fd), 0, "");
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

static bool TestCorruptionRegression(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdisk_fd, 0, "");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart (writes to backup)
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");

    // Extend the vpart (writes to primary)
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "");
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * 2, "");

    // Initial slice access
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, 0, 1), "");
    // Extended slice access
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, slice_size / info.block_size, 1), "");

    ASSERT_EQ(close(vp_fd), 0, "");

    // Corrupt the (primary) metadata and rebind.
    // The 'primary' was the last one written, so the backup will be used.
    off_t off = 0;
    uint8_t buf[FVM_BLOCK_SIZE];
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off, "");
    ASSERT_EQ(read(ramdisk_fd, buf, sizeof(buf)), sizeof(buf), "");
    buf[128]++;
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off, "");
    ASSERT_EQ(write(ramdisk_fd, buf, sizeof(buf)), sizeof(buf), "");

    const partition_entry_t entries[] = {
        {kTestPartName1, 1},
    };
    fd = FVMRebind(fd, ramdisk_path, entries, 1);
    ASSERT_GT(fd, 0, "Failed to rebind FVM driver");
    vp_fd = openVPartition(kTestUniqueGUID, kTestPartGUIDData);
    ASSERT_GT(vp_fd, 0, "");

    // The slice extension is no longer accessible
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, 0, 1), "");
    ASSERT_TRUE(checkNoAccessBlock(vp_fd, slice_size / info.block_size, 1), "");

    // Clean up
    ASSERT_EQ(close(vp_fd), 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(ramdisk_fd), 0, "");
    ASSERT_EQ(EndFVMTest(ramdisk_path), 0, "unmounting FVM");
    END_TEST;
}

static bool TestCorruptionUnrecoverable(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    char fvm_driver[PATH_MAX];
    ASSERT_EQ(StartFVMTest(512, 1 << 20, 64lu * (1 << 20), ramdisk_path, fvm_driver), 0, "error mounting FVM");
    size_t disk_size = 512 * (1 << 20);
    int ramdisk_fd = open(ramdisk_path, O_RDWR);
    ASSERT_GT(ramdisk_fd, 0, "");

    int fd = open(fvm_driver, O_RDWR);
    ASSERT_GT(fd, 0, "");
    fvm_info_t fvm_info;
    ASSERT_GT(ioctl_block_fvm_query(fd, &fvm_info), 0, "");
    size_t slice_size = fvm_info.slice_size;

    // Allocate one VPart (writes to backup)
    alloc_req_t request;
    request.slice_count = 1;
    memcpy(request.guid, kTestUniqueGUID, GUID_LEN);
    strcpy(request.name, kTestPartName1);
    memcpy(request.type, kTestPartGUIDData, GUID_LEN);
    int vp_fd;
    ASSERT_TRUE(fvmAllocHelper(fd, &request, &vp_fd), "");

    // Extend the vpart (writes to primary)
    extend_request_t erequest;
    erequest.offset = 1;
    erequest.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(vp_fd, &erequest), 0, "");
    block_info_t info;
    ASSERT_GE(ioctl_block_get_info(vp_fd, &info), 0, "");
    ASSERT_EQ(info.block_count * info.block_size, slice_size * 2, "");

    // Initial slice access
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, 0, 1), "");
    // Extended slice access
    ASSERT_TRUE(checkWriteReadBlock(vp_fd, slice_size / info.block_size, 1), "");

    ASSERT_EQ(close(vp_fd), 0, "");

    // Corrupt both copies of the metadata.
    // The 'primary' was the last one written, so the backup will be used.
    off_t off = 0;
    uint8_t buf[FVM_BLOCK_SIZE];
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off, "");
    ASSERT_EQ(read(ramdisk_fd, buf, sizeof(buf)), sizeof(buf), "");
    buf[128]++;
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off, "");
    ASSERT_EQ(write(ramdisk_fd, buf, sizeof(buf)), sizeof(buf), "");
    off = fvm::BackupStart(disk_size, slice_size);
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off, "");
    ASSERT_EQ(read(ramdisk_fd, buf, sizeof(buf)), sizeof(buf), "");
    buf[128]++;
    ASSERT_EQ(lseek(ramdisk_fd, off, SEEK_SET), off, "");
    ASSERT_EQ(write(ramdisk_fd, buf, sizeof(buf)), sizeof(buf), "");

    const partition_entry_t entries[] = {
        {kTestPartName1, 1},
    };
    ASSERT_LT(FVMRebind(fd, ramdisk_path, entries, 1), 0, "FVM Should have failed to rebind");

    // Clean up
    ASSERT_EQ(close(ramdisk_fd), 0, "");
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
RUN_TEST_MEDIUM(TestVPartitionShrink)
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

// TODO(smklein): Implement the following tests
// RUN_TEST_MEDIUM(TestExtendShrinkMultithreaded)
// RUN_TEST_MEDIUM(TestPersistenceMultithreaded)
END_TEST_CASE(fvm_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
