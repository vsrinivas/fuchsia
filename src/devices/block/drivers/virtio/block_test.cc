// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block.h"

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sync/completion.h>
#include <lib/virtio/backends/fake.h>

#include <condition_variable>
#include <memory>

#include <zxtest/zxtest.h>

namespace {

constexpr uint64_t kCapacity = 1024;
constexpr uint64_t kSizeMax = 4000;
constexpr uint64_t kSegMax = 1024;
constexpr uint64_t kBlkSize = 1024;
const uint16_t kRingSize = 128;  // Should match block.h

// Fake virtio 'backend' for a virtio-scsi device.
class FakeBackendForBlock : public virtio::FakeBackend {
 public:
  FakeBackendForBlock(zx_handle_t fake_bti)
      : virtio::FakeBackend({{0, 1024}}), fake_bti_(fake_bti) {
    // Fill out a block config:
    virtio_blk_config config;
    memset(&config, 0, sizeof(config));
    config.capacity = kCapacity;
    config.size_max = kSizeMax;
    config.seg_max = kSegMax;
    config.blk_size = kBlkSize;

    for (uint16_t i = 0; i < sizeof(config); ++i) {
      AddClassRegister(i, reinterpret_cast<uint8_t*>(&config)[i]);
    }
  }

  void set_status(uint8_t status) { status_ = status; }

  void RingKick(uint16_t ring_index) override {
    FakeBackend::RingKick(ring_index);

    fake_bti_pinned_vmo_info_t vmos[16];
    size_t count;
    ASSERT_OK(fake_bti_get_pinned_vmos(fake_bti_, vmos, 16, &count));
    ASSERT_LE(2, count);

    union __PACKED Used {
      vring_used head;
      struct __PACKED {
        uint8_t header[sizeof(vring_used)];
        vring_used_elem elements[kRingSize];
      };
    } used;
    union __PACKED Avail {
      vring_avail head;
      struct __PACKED {
        uint8_t header[sizeof(vring_avail)];
        uint16_t ring[kRingSize];
      };
    } avail;

    // This assumes that the ring is in the first VMO.
    ASSERT_OK(zx_vmo_read(vmos[0].vmo, &used, vmos[0].offset + used_offset_, sizeof(used)));
    ASSERT_OK(zx_vmo_read(vmos[0].vmo, &avail, vmos[0].offset + avail_offset_, sizeof(avail)));

    if (avail.head.idx != used.head.idx) {
      ASSERT_EQ(avail.head.idx, used.head.idx + 1);  // We can only handle one queued entry.

      size_t index = used.head.idx & (kRingSize - 1);

      // Read the descriptors.
      vring_desc descriptors[kRingSize];
      ASSERT_OK(zx_vmo_read(vmos[0].vmo, descriptors, vmos[0].offset + desc_offset_,
                            sizeof(descriptors)));

      // Find the last descriptor.
      vring_desc* desc = &descriptors[avail.ring[index]];
      uint16_t count = 1;
      while (desc->flags & VRING_DESC_F_NEXT) {
        desc = &descriptors[desc->next];
        ++count;
      }

      // It should be the status descriptor.
      ASSERT_EQ(1, desc->len);

      // This assumes the results are in the second VMO.
      size_t offset = vmos[1].offset + desc->addr - FAKE_BTI_PHYS_ADDR;
      ASSERT_OK(zx_vmo_write(vmos[1].vmo, &status_, offset, sizeof(status_)));

      used.elements[index].id = avail.ring[index];
      used.elements[index].len = count;

      ++used.head.idx;

      ASSERT_OK(zx_vmo_write(vmos[0].vmo, &used, vmos[0].offset + used_offset_, sizeof(used)));

      // Trigger an interrupt.
      uint8_t isr_status;
      ReadRegister(kISRStatus, &isr_status);
      isr_status |= VIRTIO_ISR_QUEUE_INT;
      SetRegister(kISRStatus, isr_status);

      std::scoped_lock lock(mutex_);
      interrupt_ = true;
      cond_.notify_all();
    }
  }

  zx_status_t SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
                      zx_paddr_t pa_used) override {
    FakeBackend::SetRing(index, count, pa_desc, pa_avail, pa_used);
    used_offset_ = pa_used - FAKE_BTI_PHYS_ADDR;
    avail_offset_ = pa_avail - FAKE_BTI_PHYS_ADDR;
    desc_offset_ = pa_desc - FAKE_BTI_PHYS_ADDR;
    ZX_ASSERT(count == kRingSize);
    return ZX_OK;
  }

  zx_status_t InterruptValid() override {
    std::scoped_lock lock(mutex_);
    return terminate_ ? ZX_ERR_CANCELED : ZX_OK;
  }

  zx::status<uint32_t> WaitForInterrupt() override {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      if (terminate_)
        return zx::error(ZX_ERR_CANCELED);
      if (interrupt_)
        return zx::ok(0);
      cond_.wait(lock);
    }
  }

  void InterruptAck(uint32_t key) override {
    std::scoped_lock lock(mutex_);
    interrupt_ = false;
  }

  void Terminate() override {
    std::scoped_lock lock(mutex_);
    terminate_ = true;
    cond_.notify_all();
  }

 private:
  // The vring offsets.
  size_t used_offset_ = 0;
  size_t avail_offset_ = 0;
  size_t desc_offset_ = 0;

  zx_handle_t fake_bti_;

  std::mutex mutex_;
  std::condition_variable cond_;
  bool terminate_ = false;
  bool interrupt_ = false;

  // The status returned for any operations.
  uint8_t status_ = VIRTIO_BLK_S_OK;
};

