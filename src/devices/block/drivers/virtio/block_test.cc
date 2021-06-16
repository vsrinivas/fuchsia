// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block.h"

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sync/completion.h>
#include <lib/virtio/backends/fake.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace {

constexpr uint64_t kCapacity = 1024;
constexpr uint64_t kSizeMax = 4000;
constexpr uint64_t kSegMax = 1024;
constexpr uint64_t kBlkSize = 1024;

// Fake virtio 'backend' for a virtio-scsi device.
class FakeBackendForBlock : public virtio::FakeBackend {
 public:
  FakeBackendForBlock() : virtio::FakeBackend({{0, 1024}}) {
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
};

TEST(BlockTest, InitSuccess) {
  std::unique_ptr<virtio::Backend> backend = std::make_unique<FakeBackendForBlock>();
  zx::bti bti(ZX_HANDLE_INVALID);
  fake_bti_create(bti.reset_and_get_address());
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

  void InitDevice() {
    std::unique_ptr<virtio::Backend> backend = std::make_unique<FakeBackendForBlock>();
    zx::bti bti(ZX_HANDLE_INVALID);
    fake_bti_create(bti.reset_and_get_address());
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
}  // anonymous namespace
