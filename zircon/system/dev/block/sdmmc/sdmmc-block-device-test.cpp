// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-block-device.h"

#include <hw/sdmmc.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-function/mock-function.h>
#include <zircon/thread_annotations.h>
#include <zxtest/zxtest.h>

#include "mock-sdmmc-device.h"

namespace sdmmc {

class SdmmcBlockDeviceTest : public SdmmcBlockDevice {
public:
    SdmmcBlockDeviceTest(MockSdmmcDevice* mock_sdmmc, const block_info_t& block_info)
        : SdmmcBlockDevice(fake_ddk::kFakeParent, SdmmcDevice({}, {})), mock_sdmmc_(mock_sdmmc) {
        block_info_ = block_info;
    }

    auto& mock_DoTxn() { return mock_do_txn_; }
    auto& mock_BlockComplete() { return mock_block_complete_; }

    void VerifyAll() {
        mock_do_txn_.VerifyAndClear();
        mock_block_complete_.VerifyAndClear();
    }

    void WaitForBlockOps(uint32_t count) {
        fbl::AutoLock lock(&lock_);

        for (;;) {
            if (block_ops_done_ >= count) {
                block_ops_done_ = 0;
                return;
            }

            block_ops_event_.Wait(&lock_);
        }
    }

    void DoTxn(uint32_t command, uint32_t length, uint64_t offset) {
        std::optional<block::Operation<>> op = MakeBlockOp(command, length, offset);
        ASSERT_TRUE(op.has_value());

        BlockOperation unowned_op(op->operation(), nullptr, nullptr, sizeof(block_op_t), false);
        DoTxn(&unowned_op);
    }

    void DoTxn(BlockOperation* txn) override {
        if (mock_do_txn_.HasExpectations()) {
            fbl::AutoLock lock(&lock_);

            const block_read_write_t& brw = txn->operation()->rw;
            mock_do_txn_.Call(brw.command, brw.length, brw.offset_dev);

            block_ops_done_++;
            block_ops_event_.Broadcast();
        } else {
            SdmmcBlockDevice::DoTxn(txn);
        }
    }

    zx_status_t WaitForTran() override { return ZX_OK; }

    std::optional<block::Operation<>> MakeBlockOp(uint32_t command, uint32_t length,
                                                  uint64_t offset) {
        block_info_t block_info;
        size_t op_size;
        BlockImplQuery(&block_info, &op_size);

        std::optional<block::Operation<>> op = block::Operation<>::Alloc(op_size);
        if (op) {
            *op->operation() = block_op_t{
                .rw = {
                    .command = command,
                    .extra = 0,
                    .vmo = ZX_HANDLE_INVALID,
                    .length = length,
                    .offset_dev = offset,
                    .offset_vmo = 0
                }
            };
        }

        return op;
    }

private:
    SdmmcDevice& sdmmc() override { return *mock_sdmmc_; }

    void BlockComplete(BlockOperation* txn, zx_status_t status,
                       trace_async_id_t async_id) override {
        const block_read_write_t& brw = txn->operation()->rw;
        mock_block_complete_.Call(brw.command, brw.length, brw.offset_dev, status);
    }