TEST(BlockTest, InitSuccess) {
  zx::bti bti(ZX_HANDLE_INVALID);
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));
  std::unique_ptr<virtio::Backend> backend = std::make_unique<FakeBackendForBlock>(bti.get());
  fake_ddk::Bind ddk;
  virtio::BlockDevice block(fake_ddk::FakeParent(), std::move(bti), std::move(backend));
  ASSERT_EQ(block.Init(), ZX_OK);
  block.DdkAsyncRemove();
  EXPECT_TRUE(ddk.Ok());
  block.DdkRelease();
}

// Provides control primitives for tests that issue IO requests to the device.
class BlockDeviceTest : public zxtest::Test {
 public:
  ~BlockDeviceTest() {}

  void InitDevice(uint8_t status = VIRTIO_BLK_S_OK) {
    zx::bti bti(ZX_HANDLE_INVALID);
    ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));
    auto backend = std::make_unique<FakeBackendForBlock>(bti.get());
    backend->set_status(status);
    ddk_ = std::make_unique<fake_ddk::Bind>();
    device_ = std::make_unique<virtio::BlockDevice>(fake_ddk::FakeParent(), std::move(bti),
                                                    std::move(backend));
    ASSERT_EQ(device_->Init(), ZX_OK);
    device_->BlockImplQuery(&info_, &operation_size_);
  }

  void RemoveDevice() {
    device_->DdkAsyncRemove();
    EXPECT_TRUE(ddk_->Ok());
    device_->DdkRelease();
  }

  static void CompletionCb(void* cookie, zx_status_t status, block_op_t* op) {
    BlockDeviceTest* operation = reinterpret_cast<BlockDeviceTest*>(cookie);
    operation->operation_status_ = status;
    sync_completion_signal(&operation->event_);
  }

  bool Wait() {
    zx_status_t status = sync_completion_wait(&event_, ZX_SEC(5));
    sync_completion_reset(&event_);
    return status == ZX_OK;
  }

  zx_status_t OperationStatus() { return operation_status_; }

 protected:
  std::unique_ptr<virtio::BlockDevice> device_;
  block_info_t info_;
  size_t operation_size_;

 private:
  sync_completion_t event_;
  std::unique_ptr<fake_ddk::Bind> ddk_;
  zx_status_t operation_status_;
};

// Tests trivial attempts to queue one operation.
TEST_F(BlockDeviceTest, QueueOne) {
  InitDevice();

  virtio::block_txn_t txn;
  memset(&txn, 0, sizeof(txn));
  txn.op.rw.command = BLOCK_OP_READ;
  txn.op.rw.length = 0;
  // TODO(fxbug.dev/43065): This should not return ZX_OK when length == 0.
  device_->BlockImplQueue(reinterpret_cast<block_op_t*>(&txn), &BlockDeviceTest::CompletionCb,
                          this);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_OK, OperationStatus());

  txn.op.rw.length = kCapacity * 10;
  device_->BlockImplQueue(reinterpret_cast<block_op_t*>(&txn), &BlockDeviceTest::CompletionCb,
                          this);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, OperationStatus());

  RemoveDevice();
}

TEST_F(BlockDeviceTest, CheckQuery) {
  InitDevice();
  ASSERT_EQ(info_.block_size, kBlkSize);
  ASSERT_EQ(info_.block_count, kCapacity);
  ASSERT_GE(info_.max_transfer_size, zx_system_get_page_size());
  ASSERT_GT(operation_size_, sizeof(block_op_t));
  RemoveDevice();
}

TEST_F(BlockDeviceTest, ReadOk) {
  InitDevice();

  virtio::block_txn_t txn;
  memset(&txn, 0, sizeof(txn));
  txn.op.rw.command = BLOCK_OP_READ;
  txn.op.rw.length = 1;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  txn.op.rw.vmo = vmo.get();
  device_->BlockImplQueue(reinterpret_cast<block_op_t*>(&txn), &BlockDeviceTest::CompletionCb,
                          this);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_OK, OperationStatus());

  RemoveDevice();
}

TEST_F(BlockDeviceTest, ReadError) {
  InitDevice(VIRTIO_BLK_S_IOERR);

  virtio::block_txn_t txn;
  memset(&txn, 0, sizeof(txn));
  txn.op.rw.command = BLOCK_OP_READ;
  txn.op.rw.length = 1;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  txn.op.rw.vmo = vmo.get();
  device_->BlockImplQueue(reinterpret_cast<block_op_t*>(&txn), &BlockDeviceTest::CompletionCb,
                          this);
  ASSERT_TRUE(Wait());
  ASSERT_EQ(ZX_ERR_IO, OperationStatus());

  RemoveDevice();
}

}  // anonymous namespace
