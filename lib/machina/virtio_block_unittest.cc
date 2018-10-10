// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <lib/gtest/test_loop_fixture.h>
#include <virtio/block.h>
#include <virtio/virtio_ring.h>

#include "garnet/lib/machina/device/virtio_queue.h"
#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/vcpu.h"
#include "garnet/lib/machina/virtio_block.h"
#include "garnet/lib/machina/virtio_queue_fake.h"

namespace machina {
namespace {

static constexpr uint16_t kVirtioBlockQueueSize = 32;
static constexpr ssize_t kDataSize = 512;

class VirtioBlockTest : public ::gtest::TestLoopFixture {
 protected:
  fbl::unique_fd fd_;
  PhysMemFake phys_mem_;
  std::unique_ptr<VirtioBlock> block_;
  std::unique_ptr<VirtioQueueFake> queue_;

  zx_status_t Init(char* block_path, bool read_only) {
    fd_ = CreateBlockFile(block_path);
    if (!fd_) {
      return ZX_ERR_IO;
    }

    std::unique_ptr<machina::BlockDispatcher> dispatcher;
    zx_status_t status = machina::BlockDispatcher::CreateFromPath(
        block_path,
        read_only ? fuchsia::guest::device::BlockMode::READ_ONLY
                  : fuchsia::guest::device::BlockMode::READ_WRITE,
        fuchsia::guest::device::BlockFormat::RAW, phys_mem_, &dispatcher);
    if (status != ZX_OK) {
      return status;
    }
    block_ = std::make_unique<VirtioBlock>(phys_mem_, std::move(dispatcher));
    queue_ = std::make_unique<VirtioQueueFake>(block_->request_queue(),
                                               kVirtioBlockQueueSize);
    return ZX_OK;
  }

  zx_status_t WriteSector(uint8_t value, uint32_t sector, size_t len) {
    if (len > VirtioBlock::kSectorSize) {
      return ZX_ERR_OUT_OF_RANGE;
    }

    uint8_t buffer[VirtioBlock::kSectorSize];
    memset(buffer, value, len);

    ssize_t ret = lseek(fd_.get(), sector * VirtioBlock::kSectorSize, SEEK_SET);
    if (ret < 0) {
      return ZX_ERR_IO;
    }
    ret = write(fd_.get(), buffer, len);
    if (ret < 0) {
      return ZX_ERR_IO;
    }

    return ZX_OK;
  }

 private:
  fbl::unique_fd CreateBlockFile(char* path) {
    fbl::unique_fd fd(mkstemp(path));
    if (!fd) {
      FXL_LOG(ERROR) << "Failed to create " << path << ": " << strerror(errno);
      return fd;
    }
    uint8_t zeroes[VirtioBlock::kSectorSize * 8];
    memset(zeroes, 0, sizeof(zeroes));
    ssize_t ret = write(fd.get(), zeroes, sizeof(zeroes));
    if (ret < 0) {
      FXL_LOG(ERROR) << "Failed to write to " << path << ": " << strerror(errno);
      fd.reset();
    }
    return fd;
  }
};

TEST_F(VirtioBlockTest, BadHeader) {
  char path[] = "/tmp/file-block-device-bad-header.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t req = {};
  uint8_t status;

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&req, sizeof(req) - 1)
                .AppendWritable(&status, 1)
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&req, sizeof(req) + 1)
                .AppendWritable(&status, 1)
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);
}

TEST_F(VirtioBlockTest, BadPayload) {
  char path[] = "/tmp/file-block-device-bad-payload.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t req = {};
  uint8_t status;

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&req, sizeof(req))
                .AppendReadable(UINTPTR_MAX, 1)
                .AppendWritable(&status, 1)
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);
}

TEST_F(VirtioBlockTest, BadStatus) {
  char path[] = "/tmp/file-block-device-bad-status.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data[kDataSize];
  uint8_t status = 0xff;

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data, kDataSize)
                .AppendReadable(&status, 0)
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);
  ASSERT_EQ(status, 0xff);
}

