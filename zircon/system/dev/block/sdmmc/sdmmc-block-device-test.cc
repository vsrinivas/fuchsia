// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-block-device.h"

#include <endian.h>

#include <fbl/algorithm.h>
#include <hw/sdmmc.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fzl/vmo-mapper.h>
#include <zxtest/zxtest.h>

#include "fake-sdmmc-device.h"

namespace sdmmc {

class SdmmcBlockDeviceTest : public zxtest::Test {
 public:
  SdmmcBlockDeviceTest() : dut_(fake_ddk::kFakeParent, SdmmcDevice(sdmmc_.GetClient())) {
    for (size_t i = 0; i < (FakeSdmmcDevice::kBlockSize / sizeof(kTestData)); i++) {
      test_block.insert(test_block.end(), kTestData, kTestData + sizeof(kTestData));
    }
  }

  void SetUp() override {
    sdmmc_.Reset();

    sdmmc_.set_command_callback(SDMMC_SEND_CSD, [](sdmmc_req_t* req) -> void {
      uint8_t* response = reinterpret_cast<uint8_t*>(req->response);
      response[MMC_CSD_SPEC_VERSION] = MMC_CID_SPEC_VRSN_40 << 2;
      response[MMC_CSD_SIZE_START] = 0x03 << 6;
      response[MMC_CSD_SIZE_START + 1] = 0xff;
      response[MMC_CSD_SIZE_START + 2] = 0x03;
    });

    sdmmc_.set_command_callback(MMC_SEND_EXT_CSD, [](sdmmc_req_t* req) -> void {
      uint8_t* const virt_buffer = reinterpret_cast<uint8_t*>(req->virt_buffer) + req->buf_offset;
      *reinterpret_cast<uint32_t*>(&virt_buffer[212]) = htole32(kBlockCount);
    });

    EXPECT_OK(dut_.ProbeMmc());
    EXPECT_OK(dut_.StartWorkerThread());

    block_info_t info;
    dut_.BlockImplQuery(&info, &block_op_size_);
  }

  void TearDown() override { dut_.StopWorkerThread(); }

 protected:
  static constexpr uint32_t kBlockCount = 0x100000;

  struct OperationContext {
    zx::vmo vmo;
    fzl::VmoMapper mapper;
    zx_status_t status;
    bool completed;
  };

  struct CallbackContext {
    CallbackContext(uint32_t exp_op, size_t block_op_sz)
        : expected_operations(exp_op), block_op_size(block_op_sz) {}

    uint32_t expected_operations;
    sync_completion_t completion;
    const size_t block_op_size;
  };

  static void OperationCallback(void* ctx, zx_status_t status, block_op_t* op) {
    auto* const cb_ctx = reinterpret_cast<CallbackContext*>(ctx);

    block::Operation<OperationContext> block_op(op, cb_ctx->block_op_size, false);
    block_op.private_storage()->completed = true;
    block_op.private_storage()->status = status;

    if (--(cb_ctx->expected_operations) == 0) {
      sync_completion_signal(&cb_ctx->completion);
    }
  }

  void MakeBlockOp(uint32_t command, uint32_t length, uint64_t offset,
                   std::optional<block::Operation<OperationContext>>* out_op) {
    *out_op = block::Operation<OperationContext>::Alloc(block_op_size_);
    ASSERT_TRUE(*out_op);

    *(*out_op)->operation() = block_op_t{
        .rw =
            {
                .command = command,
                .extra = 0,
                .vmo = ZX_HANDLE_INVALID,
                .length = length,
                .offset_dev = offset,
                .offset_vmo = 0,
            },
    };

    if ((command == BLOCK_OP_READ || command == BLOCK_OP_WRITE) && length > 0) {
      OperationContext* const ctx = (*out_op)->private_storage();
      const size_t vmo_size =
          fbl::round_up<size_t, size_t>(length * FakeSdmmcDevice::kBlockSize, PAGE_SIZE);
      ASSERT_OK(ctx->mapper.CreateAndMap(vmo_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                                         &ctx->vmo));
      ctx->completed = false;
      ctx->status = ZX_OK;
      (*out_op)->operation()->rw.vmo = ctx->vmo.get();
    }
  }

