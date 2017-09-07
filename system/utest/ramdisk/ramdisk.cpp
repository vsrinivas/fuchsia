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
#include <magenta/device/block.h>
#include <magenta/device/ramdisk.h>
#include <magenta/syscalls.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

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

static bool ramdisk_test_simple(void) {
    uint8_t buf[PAGE_SIZE];
    uint8_t out[PAGE_SIZE];

    BEGIN_TEST;
    int fd = get_ramdisk(PAGE_SIZE / 2, 512);
    memset(buf, 'a', sizeof(buf));
    memset(out, 0, sizeof(out));

    // Write a page and a half
    ASSERT_EQ(write(fd, buf, sizeof(buf)), (ssize_t) sizeof(buf));
    ASSERT_EQ(write(fd, buf, sizeof(buf) / 2), (ssize_t) (sizeof(buf) / 2));

    // Seek to the start of the device and read the contents
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, out, sizeof(out)), (ssize_t) sizeof(out));
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0);

    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
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

    bool dev_class_block_found = false;

    struct dirent* de;
    while (!dev_class_block_found && ((de = readdir(dir)) != NULL)) {
        if ((strcmp(de->d_name, ".") == 0) || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        int devfd = openat(dirfd(dir), de->d_name, O_RDONLY);
        if (devfd > 0) {
            if ((ioctl_block_get_name(devfd, out, sizeof(out)) == (ssize_t) strlen(name)) &&
                strncmp(out, name, strlen(name)) == 0) {
                // Found a device under /dev/class/block/XYZ with the name of the
                // ramdisk we originally created.
                strcat(blockpath, de->d_name);
                dev_class_block_found = true;
            }
            close(devfd);
        }
    }
    ASSERT_EQ(closedir(dir), 0, "Could not close /dev/class/block");
    ASSERT_TRUE(dev_class_block_found, "Ramdisk did not appear in /dev/class/block");

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
    // Ensure that the block driver rebinds too.
    char *path_end = strrchr(ramdisk_path, '/');
    ASSERT_EQ(strcmp(path_end, "/block"), 0);
    *path_end = '\0';
    printf("ramdisk_test: [%s] waiting for child [%s]\n", ramdisk_path, "block");
    ASSERT_EQ(wait_for_driver_bind(ramdisk_path, "block"), 0);

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

    // Read / write from beyond end of device
    off_t dev_size = PAGE_SIZE * 512;
    ASSERT_EQ(lseek(fd, dev_size, SEEK_SET), dev_size);
    ASSERT_EQ(write(fd, buf, PAGE_SIZE), 0);
    ASSERT_EQ(read(fd, buf, PAGE_SIZE), 0);

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
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);
    uint64_t vmo_size = PAGE_SIZE * 3;
    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(vmo_size, 0, &vmo), MX_OK, "Failed to create VMO");
    mx_handle_t xfer_vmo;
    ASSERT_EQ(mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo), MX_OK);
    vmoid_t vmoid;
    expected = sizeof(vmoid_t);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &vmoid), expected,
              "Failed to attach vmo");
    block_fifo_request_t request;
    request.txnid      = txnid;
    request.vmoid      = vmoid;
    request.opcode     = BLOCKIO_WRITE;
    request.length     = PAGE_SIZE;
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
        mx_status_t status;
        while ((status = block_fifo_txn(ta->client, ta->request, 1)) == MX_OK) {}
        return (status == MX_ERR_BAD_STATE) ? 0 : -1;
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
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(ioctl_block_fifo_close(fd), MX_OK, "Failed to close fifo");
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO after closing");
    ASSERT_EQ(ioctl_block_fifo_close(fd), MX_OK, "Failed to close fifo");
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
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create an arbitrary VMO, fill it with some stuff
    uint64_t vmo_size = PAGE_SIZE * 3;
    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(vmo_size, 0, &vmo), MX_OK, "Failed to create VMO");
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[vmo_size]);
    ASSERT_TRUE(ac.check());
    fill_random(buf.get(), vmo_size);

    size_t actual;
    ASSERT_EQ(mx_vmo_write(vmo, buf.get(), 0, vmo_size, &actual), MX_OK);
    ASSERT_EQ(actual, vmo_size);

    // Send a handle to the vmo to the block device, get a vmoid which identifies it
    vmoid_t vmoid;
    expected = sizeof(vmoid_t);
    mx_handle_t xfer_vmo;
    ASSERT_EQ(mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo), MX_OK);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &vmoid), expected,
              "Failed to attach vmo");

    // Batch write the VMO to the ramdisk
    // Split it into two requests, spread across the disk
    block_fifo_request_t requests[2];
    requests[0].txnid      = txnid;
    requests[0].vmoid      = vmoid;
    requests[0].opcode     = BLOCKIO_WRITE;
    requests[0].length     = PAGE_SIZE;
    requests[0].vmo_offset = 0;
    requests[0].dev_offset = 0;

    requests[1].txnid      = txnid;
    requests[1].vmoid      = vmoid;
    requests[1].opcode     = BLOCKIO_WRITE;
    requests[1].length     = PAGE_SIZE * 2;
    requests[1].vmo_offset = PAGE_SIZE;
    requests[1].dev_offset = PAGE_SIZE * 100;

    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), MX_OK);

    // Empty the vmo, then read the info we just wrote to the disk
    fbl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[vmo_size]());
    ASSERT_TRUE(ac.check());

    ASSERT_EQ(mx_vmo_write(vmo, out.get(), 0, vmo_size, &actual), MX_OK);
    requests[0].opcode = BLOCKIO_READ;
    requests[1].opcode = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], fbl::count_of(requests)), MX_OK);
    ASSERT_EQ(mx_vmo_read(vmo, out.get(), 0, vmo_size, &actual), MX_OK);
    ASSERT_EQ(memcmp(buf.get(), out.get(), vmo_size), 0, "Read data not equal to written data");

    // Close the current vmo
    requests[0].opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], 1), MX_OK);

    ASSERT_EQ(mx_handle_close(vmo), MX_OK);
    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

