// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/block.h>
#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <unittest/unittest.h>
#include <virtio/block.h>
#include <virtio/virtio_ring.h>

#define QUEUE_SIZE 8u
#define DATA_SIZE 128u

typedef struct virtio_mem {
    struct vring_desc desc[QUEUE_SIZE];
    uint8_t avail_buf[sizeof(struct vring_avail) + sizeof(uint16_t) * QUEUE_SIZE];
    uint8_t used_buf[sizeof(struct vring_used) + sizeof(struct vring_used_elem) * QUEUE_SIZE];
    union {
        struct {
            virtio_blk_req_t req;
            uint8_t data[DATA_SIZE];
            uint8_t status;
        } requests[2];

        // Convenience accessor for requests[0].
        struct {
            virtio_blk_req_t req;
            uint8_t data[DATA_SIZE];
            uint8_t status;
        };
    };
} virtio_mem_t;

static void setup_queue(virtio_queue_t* queue, virtio_mem_t* mem) {
    memset(mem, 0, sizeof(virtio_mem_t));
    queue->size = QUEUE_SIZE;
    queue->desc = mem->desc;
    queue->avail = (struct vring_avail*)mem->avail_buf;
    queue->used = (struct vring_used*)mem->used_buf;
}

static void setup_block(block_t* block, const char* block_path, virtio_mem_t* mem) {
    block_init(block, block_path, (uintptr_t)mem, sizeof(*mem));
    setup_queue(&block->queue, mem);
}

static ssize_t mkblk(char* path) {
    int fd = mkstemp(path);
    if (fd >= 0) {
        uint8_t zeroes[SECTOR_SIZE * 8];
        memset(zeroes, 0, sizeof(zeroes));
        ssize_t ret = write(fd, zeroes, sizeof(zeroes));
        if (ret < 0)
            return ret;
    }
    return fd;
}

static void set_desc(virtio_mem_t* mem, size_t i, uint64_t off, uint32_t len, uint16_t next) {
    struct vring_desc* desc = &mem->desc[i];
    desc->addr = off;
    desc->len = len;
    desc->flags = next < QUEUE_SIZE ? VRING_DESC_F_NEXT : 0;
    desc->next = next;
}

static bool file_block_device_empty_queue(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-empty-queue.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    END_TEST;
}

static bool file_block_device_bad_ring(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-ring.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    block.queue.avail->ring[0] = QUEUE_SIZE;
    ASSERT_EQ(file_block_device(&block), MX_ERR_OUT_OF_RANGE);

    END_TEST;
}

static bool file_block_device_bad_header(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-header.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;

    block.queue.index = 0;
    set_desc(&mem, 0, sizeof(virtio_mem_t), 1, 0);
    ASSERT_EQ(file_block_device(&block), MX_ERR_OUT_OF_RANGE);
    ASSERT_EQ(block.queue.used->idx, 0u);

    block.queue.index = 0;
    set_desc(&mem, 0, UINT64_MAX, 0, 0);
    ASSERT_EQ(file_block_device(&block), MX_ERR_OUT_OF_RANGE);
    ASSERT_EQ(block.queue.used->idx, 0u);

    block.queue.index = 0;
    set_desc(&mem, 0, 0, UINT32_MAX, 0);
    ASSERT_EQ(file_block_device(&block), MX_ERR_OUT_OF_RANGE);
    ASSERT_EQ(block.queue.used->idx, 0u);

    block.queue.index = 0;
    set_desc(&mem, 0, UINT64_MAX, UINT32_MAX, 0);
    ASSERT_EQ(file_block_device(&block), MX_ERR_OUT_OF_RANGE);
    ASSERT_EQ(block.queue.used->idx, 0u);

    block.queue.index = 0;
    set_desc(&mem, 0, 0, 1, 0);
    ASSERT_EQ(file_block_device(&block), MX_ERR_INVALID_ARGS);
    ASSERT_EQ(block.queue.used->idx, 0u);

    END_TEST;
}

static bool file_block_device_bad_payload(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-payload.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, sizeof(virtio_mem_t), 1, 2);
    ASSERT_EQ(file_block_device(&block), MX_ERR_OUT_OF_RANGE);
    ASSERT_EQ(block.queue.used->idx, 0u);

    END_TEST;
}

static bool file_block_device_bad_status(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-status.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, data), DATA_SIZE, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, status), 0, QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_ERR_INVALID_ARGS);
    ASSERT_EQ(block.queue.used->idx, 0u);

    END_TEST;
}

