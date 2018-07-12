// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <threads.h>
#include <unistd.h>

#include <block-client/client.h>
#include <fs-management/ramdisk.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/syscalls.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/mapped-vmo.h>
#include <sync/completion.h>
#include <unittest/unittest.h>
#include <lib/zx/time.h>

#define RAMCTL_PATH "/dev/misc/ramctl"

namespace tests {

static int get_ramdisk(uint64_t blk_size, uint64_t blk_count) {
    char ramdisk_path[PATH_MAX];
    if (create_ramdisk(blk_size, blk_count, ramdisk_path)) {
        return -1;
    }

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");
    return fd;
}

static bool ramdisk_test_wait_for_device(void) {
    BEGIN_TEST;

    EXPECT_EQ(wait_for_device("/", ZX_SEC(1)), ZX_ERR_BAD_PATH);

    char path[PATH_MAX];
    char mod[PATH_MAX];
    if (create_ramdisk(512, 64, path)) {
        return -1;
    }

    // Null path/zero timeout
    EXPECT_EQ(wait_for_device(path, 0), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(wait_for_device(nullptr, ZX_SEC(1)), ZX_ERR_INVALID_ARGS);

    // Trailing slash
    snprintf(mod, sizeof(mod), "%s/", path);
    EXPECT_EQ(wait_for_device(mod, ZX_SEC(1)), ZX_OK);

    // Repeated slashes/empty path segment
    char* sep = strrchr(path, '/');
    ASSERT_NONNULL(sep);
    size_t off = sep - path;
    snprintf(&mod[off], sizeof(mod) - off, "/%s", sep);
    printf("%s\n", mod);
    EXPECT_EQ(wait_for_device(mod, ZX_SEC(1)), ZX_OK);

    // Valid
    EXPECT_EQ(wait_for_device(path, ZX_SEC(1)), ZX_OK);

    ASSERT_GE(destroy_ramdisk(path), 0, "Could not destroy ramdisk device");

    END_TEST;
}

static bool ramdisk_test_simple(void) {
    uint8_t buf[PAGE_SIZE];
    uint8_t out[PAGE_SIZE];

    BEGIN_TEST;
    int fd = get_ramdisk(PAGE_SIZE / 2, 512);
    memset(buf, 'a', sizeof(buf));
    memset(out, 0, sizeof(out));

    // Write a page and a half
    ASSERT_EQ(write(fd, buf, sizeof(buf)), (ssize_t)sizeof(buf));
    ASSERT_EQ(write(fd, buf, sizeof(buf) / 2), (ssize_t) (sizeof(buf) / 2));

    // Seek to the start of the device and read the contents
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, out, sizeof(out)), (ssize_t)sizeof(out));
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    close(fd);
    END_TEST;
}

static bool ramdisk_test_vmo(void) {
    BEGIN_TEST;

    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(256 * PAGE_SIZE, 0, &vmo), ZX_OK);

    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(create_ramdisk_from_vmo(vmo, ramdisk_path), 0);

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");

    uint8_t buf[PAGE_SIZE * 2];
    uint8_t out[PAGE_SIZE * 2];
    memset(buf, 'a', sizeof(buf));
    memset(out, 0, sizeof(out));

    EXPECT_EQ(write(fd, buf, sizeof(buf)), (ssize_t)sizeof(buf));
    EXPECT_EQ(write(fd, buf, sizeof(buf) / 2), (ssize_t)(sizeof(buf) / 2));

    // Seek to the start of the device and read the contents
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    EXPECT_EQ(read(fd, out, sizeof(out)), (ssize_t) sizeof(out));
    EXPECT_EQ(memcmp(out, buf, sizeof(out)), 0);

    EXPECT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    close(fd);

    END_TEST;
}

// This test creates a ramdisk, verifies it is visible in the filesystem
// (where we expect it to be!) and verifies that it is removed when we
// "unplug" the device.
static bool ramdisk_test_filesystem(void) {
    BEGIN_TEST;

    // Make a ramdisk
    char ramdisk_path[PATH_MAX];
    if (create_ramdisk(PAGE_SIZE / 2, 512, ramdisk_path)) {
        return -1;
    }

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");

    // Ramdisk name is of the form: ".../NAME/block"
    // Extract "NAME".
    const char* name_end = strrchr(ramdisk_path, '/');
    const char* name_start = name_end - 1;
    while (*(name_start - 1) != '/') name_start--;
    char name[NAME_MAX];
    memcpy(name, name_start, name_end - name_start);
    name[name_end - name_start] = 0;

    // Verify the ramdisk name
    char out[sizeof(name)];
    ASSERT_EQ(ioctl_block_get_name(fd, out, sizeof(out)), (ssize_t) strlen(name));
    ASSERT_EQ(strncmp(out, name, strlen(name)), 0, "Unexpected ramdisk name");

    // Find the name of the ramdisk under "/dev/class/block", since it is a block device.
    // Be slightly more lenient with errors during this section, since we might be poking
    // block devices that don't belong to us.
    char blockpath[PATH_MAX];
    strcpy(blockpath, "/dev/class/block/");
    DIR* dir = opendir(blockpath);
    ASSERT_NONNULL(dir);

    typedef struct watcher_args {
        const char* expected_name;
        char* blockpath;
        bool found;
    } watcher_args_t;

    watcher_args_t args;
    args.expected_name = name;
    args.blockpath = blockpath;
    args.found = false;

    auto cb = [](int dirfd, int event, const char* fn, void* cookie) {
        watcher_args_t* args = static_cast<watcher_args_t*>(cookie);
        if (event == WATCH_EVENT_ADD_FILE) {
            fbl::unique_fd fd(openat(dirfd, fn, O_RDONLY));
            if (!fd) {
                return ZX_OK;
            }
            char out[PATH_MAX];
            if ((ioctl_block_get_name(fd.get(), out, sizeof(out)) == (ssize_t)
                 strlen(args->expected_name)) &&
                strncmp(out, args->expected_name, strlen(args->expected_name)) == 0) {
                // Found a device under /dev/class/block/XYZ with the name of the
                // ramdisk we originally created.
                strncat(args->blockpath, fn, sizeof(blockpath) - (strlen(args->blockpath) + 1));
                args->found = true;
                return ZX_ERR_STOP;
            }
        }
        return ZX_OK;
    };

    zx_time_t deadline = zx_deadline_after(ZX_SEC(3));
    ASSERT_EQ(fdio_watch_directory(dirfd(dir), cb, deadline, &args), ZX_ERR_STOP);
    ASSERT_TRUE(args.found);
    ASSERT_EQ(closedir(dir), 0, "Could not close /dev/class/block");

    // Check dev block is accessible before destruction
    int devfd = open(blockpath, O_RDONLY);
    ASSERT_GE(devfd, 0, "Ramdisk is not visible in /dev/class/block");
    ASSERT_EQ(close(devfd), 0);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0, "Could not close ramdisk device");

    // Now that we've unlinked the ramdisk, we should notice that it doesn't appear
    // under /dev/class/block.
    ASSERT_EQ(open(blockpath, O_RDONLY), -1, "Ramdisk is visible in /dev after destruction");

    END_TEST;
}

