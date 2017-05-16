// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <magenta/device/block.h>
#include <magenta/syscalls.h>
#include <mxalloc/new.h>
#include <mxtl/array.h>
#include <mxtl/unique_ptr.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>

#include <blktest/blktest.h>

#define RAMCTL_PATH "/dev/misc/ramctl"

namespace tests {

static int get_testdev(uint64_t* blk_size, uint64_t* blk_count) {
    const char* blkdev_path = getenv(BLKTEST_BLK_DEV);
    ASSERT_NONNULL(blkdev_path, "No test device specified");
    // Open the block device
    int fd = open(blkdev_path, O_RDWR);
    if (fd < 0) {
        printf("OPENING BLKDEV (path=%s) FAILURE. Errno: %d\n", blkdev_path, errno);
    }
    ASSERT_GE(fd, 0, "Could not open block device");
    block_info_t info;
    ssize_t rc = ioctl_block_get_info(fd, &info);
    ASSERT_GE(rc, 0, "Could not get block size");
    *blk_size = info.block_size;
    *blk_count = info.block_count;
    return fd;
}

static bool blkdev_test_simple(void) {
    uint64_t blk_size, blk_count;
    uint8_t buf[PAGE_SIZE];
    uint8_t out[PAGE_SIZE];

    BEGIN_TEST;
    int fd = get_testdev(&blk_size, &blk_count);
    memset(buf, 'a', sizeof(buf));
    memset(out, 0, sizeof(out));

    // Write a page and a half
    ASSERT_EQ(write(fd, buf, sizeof(buf)), (ssize_t) sizeof(buf), "");
    ASSERT_EQ(write(fd, buf, sizeof(buf) / 2), (ssize_t) (sizeof(buf) / 2), "");

    // Seek to the start of the device and read the contents
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_EQ(read(fd, out, sizeof(out)), (ssize_t) sizeof(out), "");
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0, "");

    close(fd);
    END_TEST;
}

bool blkdev_test_bad_requests(void) {
    uint64_t blk_size, blk_count;
    uint8_t buf[PAGE_SIZE];

    BEGIN_TEST;
    int fd = get_testdev(&blk_size, &blk_count);
    memset(buf, 'a', sizeof(buf));
    ASSERT_LE(blk_size, PAGE_SIZE, "Block size is too big");

    // Read / write non-multiples of the block size
    ASSERT_EQ(write(fd, buf, blk_size - 1), 0, "");
    ASSERT_EQ(write(fd, buf, blk_size / 2), 0, "");
    ASSERT_EQ(read(fd, buf, blk_size - 1), 0, "");
    ASSERT_EQ(read(fd, buf, blk_size / 2), 0, "");

    // Read / write from unaligned offset
    ASSERT_EQ(lseek(fd, 1, SEEK_SET), 1, "");
    ASSERT_EQ(write(fd, buf, blk_size), -1, "");
    ASSERT_EQ(errno, EINVAL, "");
    ASSERT_EQ(read(fd, buf, blk_size), -1, "");
    ASSERT_EQ(errno, EINVAL, "");

    // Read / write from beyond end of device
    off_t dev_size = blk_size * blk_count;
    ASSERT_EQ(lseek(fd, dev_size, SEEK_SET), dev_size, "");
    ASSERT_EQ(write(fd, buf, blk_size), 0, "");
    ASSERT_EQ(read(fd, buf, blk_size), 0, "");

    close(fd);
    END_TEST;
}

#if 0
bool blkdev_test_multiple(void) {
    uint8_t buf[PAGE_SIZE];
    uint8_t out[PAGE_SIZE];

    BEGIN_TEST;
    int fd1 = get_testdev("blkdev-test-A", PAGE_SIZE, 512);
    int fd2 = get_testdev("blkdev-test-B", PAGE_SIZE, 512);

    // Write 'a' to fd1, write 'b', to fd2
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(write(fd1, buf, sizeof(buf)), (ssize_t) sizeof(buf), "");
    memset(buf, 'b', sizeof(buf));
    ASSERT_EQ(write(fd2, buf, sizeof(buf)), (ssize_t) sizeof(buf), "");

    ASSERT_EQ(lseek(fd1, 0, SEEK_SET), 0, "");
    ASSERT_EQ(lseek(fd2, 0, SEEK_SET), 0, "");

    // Read 'b' from fd2, read 'a' from fd1
    ASSERT_EQ(read(fd2, out, sizeof(buf)), (ssize_t) sizeof(buf), "");
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0, "");
    close(fd2);

    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(read(fd1, out, sizeof(buf)), (ssize_t) sizeof(buf), "");
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0, "");
    close(fd1);

    END_TEST;
}
#endif

