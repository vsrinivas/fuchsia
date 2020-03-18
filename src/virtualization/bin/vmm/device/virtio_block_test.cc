// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <virtio/block.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/virtualization/bin/vmm/device/block.h"
#include "src/virtualization/bin/vmm/device/test_with_device.h"
#include "src/virtualization/bin/vmm/device/virtio_queue_fake.h"

static constexpr char kVirtioBlockUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_block#meta/virtio_block.cmx";
static constexpr uint16_t kNumQueues = 1;
static constexpr uint16_t kQueueSize = 16;
static constexpr size_t kQueueDataSize = 10 * fuchsia::io::MAX_BUF;

static constexpr char kVirtioBlockId[] = "block-id";
static constexpr size_t kNumSectors = 2;
static constexpr uint8_t kSectorBytes[kNumSectors] = {0xab, 0xcd};

class VirtioBlockTest : public TestWithDevice {
 protected:
  VirtioBlockTest() : request_queue_(phys_mem_, kQueueDataSize * kNumQueues, kQueueSize) {}

  void SetUp() override {
    // Launch device process.
    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = LaunchDevice(kVirtioBlockUrl, request_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Setup block file.
    // Open the file twice; once to get a FilePtr to provide to the
    // virtio_block process and another to retain to verify the file
    // contents.
    char path_template[] = "/tmp/block.XXXXXX";
    fd_ = CreateBlockFile(path_template);
    ASSERT_TRUE(fd_);
    zx_handle_t handle;
    status = fdio_get_service_handle(fd_.release(), &handle);
    ASSERT_EQ(ZX_OK, status);
    fuchsia::io::FilePtr file;
    file.Bind(zx::channel(handle));
    fd_ = fbl::unique_fd(open(path_template, O_RDWR));
    ASSERT_TRUE(fd_);

    // Start device execution.
    services_->Connect(block_.NewRequest());
    RunLoopUntilIdle();

    uint64_t size;
    status = block_->Start(std::move(start_info), kVirtioBlockId,
                           fuchsia::virtualization::BlockMode::READ_WRITE,
                           fuchsia::virtualization::BlockFormat::RAW, std::move(file), &size);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(kBlockSectorSize * kNumSectors, size);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&request_queue_};
    for (size_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(kQueueDataSize * i, kQueueDataSize);
      status = block_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
    }
  }

  fbl::unique_fd fd_;
  fuchsia::virtualization::hardware::VirtioBlockSyncPtr block_;
  VirtioQueueFake request_queue_;

 private:
  fbl::unique_fd CreateBlockFile(char* path) {
    fbl::unique_fd fd(mkstemp(path));
    if (!fd) {
      FX_LOGS(ERROR) << "Failed to create " << path << ": " << strerror(errno);
      return fd;
    }
    std::vector<uint8_t> buf(kBlockSectorSize * kNumSectors);
    auto addr = buf.data();
    for (uint8_t byte : kSectorBytes) {
      memset(addr, byte, kBlockSectorSize);
      addr += kBlockSectorSize;
    }
    ssize_t ret = pwrite(fd.get(), buf.data(), buf.size(), 0);
    if (ret < 0) {
      FX_LOGS(ERROR) << "Failed to zero " << path << ": " << strerror(errno);
      fd.reset();
    }
    return fd;
  }
};

TEST_F(VirtioBlockTest, BadHeaderShort) {
  uint8_t header[sizeof(virtio_blk_req_t) - 1] = {};
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(header, sizeof(header))
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);
}

TEST_F(VirtioBlockTest, BadHeaderLong) {
  uint8_t header[sizeof(virtio_blk_req_t) + 1] = {};
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(header, sizeof(header))
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);
}

TEST_F(VirtioBlockTest, BadPayload) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_IN,
  };
  uint8_t* sector;
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&sector, kBlockSectorSize + 1)
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);
}

TEST_F(VirtioBlockTest, BadStatus) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_IN,
  };
  uint8_t* sector;
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&sector, kBlockSectorSize)
                           .AppendWritableDescriptor(&blk_status, 2)
                           .Build();
  ASSERT_EQ(ZX_OK, status);
  *blk_status = UINT8_MAX;

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(UINT8_MAX, *blk_status);
}

TEST_F(VirtioBlockTest, BadRequestType) {
  virtio_blk_req_t header = {
      .type = UINT32_MAX,
  };
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_UNSUPP, *blk_status);
}

TEST_F(VirtioBlockTest, Read) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_IN,
  };
  uint8_t* sector;
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&sector, kBlockSectorSize)
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_OK, *blk_status);
  for (size_t i = 0; i < kBlockSectorSize; i++) {
    EXPECT_EQ(kSectorBytes[0], sector[i]) << " mismatched byte " << i;
  }
}

TEST_F(VirtioBlockTest, ReadMultipleDescriptors) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_IN,
  };
  uint8_t* sector_1;
  uint8_t* sector_2;
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&sector_1, kBlockSectorSize)
                           .AppendWritableDescriptor(&sector_2, kBlockSectorSize)
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_OK, *blk_status);
  for (size_t i = 0; i < kBlockSectorSize; i++) {
    EXPECT_EQ(kSectorBytes[0], sector_1[i]) << " mismatched byte " << i;
    EXPECT_EQ(kSectorBytes[1], sector_2[i]) << " mismatched byte " << i;
  }
}

TEST_F(VirtioBlockTest, Write) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_OUT,
  };
  std::vector<uint8_t> sector(kBlockSectorSize, UINT8_MAX);
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendReadableDescriptor(sector.data(), sector.size())
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_OK, *blk_status);
}

TEST_F(VirtioBlockTest, WriteMultipleDescriptors) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_OUT,
      .sector = 0,
  };

  std::vector<uint8_t> block_1(kBlockSectorSize, 0xff);
  std::vector<uint8_t> block_2(kBlockSectorSize, 0xab);
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendReadableDescriptor(block_1.data(), block_1.size())
                           .AppendReadableDescriptor(block_2.data(), block_2.size())
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_OK, *blk_status);

  std::vector<uint8_t> result(2 * kBlockSectorSize, 0);
  ASSERT_EQ(pread(fd_.get(), result.data(), result.size(), 0), static_cast<ssize_t>(result.size()));
  ASSERT_EQ(memcmp(result.data(), block_1.data(), block_1.size()), 0);
  ASSERT_EQ(memcmp(result.data() + block_1.size(), block_2.data(), block_2.size()), 0);
}

TEST_F(VirtioBlockTest, Sync) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_FLUSH,
  };
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_OK, *blk_status);
}

TEST_F(VirtioBlockTest, SyncWithData) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_FLUSH,
  };
  std::vector<uint8_t> sector(kBlockSectorSize);
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendReadableDescriptor(sector.data(), sector.size())
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_OK, *blk_status);
}

TEST_F(VirtioBlockTest, SyncNonZeroSector) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_FLUSH,
      .sector = 1,
  };
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);
}

TEST_F(VirtioBlockTest, Id) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_GET_ID,
  };
  char* id;
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&id, VIRTIO_BLK_ID_BYTES)
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_OK, *blk_status);
  EXPECT_EQ(0, memcmp(id, kVirtioBlockId, sizeof(kVirtioBlockId)));
}

TEST_F(VirtioBlockTest, IdLengthIncorrect) {
  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_GET_ID,
  };
  char* id;
  uint8_t* blk_status;
  zx_status_t status = DescriptorChainBuilder(request_queue_)
                           .AppendReadableDescriptor(&header, sizeof(header))
                           .AppendWritableDescriptor(&id, VIRTIO_BLK_ID_BYTES + 1)
                           .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);
}