static bool ramdisk_test_rebind(void) {
    BEGIN_TEST;

    // Make a ramdisk
    char ramdisk_path[PATH_MAX];
    if (create_ramdisk(PAGE_SIZE / 2, 512, ramdisk_path)) {
        return -1;
    }

    int fd = open(ramdisk_path, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramdisk device");

    // Rebind the ramdisk driver
    ASSERT_EQ(ioctl_block_rr_part(fd), 0);
    ASSERT_EQ(wait_for_device(ramdisk_path, ZX_SEC(3)), ZX_OK);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0, "Could not close ramdisk device");

    END_TEST;
}

bool ramdisk_test_bad_requests(void) {
    uint8_t buf[PAGE_SIZE];

    BEGIN_TEST;
    int fd = get_ramdisk(PAGE_SIZE, 512);
    memset(buf, 'a', sizeof(buf));

    // Read / write non-multiples of the block size
    ASSERT_EQ(write(fd, buf, PAGE_SIZE - 1), -1);
    ASSERT_EQ(errno, EINVAL);
    ASSERT_EQ(write(fd, buf, PAGE_SIZE / 2), -1);
    ASSERT_EQ(errno, EINVAL);
    ASSERT_EQ(read(fd, buf, PAGE_SIZE - 1), -1);
    ASSERT_EQ(errno, EINVAL);
    ASSERT_EQ(read(fd, buf, PAGE_SIZE / 2), -1);
    ASSERT_EQ(errno, EINVAL);

    // Read / write from unaligned offset
    ASSERT_EQ(lseek(fd, 1, SEEK_SET), 1);
    ASSERT_EQ(write(fd, buf, PAGE_SIZE), -1);
    ASSERT_EQ(errno, EINVAL);
    ASSERT_EQ(read(fd, buf, PAGE_SIZE), -1);
    ASSERT_EQ(errno, EINVAL);

    // Read / write at end of device
    off_t offset = PAGE_SIZE * 512;
    ASSERT_EQ(lseek(fd, offset, SEEK_SET), offset);
    ASSERT_EQ(write(fd, buf, PAGE_SIZE), -1);
    ASSERT_EQ(read(fd, buf, PAGE_SIZE), -1);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    close(fd);
    END_TEST;
}

bool ramdisk_test_release_during_access(void) {
    BEGIN_TEST;
    int fd = get_ramdisk(PAGE_SIZE, 512);

    // Spin up a background thread to repeatedly access
    // the first few blocks.
    auto bg_thread = [](void* arg) {
        int fd = *reinterpret_cast<int*>(arg);
        while (true) {
            uint8_t in[8192];
            memset(in, 'a', sizeof(in));
            if (write(fd, in, sizeof(in)) != static_cast<ssize_t>(sizeof(in))) {
                return 0;
            }
            uint8_t out[8192];
            memset(out, 0, sizeof(out));
            lseek(fd, 0, SEEK_SET);
            if (read(fd, out, sizeof(out)) != static_cast<ssize_t>(sizeof(out))) {
                return 0;
            }
            // If we DID manage to read it, then the data should be valid...
            if (memcmp(in, out, sizeof(in)) != 0) {
                return -1;
            }
        }
    };

    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, bg_thread, &fd), thrd_success);
    // Let the background thread warm up a little bit...
    usleep(10000);
    // ... and close the entire ramdisk from undearneath it!
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");

    int res;
    ASSERT_EQ(thrd_join(thread, &res), thrd_success);
    ASSERT_EQ(res, 0, "Background thread failed");
    close(fd);
    END_TEST;
}