TEST_F(VirtioBlockTest, BadRequest) {
  char path[] = "/tmp/file-block-device-bad-request.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  // Build a request with an invalid 'type'. The device will handle the
  // request successfully but indicate an error to the driver via the
  // status field in the request.
  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data[kDataSize];
  uint8_t status = 0;
  header.type = UINT32_MAX;

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_UNSUPP);
}

TEST_F(VirtioBlockTest, BadFlush) {
  char path[] = "/tmp/file-block-device-bad-flush.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t req = {};
  req.type = VIRTIO_BLK_T_FLUSH;
  req.sector = 1;
  uint8_t status;

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&req, sizeof(req))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);
}

TEST_F(VirtioBlockTest, Read) {
  char path[] = "/tmp/file-block-device-read.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data[kDataSize];
  uint8_t status = 0;
  header.type = VIRTIO_BLK_T_IN;
  memset(data, UINT8_MAX, sizeof(data));

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendWritable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);

  uint8_t expected[kDataSize];
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  memset(expected, 0, kDataSize);
  ASSERT_EQ(memcmp(data, expected, kDataSize), 0);
}

TEST_F(VirtioBlockTest, ReadChain) {
  char path[] = "/tmp/file-block-device-read-chain.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data1[kDataSize];
  uint8_t data2[kDataSize];
  uint8_t status = 0;
  header.type = VIRTIO_BLK_T_IN;
  memset(data1, UINT8_MAX, sizeof(data1));
  memset(data2, UINT8_MAX, sizeof(data2));

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendWritable(data1, sizeof(data1))
                .AppendWritable(data2, sizeof(data2))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);

  uint8_t expected[kDataSize];
  memset(expected, 0, kDataSize);
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(memcmp(data1, expected, kDataSize), 0);
  ASSERT_EQ(memcmp(data2, expected, kDataSize), 0);
  ASSERT_EQ(used, sizeof(data1) + sizeof(data2) + sizeof(status));
}

TEST_F(VirtioBlockTest, Write) {
  char path[] = "/tmp/file-block-device-write.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data[kDataSize];
  uint8_t status = 0;
  header.type = VIRTIO_BLK_T_OUT;
  memset(data, UINT8_MAX, sizeof(data));

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);

  uint8_t actual[kDataSize];
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(lseek(fd_.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd_.get(), actual, kDataSize), kDataSize);

  uint8_t expected[kDataSize];
  memset(expected, UINT8_MAX, kDataSize);
  ASSERT_EQ(memcmp(actual, expected, kDataSize), 0);
}

TEST_F(VirtioBlockTest, WriteChain) {
  char path[] = "/tmp/file-block-device-write-chain.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data1[kDataSize];
  uint8_t data2[kDataSize];
  uint8_t status = 0;
  header.type = VIRTIO_BLK_T_OUT;
  memset(data1, UINT8_MAX, sizeof(data1));
  memset(data2, UINT8_MAX, sizeof(data2));

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data1, sizeof(data1))
                .AppendReadable(data2, sizeof(data2))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);

  uint8_t actual[kDataSize];
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(lseek(fd_.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd_.get(), actual, kDataSize), kDataSize);

  uint8_t expected[kDataSize];
  memset(expected, UINT8_MAX, kDataSize);
  ASSERT_EQ(memcmp(actual, expected, kDataSize), 0);
  ASSERT_EQ(used, sizeof(status));
}

TEST_F(VirtioBlockTest, Flush) {
  char path[] = "/tmp/file-block-device-flush.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  header.type = VIRTIO_BLK_T_FLUSH;
  uint8_t status = 0;

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
}

TEST_F(VirtioBlockTest, FlushWithData) {
  char path[] = "/tmp/file-block-device-flush-data.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data[kDataSize];
  uint8_t status = 0;
  header.type = VIRTIO_BLK_T_FLUSH;
  memset(data, UINT8_MAX, sizeof(data));

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendWritable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);

  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);
  ASSERT_EQ(status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(used, sizeof(status));
}