bool blkdev_test_fifo_no_op(void) {
    // Get a FIFO connection to a blkdev and immediately close it
    BEGIN_TEST;
    uint64_t blk_size, blk_count;
    int fd = get_testdev(&blk_size, &blk_count);
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

static void fill_random(uint8_t* buf, uint64_t size) {
    for (size_t i = 0; i < size; i++) {
        buf[i] = static_cast<uint8_t>(rand());
    }
}

bool blkdev_test_fifo_basic(void) {
    BEGIN_TEST;
    uint64_t blk_size, blk_count;
    // Set up the initial handshake connection with the blkdev
    int fd = get_testdev(&blk_size, &blk_count);
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create an arbitrary VMO, fill it with some stuff
    //uint64_t vmo_size = blk_size * 3;
    uint64_t vmo_size = PAGE_SIZE * 3;
    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(vmo_size, 0, &vmo), NO_ERROR, "Failed to create VMO");
    AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[vmo_size]);
    ASSERT_TRUE(ac.check(), "");
    fill_random(buf.get(), vmo_size);

    size_t actual;
    ASSERT_EQ(mx_vmo_write(vmo, buf.get(), 0, vmo_size, &actual), NO_ERROR, "");
    ASSERT_EQ(actual, vmo_size, "");

    // Send a handle to the vmo to the block device, get a vmoid which identifies it
    vmoid_t vmoid;
    expected = sizeof(vmoid_t);
    mx_handle_t xfer_vmo;
    ASSERT_EQ(mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo), NO_ERROR, "");
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &vmoid), expected,
              "Failed to attach vmo");

    // Batch write the VMO to the blkdev
    // Split it into two requests, spread across the disk
    block_fifo_request_t requests[2];
    requests[0].txnid      = txnid;
    requests[0].vmoid      = vmoid;
    requests[0].opcode     = BLOCKIO_WRITE;
    requests[0].length     = blk_size;
    requests[0].vmo_offset = 0;
    requests[0].dev_offset = 0;

    requests[1].txnid      = txnid;
    requests[1].vmoid      = vmoid;
    requests[1].opcode     = BLOCKIO_WRITE;
    requests[1].length     = blk_size * 2;
    requests[1].vmo_offset = blk_size;
    requests[1].dev_offset = blk_size * 100;

    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");
    ASSERT_EQ(block_fifo_txn(client, &requests[0], countof(requests)), NO_ERROR, "");

    // Empty the vmo, then read the info we just wrote to the disk
    mxtl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[vmo_size]());
    ASSERT_TRUE(ac.check(), "");

    ASSERT_EQ(mx_vmo_write(vmo, out.get(), 0, vmo_size, &actual), NO_ERROR, "");
    requests[0].opcode = BLOCKIO_READ;
    requests[1].opcode = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], countof(requests)), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_read(vmo, out.get(), 0, vmo_size, &actual), NO_ERROR, "");
    ASSERT_EQ(memcmp(buf.get(), out.get(), blk_size * 3), 0, "Read data not equal to written data");

    // Close the current vmo
    requests[0].opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_EQ(block_fifo_txn(client, &requests[0], 1), NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

bool blkdev_test_fifo_whole_disk(void) {
    BEGIN_TEST;
    uint64_t blk_size, blk_count;
    // Set up the initial handshake connection with the blkdev
    int fd = get_testdev(&blk_size, &blk_count);
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create an arbitrary VMO, fill it with some stuff
    uint64_t vmo_size = blk_size * blk_count;
    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(vmo_size, 0, &vmo), NO_ERROR, "Failed to create VMO");
    AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[vmo_size]);
    ASSERT_TRUE(ac.check(), "");
    fill_random(buf.get(), vmo_size);

    size_t actual;
    ASSERT_EQ(mx_vmo_write(vmo, buf.get(), 0, vmo_size, &actual), NO_ERROR, "");
    ASSERT_EQ(actual, vmo_size, "");

    // Send a handle to the vmo to the block device, get a vmoid which identifies it
    vmoid_t vmoid;
    expected = sizeof(vmoid_t);
    mx_handle_t xfer_vmo;
    ASSERT_EQ(mx_handle_duplicate(vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo), NO_ERROR, "");
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &vmoid), expected,
              "Failed to attach vmo");

    // Batch write the VMO to the blkdev
    block_fifo_request_t request;
    request.txnid      = txnid;
    request.vmoid      = vmoid;
    request.opcode     = BLOCKIO_WRITE;
    request.length     = vmo_size;
    request.vmo_offset = 0;
    request.dev_offset = 0;

    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");
    ASSERT_EQ(block_fifo_txn(client, &request, 1), NO_ERROR, "");

    // Empty the vmo, then read the info we just wrote to the disk
    mxtl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[vmo_size]());
    ASSERT_TRUE(ac.check(), "");

    ASSERT_EQ(mx_vmo_write(vmo, out.get(), 0, vmo_size, &actual), NO_ERROR, "");
    request.opcode = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), NO_ERROR, "");
    ASSERT_EQ(mx_vmo_read(vmo, out.get(), 0, vmo_size, &actual), NO_ERROR, "");
    ASSERT_EQ(memcmp(buf.get(), out.get(), blk_size * 3), 0, "Read data not equal to written data");

    // Close the current vmo
    request.opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), NO_ERROR, "");

    ASSERT_EQ(mx_handle_close(vmo), NO_ERROR, "");
    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