bool ramdisk_test_release_during_fifo_access(void) {
    BEGIN_TEST;
    int fd = get_ramdisk(PAGE_SIZE, 512);

    // Set up fifo, txn, client, vmo...
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    groupid_t group = 0;
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);
    uint64_t vmo_size = PAGE_SIZE * 3;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(vmo_size, 0, &vmo), ZX_OK, "Failed to create VMO");
    zx_handle_t xfer_vmo;
    ASSERT_EQ(zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &xfer_vmo), ZX_OK);
    vmoid_t vmoid;
    expected = sizeof(vmoid_t);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &vmoid), expected,
              "Failed to attach vmo");
    block_fifo_request_t request;
    request.group      = group;
    request.vmoid      = vmoid;
    request.opcode     = BLOCKIO_WRITE;
    request.length     = 1;
    request.vmo_offset = 0;
    request.dev_offset = 0;

    typedef struct thread_args {
        block_fifo_request_t* request;
        fifo_client_t* client;
    } thread_args_t;

    // Spin up a background thread to repeatedly access
    // the first few blocks.
    auto bg_thread = [](void* arg) {
        thread_args_t* ta = reinterpret_cast<thread_args_t*>(arg);
        zx_status_t status;
        while ((status = block_fifo_txn(ta->client, ta->request, 1)) == ZX_OK) {}
        return (status == ZX_ERR_BAD_STATE) ? 0 : -1;
    };

    thread_args_t args;
    args.request = &request;
    args.client = client;

    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, bg_thread, (void*)&args), thrd_success);
    // Let the background thread warm up a little bit...
    usleep(10000);
    // ... and close the entire ramdisk from undearneath it!
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");

    int res;
    ASSERT_EQ(thrd_join(thread, &res), thrd_success);
    ASSERT_EQ(res, 0, "Background thread failed");
    close(fd);
    END_TEST;
}

bool ramdisk_test_multiple(void) {
    uint8_t buf[PAGE_SIZE];
    uint8_t out[PAGE_SIZE];

    BEGIN_TEST;
    int fd1 = get_ramdisk(PAGE_SIZE, 512);
    int fd2 = get_ramdisk(PAGE_SIZE, 512);

    // Write 'a' to fd1, write 'b', to fd2
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(write(fd1, buf, sizeof(buf)), (ssize_t) sizeof(buf));
    memset(buf, 'b', sizeof(buf));
    ASSERT_EQ(write(fd2, buf, sizeof(buf)), (ssize_t) sizeof(buf));

    ASSERT_EQ(lseek(fd1, 0, SEEK_SET), 0);
    ASSERT_EQ(lseek(fd2, 0, SEEK_SET), 0);

    // Read 'b' from fd2, read 'a' from fd1
    ASSERT_EQ(read(fd2, out, sizeof(buf)), (ssize_t) sizeof(buf));
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0);
    ASSERT_GE(ioctl_ramdisk_unlink(fd2), 0, "Could not unlink ramdisk device");
    close(fd2);

    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(read(fd1, out, sizeof(buf)), (ssize_t) sizeof(buf));
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0);
    ASSERT_GE(ioctl_ramdisk_unlink(fd1), 0, "Could not unlink ramdisk device");
    close(fd1);

    END_TEST;
}

bool ramdisk_test_fifo_no_op(void) {
    // Get a FIFO connection to a ramdisk and immediately close it
    BEGIN_TEST;
    int fd = get_ramdisk(PAGE_SIZE / 2, 512);
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(ioctl_block_fifo_close(fd), ZX_OK, "Failed to close fifo");
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO after closing");
    ASSERT_EQ(ioctl_block_fifo_close(fd), ZX_OK, "Failed to close fifo");
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    close(fd);
    END_TEST;
}

static void fill_random(uint8_t* buf, uint64_t size) {
    for (size_t i = 0; i < size; i++) {
        buf[i] = static_cast<uint8_t>(rand());
    }
}

bool ramdisk_test_fifo_basic(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the ramdisk
    int fd = get_ramdisk(PAGE_SIZE, 512);

    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    groupid_t group = 0;

    // Create an arbitrary VMO, fill it with some stuff
    uint64_t vmo_size = PAGE_SIZE * 3;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(vmo_size, 0, &vmo), ZX_OK, "Failed to create VMO");
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[vmo_size]);
    ASSERT_TRUE(ac.check());
    fill_random(buf.get(), vmo_size);

    ASSERT_EQ(zx_vmo_write(vmo, buf.get(), 0, vmo_size), ZX_OK);

    // Send a handle to the vmo to the block device, get a vmoid which identifies it
    vmoid_t vmoid;
    expected = sizeof(vmoid_t);
    zx_handle_t xfer_vmo;
    ASSERT_EQ(zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &xfer_vmo), ZX_OK);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &vmoid), expected,
              "Failed to attach vmo");

    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);

    // Batch write the VMO to the ramdisk
    // Split it into two requests, spread across the disk
    block_fifo_request_t requests[2];
    requests[0].group      = group;
    requests[0].vmoid      = vmoid;
    requests[0].opcode     = BLOCKIO_WRITE;
    requests[0].length     = 1;
    requests[0].vmo_offset = 0;
    requests[0].dev_offset = 0;

    requests[1].group      = group;
    requests[1].vmoid      = vmoid;
    requests[1].opcode     = BLOCKIO_WRITE;
    requests[1].length     = 2;
    requests[1].vmo_offset = 1;
    requests[1].dev_offset = 100;

    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_OK);

    // Empty the vmo, then read the info we just wrote to the disk
    fbl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[vmo_size]());
    ASSERT_TRUE(ac.check());

    ASSERT_EQ(zx_vmo_write(vmo, out.get(), 0, vmo_size), ZX_OK);
    requests[0].opcode = BLOCKIO_READ;
    requests[1].opcode = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_OK);
    ASSERT_EQ(zx_vmo_read(vmo, out.get(), 0, vmo_size), ZX_OK);
    ASSERT_EQ(memcmp(buf.get(), out.get(), vmo_size), 0, "Read data not equal to written data");

    // Close the current vmo
    requests[0].opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], 1), ZX_OK);

    ASSERT_EQ(zx_handle_close(vmo), ZX_OK);
    block_fifo_release_client(client);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

typedef struct {
    uint64_t vmo_size;
    zx_handle_t vmo;
    vmoid_t vmoid;
    fbl::unique_ptr<uint8_t[]> buf;
} test_vmo_object_t;