struct TestBlockRequest {
  uint16_t desc;
  uint32_t used;
  virtio_blk_req_t header;
  uint8_t data[kDataSize];
  uint8_t status;
};

// Queue up 2 read requests for different sectors and verify both will be
// handled correctly.
TEST_F(VirtioBlockTest, ReadMultipleDescriptors) {
  char path[] = "/tmp/file-block-multiple-descriptors.XXXXXX";
  ASSERT_EQ(Init(path, false), ZX_OK);

  // Request 1 (descriptors 0,1,2).
  TestBlockRequest request1;
  const uint8_t request1_bitpattern = 0xaa;
  memset(request1.data, UINT8_MAX, kDataSize);
  request1.used = 0;
  request1.header.type = VIRTIO_BLK_T_IN;
  request1.header.sector = 0;
  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&request1.header, sizeof(request1.header))
                .AppendWritable(request1.data, sizeof(request1.data))
                .AppendWritable(&request1.status, sizeof(request1.status))
                .Build(&request1.desc),
            ZX_OK);

  // Request 2 (descriptors 3,4,5).
  TestBlockRequest request2;
  const uint8_t request2_bitpattern = 0xdd;
  memset(request2.data, UINT8_MAX, kDataSize);
  request2.used = 0;
  request2.header.type = VIRTIO_BLK_T_IN;
  request2.header.sector = 1;
  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&request2.header, sizeof(request2.header))
                .AppendWritable(request2.data, sizeof(request2.data))
                .AppendWritable(&request2.status, sizeof(request2.status))
                .Build(&request2.desc),
            ZX_OK);

  // Initalize block device. Write unique bit patterns to sector 1 and 2.
  ASSERT_EQ(WriteSector(request1_bitpattern, 0, kDataSize), ZX_OK);
  ASSERT_EQ(WriteSector(request2_bitpattern, 1, kDataSize), ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), request1.desc,
                                       &request1.used),
            ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), request2.desc,
                                       &request2.used),
            ZX_OK);

  // Verify request 1.
  uint8_t expected[kDataSize];
  memset(expected, request1_bitpattern, kDataSize);
  ASSERT_EQ(memcmp(request1.data, expected, kDataSize), 0);
  ASSERT_EQ(request1.status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(request1.used, kDataSize + sizeof(request1.status));

  // Verify request 2.
  memset(expected, request2_bitpattern, kDataSize);
  ASSERT_EQ(memcmp(request2.data, expected, kDataSize), 0);
  ASSERT_EQ(request2.status, VIRTIO_BLK_S_OK);
  ASSERT_EQ(request2.used, kDataSize + sizeof(request2.status));
}

TEST_F(VirtioBlockTest, WriteToReadOnlyDevice) {
  char path[] = "/tmp/file-block-device-read-only.XXXXXX";
  ASSERT_EQ(Init(path, true), ZX_OK);

  uint16_t desc;
  uint32_t used = 0;
  virtio_blk_req_t header = {};
  uint8_t data[kDataSize];
  uint8_t status = 0;
  header.type = VIRTIO_BLK_T_OUT;

  ASSERT_EQ(queue_->BuildDescriptor()
                .AppendReadable(&header, sizeof(header))
                .AppendReadable(data, sizeof(data))
                .AppendWritable(&status, sizeof(status))
                .Build(&desc),
            ZX_OK);
  ASSERT_EQ(block_->HandleBlockRequest(block_->request_queue(), desc, &used),
            ZX_OK);

  // No bytes written and error status set.
  ASSERT_EQ(status, VIRTIO_BLK_S_IOERR);
  ASSERT_EQ(used, sizeof(status));

  // Read back bytes from the file.
  uint8_t actual[kDataSize];
  ASSERT_EQ(lseek(fd_.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(read(fd_.get(), actual, kDataSize), kDataSize);

  // The image file is initialized to all 0's and we attempted to write all
  // 1's. Verify that the file contents are unchanged.
  uint8_t expected[kDataSize];
  memset(expected, 0, kDataSize);
  ASSERT_EQ(memcmp(actual, expected, kDataSize), 0);
}

}  // namespace
}  // namespace machina