typedef struct {
    uint64_t vmo_size;
    mx_handle_t vmo;
    vmoid_t vmoid;
    mxtl::unique_ptr<uint8_t[]> buf;
} test_vmo_object_t;

// Creates a VMO, fills it with data, and gives it to the block device.
bool create_vmo_helper(int fd, test_vmo_object_t* obj, size_t kBlockSize) {
    obj->vmo_size = kBlockSize + (rand() % 5) * kBlockSize;
    ASSERT_EQ(mx_vmo_create(obj->vmo_size, 0, &obj->vmo), NO_ERROR,
              "Failed to create vmo");
    AllocChecker ac;
    obj->buf.reset(new (&ac) uint8_t[obj->vmo_size]);
    ASSERT_TRUE(ac.check(), "");
    fill_random(obj->buf.get(), obj->vmo_size);
    size_t actual;
    ASSERT_EQ(mx_vmo_write(obj->vmo, obj->buf.get(), 0, obj->vmo_size, &actual),
              NO_ERROR, "Failed to write to vmo");
    ASSERT_EQ(obj->vmo_size, actual, "Could not write entire VMO");

    ssize_t expected = sizeof(vmoid_t);
    mx_handle_t xfer_vmo;
    ASSERT_EQ(mx_handle_duplicate(obj->vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo), NO_ERROR,
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
    AllocChecker ac;
    mxtl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[blocks], blocks);
    ASSERT_TRUE(ac.check(), "");
    for (size_t b = 0; b < blocks; b++) {
        requests[b].txnid      = txnid;
        requests[b].vmoid      = obj->vmoid;
        requests[b].opcode     = BLOCKIO_WRITE;
        requests[b].length     = static_cast<uint32_t>(kBlockSize);
        requests[b].vmo_offset = b * kBlockSize;
        requests[b].dev_offset = i * kBlockSize + b * (kBlockSize * objs);
    }
    // Write entire vmos at once
    ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), NO_ERROR, "");
    return true;
}