  void FillSdmmc(uint32_t length, uint64_t offset) {
    for (uint32_t i = 0; i < length; i++) {
      sdmmc_.Write((offset + i) * test_block.size(), test_block);
    }
  }

  void FillVmo(const fzl::VmoMapper& mapper, uint32_t length) {
    auto* ptr = reinterpret_cast<uint8_t*>(mapper.start());
    for (uint32_t i = 0; i < length; i++, ptr += test_block.size()) {
      memcpy(ptr, test_block.data(), test_block.size());
    }
  }

  void CheckSdmmc(uint32_t length, uint64_t offset) {
    const std::vector<uint8_t> data =
        sdmmc_.Read(offset * test_block.size(), length * test_block.size());
    const uint8_t* ptr = data.data();
    for (uint32_t i = 0; i < length; i++, ptr += test_block.size()) {
      EXPECT_BYTES_EQ(ptr, test_block.data(), test_block.size());
    }
  }

  void CheckVmo(const fzl::VmoMapper& mapper, uint32_t length) {
    const uint8_t* ptr = reinterpret_cast<uint8_t*>(mapper.start());
    for (uint32_t i = 0; i < length; i++, ptr += test_block.size()) {
      EXPECT_BYTES_EQ(ptr, test_block.data(), test_block.size());
    }
  }

  FakeSdmmcDevice sdmmc_;
  SdmmcBlockDevice dut_;
  size_t block_op_size_ = 0;

 private:
  static constexpr uint8_t kTestData[] = {
      // clang-format off
      0xd0, 0x0d, 0x7a, 0xf2, 0xbc, 0x13, 0x81, 0x07,
      0x72, 0xbe, 0x33, 0x5f, 0x21, 0x4e, 0xd7, 0xba,
      0x1b, 0x0c, 0x25, 0xcf, 0x2c, 0x6f, 0x46, 0x3a,
      0x78, 0x22, 0xea, 0x9e, 0xa0, 0x41, 0x65, 0xf8,
      // clang-format on
  };
  static_assert(FakeSdmmcDevice::kBlockSize % sizeof(kTestData) == 0);

  std::vector<uint8_t> test_block;
};

TEST_F(SdmmcBlockDeviceTest, BlockImplQuery) {
  size_t _;
  block_info_t info;
  dut_.BlockImplQuery(&info, &_);

  EXPECT_EQ(info.block_count, kBlockCount);
  EXPECT_EQ(info.block_size, FakeSdmmcDevice::kBlockSize);
}

TEST_F(SdmmcBlockDeviceTest, BlockImplQueue) {
  std::optional<block::Operation<OperationContext>> op1;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 1, 0, &op1));

  std::optional<block::Operation<OperationContext>> op2;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 5, 0x8000, &op2));

  std::optional<block::Operation<OperationContext>> op3;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_FLUSH, 0, 0, &op3));

  std::optional<block::Operation<OperationContext>> op4;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 1, 0x400, &op4));

  std::optional<block::Operation<OperationContext>> op5;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 10, 0x2000, &op5));

  CallbackContext ctx(5, block_op_size_);

  FillVmo(op1->private_storage()->mapper, 1);
  FillVmo(op2->private_storage()->mapper, 5);
  FillSdmmc(1, 0x400);
  FillSdmmc(10, 0x2000);

  dut_.BlockImplQueue(op1->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op2->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op3->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op4->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op5->operation(), OperationCallback, &ctx);

  EXPECT_OK(sync_completion_wait(&ctx.completion, zx::duration::infinite().get()));

  EXPECT_TRUE(op1->private_storage()->completed);
  EXPECT_TRUE(op2->private_storage()->completed);
  EXPECT_TRUE(op3->private_storage()->completed);
  EXPECT_TRUE(op4->private_storage()->completed);
  EXPECT_TRUE(op5->private_storage()->completed);

  EXPECT_OK(op1->private_storage()->status);
  EXPECT_OK(op2->private_storage()->status);
  EXPECT_OK(op3->private_storage()->status);
  EXPECT_OK(op4->private_storage()->status);
  EXPECT_OK(op5->private_storage()->status);

  ASSERT_NO_FATAL_FAILURES(CheckSdmmc(1, 0));
  ASSERT_NO_FATAL_FAILURES(CheckSdmmc(5, 0x8000));
  ASSERT_NO_FATAL_FAILURES(CheckVmo(op4->private_storage()->mapper, 1));
  ASSERT_NO_FATAL_FAILURES(CheckVmo(op5->private_storage()->mapper, 10));
}