typedef struct {
    uint64_t vmo_size;
    mx_handle_t vmo;
    vmoid_t vmoid;
    fbl::unique_ptr<uint8_t[]> buf;
} test_vmo_object_t;

// Creates a VMO, fills it with data, and gives it to the block device.
bool create_vmo_helper(int fd, test_vmo_object_t* obj, size_t kBlockSize) {
    obj->vmo_size = kBlockSize + (rand() % 5) * kBlockSize;
    ASSERT_EQ(mx_vmo_create(obj->vmo_size, 0, &obj->vmo), MX_OK,
              "Failed to create vmo");
    fbl::AllocChecker ac;
    obj->buf.reset(new (&ac) uint8_t[obj->vmo_size]);
    ASSERT_TRUE(ac.check());
    fill_random(obj->buf.get(), obj->vmo_size);
    size_t actual;
    ASSERT_EQ(mx_vmo_write(obj->vmo, obj->buf.get(), 0, obj->vmo_size, &actual),
              MX_OK, "Failed to write to vmo");
    ASSERT_EQ(obj->vmo_size, actual, "Could not write entire VMO");

    ssize_t expected = sizeof(vmoid_t);
    mx_handle_t xfer_vmo;
    ASSERT_EQ(mx_handle_duplicate(obj->vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo), MX_OK,
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
                              txnid_t txnid, size_t kBlockSize) {
    // Make a separate request for each block
    size_t blocks = obj->vmo_size / kBlockSize;
    fbl::AllocChecker ac;
    fbl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[blocks], blocks);
    ASSERT_TRUE(ac.check());
    for (size_t b = 0; b < blocks; b++) {
        requests[b].txnid      = txnid;
        requests[b].vmoid      = obj->vmoid;
        requests[b].opcode     = BLOCKIO_WRITE;
        requests[b].length     = static_cast<uint32_t>(kBlockSize);
        requests[b].vmo_offset = b * kBlockSize;
        requests[b].dev_offset = i * kBlockSize + b * (kBlockSize * objs);
    }
    // Write entire vmos at once
    ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), MX_OK);
    return true;
}