// Verifies the result from "write_striped_vmo_helper"
bool read_striped_vmo_helper(fifo_client_t* client, test_vmo_object_t* obj, size_t i, size_t objs,
                             txnid_t txnid, size_t kBlockSize) {
    // First, empty out the VMO
    AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> out(new (&ac) uint8_t[obj->vmo_size]());
    ASSERT_TRUE(ac.check(), "");
    size_t actual;
    ASSERT_EQ(mx_vmo_write(obj->vmo, out.get(), 0, obj->vmo_size, &actual),
              NO_ERROR, "");

    // Next, read to the vmo from the disk
    size_t blocks = obj->vmo_size / kBlockSize;
    mxtl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[blocks], blocks);
    ASSERT_TRUE(ac.check(), "");
    for (size_t b = 0; b < blocks; b++) {
        requests[b].txnid      = txnid;
        requests[b].vmoid      = obj->vmoid;
        requests[b].opcode     = BLOCKIO_READ;
        requests[b].length     = static_cast<uint32_t>(kBlockSize);
        requests[b].vmo_offset = b * kBlockSize;
        requests[b].dev_offset = i * kBlockSize + b * (kBlockSize * objs);
    }
    // Read entire vmos at once
    ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), NO_ERROR, "");

    // Finally, write from the vmo to an out buffer, where we can compare
    // the results with the input buffer.
    ASSERT_EQ(mx_vmo_read(obj->vmo, out.get(), 0, obj->vmo_size, &actual),
              NO_ERROR, "");
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
    ASSERT_EQ(block_fifo_txn(client, &request, 1), NO_ERROR, "");
    ASSERT_EQ(mx_handle_close(obj->vmo), NO_ERROR, "");
    return true;
}

bool blkdev_test_fifo_multiple_vmo(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the blkdev
    uint64_t blk_size, blk_count;
    int fd = get_testdev(&blk_size, &blk_count);
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    txnid_t txnid;
    expected = sizeof(txnid);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");

    // Create multiple VMOs
    AllocChecker ac;
    mxtl::Array<test_vmo_object_t> objs(new (&ac) test_vmo_object_t[10](), 10);
    ASSERT_TRUE(ac.check(), "");
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], blk_size), "");
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(write_striped_vmo_helper(client, &objs[i], i, objs.size(), txnid, blk_size), "");
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(read_striped_vmo_helper(client, &objs[i], i, objs.size(), txnid, blk_size), "");
    }

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(close_vmo_helper(client, &objs[i], txnid), "");
    }

    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
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

    ASSERT_TRUE(create_vmo_helper(fd, obj, kBlockSize), "");
    ASSERT_TRUE(write_striped_vmo_helper(client, obj, i, objs, txnid, kBlockSize), "");
    ASSERT_TRUE(read_striped_vmo_helper(client, obj, i, objs, txnid, kBlockSize), "");
    ASSERT_TRUE(close_vmo_helper(client, obj, txnid), "");
    return 0;
}

bool blkdev_test_fifo_multiple_vmo_multithreaded(void) {
    BEGIN_TEST;
    // Set up the initial handshake connection with the blkdev
    uint64_t kBlockSize, blk_count;
    int fd = get_testdev(&kBlockSize, &blk_count);
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");

    // Create multiple VMOs
    size_t num_threads = 10;
    AllocChecker ac;
    mxtl::Array<test_vmo_object_t> objs(new (&ac) test_vmo_object_t[num_threads](), num_threads);
    ASSERT_TRUE(ac.check(), "");

    mxtl::Array<thrd_t> threads(new (&ac) thrd_t[num_threads](), num_threads);
    ASSERT_TRUE(ac.check(), "");

    mxtl::Array<test_thread_arg_t> thread_args(new (&ac) test_thread_arg_t[num_threads](),
                                               num_threads);
    ASSERT_TRUE(ac.check(), "");

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
                  thrd_success, "");
    }

    for (size_t i = 0; i < num_threads; i++) {
        int res;
        ASSERT_EQ(thrd_join(threads[i], &res), thrd_success, "");
        ASSERT_EQ(res, 0, "");
    }

    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