TEST_F(SdmmcBlockDeviceTest, BlockImplQueueOutOfRange) {
  std::optional<block::Operation<OperationContext>> op1;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 1, 0x100000, &op1));

  std::optional<block::Operation<OperationContext>> op2;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 10, 0x200000, &op2));

  std::optional<block::Operation<OperationContext>> op3;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 8, 0xffff8, &op3));

  std::optional<block::Operation<OperationContext>> op4;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 9, 0xffff8, &op4));

  std::optional<block::Operation<OperationContext>> op5;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 16, 0xffff8, &op5));

  std::optional<block::Operation<OperationContext>> op6;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 0, 0x80000, &op6));

  std::optional<block::Operation<OperationContext>> op7;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 1, 0xfffff, &op7));

  CallbackContext ctx(7, block_op_size_);

  dut_.BlockImplQueue(op1->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op2->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op3->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op4->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op5->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op6->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op7->operation(), OperationCallback, &ctx);

  EXPECT_OK(sync_completion_wait(&ctx.completion, zx::duration::infinite().get()));

  EXPECT_TRUE(op1->private_storage()->completed);
  EXPECT_TRUE(op2->private_storage()->completed);
  EXPECT_TRUE(op3->private_storage()->completed);
  EXPECT_TRUE(op4->private_storage()->completed);
  EXPECT_TRUE(op5->private_storage()->completed);
  EXPECT_TRUE(op6->private_storage()->completed);
  EXPECT_TRUE(op7->private_storage()->completed);

  EXPECT_NOT_OK(op1->private_storage()->status);
  EXPECT_NOT_OK(op2->private_storage()->status);
  EXPECT_OK(op3->private_storage()->status);
  EXPECT_NOT_OK(op4->private_storage()->status);
  EXPECT_NOT_OK(op5->private_storage()->status);
  EXPECT_OK(op6->private_storage()->status);
  EXPECT_OK(op7->private_storage()->status);
}

TEST_F(SdmmcBlockDeviceTest, MultiBlockACmd12) {
  sdmmc_.set_host_info({
      .caps = SDMMC_HOST_CAP_AUTO_CMD12,
      .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
      .max_transfer_size_non_dma = 0,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  std::optional<block::Operation<OperationContext>> op1;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 1, 0, &op1));

  std::optional<block::Operation<OperationContext>> op2;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 5, 0x8000, &op2));

  std::optional<block::Operation<OperationContext>> op3;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_FLUSH, 0, 0, &op3));

  std::optional<block::Operation<OperationContext>> op4;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 1, 0x400, &op4));

  std::optional<block::Operation<OperationContext>> op5;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 10, 0x2000, &op5));

  CallbackContext ctx(5, block_op_size_);

  dut_.BlockImplQueue(op1->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op2->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op3->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op4->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op5->operation(), OperationCallback, &ctx);

  EXPECT_OK(sync_completion_wait(&ctx.completion, zx::duration::infinite().get()));

  const std::map<uint32_t, uint32_t> command_counts = sdmmc_.command_counts();
  EXPECT_EQ(command_counts.find(SDMMC_STOP_TRANSMISSION), command_counts.end());
}

TEST_F(SdmmcBlockDeviceTest, MultiBlockNoACmd12) {
  sdmmc_.set_host_info({
      .caps = 0,
      .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
      .max_transfer_size_non_dma = 0,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  std::optional<block::Operation<OperationContext>> op1;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 1, 0, &op1));

  std::optional<block::Operation<OperationContext>> op2;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 5, 0x8000, &op2));

  std::optional<block::Operation<OperationContext>> op3;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_FLUSH, 0, 0, &op3));

  std::optional<block::Operation<OperationContext>> op4;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 1, 0x400, &op4));

  std::optional<block::Operation<OperationContext>> op5;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 10, 0x2000, &op5));

  CallbackContext ctx(5, block_op_size_);

  dut_.BlockImplQueue(op1->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op2->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op3->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op4->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op5->operation(), OperationCallback, &ctx);

  EXPECT_OK(sync_completion_wait(&ctx.completion, zx::duration::infinite().get()));

  EXPECT_EQ(sdmmc_.command_counts().at(SDMMC_STOP_TRANSMISSION), 2);
}

