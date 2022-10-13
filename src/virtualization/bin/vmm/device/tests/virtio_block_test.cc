// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>
#include <virtio/block.h>

#include "src/virtualization/bin/vmm/device/block.h"
#include "src/virtualization/bin/vmm/device/tests/test_with_device.h"
#include "src/virtualization/bin/vmm/device/tests/virtio_queue_fake.h"

static constexpr uint16_t kNumQueues = 1;
static constexpr uint16_t kQueueSize = 16;
static constexpr size_t kQueueDataSize = 10 * fuchsia::io::MAX_BUF;

static constexpr char kVirtioBlockId[] = "block-id";
static constexpr size_t kNumSectors = 2;
static constexpr uint8_t kSectorBytes[kNumSectors] = {0xab, 0xcd};

constexpr auto kVirtioBlockUrl = "fuchsia-pkg://fuchsia.com/virtio_block#meta/virtio_block.cm";

struct VirtioBlockTestParam {
  std::string test_name;
  std::string component_url;
};

class VirtioBlockTest : public TestWithDevice,
                        public ::testing::WithParamInterface<VirtioBlockTestParam> {
 protected:
  VirtioBlockTest() : request_queue_(phys_mem_, kQueueDataSize * kNumQueues, kQueueSize) {}

  void SetUp() override {
    using component_testing::ChildRef;
    using component_testing::ParentRef;
    using component_testing::Protocol;
    using component_testing::RealmBuilder;
    using component_testing::RealmRoot;
    using component_testing::Route;

    constexpr auto kComponentName = "virtio_block";
    auto component_url = kVirtioBlockUrl;

    auto realm_builder = RealmBuilder::Create();
    realm_builder.AddChild(kComponentName, component_url);

    realm_builder
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::logger::LogSink::Name_},
                                Protocol{fuchsia::tracing::provider::Registry::Name_},
                            },
                        .source = ParentRef(),
                        .targets = {ChildRef{kComponentName}}})
        .AddRoute(Route{.capabilities =
                            {
                                Protocol{fuchsia::virtualization::hardware::VirtioBlock::Name_},
                            },
                        .source = ChildRef{kComponentName},
                        .targets = {ParentRef()}});

    realm_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
  }

  struct StartDeviceOptions {
    uint32_t negotiated_features;
    fuchsia::virtualization::BlockMode block_mode;

    static StartDeviceOptions Default() {
      return {0, fuchsia::virtualization::BlockMode::READ_WRITE};
    }
  };

  void StartFileBlockDevice(StartDeviceOptions options = StartDeviceOptions::Default()) {
    // Setup block file.
    // Open the file twice; once to get a FilePtr to provide to the
    // virtio_block process and another to retain to verify the file
    // contents.
    char path_template[] = "/tmp/block.XXXXXX";
    fd_ = CreateBlockFile(path_template);
    ASSERT_TRUE(fd_);
    zx::channel client;
    zx_status_t status = fdio_get_service_handle(fd_.release(), client.reset_and_get_address());
    ASSERT_EQ(ZX_OK, status);
    fd_ = fbl::unique_fd(open(path_template, O_RDWR));
    ASSERT_TRUE(fd_);

    block_ = realm_->ConnectSync<fuchsia::virtualization::hardware::VirtioBlock>();

    uint64_t capacity;
    uint32_t block_size;

    fuchsia::virtualization::hardware::StartInfo start_info;
    status = MakeStartInfo(request_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    status = block_->Start(std::move(start_info), kVirtioBlockId, options.block_mode,
                           fuchsia::virtualization::BlockFormat::FILE, std::move(client), &capacity,
                           &block_size);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(kBlockSectorSize * kNumSectors, capacity);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&request_queue_};
    for (uint16_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(kQueueDataSize * i, kQueueDataSize);
      status = block_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used());
      ASSERT_EQ(ZX_OK, status);
    }

    // Finish negotiating features.
    status = block_->Ready(options.negotiated_features);
    ASSERT_EQ(ZX_OK, status);
  }

  void VerifySectorNotWritten(uint64_t sector) {
    ASSERT_LT(sector, kNumSectors);

    std::vector<uint8_t> expected(kBlockSectorSize, kSectorBytes[sector]);
    std::vector<uint8_t> result(kBlockSectorSize, 0);
    size_t file_offset = sector * kBlockSectorSize;
    ASSERT_EQ(pread(fd_.get(), result.data(), result.size(), file_offset),
              static_cast<ssize_t>(result.size()));
    ASSERT_EQ(memcmp(result.data(), expected.data(), expected.size()), 0);
  }

  void TestWriteReadOnlyDevice(uint32_t features) {
    StartFileBlockDevice(StartDeviceOptions{
        .negotiated_features = features,
        .block_mode = fuchsia::virtualization::BlockMode::READ_ONLY,
    });

    virtio_blk_req_t header = {
        .type = VIRTIO_BLK_T_OUT,
    };
    std::vector<uint8_t> sector(kBlockSectorSize, UINT8_MAX);
    uint8_t* blk_status;
    zx_status_t status =
        DescriptorChainBuilder(request_queue_)
            .AppendReadableDescriptor(&header, sizeof(header))
            .AppendReadableDescriptor(sector.data(), static_cast<uint32_t>(sector.size()))
            .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
            .Build();
    ASSERT_EQ(ZX_OK, status);

    status = block_->NotifyQueue(0);
    ASSERT_EQ(ZX_OK, status);
    status = WaitOnInterrupt();
    ASSERT_EQ(ZX_OK, status);

    EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);

    // Ensure nothing was written to the file.
    for (size_t sector = 0; sector < kNumSectors; ++sector) {
      VerifySectorNotWritten(sector);
    }
  }

  fbl::unique_fd fd_;
  // Note: use of sync can be problematic here if the test environment needs to handle
  // some incoming FIDL requests.
  fuchsia::virtualization::hardware::VirtioBlockSyncPtr block_;
  VirtioQueueFake request_queue_;
  std::unique_ptr<component_testing::RealmRoot> realm_;

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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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