static bool file_block_device_bad_request(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-request.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    mem.req.type = UINT32_MAX;

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, data), DATA_SIZE, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, 0u);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_IOERR);

    END_TEST;
}

static bool file_block_device_bad_flush(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-flush.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    mem.req.type = VIRTIO_BLK_T_FLUSH;
    mem.req.sector = 1;

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, 0u);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_IOERR);

    END_TEST;
}

static bool file_block_device_read(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-read.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    memset(mem.data, UINT8_MAX, DATA_SIZE);

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, data), DATA_SIZE, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    uint8_t expected[DATA_SIZE];
    memset(expected, 0, DATA_SIZE);
    ASSERT_EQ(memcmp(mem.data, expected, DATA_SIZE), 0);

    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, DATA_SIZE);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_OK);

    END_TEST;
}

static bool file_block_device_read_chain(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-read-chain.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    memset(mem.data, UINT8_MAX, DATA_SIZE);

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, data), DATA_SIZE / 2, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, data) + (DATA_SIZE / 2), DATA_SIZE / 2, 3);
    set_desc(&mem, 3, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    uint8_t expected[DATA_SIZE];
    memset(expected, 0, DATA_SIZE);
    ASSERT_EQ(memcmp(mem.data, expected, DATA_SIZE), 0);

    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, DATA_SIZE);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_OK);

    END_TEST;
}

static bool file_block_device_write(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-write.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    mem.req.type = VIRTIO_BLK_T_OUT;
    memset(mem.data, UINT8_MAX, DATA_SIZE);

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, data), DATA_SIZE, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    uint8_t actual[DATA_SIZE];
    ASSERT_EQ(lseek(block.fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(block.fd, actual, DATA_SIZE), DATA_SIZE);

    uint8_t expected[DATA_SIZE];
    memset(expected, UINT8_MAX, DATA_SIZE);
    ASSERT_EQ(memcmp(actual, expected, DATA_SIZE), 0);

    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, DATA_SIZE);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_OK);

    END_TEST;
}

static bool file_block_device_write_chain(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-write-chain.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    mem.req.type = VIRTIO_BLK_T_OUT;
    memset(mem.data, UINT8_MAX, DATA_SIZE);

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, data), DATA_SIZE / 2, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, data) + (DATA_SIZE / 2), DATA_SIZE / 2, 3);
    set_desc(&mem, 3, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    uint8_t actual[DATA_SIZE];
    ASSERT_EQ(lseek(block.fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(block.fd, actual, DATA_SIZE), DATA_SIZE);

    uint8_t expected[DATA_SIZE];
    memset(expected, UINT8_MAX, DATA_SIZE);
    ASSERT_EQ(memcmp(actual, expected, DATA_SIZE), 0);

    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, DATA_SIZE);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_OK);

    END_TEST;
}

static bool file_block_device_flush(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-flush.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    mem.req.type = VIRTIO_BLK_T_FLUSH;

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, 0u);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_OK);

    END_TEST;
}

static bool file_block_device_flush_data(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-flush-data.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    mem.req.type = VIRTIO_BLK_T_FLUSH;

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, data), DATA_SIZE, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, 128u);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_OK);

    END_TEST;
}

static mx_status_t write_sector(int fd, uint8_t value, uint32_t sector, size_t len) {
    if (len > SECTOR_SIZE)
        return MX_ERR_OUT_OF_RANGE;

    uint8_t buffer[SECTOR_SIZE];
    memset(buffer, value, len);

    ssize_t ret = lseek(fd, sector * SECTOR_SIZE, SEEK_SET);
    if (ret < 0)
        return MX_ERR_IO;
    ret = write(fd, buffer, len);
    if (ret < 0)
        return MX_ERR_IO;

    return MX_OK;
}

/* Queue up 2 read requests for different sectors and verify both will be
 * handled correctly.
 */