// Verifies the result from "write_striped_vmo_helper"
bool read_striped_vmo_helper(fifo_client_t* client, test_vmo_object_t* obj, size_t i, size_t objs,
                             txnid_t txnid, size_t kBlockSize) {
    // First, empty out the VMO
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[obj->vmo_size]());
    ASSERT_TRUE(ac.check());
    size_t actual;
    ASSERT_EQ(mx_vmo_write(obj->vmo, out.get(), 0, obj->vmo_size, &actual),
              MX_OK);

    // Next, read to the vmo from the disk
    size_t blocks = obj->vmo_size / kBlockSize;
    fbl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[blocks], blocks);
    ASSERT_TRUE(ac.check());
    for (size_t b = 0; b < blocks; b++) {
        requests[b].txnid      = txnid;
        requests[b].vmoid      = obj->vmoid;
        requests[b].opcode     = BLOCKIO_READ;
        requests[b].length     = static_cast<uint32_t>(kBlockSize);
        requests[b].vmo_offset = b * kBlockSize;
        requests[b].dev_offset = i * kBlockSize + b * (kBlockSize * objs);
    }
    // Read entire vmos at once
    ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), MX_OK);

    // Finally, write from the vmo to an out buffer, where we can compare
    // the results with the input buffer.
    ASSERT_EQ(mx_vmo_read(obj->vmo, out.get(), 0, obj->vmo_size, &actual),
              MX_OK);
    ASSERT_EQ(memcmp(obj->buf.get(), out.get(), obj->vmo_size), 0,
              "Read data not equal to written data");
    return true;
}

// Tears down an object created by "create_vmo_helper".
bool close_vmo_helper(fifo_client_t* client, test_vmo_object_t* obj, txnid_t txnid) {
    block_fifo_request_t request;
    request.txnid = txnid;
    request.vmoid = obj->vmoid;
    request.opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_OK);
    ASSERT_EQ(mx_handle_close(obj->vmo), MX_OK);
    return true;
}

bool ramdisk_test_fifo_multiple_vmo(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    txnid_t txnid;
    expected = sizeof(txnid);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);

    // Create multiple VMOs
    fbl::AllocChecker ac;
    fbl::Array<test_vmo_object_t> objs(new (&ac) test_vmo_object_t[10](), 10);
    ASSERT_TRUE(ac.check());
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], kBlockSize));
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(write_striped_vmo_helper(client, &objs[i], i, objs.size(), txnid, kBlockSize));
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(read_striped_vmo_helper(client, &objs[i], i, objs.size(), txnid, kBlockSize));
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(close_vmo_helper(client, &objs[i], txnid));
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
    size_t kBlockSize;
} test_thread_arg_t;

int fifo_vmo_thread(void* arg) {
    test_thread_arg_t* fifoarg = (test_thread_arg_t*) arg;
    test_vmo_object_t* obj = fifoarg->obj;
    size_t i = fifoarg->i;
    size_t objs = fifoarg->objs;
    mx_handle_t fd = fifoarg->fd;
    fifo_client_t* client = fifoarg->client;
    size_t kBlockSize = fifoarg->kBlockSize;

    // Each thread should create it's own txnid
    txnid_t txnid;
    ssize_t expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    ASSERT_TRUE(create_vmo_helper(fd, obj, kBlockSize));
    ASSERT_TRUE(write_striped_vmo_helper(client, obj, i, objs, txnid, kBlockSize));
    ASSERT_TRUE(read_striped_vmo_helper(client, obj, i, objs, txnid, kBlockSize));
    ASSERT_TRUE(close_vmo_helper(client, obj, txnid));
    return 0;
}