bool blkdev_test_fifo_unclean_shutdown(void) {
    BEGIN_TEST;
    // Set up the blkdev
    uint64_t kBlockSize, blk_count;
    int fd = get_testdev(&kBlockSize, &blk_count);

    // Create a connection to the blkdev
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), ERR_ALREADY_BOUND,
              "Expected fifo to already be bound");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create multiple VMOs
    AllocChecker ac;
    mxtl::Array<test_vmo_object_t> objs(new (&ac) test_vmo_object_t[10](), 10);
    ASSERT_TRUE(ac.check(), "");
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], kBlockSize), "");
    }

    // Now that we've set up the connection for a few VMOs, shut down the fifo
    ASSERT_EQ(mx_handle_close(fifo), NO_ERROR, "");

    // Attempting to batch any operations to the fifo should fail
    block_fifo_request_t request;
    request.txnid = txnid;
    request.vmoid = objs[0].vmoid;
    request.opcode = BLOCKIO_CLOSE_VMO;
    ASSERT_NEQ(block_fifo_txn(client, &request, 1), NO_ERROR,
               "Expected operation to fail after closing FIFO");

    // Free the dead client
    block_fifo_release_client(client);

    // Give the block server a moment to realize our side of the fifo has been closed
    usleep(10000);

    // The block server should still be functioning. We should be able to re-bind to it
    expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");
    expected = sizeof(txnid);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(create_vmo_helper(fd, &objs[i], kBlockSize), "");
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(write_striped_vmo_helper(client, &objs[i], i, objs.size(), txnid, kBlockSize), "");
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(read_striped_vmo_helper(client, &objs[i], i, objs.size(), txnid, kBlockSize), "");
    }
    for (size_t i = 0; i < objs.size(); i++) {
        ASSERT_TRUE(close_vmo_helper(client, &objs[i], txnid), "");
    }

    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

bool blkdev_test_fifo_large_ops_count(void) {
    BEGIN_TEST;
    // Set up the blkdev
    uint64_t kBlockSize, blk_count;
    int fd = get_testdev(&kBlockSize, &blk_count);

    // Create a connection to the blkdev
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize), "");

    for (size_t num_ops = 1; num_ops <= MAX_TXN_MESSAGES; num_ops++) {
        txnid_t txnid;
        expected = sizeof(txnid_t);
        ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

        AllocChecker ac;
        mxtl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[num_ops](),
                                                   num_ops);
        ASSERT_TRUE(ac.check(), "");

        for (size_t b = 0; b < num_ops; b++) {
            requests[b].txnid      = txnid;
            requests[b].vmoid      = obj.vmoid;
            requests[b].opcode     = BLOCKIO_WRITE;
            requests[b].length     = static_cast<uint32_t>(kBlockSize);
            requests[b].vmo_offset = 0;
            requests[b].dev_offset = 0;
        }

        ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), NO_ERROR, "");
        ASSERT_EQ(ioctl_block_free_txn(fd, &txnid), NO_ERROR, "Failed to free txn");
    }

    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

