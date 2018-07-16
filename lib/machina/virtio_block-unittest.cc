// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_ptr.h>
#include <virtio/block.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/virtio_block.h"
#include "garnet/lib/machina/virtio_queue.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

#define QUEUE_SIZE 8u
#define DATA_SIZE 512u

namespace machina {
namespace {

class VirtioBlockTest {
 public:
  VirtioBlockTest() : block_(phys_mem_), queue_(block_.request_queue()) {}

  ~VirtioBlockTest() {
    if (fd_ > 0) {
      close(fd_);
    }
  }

  zx_status_t Init(char* block_path) {
    fd_ = CreateBlockFile(block_path);
    if (fd_ < 0) {
      return ZX_ERR_IO;
    }

    fbl::unique_ptr<machina::BlockDispatcher> dispatcher;
    zx_status_t status = machina::BlockDispatcher::CreateFromPath(
        block_path, machina::BlockDispatcher::Mode::RW,
        machina::BlockDispatcher::DataPlane::FDIO, phys_mem_, &dispatcher);
    status = block_.SetDispatcher(std::move(dispatcher));
    if (status != ZX_OK) {
      return status;
    }

    return queue_.Init(QUEUE_SIZE);
  }

  zx_status_t WriteSector(uint8_t value, uint32_t sector, size_t len) {
    if (len > VirtioBlock::kSectorSize) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    uint8_t buffer[VirtioBlock::kSectorSize];
    memset(buffer, value, len);

    ssize_t ret = lseek(fd_, sector * VirtioBlock::kSectorSize, SEEK_SET);
    if (ret < 0) {
      return ZX_ERR_IO;
    }
    ret = write(fd_, buffer, len);
    if (ret < 0) {
      return ZX_ERR_IO;
    }

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
      if (ret < 0) {
        return static_cast<int>(ret);
      }
    }
    return fd;
  }

  int fd_ = 0;
  PhysMemFake phys_mem_;
  VirtioBlock block_;
  VirtioQueueFake queue_;
};

TEST(VirtioBlockTest, BadHeader) {
  char path[] = "/tmp/file-block-device-bad-header.XXXXXX";
  VirtioBlockTest test;
  ASSERT_EQ(test.Init(path), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t req = {};
  uint8_t status;

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&req, sizeof(req) - 1)
                .AppendWritable(&status, 1)
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&req, sizeof(req) + 1)
                .AppendWritable(&status, 1)
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);
}

TEST(VirtioBlockTest, BadPayload) {
  char path[] = "/tmp/file-block-device-bad-payload.XXXXXX";
  VirtioBlockTest test;
  ASSERT_EQ(test.Init(path), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t req = {};
  uint8_t status;

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&req, sizeof(req))
                .AppendReadable(UINTPTR_MAX, 1)
                .AppendWritable(&status, 1)
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);
}

TEST(VirtioBlockTest, BadStatus) {
  char path[] = "/tmp/file-block-device-bad-status.XXXXXX";
  VirtioBlockTest test;
  ASSERT_EQ(test.Init(path), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data[DATA_SIZE];
  uint8_t status = 0xff;

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data, DATA_SIZE)
                .AppendReadable(&status, 0)
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);
  ASSERT_EQ(status, 0xff);
}

TEST(VirtioBlock, BadRequest) {
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

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_UNSUPP);
}

TEST(VirtioBlock, BadFlush) {
  char path[] = "/tmp/file-block-device-bad-flush.XXXXXX";
  VirtioBlockTest test;
  ASSERT_EQ(test.Init(path), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t req = {};
  req.type = VIRTIO_BLK_T_FLUSH;
  req.sector = 1;
  uint8_t status;

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&req, sizeof(req))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);
}

TEST(VirtioBlock, Read) {
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

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendWritable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);

  uint8_t expected[DATA_SIZE];
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  memset(expected, 0, DATA_SIZE);
  ASSERT_EQ(memcmp(data, expected, DATA_SIZE), 0);
}

TEST(VirtioBlock, ReadChain) {
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

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendWritable(data1, sizeof(data1))
                .AppendWritable(data2, sizeof(data2))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);

  uint8_t expected[DATA_SIZE];
  memset(expected, 0, DATA_SIZE);
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(memcmp(data1, expected, DATA_SIZE), 0);
  ASSERT_EQ(memcmp(data2, expected, DATA_SIZE), 0);
  ASSERT_EQ(used, sizeof(data1) + sizeof(data2) + sizeof(status));
}

TEST(VirtioBlock, Write) {
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

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);

  int fd = test.fd();
  uint8_t actual[DATA_SIZE];
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd, actual, DATA_SIZE), DATA_SIZE);

  uint8_t expected[DATA_SIZE];
  memset(expected, UINT8_MAX, DATA_SIZE);
  ASSERT_EQ(memcmp(actual, expected, DATA_SIZE), 0);
}

TEST(VirtioBlockTest, WriteChain) {
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

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data1, sizeof(data1))
                .AppendReadable(data2, sizeof(data2))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);

  int fd = test.fd();
  uint8_t actual[DATA_SIZE];
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd, actual, DATA_SIZE), DATA_SIZE);

  uint8_t expected[DATA_SIZE];
  memset(expected, UINT8_MAX, DATA_SIZE);
  ASSERT_EQ(memcmp(actual, expected, DATA_SIZE), 0);
  ASSERT_EQ(used, sizeof(status));
}

TEST(VirtioBlockTest, Flush) {
  char path[] = "/tmp/file-block-device-flush.XXXXXX";
  VirtioBlockTest test;
  ASSERT_EQ(test.Init(path), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  header.type = VIRTIO_BLK_T_FLUSH;
  uint8_t status = 0;

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
}

TEST(VirtioBlockTest, FlushWithData) {
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

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendWritable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(used, sizeof(status));
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
TEST(VirtioBlockTest, ReadMultipleDescriptors) {
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
  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&request1.header, sizeof(request1.header))
                .AppendWritable(request1.data, sizeof(request1.data))
                .AppendWritable(&request1.status, sizeof(request1.status))
                .Build(&request1.desc),
            ZX_OK);

  // Request 2 (descriptors 3,4,5).
  TestBlockRequest request2;
  const uint8_t request2_bitpattern = 0xdd;
  memset(request2.data, UINT8_MAX, DATA_SIZE);
  request2.used = 0;
  request2.header.type = VIRTIO_BLK_T_IN;
  request2.header.sector = 1;
  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&request2.header, sizeof(request2.header))
                .AppendWritable(request2.data, sizeof(request2.data))
                .AppendWritable(&request2.status, sizeof(request2.status))
                .Build(&request2.desc),
            ZX_OK);

  // Initalize block device. Write unique bit patterns to sector 1 and 2.
  ASSERT_EQ(test.WriteSector(request1_bitpattern, 0, DATA_SIZE), ZX_OK);
  ASSERT_EQ(test.WriteSector(request2_bitpattern, 1, DATA_SIZE), ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(),
                                            request1.desc, &request1.used),
            ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(),
                                            request2.desc, &request2.used),
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
}

TEST(VirtioBlockTest, WriteToReadOnlyDevice) {
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

  ASSERT_EQ(test.queue()
                .BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(test.block().HandleBlockRequest(test.block().request_queue(), desc,
                                            &used),
            ZX_OK);

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
}

}  // namespace
}  // namespace machina