bool ramdisk_test_fifo_multiple_vmo_multithreaded(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);

    // Create multiple VMOs
    size_t num_threads = 10;
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
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), MX_ERR_ALREADY_BOUND,
              "Expected fifo to already be bound");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create multiple VMOs
    fbl::AllocChecker ac;
    fbl::Array<test_vmo_object_t> objs(new (&ac) test_vmo_object_t[10](), 10);
    ASSERT_TRUE(ac.check());
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], kBlockSize));
    }

    // Now that we've set up the connection for a few VMOs, shut down the fifo
    ASSERT_EQ(mx_handle_close(fifo), MX_OK);

    // Attempting to batch any operations to the fifo should fail
    block_fifo_request_t request;
    request.txnid = txnid;
    request.vmoid = objs[0].vmoid;
    request.opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_NE(block_fifo_txn(client, &request, 1), MX_OK,
              "Expected operation to fail after closing FIFO");

    // Free the dead client
    block_fifo_release_client(client);

    // Give the block server a moment to realize our side of the fifo has been closed
    usleep(10000);

    // The block server should still be functioning. We should be able to re-bind to it
    expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);
    expected = sizeof(txnid);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], kBlockSize));
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(write_striped_vmo_helper(client, &objs[i], i, objs.size(), txnid, kBlockSize));
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(read_striped_vmo_helper(client, &objs[i], i, objs.size(), txnid, kBlockSize));
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(close_vmo_helper(client, &objs[i], txnid));
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
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize));

    for (size_t num_ops = 1; num_ops <= MAX_TXN_MESSAGES; num_ops++) {
        txnid_t txnid;
        expected = sizeof(txnid_t);
        ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

        fbl::AllocChecker ac;
        fbl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[num_ops](),
                                                   num_ops);
        ASSERT_TRUE(ac.check());

        for (size_t b = 0; b < num_ops; b++) {
            requests[b].txnid      = txnid;
            requests[b].vmoid      = obj.vmoid;
            requests[b].opcode     = BLOCKIO_WRITE;
            requests[b].length     = static_cast<uint32_t>(kBlockSize);
            requests[b].vmo_offset = 0;
            requests[b].dev_offset = 0;
        }

        ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), MX_OK);
        ASSERT_EQ(ioctl_block_free_txn(fd, &txnid), MX_OK, "Failed to free txn");
    }

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_too_many_ops(void) {
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize));

    // This is one too many messages
    size_t num_ops = MAX_TXN_MESSAGES + 1;
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    fbl::AllocChecker ac;
    fbl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[num_ops](),
                                               num_ops);
    ASSERT_TRUE(ac.check());

    for (size_t b = 0; b < num_ops; b++) {
        requests[b].txnid      = txnid;
        requests[b].vmoid      = obj.vmoid;
        requests[b].opcode     = BLOCKIO_WRITE;
        requests[b].length     = static_cast<uint32_t>(kBlockSize);
        requests[b].vmo_offset = 0;
        requests[b].dev_offset = 0;
    }

    // This should be caught locally by the client library
    ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), MX_ERR_INVALID_ARGS);

    // Since the client-side automatically appends the "TXN_END" flag, we avoid using it here.
    for (size_t i = 0; i < requests.size(); i++) {
        uint32_t actual;
retry_write:
        mx_status_t status = mx_fifo_write(fifo, &requests[i], sizeof(block_fifo_request_t),
                                           &actual);
        if (status == MX_ERR_SHOULD_WAIT) {
            mx_signals_t signals;
            ASSERT_EQ(mx_object_wait_one(fifo, MX_FIFO_WRITABLE, MX_TIME_INFINITE, &signals),
                      MX_OK);
            ASSERT_EQ(signals & MX_FIFO_WRITABLE, MX_FIFO_WRITABLE);
            goto retry_write;
        } else {
            ASSERT_EQ(status, MX_OK);
        }
    }

    // Even though we never sent a request for TXN_END, we'll get a response because
    // we filled our txn to the brim.
    mx_signals_t signals;
    ASSERT_EQ(mx_object_wait_one(fifo, MX_FIFO_READABLE, MX_TIME_INFINITE, &signals),
              MX_OK);
    ASSERT_EQ(signals & MX_FIFO_READABLE, MX_FIFO_READABLE);
    block_fifo_response_t response;
    uint32_t count;
    ASSERT_EQ(mx_fifo_read(fifo, &response, sizeof(block_fifo_response_t), &count), MX_OK);
    ASSERT_EQ(response.status, MX_OK);
    ASSERT_EQ(response.txnid, txnid);

    // The txn should still be usable! We should still be able to send a close request.
    ASSERT_EQ(ioctl_block_free_txn(fd, &txnid), MX_OK, "Failed to free txn");
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
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize));

    // Bad request: Writing to the wrong vmoid
    block_fifo_request_t request;
    request.txnid      = txnid;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid + 5);
    request.opcode     = BLOCKIO_WRITE;
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_ERR_IO, "Expected IO error with bad vmoid");

    ASSERT_EQ(ioctl_block_free_txn(fd, &txnid), MX_OK, "Failed to free txn");
    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