TEST_F(SdmmcBlockDeviceTest, ErrorsPropagate) {
  std::optional<block::Operation<OperationContext>> op1;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 1, FakeSdmmcDevice::kBadRegionStart, &op1));

  std::optional<block::Operation<OperationContext>> op2;
  ASSERT_NO_FATAL_FAILURES(
      MakeBlockOp(BLOCK_OP_WRITE, 5, FakeSdmmcDevice::kBadRegionStart | 0x80, &op2));

  std::optional<block::Operation<OperationContext>> op3;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_FLUSH, 0, 0, &op3));

  std::optional<block::Operation<OperationContext>> op4;
  ASSERT_NO_FATAL_FAILURES(
      MakeBlockOp(BLOCK_OP_READ, 1, FakeSdmmcDevice::kBadRegionStart | 0x40, &op4));

  std::optional<block::Operation<OperationContext>> op5;
  ASSERT_NO_FATAL_FAILURES(
      MakeBlockOp(BLOCK_OP_READ, 10, FakeSdmmcDevice::kBadRegionStart | 0x20, &op5));

  CallbackContext ctx(5, block_op_size_);

  dut_.BlockImplQueue(op1->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op2->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op3->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op4->operation(), OperationCallback, &ctx);
  dut_.BlockImplQueue(op5->operation(), OperationCallback, &ctx);

  EXPECT_OK(sync_completion_wait(&ctx.completion, zx::duration::infinite().get()));

  EXPECT_TRUE(op1->private_storage()->completed);
  EXPECT_TRUE(op2->private_storage()->completed);
  EXPECT_TRUE(op3->private_storage()->completed);
  EXPECT_TRUE(op4->private_storage()->completed);
  EXPECT_TRUE(op5->private_storage()->completed);

  EXPECT_NOT_OK(op1->private_storage()->status);
  EXPECT_NOT_OK(op2->private_storage()->status);
  EXPECT_OK(op3->private_storage()->status);
  EXPECT_NOT_OK(op4->private_storage()->status);
  EXPECT_NOT_OK(op5->private_storage()->status);
}

TEST_F(SdmmcBlockDeviceTest, DdkLifecycle) {
  fake_ddk::Bind ddk;
  EXPECT_OK(dut_.AddDevice());
  dut_.DdkUnbind();
  EXPECT_TRUE(ddk.Ok());
}

TEST_F(SdmmcBlockDeviceTest, CompleteTransactions) {
  std::optional<block::Operation<OperationContext>> op1;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 1, 0, &op1));

  std::optional<block::Operation<OperationContext>> op2;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_WRITE, 5, 0x8000, &op2));

  std::optional<block::Operation<OperationContext>> op3;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_FLUSH, 0, 0, &op3));

  std::optional<block::Operation<OperationContext>> op4;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 1, 0x400, &op4));

  std::optional<block::Operation<OperationContext>> op5;
  ASSERT_NO_FATAL_FAILURES(MakeBlockOp(BLOCK_OP_READ, 10, 0x2000, &op5));

  CallbackContext ctx(5, block_op_size_);

  {
    SdmmcBlockDevice dut(nullptr, SdmmcDevice(sdmmc_.GetClient()));

    dut.BlockImplQueue(op1->operation(), OperationCallback, &ctx);
    dut.BlockImplQueue(op2->operation(), OperationCallback, &ctx);
    dut.BlockImplQueue(op3->operation(), OperationCallback, &ctx);
    dut.BlockImplQueue(op4->operation(), OperationCallback, &ctx);
    dut.BlockImplQueue(op5->operation(), OperationCallback, &ctx);
  }

  EXPECT_OK(sync_completion_wait(&ctx.completion, zx::duration::infinite().get()));

  EXPECT_TRUE(op1->private_storage()->completed);
  EXPECT_TRUE(op2->private_storage()->completed);
  EXPECT_TRUE(op3->private_storage()->completed);
  EXPECT_TRUE(op4->private_storage()->completed);
  EXPECT_TRUE(op5->private_storage()->completed);
}

}  // namespace sdmmc