// Creates a VMO, fills it with data, and gives it to the block device.
bool create_vmo_helper(int fd, test_vmo_object_t* obj, size_t kBlockSize) {
    obj->vmo_size = kBlockSize + (rand() % 5) * kBlockSize;
    ASSERT_EQ(zx_vmo_create(obj->vmo_size, 0, &obj->vmo), ZX_OK,
              "Failed to create vmo");
    fbl::AllocChecker ac;
    obj->buf.reset(new (&ac) uint8_t[obj->vmo_size]);
    ASSERT_TRUE(ac.check());
    fill_random(obj->buf.get(), obj->vmo_size);
    ASSERT_EQ(zx_vmo_write(obj->vmo, obj->buf.get(), 0, obj->vmo_size),
              ZX_OK, "Failed to write to vmo");

    ssize_t expected = sizeof(vmoid_t);
    zx_handle_t xfer_vmo;
    ASSERT_EQ(zx_handle_duplicate(obj->vmo, ZX_RIGHT_SAME_RIGHTS, &xfer_vmo), ZX_OK,
              "Failed to duplicate vmo");
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &obj->vmoid), expected,
              "Failed to attach vmo");
    return true;
}

// Write all vmos in a striped pattern on disk.
// For objs.size() == 10,
// i = 0 will write vmo block 0, 1, 2, 3... to dev block 0, 10, 20, 30...
// i = 1 will write vmo block 0, 1, 2, 3... to dev block 1, 11, 21, 31...
bool write_striped_vmo_helper(fifo_client_t* client, test_vmo_object_t* obj, size_t i, size_t objs,
                              groupid_t group, size_t kBlockSize) {
    // Make a separate request for each block
    size_t blocks = obj->vmo_size / kBlockSize;
    fbl::AllocChecker ac;
    fbl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[blocks], blocks);
    ASSERT_TRUE(ac.check());
    for (size_t b = 0; b < blocks; b++) {
        requests[b].group      = group;
        requests[b].vmoid      = obj->vmoid;
        requests[b].opcode     = BLOCKIO_WRITE;
        requests[b].length     = 1;
        requests[b].vmo_offset = b;
        requests[b].dev_offset = i + b * objs;
    }
    // Write entire vmos at once
    ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), ZX_OK);
    return true;
}

// Verifies the result from "write_striped_vmo_helper"
bool read_striped_vmo_helper(fifo_client_t* client, test_vmo_object_t* obj, size_t i, size_t objs,
                             groupid_t group, size_t kBlockSize) {
    // First, empty out the VMO
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[obj->vmo_size]());
    ASSERT_TRUE(ac.check());
    ASSERT_EQ(zx_vmo_write(obj->vmo, out.get(), 0, obj->vmo_size), ZX_OK);

    // Next, read to the vmo from the disk
    size_t blocks = obj->vmo_size / kBlockSize;
    fbl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[blocks], blocks);
    ASSERT_TRUE(ac.check());
    for (size_t b = 0; b < blocks; b++) {
        requests[b].group      = group;
        requests[b].vmoid      = obj->vmoid;
        requests[b].opcode     = BLOCKIO_READ;
        requests[b].length     = 1;
        requests[b].vmo_offset = b;
        requests[b].dev_offset = i + b * objs;
    }
    // Read entire vmos at once
    ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), ZX_OK);

    // Finally, write from the vmo to an out buffer, where we can compare
    // the results with the input buffer.
    ASSERT_EQ(zx_vmo_read(obj->vmo, out.get(), 0, obj->vmo_size), ZX_OK);
    ASSERT_EQ(memcmp(obj->buf.get(), out.get(), obj->vmo_size), 0,
              "Read data not equal to written data");
    return true;
}

// Tears down an object created by "create_vmo_helper".
bool close_vmo_helper(fifo_client_t* client, test_vmo_object_t* obj, groupid_t group) {
    block_fifo_request_t request;
    request.group = group;
    request.vmoid = obj->vmoid;
    request.opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_OK);
    ASSERT_EQ(zx_handle_close(obj->vmo), ZX_OK);
    return true;
}

bool ramdisk_test_fifo_multiple_vmo(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    groupid_t group = 0;
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);

    // Create multiple VMOs
    fbl::AllocChecker ac;
    fbl::Array<test_vmo_object_t> objs(new (&ac) test_vmo_object_t[10](), 10);
    ASSERT_TRUE(ac.check());
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], kBlockSize));
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(write_striped_vmo_helper(client, &objs[i], i, objs.size(), group, kBlockSize));
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(read_striped_vmo_helper(client, &objs[i], i, objs.size(), group, kBlockSize));
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(close_vmo_helper(client, &objs[i], group));
    }

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

typedef struct {
    test_vmo_object_t* obj;
    size_t i;
    size_t objs;
    int fd;
    fifo_client_t* client;
    groupid_t group;
    size_t kBlockSize;
} test_thread_arg_t;

int fifo_vmo_thread(void* arg) {
    test_thread_arg_t* fifoarg = (test_thread_arg_t*) arg;
    test_vmo_object_t* obj = fifoarg->obj;
    size_t i = fifoarg->i;
    size_t objs = fifoarg->objs;
    int fd = fifoarg->fd;
    fifo_client_t* client = fifoarg->client;
    groupid_t group = fifoarg->group;
    size_t kBlockSize = fifoarg->kBlockSize;

    ASSERT_TRUE(create_vmo_helper(fd, obj, kBlockSize));
    ASSERT_TRUE(write_striped_vmo_helper(client, obj, i, objs, group, kBlockSize));
    ASSERT_TRUE(read_striped_vmo_helper(client, obj, i, objs, group, kBlockSize));
    ASSERT_TRUE(close_vmo_helper(client, obj, group));
    return 0;
}