bool blkdev_test_fifo_too_many_ops(void) {
    BEGIN_TEST;
    // Set up the blkdev
    uint64_t kBlockSize, blk_count;
    int fd = get_testdev(&kBlockSize, &blk_count);

    // Create a connection to the blkdev
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize), "");

    // This is one too many messages
    size_t num_ops = MAX_TXN_MESSAGES + 1;
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    AllocChecker ac;
    mxtl::Array<block_fifo_request_t> requests(new (&ac) block_fifo_request_t[num_ops](),
                                               num_ops);
    ASSERT_TRUE(ac.check(), "");

    for (size_t b = 0; b < num_ops; b++) {
        requests[b].txnid      = txnid;
        requests[b].vmoid      = obj.vmoid;
        requests[b].opcode     = BLOCKIO_WRITE;
        requests[b].length     = static_cast<uint32_t>(kBlockSize);
        requests[b].vmo_offset = 0;
        requests[b].dev_offset = 0;
    }

    // This should be caught locally by the client library
    ASSERT_EQ(block_fifo_txn(client, &requests[0], requests.size()), ERR_INVALID_ARGS, "");

    // Since the client-side automatically appends the "TXN_END" flag, we avoid using it here.
    for (size_t i = 0; i < requests.size(); i++) {
        uint32_t actual;
retry_write:
        mx_status_t status = mx_fifo_write(fifo, &requests[i], sizeof(block_fifo_request_t),
                                           &actual);
        if (status == ERR_SHOULD_WAIT) {
            mx_signals_t signals;
            ASSERT_EQ(mx_object_wait_one(fifo, MX_FIFO_WRITABLE, MX_TIME_INFINITE, &signals),
                      NO_ERROR, "");
            ASSERT_EQ(signals & MX_FIFO_WRITABLE, MX_FIFO_WRITABLE, "");
            goto retry_write;
        } else {
            ASSERT_EQ(status, NO_ERROR, "");
        }
    }

    // Even though we never sent a request for TXN_END, we'll get a response because
    // we filled our txn to the brim.
    mx_signals_t signals;
    ASSERT_EQ(mx_object_wait_one(fifo, MX_FIFO_READABLE, MX_TIME_INFINITE, &signals),
              NO_ERROR, "");
    ASSERT_EQ(signals & MX_FIFO_READABLE, MX_FIFO_READABLE, "");
    block_fifo_response_t response;
    uint32_t count;
    ASSERT_EQ(mx_fifo_read(fifo, &response, sizeof(block_fifo_response_t), &count), NO_ERROR, "");
    ASSERT_EQ(response.status, NO_ERROR, "");
    ASSERT_EQ(response.txnid, txnid, "");

    // The txn should still be usable! We should still be able to send a close request.
    ASSERT_EQ(ioctl_block_free_txn(fd, &txnid), NO_ERROR, "Failed to free txn");
    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

bool blkdev_test_fifo_bad_client_vmoid(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the blkdev
    uint64_t kBlockSize, blk_count;
    int fd = get_testdev(&kBlockSize, &blk_count);

    // Create a connection to the blkdev
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize), "");

    // Bad request: Writing to the wrong vmoid
    block_fifo_request_t request;
    request.txnid      = txnid;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid + 5);
    request.opcode     = BLOCKIO_WRITE;
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ERR_IO, "Expected IO error with bad vmoid");

    ASSERT_EQ(ioctl_block_free_txn(fd, &txnid), NO_ERROR, "Failed to free txn");
    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

bool blkdev_test_fifo_bad_client_txnid(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the blkdev
    uint64_t kBlockSize, blk_count;
    int fd = get_testdev(&kBlockSize, &blk_count);

    // Create a connection to the blkdev
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");

    // Create a vmo
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize), "");

    // Bad request: Invalid txnid (not allocated)
    block_fifo_request_t request;
    request.txnid      = static_cast<txnid_t>(5);
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ERR_IO, "Expected IO error with bad txnid");

    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

bool blkdev_test_fifo_bad_client_unaligned_request(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the blkdev
    uint64_t kBlockSize, blk_count;
    int fd = get_testdev(&kBlockSize, &blk_count);

    // Create a connection to the blkdev
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create a vmo of at least size "kBlockSize * 2", since we'll
    // be reading "kBlockSize" bytes from an offset below, and we want it
    // to fit within the bounds of the VMO.
    test_vmo_object_t obj;
    ASSERT_TRUE(create_vmo_helper(fd, &obj, kBlockSize * 2), "");

    block_fifo_request_t request;
    request.txnid      = txnid;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;

    // Send a request that has a non-block aligned length (-1)
    request.length     = static_cast<uint32_t>(kBlockSize - 1);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ERR_INVALID_ARGS, "");

    // Send a request that has a non-block aligned length (+1)
    request.length     = static_cast<uint32_t>(kBlockSize + 1);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ERR_INVALID_ARGS, "");

    // Send a request that has a non-block aligned device offset
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 0;
    request.dev_offset = 1;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ERR_INVALID_ARGS, "");

    // Actually, we don't care about aligning VMO offsets, so this request should be fine
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 1;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), NO_ERROR, "");

    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

