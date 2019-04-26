// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdio-device.h"

#include <hw/sdio.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <zxtest/zxtest.h>

#include "mock-sdmmc-device.h"

namespace sdmmc {

class SdioDeviceTest : public SdioDevice {
public:
    SdioDeviceTest(MockSdmmcDevice* mock_sdmmc, const sdio_device_hw_info_t& hw_info)
        : SdioDevice(fake_ddk::kFakeParent, SdmmcDevice({}, {})), mock_sdmmc_(mock_sdmmc) {
        hw_info_ = hw_info;
    }

    void SetSdioFunctionInfo(uint8_t fn_idx, const SdioFunction& info) { funcs_[fn_idx] = info; }

    auto& mock_SdioDoRwByte() { return mock_sdio_do_rw_byte_; }

    void VerifyAll() { mock_sdio_do_rw_byte_.VerifyAndClear(); }

    zx_status_t SdioDoRwByte(bool write, uint8_t fn_idx, uint32_t addr, uint8_t write_byte,
                             uint8_t* out_read_byte) override {
        if (mock_sdio_do_rw_byte_.HasExpectations()) {
            std::tuple<zx_status_t, uint8_t> ret =
                mock_sdio_do_rw_byte_.Call(write, fn_idx, addr, write_byte);
            if (out_read_byte != nullptr) {
                *out_read_byte = std::get<1>(ret);
            }

            return std::get<0>(ret);
        } else {
            return SdioDevice::SdioDoRwByte(write, fn_idx, addr, write_byte, out_read_byte);
        }
    }

private:
    SdmmcDevice& sdmmc() override { return *mock_sdmmc_; }

    MockSdmmcDevice* mock_sdmmc_;
    mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>, bool, uint8_t, uint32_t, uint8_t>
        mock_sdio_do_rw_byte_;
};

TEST(SdioDeviceTest, SdioDoRwTxn) {
    MockSdmmcDevice mock_sdmmc({
        .caps = 0,
        .max_transfer_size = 16,
        .max_transfer_size_non_dma = 16,
        .prefs = 0
    });
    SdioDeviceTest dut(&mock_sdmmc, {});
    dut.SetSdioFunctionInfo(3, {
        .hw_info = {},
        .cur_blk_size = 8,
        .enabled = true,
        .intr_enabled = false
    });

    mock_sdmmc.mock_SdioIoRwExtended()
        .ExpectCall(ZX_OK, 0, true, 3, 0xabcd0008, false, 1, 8, 16)
        .ExpectCall(ZX_OK, 0, true, 3, 0xabcd0008, false, 1, 8, 24)
        .ExpectCall(ZX_OK, 0, true, 3, 0xabcd0008, false, 1, 8, 32)
        .ExpectCall(ZX_OK, 0, true, 3, 0xabcd0008, false, 1, 8, 40)
        .ExpectCall(ZX_OK, 0, true, 3, 0xabcd0008, false, 1, 4, 48)
        .ExpectCall(ZX_OK, 0, false, 3, 0x12340008, true, 1, 8, 16)
        .ExpectCall(ZX_OK, 0, false, 3, 0x12340010, true, 1, 8, 24)
        .ExpectCall(ZX_OK, 0, false, 3, 0x12340018, true, 1, 8, 32)
        .ExpectCall(ZX_OK, 0, false, 3, 0x12340020, true, 1, 8, 40)
        .ExpectCall(ZX_OK, 0, false, 3, 0x12340028, true, 1, 4, 48);

    sdio_rw_txn_t txn = {
        .addr = 0xabcd0008,
        .data_size = 36,
        .incr = false,
        .fifo = false,
        .write = true,
        .use_dma = false,
        .dma_vmo = ZX_HANDLE_INVALID,
        .virt_buffer = nullptr,
        .virt_size = 0,
        .buf_offset = 16
    };
    EXPECT_OK(dut.SdioDoRwTxn(3, &txn));

    txn = {
        .addr = 0x12340008,
        .data_size = 36,
        .incr = true,
        .fifo = false,
        .write = false,
        .use_dma = false,
        .dma_vmo = ZX_HANDLE_INVALID,
        .virt_buffer = nullptr,
        .virt_size = 0,
        .buf_offset = 16
    };
    EXPECT_OK(dut.SdioDoRwTxn(3, &txn));

    dut.VerifyAll();
    mock_sdmmc.VerifyAll();
}

TEST(SdioDeviceTest, SdioDoRwTxnMultiBlock) {
    MockSdmmcDevice mock_sdmmc({
        .caps = 0,
        .max_transfer_size = 32,
        .max_transfer_size_non_dma = 32,
        .prefs = 0
    });
    SdioDeviceTest dut(&mock_sdmmc, {
        .num_funcs = 0,
        .sdio_vsn = 0,
        .cccr_vsn = 0,
        .caps = SDIO_CARD_MULTI_BLOCK
    });
    dut.SetSdioFunctionInfo(7, {
        .hw_info = {},
        .cur_blk_size = 8,
        .enabled = true,
        .intr_enabled = false
    });

    mock_sdmmc.mock_SdioIoRwExtended()
        .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, false, 7, 0xabcd0008, false, 4, 8, 64)
        .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, false, 7, 0xabcd0008, false, 4, 8, 96)
        .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, false, 7, 0xabcd0008, false, 1, 4, 128)
        .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, true, 7, 0x12340008, true, 4, 8, 64)
        .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, true, 7, 0x12340028, true, 4, 8, 96)
        .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, true, 7, 0x12340048, true, 1, 4, 128);

    sdio_rw_txn_t txn = {
        .addr = 0xabcd0008,
        .data_size = 68,
        .incr = false,
        .fifo = false,
        .write = false,
        .use_dma = false,
        .dma_vmo = ZX_HANDLE_INVALID,
        .virt_buffer = nullptr,
        .virt_size = 0,
        .buf_offset = 64
    };
    EXPECT_OK(dut.SdioDoRwTxn(7, &txn));

    txn = {
        .addr = 0x12340008,
        .data_size = 68,
        .incr = true,
        .fifo = false,
        .write = true,
        .use_dma = false,
        .dma_vmo = ZX_HANDLE_INVALID,
        .virt_buffer = nullptr,
        .virt_size = 0,
        .buf_offset = 64
    };
    EXPECT_OK(dut.SdioDoRwTxn(7, &txn));

    dut.VerifyAll();
    mock_sdmmc.VerifyAll();
}

}  // namespace sdmmc