bool ramdisk_test_fifo_multiple_vmo_multithreaded(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);

    // Create multiple VMOs
    size_t num_threads = MAX_TXN_GROUP_COUNT;
    fbl::AllocChecker ac;
    fbl::Array<test_vmo_object_t> objs(new (&ac) test_vmo_object_t[num_threads](), num_threads);
    ASSERT_TRUE(ac.check());

    fbl::Array<thrd_t> threads(new (&ac) thrd_t[num_threads](), num_threads);
    ASSERT_TRUE(ac.check());

    fbl::Array<test_thread_arg_t> thread_args(new (&ac) test_thread_arg_t[num_threads](),
                                               num_threads);
    ASSERT_TRUE(ac.check());

    for (size_t i = 0; i < num_threads; i++) {
        // Yes, this does create a bunch of duplicate fields, but it's an easy way to
        // transfer some data without creating global variables.
        thread_args[i].obj = &objs[i];
        thread_args[i].i = i;
        thread_args[i].objs = objs.size();
        thread_args[i].fd = fd;
        thread_args[i].client = client;
        thread_args[i].group = static_cast<groupid_t>(i);
        thread_args[i].kBlockSize = kBlockSize;
        ASSERT_EQ(thrd_create(&threads[i], fifo_vmo_thread, &thread_args[i]),
                  thrd_success);
    }

    for (size_t i = 0; i < num_threads; i++) {
        int res;
        ASSERT_EQ(thrd_join(threads[i], &res), thrd_success);
        ASSERT_EQ(res, 0);
    }

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_unclean_shutdown(void) {
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), ZX_ERR_ALREADY_BOUND,
              "Expected fifo to already be bound");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);
    groupid_t group = 0;

    // Create multiple VMOs
    fbl::AllocChecker ac;
    fbl::Array<test_vmo_object_t> objs(new (&ac) test_vmo_object_t[10](), 10);
    ASSERT_TRUE(ac.check());
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], kBlockSize));
    }

    // Now that we've set up the connection for a few VMOs, shut down the fifo
    ASSERT_EQ(zx_handle_close(fifo), ZX_OK);

    // Attempting to batch any operations to the fifo should fail
    block_fifo_request_t request;
    request.group = group;
    request.vmoid = objs[0].vmoid;
    request.opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_NE(block_fifo_txn(client, &request, 1), ZX_OK,
              "Expected operation to fail after closing FIFO");

    // Free the dead client
    block_fifo_release_client(client);

    // Give the block server a moment to realize our side of the fifo has been closed
    usleep(10000);

    // The block server should still be functioning. We should be able to re-bind to it
    expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], kBlockSize));
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(write_striped_vmo_helper(client, &objs[i], i, objs.size(), group, kBlockSize));
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(read_striped_vmo_helper(client, &objs[i], i, objs.size(), group, kBlockSize));
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(close_vmo_helper(client, &objs[i], group));
    }

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_large_ops_count(void) {
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize));

    for (size_t num_ops = 1; num_ops <= 32; num_ops++) {
        groupid_t group = 0;

        fbl::AllocChecker ac;
        fbl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[num_ops](),
                                                   num_ops);
        ASSERT_TRUE(ac.check());

        for (size_t b = 0; b < num_ops; b++) {
            requests[b].group      = group;
            requests[b].vmoid      = obj.vmoid;
            requests[b].opcode     = BLOCKIO_WRITE;
            requests[b].length     = 1;
            requests[b].vmo_offset = 0;
            requests[b].dev_offset = 0;
        }

        ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), ZX_OK);
    }

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_large_ops_count_shutdown(void) {
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize));

    const size_t kNumOps = BLOCK_FIFO_MAX_DEPTH;
    groupid_t group = 0;

    fbl::AllocChecker ac;
    fbl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[kNumOps](),
                                              kNumOps);
    ASSERT_TRUE(ac.check());

    for (size_t b = 0; b < kNumOps; b++) {
        requests[b].group      = group;
        requests[b].vmoid      = obj.vmoid;
        requests[b].opcode     = BLOCKIO_WRITE | BLOCKIO_BARRIER_BEFORE |
                                 BLOCKIO_GROUP_ITEM;
        requests[b].length     = 1;
        requests[b].vmo_offset = 0;
        requests[b].dev_offset = b;
    }

    // Enqueue multiple barrier-based operations without waiting
    // for completion. The intention here is for the block device
    // server to be busy processing multiple pending operations
    // when the FIFO is suddenly closed, causing "server termination
    // with pending work".
    //
    // It's obviously hit-or-miss whether the server will actually
    // be processing work when we shut down the fifo, but run in a
    // loop, this test was able to trigger deadlocks in a buggy
    // version of the server; as a consequence, it is preserved
    // to help detect regressions.
    size_t actual;
    ZX_ASSERT(zx_fifo_write(fifo, sizeof(block_fifo_request_t), &requests[0],
                                requests.size(), &actual) == ZX_OK);
    usleep(100);
    ZX_ASSERT(zx_handle_close(fifo) == ZX_OK);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_intermediate_op_failure(void) {
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);
    groupid_t group = 0;

    constexpr size_t kRequestCount = 3;
    constexpr size_t kBufferSize = kRequestCount * kBlockSize;

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBufferSize));

    // Store the original value of the VMO
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> originalbuf;
    originalbuf.reset(new (&ac) uint8_t[kBufferSize]);
    ASSERT_TRUE(ac.check());

    ASSERT_EQ(zx_vmo_read(obj.vmo, originalbuf.get(), 0, kBufferSize), ZX_OK);

    // Test that we can use regular transactions (writing)
    block_fifo_request_t requests[kRequestCount];
    for (size_t i = 0; i < fbl::count_of(requests); i++) {
        requests[i].group      = group;
        requests[i].vmoid      = obj.vmoid;
        requests[i].opcode     = BLOCKIO_WRITE;
        requests[i].length     = 1;
        requests[i].vmo_offset = i;
        requests[i].dev_offset = i;
    }
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_OK);

    fbl::unique_ptr<uint8_t[]> tmpbuf;
    tmpbuf.reset(new (&ac) uint8_t[kBufferSize]);
    ASSERT_TRUE(ac.check());

    for (size_t bad_arg = 0; bad_arg < fbl::count_of(requests); bad_arg++) {
        // Empty out the VMO so we can test reading it
        memset(tmpbuf.get(), 0, kBufferSize);
        ASSERT_EQ(zx_vmo_write(obj.vmo, tmpbuf.get(), 0, kBufferSize), ZX_OK);

        // Test that invalid intermediate operations cause:
        // - Previous operations to continue anyway
        // - Later operations to fail
        for (size_t i = 0; i < fbl::count_of(requests); i++) {
            requests[i].group      = group;
            requests[i].vmoid      = obj.vmoid;
            requests[i].opcode     = BLOCKIO_READ;
            requests[i].length     = 1;
            requests[i].vmo_offset = i;
            requests[i].dev_offset = i;
        }
        // Inserting "bad argument".
        requests[bad_arg].length = 0;
        ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)),
                  ZX_ERR_INVALID_ARGS);

        // Test that all operations up the bad argument completed, but the later
        // ones did not.
        ASSERT_EQ(zx_vmo_read(obj.vmo, tmpbuf.get(), 0, kBufferSize), ZX_OK);

        // First few (successful) operations
        ASSERT_EQ(memcmp(tmpbuf.get(), originalbuf.get(), kBlockSize * bad_arg), 0);
        // Later (failed) operations
        for (size_t i = kBlockSize * (bad_arg + 1); i < kBufferSize; i++) {
            ASSERT_EQ(tmpbuf[i], 0);
        }
    }

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_bad_client_vmoid(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);
    groupid_t group = 0;

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize));

    // Bad request: Writing to the wrong vmoid
    block_fifo_request_t request;
    request.group      = group;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid + 5);
    request.opcode     = BLOCKIO_WRITE;
    request.length     = 1;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_IO, "Expected IO error with bad vmoid");

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_bad_client_unaligned_request(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);
    groupid_t group = 0;

    // Create a vmo of at least size "kBlockSize * 2", since we'll
    // be reading "kBlockSize" bytes from an offset below, and we want it
    // to fit within the bounds of the VMO.
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize * 2));

    block_fifo_request_t request;
    request.group      = group;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;

    // Send a request that has zero length
    request.length     = 0;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_INVALID_ARGS);

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_bad_client_overflow(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the ramdisk
    const uint64_t kBlockSize = PAGE_SIZE;
    const uint64_t kBlockCount = 1 << 18;
    int fd = get_ramdisk(kBlockSize, kBlockCount);

    // Create a connection to the ramdisk
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);
    groupid_t group = 0;

    // Create a vmo of at least size "kBlockSize * 2", since we'll
    // be reading "kBlockSize" bytes from an offset below, and we want it
    // to fit within the bounds of the VMO.
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize * 2));

    block_fifo_request_t request;
    request.group      = group;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;

    // Send a request that is barely out-of-bounds for the device
    request.length     = 1;
    request.vmo_offset = 0;
    request.dev_offset = kBlockCount;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_OUT_OF_RANGE);

    // Send a request that is half out-of-bounds for the device
    request.length     = 2;
    request.vmo_offset = 0;
    request.dev_offset = kBlockCount - 1;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_OUT_OF_RANGE);

    // Send a request that is very out-of-bounds for the device
    request.length     = 1;
    request.vmo_offset = 0;
    request.dev_offset = kBlockCount + 1;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_OUT_OF_RANGE);

    // Send a request that tries to overflow the VMO
    request.length     = 2;
    request.vmo_offset = fbl::numeric_limits<uint64_t>::max();
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_OUT_OF_RANGE);

    // Send a request that tries to overflow the device
    request.length     = 2;
    request.vmo_offset = 0;
    request.dev_offset = fbl::numeric_limits<uint64_t>::max();
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_OUT_OF_RANGE);

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_bad_client_bad_vmo(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);
    groupid_t group = 0;

    // create a VMO of 1 block, which will round up to PAGE_SIZE
    test_vmo_object_t obj;
    obj.vmo_size = kBlockSize;
    ASSERT_EQ(zx_vmo_create(obj.vmo_size, 0, &obj.vmo), ZX_OK,
              "Failed to create vmo");
    fbl::AllocChecker ac;
    obj.buf.reset(new (&ac) uint8_t[obj.vmo_size]);
    ASSERT_TRUE(ac.check());
    fill_random(obj.buf.get(), obj.vmo_size);
    ASSERT_EQ(zx_vmo_write(obj.vmo, obj.buf.get(), 0, obj.vmo_size),
              ZX_OK, "Failed to write to vmo");
    zx_handle_t xfer_vmo;
    ASSERT_EQ(zx_handle_duplicate(obj.vmo, ZX_RIGHT_SAME_RIGHTS, &xfer_vmo), ZX_OK,
              "Failed to duplicate vmo");
    expected = sizeof(vmoid_t);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &obj.vmoid), expected,
              "Failed to attach vmo");

    // Send a request to write to write 2 blocks -- even though that's larger than the VMO
    block_fifo_request_t request;
    request.group      = group;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;
    request.length     = 2;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_OUT_OF_RANGE);
    // Do the same thing, but for reading
    request.opcode     = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_OUT_OF_RANGE);
    request.length     = 2;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ZX_ERR_OUT_OF_RANGE);

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_sleep_unavailable(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the ramdisk
    int fd = get_ramdisk(PAGE_SIZE, 512);

    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    groupid_t group = 0;

    // Create an arbitrary VMO, fill it with some stuff
    uint64_t vmo_size = PAGE_SIZE * 3;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(vmo_size, 0, &vmo), ZX_OK, "Failed to create VMO");
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[vmo_size]);
    ASSERT_TRUE(ac.check());
    fill_random(buf.get(), vmo_size);

    ASSERT_EQ(zx_vmo_write(vmo, buf.get(), 0, vmo_size), ZX_OK);

    // Send a handle to the vmo to the block device, get a vmoid which identifies it
    vmoid_t vmoid;
    expected = sizeof(vmoid_t);
    zx_handle_t xfer_vmo;
    ASSERT_EQ(zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &xfer_vmo), ZX_OK);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &vmoid), expected,
              "Failed to attach vmo");

    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);

    // Put the ramdisk to sleep after 1 block (complete transaction).
    uint64_t one = 1;
    ASSERT_GE(ioctl_ramdisk_sleep_after(fd, &one), 0);

    // Batch write the VMO to the ramdisk
    // Split it into two requests, spread across the disk
    block_fifo_request_t requests[2];
    requests[0].group = group;
    requests[0].vmoid = vmoid;
    requests[0].opcode = BLOCKIO_WRITE;
    requests[0].length = 1;
    requests[0].vmo_offset = 0;
    requests[0].dev_offset = 0;

    requests[1].group = group;
    requests[1].vmoid = vmoid;
    requests[1].opcode = BLOCKIO_WRITE;
    requests[1].length = 2;
    requests[1].vmo_offset = 1;
    requests[1].dev_offset = 100;

    // Send enough requests for the ramdisk to fall asleep before completing.
    // Other callers (e.g. block_watcher) may also send requests without affecting this test.
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_ERR_UNAVAILABLE);

    ramdisk_blk_counts_t counts;
    ASSERT_GE(ioctl_ramdisk_get_blk_counts(fd, &counts), 0);
    ASSERT_EQ(counts.received, 3);
    ASSERT_EQ(counts.successful, 1);
    ASSERT_EQ(counts.failed, 2);

    // Wake the ramdisk back up
    ASSERT_GE(ioctl_ramdisk_wake_up(fd), 0);
    requests[0].opcode = BLOCKIO_READ;
    requests[1].opcode = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_OK);

    // Put the ramdisk to sleep after 1 block (partial transaction).
    ASSERT_GE(ioctl_ramdisk_sleep_after(fd, &one), 0);

    // Batch write the VMO to the ramdisk.
    // Split it into two requests, spread across the disk.
    requests[0].opcode = BLOCKIO_WRITE;
    requests[0].length = 2;

    requests[1].opcode = BLOCKIO_WRITE;
    requests[1].length = 1;
    requests[1].vmo_offset = 2;

    // Send enough requests for the ramdisk to fall asleep before completing.
    // Other callers (e.g. block_watcher) may also send requests without affecting this test.
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_ERR_UNAVAILABLE);

    ASSERT_GE(ioctl_ramdisk_get_blk_counts(fd, &counts), 0);
    ASSERT_EQ(counts.received, 3);
    ASSERT_EQ(counts.successful, 1);
    ASSERT_EQ(counts.failed, 2);

    // Wake the ramdisk back up
    ASSERT_GE(ioctl_ramdisk_wake_up(fd), 0);
    requests[0].opcode = BLOCKIO_READ;
    requests[1].opcode = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_OK);

    // Close the current vmo
    requests[0].opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], 1), ZX_OK);

    ASSERT_EQ(zx_handle_close(vmo), ZX_OK);
    block_fifo_release_client(client);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

