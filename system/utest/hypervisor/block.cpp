// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_ptr.h>
#include <hypervisor/block.h>
#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <unittest/unittest.h>
#include <virtio/block.h>
#include <virtio/virtio_ring.h>

#include "virtio_queue_fake.h"

#define QUEUE_SIZE 8u
#define DATA_SIZE 512u

class VirtioBlockTest {
public:
    VirtioBlockTest() : block_(0, UINTPTR_MAX), queue_(&block_.queue()) {}

    ~VirtioBlockTest() {
        if (fd_ > 0)
            close(fd_);
    }

    zx_status_t Init(char* block_path) {
        fd_ = CreateBlockFile(block_path);
        if (fd_ < 0)
            return ZX_ERR_IO;

        PhysMem phys_mem;
        zx_status_t status = block_.Init(block_path, phys_mem);
        if (status != ZX_OK)
            return status;

        return queue_.Init(QUEUE_SIZE);
    }

    zx_status_t WriteSector(uint8_t value, uint32_t sector, size_t len) {
        if (len > VirtioBlock::kSectorSize)
            return ZX_ERR_OUT_OF_RANGE;

        uint8_t buffer[VirtioBlock::kSectorSize];
        memset(buffer, value, len);

        ssize_t ret = lseek(fd_, sector * VirtioBlock::kSectorSize, SEEK_SET);
        if (ret < 0)
            return ZX_ERR_IO;
        ret = write(fd_, buffer, len);
        if (ret < 0)
            return ZX_ERR_IO;

        return ZX_OK;
    }

    VirtioBlock& block() { return block_; }

    VirtioQueueFake& queue() { return queue_; }

    int fd() const { return fd_; }

private:
    int CreateBlockFile(char* path) {
        int fd = mkstemp(path);
        if (fd >= 0) {
            uint8_t zeroes[VirtioBlock::kSectorSize * 8];
            memset(zeroes, 0, sizeof(zeroes));
            ssize_t ret = write(fd, zeroes, sizeof(zeroes));
            if (ret < 0)
                return static_cast<int>(ret);
        }
        return fd;
    }

    int fd_ = 0;
    VirtioBlock block_;
    VirtioQueueFake queue_;
};

static bool file_block_device_bad_header(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-header.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t req = {};
    uint8_t status;

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&req, sizeof(req) - 1)
            .AppendWriteable(&status, 1)
            .Build(&desc),
        ZX_OK);
    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);
    ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&req, sizeof(req) + 1)
            .AppendWriteable(&status, 1)
            .Build(&desc),
        ZX_OK);
    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);
    ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);

    END_TEST;
}

static bool file_block_device_bad_payload(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-payload.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t req = {};
    uint8_t status;

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&req, sizeof(req))
            .AppendReadable(UINTPTR_MAX, 1)
            .AppendWriteable(&status, 1)
            .Build(&desc),
        ZX_OK);

    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);
    END_TEST;
}

static bool file_block_device_bad_status(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-status.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    uint8_t data[DATA_SIZE];
    uint8_t status = 0xff;

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendReadable(data, DATA_SIZE)
            .AppendReadable(&status, 0)
            .Build(&desc),
        ZX_OK);

    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);
    ASSERT_EQ(status, 0xff);

    END_TEST;
}

static bool file_block_device_bad_request(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-request.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    // Build a request with an invalid 'type'. The device will handle the
    // request successfully but indicate an error to the driver via the
    // status field in the request.
    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    uint8_t data[DATA_SIZE];
    uint8_t status = 0;
    header.type = UINT32_MAX;

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendReadable(data, sizeof(data))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);

    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);
    ASSERT_EQ(status, VIRTIO_BLK_S_UNSUPP);

    END_TEST;
}

static bool file_block_device_bad_flush(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-bad-flush.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t req = {};
    req.type = VIRTIO_BLK_T_FLUSH;
    req.sector = 1;
    uint8_t status;

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&req, sizeof(req))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);

    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);
    ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);

    END_TEST;
}

