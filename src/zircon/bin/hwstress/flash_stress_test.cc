// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zircon/bin/hwstress/flash_stress.h"

#include <fcntl.h>
#include <fuchsia/hardware/block/cpp/fidl.h>
#include <fuchsia/hardware/block/cpp/fidl_test_base.h>
#include <lib/zx/fifo.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

#include <random>
#include <thread>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/lib/testing/predicates/status.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"
#include "src/zircon/bin/hwstress/status.h"
#include "src/zircon/bin/hwstress/testing_util.h"

namespace hwstress {
namespace {

constexpr size_t kBlockSize = 512;
constexpr size_t kDefaultRamDiskSize = 64 * 1024 * 1024;
constexpr size_t kDefaultFvmSliceSize = 1024 * 1024;
constexpr size_t kTestSize = 4 * 1024 * 1024;
// We deliberately select something that is not a multiple of kTransferSize.
constexpr size_t kTransferSize = 768 * 1024;
constexpr size_t kVmoSize = kTransferSize * /*kMaxInFlightRequests*/ 8;

class FakeBlock : public fuchsia::hardware::block::testing::Block_TestBase {
 public:
  FakeBlock(bool introduce_incorrect_reads, uint64_t device_size)
      : introduce_incorrect_reads_(introduce_incorrect_reads), device_size_(device_size) {}

  void GetFifo(GetFifoCallback callback) override {
    zx::fifo fifo;
    zx_status_t status =
        zx::fifo::create(/*elem_count=*/BLOCK_FIFO_MAX_DEPTH, /*elem_size=*/BLOCK_FIFO_ESIZE,
                         /*options=*/0, &fifo_, &fifo);
    callback(status, std::move(fifo));
  }

  void AttachVmo(::zx::vmo vmo, AttachVmoCallback callback) override {
    ZX_ASSERT(!vmo_.is_valid());
    vmo_ = std::move(vmo);
    uint64_t vmo_size;
    ZX_ASSERT(vmo_.get_size(&vmo_size) == ZX_OK);
    // Map the VMO into memory.
    zx_status_t status = zx::vmar::root_self()->map(
        /*options=*/(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE),
        /*vmar_offset=*/0, vmo_, /*vmo_offset=*/0, vmo_size, &vmo_addr_);
    ZX_ASSERT(status == ZX_OK);
    callback(ZX_OK, std::make_unique<fuchsia::hardware::block::VmoId>(kVmoId));
  }

  void StartServer() {
    thread_ = std::make_unique<std::thread>([this]() { this->ServerLoop(); });
  }

  void CloseServer() {
    fifo_.signal(0, ZX_USER_SIGNAL_0);
    thread_->join();
  }

  // Callback when a unimplemented FIDL method is called.
  void NotImplemented_(const std::string& name) override {
    ZX_PANIC("Unimplemented: %s", name.c_str());
  }

 private:
  static constexpr fuchsia::hardware::block::VmoId kVmoId{.id = 42};
  bool introduce_incorrect_reads_;
  uint64_t device_size_;
  zx::fifo fifo_;
  zx::vmo vmo_;
  zx_vaddr_t vmo_addr_;
  std::unique_ptr<std::thread> thread_;

  void ServerLoop() {
    std::vector<block_fifo_request_t> reqs;
    size_t expected_offset = 0;
    while (true) {
      zx_signals_t pending;
      // We want to test what happens if the block device sends requests back in a
      // different order than what they were sent to us in. Unfortunately, we have
      // no way of knowing when the client code is blocked waiting for a response
      // from us.
      //
      // Instead, we just keep waiting for more requests until the client code stops
      // sending new ones for 50 milliseconds. After such a pause, we shuffle all
      // in-flight requests and start sending them back in a different order.
      zx_status_t status = fifo_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED | ZX_USER_SIGNAL_0,
                                          zx::deadline_after(zx::msec(50)), &pending);

      if (status == ZX_OK && (pending & ZX_FIFO_READABLE) != 0) {
        block_fifo_request_t request;
        zx_status_t status = fifo_.read(sizeof(request), &request, 1, nullptr);
        ZX_ASSERT(status == ZX_OK);
        ZX_ASSERT(request.vmoid == kVmoId.id);
        // Check that the data is correct and that it is being written to the correct device
        // location.
        ZX_ASSERT(request.dev_offset == expected_offset);
        expected_offset = request.dev_offset + request.length;
        ZX_ASSERT(request.dev_offset < device_size_ * kBlockSize);
        if (request.opcode == BLOCKIO_WRITE) {
          uint64_t expected_value = request.dev_offset;
          uint64_t found_value =
              reinterpret_cast<uint64_t*>(vmo_addr_ + request.vmo_offset * kBlockSize)[0];
          ZX_ASSERT(found_value == expected_value);
        }
        reqs.push_back(request);
      }

      if (status == ZX_OK && (pending & ZX_FIFO_READABLE) == 0) {
        // Peer closed.
        return;
      }

      // There are no more requests waiting so send response.
      if (status == ZX_ERR_TIMED_OUT) {
        std::shuffle(std::begin(reqs), std::end(reqs), std::default_random_engine());
        for (block_fifo_request_t request : reqs) {
          if (request.opcode == BLOCKIO_READ) {
            for (size_t i = 0; i < request.length; i++) {
              uint64_t value = request.dev_offset + i;
              // If requested, simulate an incorrect read when we are half way through the test.
              if (introduce_incorrect_reads_ &&
                  (request.dev_offset + i) * kBlockSize == device_size_ / 2) {
                value++;
              }
              WriteSectorData(vmo_addr_ + (request.vmo_offset + i) * kBlockSize, value);
            }
          }
          block_fifo_response_t response = {
              .status = ZX_OK,
              .reqid = request.reqid,
          };
          status = fifo_.write(sizeof(response), &response, 1, nullptr);
          ZX_ASSERT(status == ZX_OK);
        }
        reqs.clear();
      }
    }
  }