// This thread and its arguments can be used to wake a ramdisk that sleeps with deferred writes.
// The correct calling sequence in the calling thread is:
//   thrd_create(&thread, fifo_wake_thread, &wake);
//   ioctl_ramdisk_sleep_after(wake->fd, &one);
//   completion_signal(&wake.start);
//   block_fifo_txn(client, requests, fbl::count_of(requests));
//   thrd_join(thread, &res);
//
// This order matters!
// * |sleep_after| must be called from the same thread as |fifo_txn| (or they may be reordered, and
//   the txn counts zeroed).
// * The do-while loop below must not be started before |sleep_after| has been called (hence the
//   'start' signal).
// * This thread must not be waiting when the calling thread blocks in |fifo_txn| (i.e. 'start' must
//   have been signaled.)

typedef struct wake_args {
    int fd;
    uint64_t after;
    completion_t start;
    zx_time_t deadline;
} wake_args_t;

static int fifo_wake_thread(void* arg) {
    ssize_t res;

    // Always send a wake-up call; even if we failed to go to sleep.
    wake_args_t* wake = static_cast<wake_args_t*>(arg);
    auto cleanup = fbl::MakeAutoCall([&] { ioctl_ramdisk_wake_up(wake->fd); });

    // Wait for the start-up signal
    zx_status_t rc = completion_wait_deadline(&wake->start, wake->deadline);
    completion_reset(&wake->start);
    if (rc != ZX_OK) {
        return rc;
    }

    // Loop until timeout, |wake_after| txns received, or error getting counts
    ramdisk_blk_counts_t counts;
    do {
        zx::nanosleep(zx::deadline_after(zx::msec(100)));
        if (wake->deadline < zx_clock_get_monotonic()) {
            return ZX_ERR_TIMED_OUT;
        }
        if ((res = ioctl_ramdisk_get_blk_counts(wake->fd, &counts)) < 0) {
            return static_cast<zx_status_t>(res);
        }
    } while (counts.received < wake->after);
    return ZX_OK;
}