static bool file_block_device_read(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-read.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    uint8_t data[DATA_SIZE];
    uint8_t status = 0;
    header.type = VIRTIO_BLK_T_IN;
    memset(data, UINT8_MAX, sizeof(data));

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendWriteable(data, sizeof(data))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);

    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);

    uint8_t expected[DATA_SIZE];
    ASSERT_EQ(status, VIRTIO_BLK_S_OK);
    memset(expected, 0, DATA_SIZE);
    ASSERT_EQ(memcmp(data, expected, DATA_SIZE), 0);

    END_TEST;
}

static bool file_block_device_read_chain(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-read-chain.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    uint8_t data1[DATA_SIZE];
    uint8_t data2[DATA_SIZE];
    uint8_t status = 0;
    header.type = VIRTIO_BLK_T_IN;
    memset(data1, UINT8_MAX, sizeof(data1));
    memset(data2, UINT8_MAX, sizeof(data2));

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendWriteable(data1, sizeof(data1))
            .AppendWriteable(data2, sizeof(data2))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);
    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);

    uint8_t expected[DATA_SIZE];
    memset(expected, 0, DATA_SIZE);
    ASSERT_EQ(status, VIRTIO_BLK_S_OK);
    ASSERT_EQ(memcmp(data1, expected, DATA_SIZE), 0);
    ASSERT_EQ(memcmp(data2, expected, DATA_SIZE), 0);
    ASSERT_EQ(used, sizeof(data1) + sizeof(data2) + sizeof(status));

    END_TEST;
}

static bool file_block_device_write(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-write.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    uint8_t data[DATA_SIZE];
    uint8_t status = 0;
    header.type = VIRTIO_BLK_T_OUT;
    memset(data, UINT8_MAX, sizeof(data));

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendReadable(data, sizeof(data))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);
    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);

    int fd = test.fd();
    uint8_t actual[DATA_SIZE];
    ASSERT_EQ(status, VIRTIO_BLK_S_OK);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, actual, DATA_SIZE), DATA_SIZE);

    uint8_t expected[DATA_SIZE];
    memset(expected, UINT8_MAX, DATA_SIZE);
    ASSERT_EQ(memcmp(actual, expected, DATA_SIZE), 0);


    END_TEST;
}

static bool file_block_device_write_chain(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-write-chain.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    uint8_t data1[DATA_SIZE];
    uint8_t data2[DATA_SIZE];
    uint8_t status = 0;
    header.type = VIRTIO_BLK_T_OUT;
    memset(data1, UINT8_MAX, sizeof(data1));
    memset(data2, UINT8_MAX, sizeof(data2));

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendReadable(data1, sizeof(data1))
            .AppendReadable(data2, sizeof(data2))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);
    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);

    int fd = test.fd();
    uint8_t actual[DATA_SIZE];
    ASSERT_EQ(status, VIRTIO_BLK_S_OK);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, actual, DATA_SIZE), DATA_SIZE);

    uint8_t expected[DATA_SIZE];
    memset(expected, UINT8_MAX, DATA_SIZE);
    ASSERT_EQ(memcmp(actual, expected, DATA_SIZE), 0);
    ASSERT_EQ(used, sizeof(status));

    END_TEST;
}

static bool file_block_device_flush(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-flush.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    header.type = VIRTIO_BLK_T_FLUSH;
    uint8_t status = 0;

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);
    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);
    ASSERT_EQ(status, VIRTIO_BLK_S_OK);

    END_TEST;
}

static bool file_block_device_flush_data(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-flush-data.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    uint8_t data[DATA_SIZE];
    uint8_t status = 0;
    header.type = VIRTIO_BLK_T_FLUSH;
    memset(data, UINT8_MAX, sizeof(data));

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendWriteable(data, sizeof(data))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);

    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);
    ASSERT_EQ(status, VIRTIO_BLK_S_OK);
    ASSERT_EQ(used, sizeof(status));

    END_TEST;
}

struct TestBlockRequest {
    uint16_t desc;
    uint32_t used;
    virtio_blk_req_t header;
    uint8_t data[DATA_SIZE];
    uint8_t status;
};