    MockSdmmcDevice* mock_sdmmc_;
    fbl::Mutex lock_;
    fbl::ConditionVariable block_ops_event_ TA_GUARDED(lock_);
    uint32_t block_ops_done_ TA_GUARDED(lock_) = 0;
    mock_function::MockFunction<void, uint32_t, uint32_t, uint64_t> mock_do_txn_;
    mock_function::MockFunction<void, uint32_t, uint32_t, uint64_t, zx_status_t>
        mock_block_complete_;
};

TEST(SdmmcBlockDeviceTest, BlockImplQueue) {
    MockSdmmcDevice mock_sdmmc({});
    SdmmcBlockDeviceTest dut(&mock_sdmmc, {
        .block_count = 0x10000,
        .block_size = 512,
        .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
        .flags = 0,
        .reserved = 0
    });

    std::optional<block::Operation<>> op1 = dut.MakeBlockOp(BLOCK_OP_WRITE, 1, 0);
    ASSERT_TRUE(op1.has_value());

    std::optional<block::Operation<>> op2 = dut.MakeBlockOp(BLOCK_OP_WRITE, 5, 0x8000);
    ASSERT_TRUE(op2.has_value());

    std::optional<block::Operation<>> op3 = dut.MakeBlockOp(BLOCK_OP_FLUSH, 0, 0);
    ASSERT_TRUE(op3.has_value());

    std::optional<block::Operation<>> op4 = dut.MakeBlockOp(BLOCK_OP_READ, 1, 0x400);
    ASSERT_TRUE(op4.has_value());

    std::optional<block::Operation<>> op5 = dut.MakeBlockOp(BLOCK_OP_READ, 10, 0x2000);
    ASSERT_TRUE(op5.has_value());

    ASSERT_OK(dut.StartWorkerThread());

    dut.mock_DoTxn()
        .ExpectCall(BLOCK_OP_WRITE, 1, 0)
        .ExpectCall(BLOCK_OP_WRITE, 5, 0x8000)
        .ExpectCall(BLOCK_OP_FLUSH, 0, 0)
        .ExpectCall(BLOCK_OP_READ, 1, 0x400)
        .ExpectCall(BLOCK_OP_READ, 10, 0x2000);

    // BlockComplete is always mocked, so the BlockOperation that gets created for this call
    // will get automatically completed with ZX_ERR_INTERNAL upon destruction. Give it a no-op
    // callback to keep this from causing test errors.
    auto noop_callback = [](void* ctx, zx_status_t status, block_op_t* op) {};

    dut.BlockImplQueue(op1->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op2->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op3->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op4->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op5->operation(), noop_callback, nullptr);

    dut.WaitForBlockOps(5);
    dut.StopWorkerThread();

    dut.VerifyAll();
    mock_sdmmc.VerifyAll();
}

TEST(SdmmcBlockDeviceTest, BlockImplQueueOutOfRange) {
    MockSdmmcDevice mock_sdmmc({});
    SdmmcBlockDeviceTest dut(&mock_sdmmc, {
        .block_count = 0x1000,
        .block_size = 512,
        .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
        .flags = 0,
        .reserved = 0
    });

    std::optional<block::Operation<>> op1 = dut.MakeBlockOp(BLOCK_OP_WRITE, 1, 0x1000);
    ASSERT_TRUE(op1.has_value());

    std::optional<block::Operation<>> op2 = dut.MakeBlockOp(BLOCK_OP_READ, 10, 0x2000);
    ASSERT_TRUE(op2.has_value());

    std::optional<block::Operation<>> op3 = dut.MakeBlockOp(BLOCK_OP_WRITE, 8, 0xff8);
    ASSERT_TRUE(op3.has_value());

    std::optional<block::Operation<>> op4 = dut.MakeBlockOp(BLOCK_OP_READ, 9, 0xff8);
    ASSERT_TRUE(op4.has_value());

    std::optional<block::Operation<>> op5 = dut.MakeBlockOp(BLOCK_OP_WRITE, 16, 0xff8);
    ASSERT_TRUE(op5.has_value());

    std::optional<block::Operation<>> op6 = dut.MakeBlockOp(BLOCK_OP_READ, 0, 0x800);
    ASSERT_TRUE(op6.has_value());

    std::optional<block::Operation<>> op7 = dut.MakeBlockOp(BLOCK_OP_WRITE, 1, 0xfff);
    ASSERT_TRUE(op7.has_value());

    ASSERT_OK(dut.StartWorkerThread());

    dut.mock_DoTxn()
        .ExpectCall(BLOCK_OP_WRITE, 8, 0xff8)
        .ExpectCall(BLOCK_OP_WRITE, 1, 0xfff);

    dut.mock_BlockComplete()
        .ExpectCall(BLOCK_OP_WRITE, 1, 0x1000, ZX_ERR_OUT_OF_RANGE)
        .ExpectCall(BLOCK_OP_READ, 10, 0x2000, ZX_ERR_OUT_OF_RANGE)
        .ExpectCall(BLOCK_OP_READ, 9, 0xff8, ZX_ERR_OUT_OF_RANGE)
        .ExpectCall(BLOCK_OP_WRITE, 16, 0xff8, ZX_ERR_OUT_OF_RANGE)
        .ExpectCall(BLOCK_OP_READ, 0, 0x800, ZX_OK);

    auto noop_callback = [](void* ctx, zx_status_t status, block_op_t* op) {};

    dut.BlockImplQueue(op1->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op2->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op3->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op4->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op5->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op6->operation(), noop_callback, nullptr);
    dut.BlockImplQueue(op7->operation(), noop_callback, nullptr);

    dut.WaitForBlockOps(2);
    dut.StopWorkerThread();

    dut.VerifyAll();
    mock_sdmmc.VerifyAll();
}

TEST(SdmmcBlockDeviceTest, DoTxn) {
    MockSdmmcDevice mock_sdmmc({
        .caps = SDMMC_HOST_CAP_ADMA2 | SDMMC_HOST_CAP_SIXTY_FOUR_BIT | SDMMC_HOST_CAP_AUTO_CMD12,
        .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
        .max_transfer_size_non_dma = 0,
        .prefs = 0
    });
    SdmmcBlockDeviceTest dut(&mock_sdmmc, {
        .block_count = 0x10000,
        .block_size = 512,
        .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
        .flags = 0,
        .reserved = 0
    });

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_WRITE_BLOCK, 0, 1, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_WRITE, 1, 0, ZX_OK);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_WRITE_MULTIPLE_BLOCK, 0x8000, 5, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_WRITE, 5, 0x8000, ZX_OK);

    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_FLUSH, 0, 0, ZX_OK);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_READ_BLOCK, 0x400, 1, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_READ, 1, 0x400, ZX_OK);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_READ_MULTIPLE_BLOCK, 0x2000, 10, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_READ, 10, 0x2000, ZX_OK);

    dut.DoTxn(BLOCK_OP_WRITE, 1, 0);
    dut.DoTxn(BLOCK_OP_WRITE, 5, 0x8000);
    dut.DoTxn(BLOCK_OP_FLUSH, 0, 0);
    dut.DoTxn(BLOCK_OP_READ, 1, 0x400);
    dut.DoTxn(BLOCK_OP_READ, 10, 0x2000);

    dut.VerifyAll();
    mock_sdmmc.VerifyAll();
}