bool ramdisk_test_fifo_sleep_deferred(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the ramdisk
    int fd = get_ramdisk(PAGE_SIZE, 512);

    zx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    groupid_t group = 0;

    // Create an arbitrary VMO, fill it with some stuff
    uint64_t vmo_size = PAGE_SIZE * 16;
    fbl::unique_ptr<fzl::MappedVmo> vmo;
    ASSERT_EQ(fzl::MappedVmo::Create(vmo_size, "ramdisk-test", &vmo), ZX_OK);

    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[vmo_size]);
    ASSERT_TRUE(ac.check());
    fill_random(buf.get(), vmo_size);

    ASSERT_EQ(zx_vmo_write(vmo->GetVmo(), buf.get(), 0, vmo_size), ZX_OK);

    // Send a handle to the vmo to the block device, get a vmoid which identifies it
    vmoid_t vmoid;
    expected = sizeof(vmoid_t);
    zx_handle_t xfer_vmo;
    ASSERT_EQ(zx_handle_duplicate(vmo->GetVmo(), ZX_RIGHT_SAME_RIGHTS, &xfer_vmo), ZX_OK);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &vmoid), expected,
              "Failed to attach vmo");

    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), ZX_OK);

    // Create a bunch of requests, some of which are guaranteed to block.
    block_fifo_request_t requests[16];
    for (size_t i = 0; i < fbl::count_of(requests); ++i) {
        requests[i].group = group;
        requests[i].vmoid = vmoid;
        requests[i].opcode = BLOCKIO_WRITE;
        requests[i].length = 1;
        requests[i].vmo_offset = i;
        requests[i].dev_offset = i;
    }

    // Sleep and wake parameters
    uint32_t flags = RAMDISK_FLAG_RESUME_ON_WAKE;
    thrd_t thread;
    wake_args_t wake;
    wake.fd = fd;
    wake.after = fbl::count_of(requests);
    completion_reset(&wake.start);
    wake.deadline = zx_deadline_after(ZX_SEC(3));
    uint64_t blks_before_sleep = 1;
    int res;

    // Send enough requests to put the ramdisk to sleep and then be awoken wake thread. The ordering
    // below matters!  See the comment on |ramdisk_wake_thread| for details.
    ASSERT_EQ(thrd_create(&thread, fifo_wake_thread, &wake), thrd_success);
    ASSERT_GE(ioctl_ramdisk_set_flags(fd, &flags), 0);
    ASSERT_GE(ioctl_ramdisk_sleep_after(fd, &blks_before_sleep), 0);
    completion_signal(&wake.start);
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_OK);
    ASSERT_EQ(thrd_join(thread, &res), thrd_success);

    // Check that the wake thread succeeded.
    ASSERT_EQ(res, 0, "Background thread failed");

    for (size_t i = 0; i < fbl::count_of(requests); ++i) {
        requests[i].opcode = BLOCKIO_READ;
    }

    // Read data we wrote to disk back into the VMO.
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), ZX_OK);

    // Verify that the contents of the vmo match the buffer.
    ASSERT_EQ(memcmp(vmo->GetData(), buf.get(), vmo_size), 0);

    // Now send 1 transaction with the full length of the VMO.
    requests[0].opcode = BLOCKIO_WRITE;
    requests[0].length = 16;
    requests[0].vmo_offset = 0;
    requests[0].dev_offset = 0;

    // Restart the wake thread and put ramdisk to sleep again.
    wake.after = 1;
    completion_reset(&wake.start);
    ASSERT_EQ(thrd_create(&thread, fifo_wake_thread, &wake), thrd_success);
    ASSERT_GE(ioctl_ramdisk_sleep_after(fd, &blks_before_sleep), 0);
    completion_signal(&wake.start);
    ASSERT_EQ(block_fifo_txn(client, &requests[0], 1), ZX_OK);
    ASSERT_EQ(thrd_join(thread, &res), thrd_success);

    // Check the wake thread succeeded, and that the contents of the ramdisk match the buffer.
    ASSERT_EQ(res, 0, "Background thread failed");
    requests[0].opcode = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], 1), ZX_OK);
    ASSERT_EQ(memcmp(vmo->GetData(), buf.get(), vmo_size), 0);

    // Check that we can do I/O normally again.
    requests[0].opcode = BLOCKIO_WRITE;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], 1), ZX_OK);

    // Close the current vmo
    requests[0].opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], 1), ZX_OK);

    block_fifo_release_client(client);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