bool ramdisk_test_fifo_bad_client_txnid(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the ramdisk
    const size_t kBlockSize = PAGE_SIZE;
    int fd = get_ramdisk(kBlockSize, 1 << 18);

    // Create a connection to the ramdisk
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize));

    // Bad request: Invalid txnid (not allocated)
    block_fifo_request_t request;
    request.txnid      = static_cast<txnid_t>(5);
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_ERR_IO, "Expected IO error with bad txnid");

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
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create a vmo of at least size "kBlockSize * 2", since we'll
    // be reading "kBlockSize" bytes from an offset below, and we want it
    // to fit within the bounds of the VMO.
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize * 2));

    block_fifo_request_t request;
    request.txnid      = txnid;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;

    // Send a request that has a non-block aligned length (-1)
    request.length     = static_cast<uint32_t>(kBlockSize - 1);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_ERR_INVALID_ARGS);

    // Send a request that has a non-block aligned length (+1)
    request.length     = static_cast<uint32_t>(kBlockSize + 1);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_ERR_INVALID_ARGS);

    // Send a request that has a non-block aligned device offset
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 0;
    request.dev_offset = 1;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_ERR_INVALID_ARGS);

    // Actually, we don't care about aligning VMO offsets, so this request should be fine
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 1;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_OK);

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
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), MX_OK);
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    test_vmo_object_t obj;
    obj.vmo_size = kBlockSize;
    ASSERT_EQ(mx_vmo_create(obj.vmo_size, 0, &obj.vmo), MX_OK,
              "Failed to create vmo");
    fbl::AllocChecker ac;
    obj.buf.reset(new (&ac) uint8_t[obj.vmo_size]);
    ASSERT_TRUE(ac.check());
    fill_random(obj.buf.get(), obj.vmo_size);
    size_t actual;
    ASSERT_EQ(mx_vmo_write(obj.vmo, obj.buf.get(), 0, obj.vmo_size, &actual),
              MX_OK, "Failed to write to vmo");
    ASSERT_EQ(obj.vmo_size, actual, "Could not write entire VMO");
    mx_handle_t xfer_vmo;
    ASSERT_EQ(mx_handle_duplicate(obj.vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo), MX_OK,
              "Failed to duplicate vmo");
    expected = sizeof(vmoid_t);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &obj.vmoid), expected,
              "Failed to attach vmo");

    // Send a request to write to write a block -- even though that's smaller than the VMO
    block_fifo_request_t request;
    request.txnid      = txnid;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;
    request.length     = static_cast<uint32_t>(kBlockSize - 1);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_ERR_INVALID_ARGS);
    request.length     = static_cast<uint32_t>(kBlockSize + 1);
    // Do the same thing, but for reading
    request.opcode     = BLOCKIO_READ;
    request.length     = static_cast<uint32_t>(kBlockSize - 1);
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_ERR_INVALID_ARGS);
    request.length     = static_cast<uint32_t>(kBlockSize + 1);
    ASSERT_EQ(block_fifo_txn(client, &request, 1), MX_ERR_INVALID_ARGS);

    block_fifo_release_client(client);
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

BEGIN_TEST_CASE(ramdisk_tests)
RUN_TEST_SMALL(ramdisk_test_simple)
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
RUN_TEST_SMALL(ramdisk_test_fifo_too_many_ops)
RUN_TEST_SMALL(ramdisk_test_fifo_bad_client_vmoid)
RUN_TEST_SMALL(ramdisk_test_fifo_bad_client_txnid)
RUN_TEST_SMALL(ramdisk_test_fifo_bad_client_unaligned_request)
RUN_TEST_SMALL(ramdisk_test_fifo_bad_client_bad_vmo)
END_TEST_CASE(ramdisk_tests)

} // namespace tests