bool blkdev_test_fifo_bad_client_bad_vmo(void) {
    // Try to flex the server's error handling by sending 'malicious' client requests.
    BEGIN_TEST;
    // Set up the blkdev
    uint64_t kBlockSize, blk_count;
    int fd = get_testdev(&kBlockSize, &blk_count);

    // Create a connection to the blkdev
    mx_handle_t fifo;
    ssize_t expected = sizeof(fifo);
    ASSERT_EQ(ioctl_block_get_fifos(fd, &fifo), expected, "Failed to get FIFO");
    fifo_client_t* client;
    ASSERT_EQ(block_fifo_create_client(fifo, &client), NO_ERROR, "");
    txnid_t txnid;
    expected = sizeof(txnid_t);
    ASSERT_EQ(ioctl_block_alloc_txn(fd, &txnid), expected, "Failed to allocate txn");

    // Create a vmo which is not block aligned
    test_vmo_object_t obj;
    obj.vmo_size = kBlockSize - 1;
    ASSERT_EQ(mx_vmo_create(obj.vmo_size, 0, &obj.vmo), NO_ERROR,
              "Failed to create vmo");
    AllocChecker ac;
    obj.buf.reset(new (&ac) uint8_t[obj.vmo_size]);
    ASSERT_TRUE(ac.check(), "");
    fill_random(obj.buf.get(), obj.vmo_size);
    size_t actual;
    ASSERT_EQ(mx_vmo_write(obj.vmo, obj.buf.get(), 0, obj.vmo_size, &actual),
              NO_ERROR, "Failed to write to vmo");
    ASSERT_EQ(obj.vmo_size, actual, "Could not write entire VMO");
    mx_handle_t xfer_vmo;
    ASSERT_EQ(mx_handle_duplicate(obj.vmo, MX_RIGHT_SAME_RIGHTS, &xfer_vmo), NO_ERROR,
              "Failed to duplicate vmo");
    expected = sizeof(vmoid_t);
    ASSERT_EQ(ioctl_block_attach_vmo(fd, &xfer_vmo, &obj.vmoid), expected,
              "Failed to attach vmo");

    // Send a request to write to write a block -- even though that's smaller than the VMO
    block_fifo_request_t request;
    request.txnid      = txnid;
    request.vmoid      = static_cast<vmoid_t>(obj.vmoid);
    request.opcode     = BLOCKIO_WRITE;
    request.length     = static_cast<uint32_t>(kBlockSize);
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ERR_INVALID_ARGS, "");
    // Do the same thing, but for reading
    request.opcode     = BLOCKIO_READ;
    ASSERT_EQ(block_fifo_txn(client, &request, 1), ERR_INVALID_ARGS, "");

    block_fifo_release_client(client);
    ASSERT_EQ(ioctl_block_fifo_close(fd), NO_ERROR, "Failed to close fifo");
    close(fd);
    END_TEST;
}

BEGIN_TEST_CASE(blkdev_tests)
RUN_TEST(blkdev_test_simple)
RUN_TEST(blkdev_test_bad_requests)
#if 0
RUN_TEST(blkdev_test_multiple)
#endif
RUN_TEST(blkdev_test_fifo_no_op)
RUN_TEST(blkdev_test_fifo_basic)
//RUN_TEST(blkdev_test_fifo_whole_disk)
RUN_TEST(blkdev_test_fifo_multiple_vmo)
RUN_TEST(blkdev_test_fifo_multiple_vmo_multithreaded)
// TODO(smklein): Test ops across different vmos
RUN_TEST(blkdev_test_fifo_unclean_shutdown)
RUN_TEST(blkdev_test_fifo_large_ops_count)
RUN_TEST(blkdev_test_fifo_too_many_ops)
RUN_TEST(blkdev_test_fifo_bad_client_vmoid)
RUN_TEST(blkdev_test_fifo_bad_client_txnid)
RUN_TEST(blkdev_test_fifo_bad_client_unaligned_request)
RUN_TEST(blkdev_test_fifo_bad_client_bad_vmo)
END_TEST_CASE(blkdev_tests)

} // namespace tests