TEST(SdmmcBlockDeviceTest, DoTxnNoACmd12) {
    MockSdmmcDevice mock_sdmmc({
        .caps = SDMMC_HOST_CAP_ADMA2 | SDMMC_HOST_CAP_SIXTY_FOUR_BIT,
        .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
        .max_transfer_size_non_dma = 0,
        .prefs = 0
    });
    SdmmcBlockDeviceTest dut(&mock_sdmmc, {
        .block_count = 0x10000,
        .block_size = 512,
        .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
        .flags = 0,
        .reserved = 0
    });

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_WRITE_BLOCK, 0, 1, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_WRITE, 1, 0, ZX_OK);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_WRITE_MULTIPLE_BLOCK, 0x8000, 5, 512);
    mock_sdmmc.mock_SdmmcStopTransmission().ExpectCall(ZX_OK);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_WRITE, 5, 0x8000, ZX_OK);

    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_FLUSH, 0, 0, ZX_OK);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_READ_BLOCK, 0x400, 1, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_READ, 1, 0x400, ZX_OK);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_READ_MULTIPLE_BLOCK, 0x2000, 10, 512);
    mock_sdmmc.mock_SdmmcStopTransmission().ExpectCall(ZX_OK);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_READ, 10, 0x2000, ZX_OK);

    dut.DoTxn(BLOCK_OP_WRITE, 1, 0);
    dut.DoTxn(BLOCK_OP_WRITE, 5, 0x8000);
    dut.DoTxn(BLOCK_OP_FLUSH, 0, 0);
    dut.DoTxn(BLOCK_OP_READ, 1, 0x400);
    dut.DoTxn(BLOCK_OP_READ, 10, 0x2000);

    dut.VerifyAll();
    mock_sdmmc.VerifyAll();
}

TEST(SdmmcBlockDeviceTest, DoTxnErrorsPropagate) {
    MockSdmmcDevice mock_sdmmc({
        .caps = SDMMC_HOST_CAP_ADMA2 | SDMMC_HOST_CAP_SIXTY_FOUR_BIT,
        .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
        .max_transfer_size_non_dma = 0,
        .prefs = 0
    });
    SdmmcBlockDeviceTest dut(&mock_sdmmc, {
        .block_count = 0x10000,
        .block_size = 512,
        .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
        .flags = 0,
        .reserved = 0
    });

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_ERR_IO, SDMMC_WRITE_BLOCK, 0, 1, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_WRITE, 1, 0, ZX_ERR_IO);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_ERR_BAD_STATE, SDMMC_WRITE_MULTIPLE_BLOCK, 0x8000,
                                              5, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_WRITE, 5, 0x8000, ZX_ERR_BAD_STATE);

    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_FLUSH, 0, 0, ZX_OK);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_ERR_TIMED_OUT, SDMMC_READ_BLOCK, 0x400, 1, 512);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_READ, 1, 0x400, ZX_ERR_TIMED_OUT);

    mock_sdmmc.mock_SdmmcRequest().ExpectCall(ZX_OK, SDMMC_READ_MULTIPLE_BLOCK, 0x2000, 10, 512);
    mock_sdmmc.mock_SdmmcStopTransmission().ExpectCall(ZX_ERR_IO_DATA_INTEGRITY);
    dut.mock_BlockComplete().ExpectCall(BLOCK_OP_READ, 10, 0x2000, ZX_ERR_IO_DATA_INTEGRITY);

    dut.DoTxn(BLOCK_OP_WRITE, 1, 0);
    dut.DoTxn(BLOCK_OP_WRITE, 5, 0x8000);
    dut.DoTxn(BLOCK_OP_FLUSH, 0, 0);
    dut.DoTxn(BLOCK_OP_READ, 1, 0x400);
    dut.DoTxn(BLOCK_OP_READ, 10, 0x2000);

    dut.VerifyAll();
    mock_sdmmc.VerifyAll();
}

TEST(SdmmcBlockDeviceTest, DdkLifecycle) {
    MockSdmmcDevice mock_sdmmc({});
    SdmmcBlockDeviceTest dut(&mock_sdmmc, {});

    fake_ddk::Bind ddk;
    EXPECT_OK(dut.AddDevice());
    dut.DdkUnbind();
    EXPECT_TRUE(ddk.Ok());

    dut.StopWorkerThread();
}

}  // namespace sdmmc