static bool file_block_device_multiple_descriptors(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-multiple-descriptors.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);

    // Request 1 (descriptors 0,1,2).
    const uint8_t request1_bitpattern = 0xaa;
    memset(mem.requests[0].data, UINT8_MAX, DATA_SIZE);
    mem.requests[0].req.type = VIRTIO_BLK_T_IN;
    mem.requests[0].req.sector = 0;
    set_desc(&mem, 0, offsetof(virtio_mem_t, requests[0].req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, requests[0].data), DATA_SIZE, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, requests[0].status), sizeof(uint8_t), QUEUE_SIZE);
    block.queue.avail->ring[0] = 0;

    // Request 2 (descriptors 3,4,5).
    const uint8_t request2_bitpattern = 0xdd;
    mem.requests[1].req.type = VIRTIO_BLK_T_IN;
    mem.requests[1].req.sector = 1;
    memset(mem.requests[1].data, UINT8_MAX, DATA_SIZE);
    block.queue.avail->ring[1] = 3;
    set_desc(&mem, 3, offsetof(virtio_mem_t, requests[1].req), sizeof(virtio_blk_req_t), 4);
    set_desc(&mem, 4, offsetof(virtio_mem_t, requests[1].data), DATA_SIZE, 5);
    set_desc(&mem, 5, offsetof(virtio_mem_t, requests[1].status), sizeof(uint8_t), QUEUE_SIZE);

    block.queue.index = 0;
    block.queue.avail->idx = 2;

    // Initalize block device. Write unique bit patterns to sector 1 and 2.
    ASSERT_EQ(write_sector(block.fd, request1_bitpattern, 0, DATA_SIZE), MX_OK);
    ASSERT_EQ(write_sector(block.fd, request2_bitpattern, 1, DATA_SIZE), MX_OK);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    // Verify request 1.
    uint8_t expected[DATA_SIZE];
    memset(expected, request1_bitpattern, DATA_SIZE);
    ASSERT_EQ(memcmp(mem.requests[0].data, expected, DATA_SIZE), 0);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);
    ASSERT_EQ(block.queue.used->ring[0].len, DATA_SIZE);

    // Verify request 2.
    memset(expected, request2_bitpattern, DATA_SIZE);
    ASSERT_EQ(memcmp(mem.requests[1].data, expected, DATA_SIZE), 0);
    ASSERT_EQ(block.queue.used->ring[1].id, 3u);
    ASSERT_EQ(block.queue.used->ring[1].len, DATA_SIZE);

    ASSERT_EQ(block.queue.used->idx, 2u);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_OK);

    END_TEST;
}

static bool file_block_device_read_only(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-read-only.XXXXXX";
    ASSERT_GE(mkblk(path), 0);

    virtio_mem_t mem;
    block_t block = {};
    setup_block(&block, path, &mem);
    block.queue.avail->idx = 1;
    mem.req.type = VIRTIO_BLK_T_OUT;
    memset(mem.data, UINT8_MAX, DATA_SIZE);
    block.virtio_device.features |= VIRTIO_BLK_F_RO;

    set_desc(&mem, 0, offsetof(virtio_mem_t, req), sizeof(virtio_blk_req_t), 1);
    set_desc(&mem, 1, offsetof(virtio_mem_t, data), DATA_SIZE, 2);
    set_desc(&mem, 2, offsetof(virtio_mem_t, status), sizeof(uint8_t), QUEUE_SIZE);
    ASSERT_EQ(file_block_device(&block), MX_OK);

    // Verify the buffer was returned to the used ring.
    ASSERT_EQ(block.queue.used->idx, 1u);
    ASSERT_EQ(block.queue.used->ring[0].id, 0u);

    // No bytes written and error status set.
    ASSERT_EQ(block.queue.used->ring[0].len, 0u);
    ASSERT_EQ(mem.status, VIRTIO_BLK_S_UNSUPP);

    // Read back bytes from the file.
    uint8_t actual[DATA_SIZE];
    ASSERT_EQ(lseek(block.fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(block.fd, actual, DATA_SIZE), DATA_SIZE);

    // The image file is initialized to all 0's and we attempted to write all
    // 1's. Verify that the file contents are unchanged.
    uint8_t expected[DATA_SIZE];
    memset(expected, 0, DATA_SIZE);
    ASSERT_EQ(memcmp(actual, expected, DATA_SIZE), 0);

    END_TEST;
}

BEGIN_TEST_CASE(block)
RUN_TEST(file_block_device_empty_queue)
RUN_TEST(file_block_device_bad_ring)
RUN_TEST(file_block_device_bad_header)
RUN_TEST(file_block_device_bad_payload)
RUN_TEST(file_block_device_bad_status)
RUN_TEST(file_block_device_bad_request)
RUN_TEST(file_block_device_bad_flush)
RUN_TEST(file_block_device_read)
RUN_TEST(file_block_device_read_chain)
RUN_TEST(file_block_device_write)
RUN_TEST(file_block_device_write_chain)
RUN_TEST(file_block_device_flush)
RUN_TEST(file_block_device_flush_data)
RUN_TEST(file_block_device_multiple_descriptors)
RUN_TEST(file_block_device_read_only)
END_TEST_CASE(block)