// Queue up 2 read requests for different sectors and verify both will be
// handled correctly.
static bool file_block_device_multiple_descriptors(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-multiple-descriptors.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    // Request 1 (descriptors 0,1,2).
    TestBlockRequest request1;
    const uint8_t request1_bitpattern = 0xaa;
    memset(request1.data, UINT8_MAX, DATA_SIZE);
    request1.used = 0;
    request1.header.type = VIRTIO_BLK_T_IN;
    request1.header.sector = 0;
    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&request1.header, sizeof(request1.header))
            .AppendWriteable(request1.data, sizeof(request1.data))
            .AppendWriteable(&request1.status, sizeof(request1.status))
            .Build(&request1.desc),
        ZX_OK);

    // Request 2 (descriptors 3,4,5).
    TestBlockRequest request2;
    const uint8_t request2_bitpattern = 0xdd;
    memset(request2.data, UINT8_MAX, DATA_SIZE);
    request2.used = 0;
    request2.header.type = VIRTIO_BLK_T_IN;
    request2.header.sector = 1;
    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&request2.header, sizeof(request2.header))
            .AppendWriteable(request2.data, sizeof(request2.data))
            .AppendWriteable(&request2.status, sizeof(request2.status))
            .Build(&request2.desc),
        ZX_OK);

    // Initalize block device. Write unique bit patterns to sector 1 and 2.
    ASSERT_EQ(test.WriteSector(request1_bitpattern, 0, DATA_SIZE), ZX_OK);
    ASSERT_EQ(test.WriteSector(request2_bitpattern, 1, DATA_SIZE), ZX_OK);
    ASSERT_EQ(
        test.block().HandleBlockRequest(&test.block().queue(), request1.desc, &request1.used),
        ZX_OK);
    ASSERT_EQ(
        test.block().HandleBlockRequest(&test.block().queue(), request2.desc, &request2.used),
        ZX_OK);

    // Verify request 1.
    uint8_t expected[DATA_SIZE];
    memset(expected, request1_bitpattern, DATA_SIZE);
    ASSERT_EQ(memcmp(request1.data, expected, DATA_SIZE), 0);
    ASSERT_EQ(request1.status, VIRTIO_BLK_S_OK);
    ASSERT_EQ(request1.used, DATA_SIZE + sizeof(request1.status));

    // Verify request 2.
    memset(expected, request2_bitpattern, DATA_SIZE);
    ASSERT_EQ(memcmp(request2.data, expected, DATA_SIZE), 0);
    ASSERT_EQ(request2.status, VIRTIO_BLK_S_OK);
    ASSERT_EQ(request2.used, DATA_SIZE + sizeof(request2.status));

    END_TEST;
}

static bool file_block_device_read_only(void) {
    BEGIN_TEST;

    char path[] = "/tmp/file-block-device-read-only.XXXXXX";
    VirtioBlockTest test;
    ASSERT_EQ(test.Init(path), ZX_OK);

    uint16_t desc;
    uint32_t used = 0;
    virtio_blk_req_t header = {};
    uint8_t data[DATA_SIZE];
    uint8_t status = 0;
    header.type = VIRTIO_BLK_T_OUT;
    test.block().add_device_features(VIRTIO_BLK_F_RO);

    ASSERT_EQ(
        test.queue().BuildDescriptor()
            .AppendReadable(&header, sizeof(header))
            .AppendReadable(data, sizeof(data))
            .AppendWriteable(&status, sizeof(status))
            .Build(&desc),
        ZX_OK);
    ASSERT_EQ(test.block().HandleBlockRequest(&test.block().queue(), desc, &used), ZX_OK);

    // No bytes written and error status set.
    ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);
    ASSERT_EQ(used, sizeof(status));

    // Read back bytes from the file.
    int fd = test.fd();
    uint8_t actual[DATA_SIZE];
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, actual, DATA_SIZE), DATA_SIZE);

    // The image file is initialized to all 0's and we attempted to write all
    // 1's. Verify that the file contents are unchanged.
    uint8_t expected[DATA_SIZE];
    memset(expected, 0, DATA_SIZE);
    ASSERT_EQ(memcmp(actual, expected, DATA_SIZE), 0);

    END_TEST;
}

BEGIN_TEST_CASE(virtio_block)
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
END_TEST_CASE(virtio_block)