BEGIN_TEST_CASE(ramdisk_tests)
RUN_TEST_SMALL(ramdisk_test_wait_for_device)
RUN_TEST_SMALL(ramdisk_test_simple)
RUN_TEST_SMALL(ramdisk_test_vmo)
RUN_TEST_SMALL(ramdisk_test_filesystem)
RUN_TEST_SMALL(ramdisk_test_rebind)
RUN_TEST_SMALL(ramdisk_test_bad_requests)
RUN_TEST_SMALL(ramdisk_test_release_during_access)
RUN_TEST_SMALL(ramdisk_test_release_during_fifo_access)
RUN_TEST_SMALL(ramdisk_test_multiple)
RUN_TEST_SMALL(ramdisk_test_fifo_no_op)
RUN_TEST_SMALL(ramdisk_test_fifo_basic)
RUN_TEST_SMALL(ramdisk_test_fifo_multiple_vmo)
RUN_TEST_SMALL(ramdisk_test_fifo_multiple_vmo_multithreaded)
// TODO(smklein): Test ops across different vmos
RUN_TEST_SMALL(ramdisk_test_fifo_unclean_shutdown)
RUN_TEST_SMALL(ramdisk_test_fifo_large_ops_count)
RUN_TEST_SMALL(ramdisk_test_fifo_large_ops_count_shutdown)
RUN_TEST_SMALL(ramdisk_test_fifo_intermediate_op_failure)
RUN_TEST_SMALL(ramdisk_test_fifo_bad_client_vmoid)
RUN_TEST_SMALL(ramdisk_test_fifo_bad_client_unaligned_request)
RUN_TEST_SMALL(ramdisk_test_fifo_bad_client_overflow)
RUN_TEST_SMALL(ramdisk_test_fifo_bad_client_bad_vmo)
RUN_TEST_SMALL(ramdisk_test_fifo_sleep_unavailable)
RUN_TEST_SMALL(ramdisk_test_fifo_sleep_deferred)
END_TEST_CASE(ramdisk_tests)

} // namespace tests