TEST_F(VirtioBlockTest, BadRequestType) {
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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

TEST_F(VirtioBlockTest, UnderflowOnWrite) {
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_OUT,
      .sector = 0,
  };
  std::vector<uint8_t> sector((kNumSectors + 1) * kBlockSectorSize, UINT8_MAX);
  uint8_t* blk_status;
  zx_status_t status =
      DescriptorChainBuilder(request_queue_)
          .AppendReadableDescriptor(&header, sizeof(header))
          .AppendReadableDescriptor(sector.data(), static_cast<uint32_t>(sector.size()))
          .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
          .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);
}

TEST_F(VirtioBlockTest, BadWriteOffset) {
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_OUT,
      .sector = kNumSectors,
  };
  std::vector<uint8_t> sector(kBlockSectorSize, UINT8_MAX);
  uint8_t* blk_status;
  zx_status_t status =
      DescriptorChainBuilder(request_queue_)
          .AppendReadableDescriptor(&header, sizeof(header))
          .AppendReadableDescriptor(sector.data(), static_cast<uint32_t>(sector.size()))
          .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
          .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);
}

TEST_F(VirtioBlockTest, Write) {
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_OUT,
  };
  std::vector<uint8_t> sector(kBlockSectorSize, UINT8_MAX);
  uint8_t* blk_status;
  zx_status_t status =
      DescriptorChainBuilder(request_queue_)
          .AppendReadableDescriptor(&header, sizeof(header))
          .AppendReadableDescriptor(sector.data(), static_cast<uint32_t>(sector.size()))
          .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
          .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_OK, *blk_status);
}

TEST_F(VirtioBlockTest, WriteGoodAndBadSectors) {
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_OUT,
      .sector = 1,
  };

  std::vector<uint8_t> block_1(kBlockSectorSize, 0xff);
  std::vector<uint8_t> block_2(kBlockSectorSize, 0xaa);

  uint8_t* blk_status;
  zx_status_t status =
      DescriptorChainBuilder(request_queue_)
          .AppendReadableDescriptor(&header, sizeof(header))
          .AppendReadableDescriptor(block_1.data(), static_cast<uint32_t>(block_1.size()))
          .AppendReadableDescriptor(block_2.data(), static_cast<uint32_t>(block_2.size()))
          .AppendWritableDescriptor(&blk_status, sizeof(*blk_status))
          .Build();
  ASSERT_EQ(ZX_OK, status);

  status = block_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_BLK_S_IOERR, *blk_status);

  // Check sector 2 to ensure we didn't overwrite it
  std::vector<uint8_t> result(2 * kBlockSectorSize, 0);
  ASSERT_EQ(pread(fd_.get(), result.data(), result.size(), kBlockSectorSize),
            static_cast<const long>(kBlockSectorSize));

  // From Virtio 1.1, Section 5.2.6.1: A driver MUST NOT submit a request which would cause a read
  // or write beyond capacity.
  //
  // Since the language is clear this is something the device MUST NOT do, strictly rejecting the
  // entire request is OK.
  std::vector<uint8_t> expected(kBlockSectorSize, kSectorBytes[1]);
  ASSERT_EQ(memcmp(result.data(), expected.data(), expected.size()), 0);
}

TEST_F(VirtioBlockTest, WriteMultipleDescriptors) {
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_OUT,
      .sector = 0,
  };

  std::vector<uint8_t> block_1(kBlockSectorSize, 0xff);
  std::vector<uint8_t> block_2(kBlockSectorSize, 0xab);
  uint8_t* blk_status;
  zx_status_t status =
      DescriptorChainBuilder(request_queue_)
          .AppendReadableDescriptor(&header, sizeof(header))
          .AppendReadableDescriptor(block_1.data(), static_cast<uint32_t>(block_1.size()))
          .AppendReadableDescriptor(block_2.data(), static_cast<uint32_t>(block_2.size()))
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

TEST_F(VirtioBlockTest, WriteReadOnlyDeviceWithFeature) {
  TestWriteReadOnlyDevice(VIRTIO_BLK_F_RO);
}

TEST_F(VirtioBlockTest, WriteReadOnlyDeviceWithoutFeature) { TestWriteReadOnlyDevice(0); }

TEST_F(VirtioBlockTest, Sync) {
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

  virtio_blk_req_t header = {
      .type = VIRTIO_BLK_T_FLUSH,
  };
  std::vector<uint8_t> sector(kBlockSectorSize);
  uint8_t* blk_status;
  zx_status_t status =
      DescriptorChainBuilder(request_queue_)
          .AppendReadableDescriptor(&header, sizeof(header))
          .AppendReadableDescriptor(sector.data(), static_cast<uint32_t>(sector.size()))
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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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
  ASSERT_NO_FATAL_FAILURE(StartFileBlockDevice());

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