  void WriteSectorData(zx_vaddr_t start, uint64_t value) {
    uint64_t num_words = kBlockSize / sizeof(value);
    uint64_t* data = reinterpret_cast<uint64_t*>(start);
    for (uint64_t i = 0; i < num_words; i++) {
      data[i] = value;
    }
  }
};

TEST(Flash, FlashStress) {
  // Create a RAM disk.
  zx::result<storage::RamDisk> ramdisk = storage::RamDisk::Create(
      /*block_size=*/kBlockSize, /*block_count=*/kDefaultRamDiskSize / kBlockSize);
  ASSERT_TRUE(ramdisk.is_ok());

  // Instantiate it as a FVM device.
  zx::result<std::string> fvm_path =
      storage::CreateFvmInstance(ramdisk->path(), kDefaultFvmSliceSize);
  ASSERT_TRUE(fvm_path.is_ok());

  CommandLineArgs args;
  args.fvm_path = fvm_path.value();
  args.mem_to_test_megabytes = 16;

  StatusLine status;
  ASSERT_TRUE(StressFlash(&status, args, zx::msec(1)));
}

TEST(Flash, WriteFlashIo) {
  testing::LoopbackConnectionFactory factory;

  // Create a fake block device and a connection to it.
  FakeBlock block(false, kTestSize);

  BlockDevice device = {
      .device = factory.CreateSyncPtrTo<fuchsia::hardware::block::Block>(&block),
  };

  device.vmo_size = kVmoSize;
  device.info.block_size = kBlockSize;

  ASSERT_EQ(SetupBlockFifo("/dev/fake", &device), ZX_OK);
  block.StartServer();
  ASSERT_EQ(FlashIo(device, kTestSize, kTransferSize, /*is_write_test=*/true), ZX_OK);

  block.CloseServer();
}

TEST(Flash, ReadFlashIo) {
  testing::LoopbackConnectionFactory factory;

  // Create a fake block device and a connection to it.
  FakeBlock block(false, kTestSize);

  BlockDevice device = {
      .device = factory.CreateSyncPtrTo<fuchsia::hardware::block::Block>(&block),
  };

  device.vmo_size = kVmoSize;
  device.info.block_size = kBlockSize;

  ASSERT_EQ(SetupBlockFifo("/dev/fake", &device), ZX_OK);
  block.StartServer();
  ASSERT_EQ(FlashIo(device, kTestSize, kTransferSize, /*is_write_test=*/false), ZX_OK);

  block.CloseServer();
}

TEST(Flash, ReadErrorFlashIo) {
  testing::LoopbackConnectionFactory factory;

  // Create a fake block device and a connection to it.
  FakeBlock block(true, kTestSize);

  BlockDevice device = {
      .device = factory.CreateSyncPtrTo<fuchsia::hardware::block::Block>(&block),
  };

  device.vmo_size = kVmoSize;
  device.info.block_size = kBlockSize;

  ASSERT_EQ(SetupBlockFifo("/dev/fake", &device), ZX_OK);
  block.StartServer();
  ASSERT_DEATH({ FlashIo(device, kTestSize, kTransferSize, /*is_write_test=*/false); }, "");

  block.CloseServer();
}

TEST(Flash, SingleBlock) {
  testing::LoopbackConnectionFactory factory;

  // Create a fake block device and a connection to it.
  FakeBlock block(false, kBlockSize);

  BlockDevice device = {
      .device = factory.CreateSyncPtrTo<fuchsia::hardware::block::Block>(&block),
  };

  device.vmo_size = kVmoSize;
  device.info.block_size = kBlockSize;

  ASSERT_EQ(SetupBlockFifo("/dev/fake", &device), ZX_OK);

  block.StartServer();
  ASSERT_EQ(FlashIo(device, kBlockSize, kBlockSize, /*is_write_test=*/true), ZX_OK);
  block.CloseServer();
}

TEST(Flash, DeletePartition) {
  // Create a RAM disk.
  zx::result<storage::RamDisk> ramdisk = storage::RamDisk::Create(
      /*block_size=*/kBlockSize, /*block_count=*/kDefaultRamDiskSize / kBlockSize);
  ASSERT_TRUE(ramdisk.is_ok());

  // Instantiate it as a FVM device.
  zx::result<std::string> fvm_path =
      storage::CreateFvmInstance(ramdisk->path(), kDefaultFvmSliceSize);
  ASSERT_TRUE(fvm_path.is_ok());

  // Access FVM.
  fbl::unique_fd fvm_fd(open(fvm_path.value().c_str(), O_RDWR));
  ASSERT_TRUE(fvm_fd);

  alloc_req_t request{.slice_count = 1, .name = "test-fs"};
  memcpy(request.guid, uuid::Uuid::Generate().bytes(), sizeof(request.guid));
  memcpy(request.type, kTestPartGUID.bytes(), sizeof(request.type));

  // Create a partition.
  ASSERT_EQ(fs_management::FvmAllocatePartition(fvm_fd.get(), &request).status_value(), ZX_OK);

  StatusLine status;
  DestroyFlashTestPartitions(&status);
  fs_management::PartitionMatcher matcher{
      .type_guid = kTestPartGUID.bytes(),
  };
  ASSERT_NE(fs_management::OpenPartition(&matcher, 0, nullptr).status_value(), ZX_OK);
}

}  // namespace
}  // namespace hwstress
