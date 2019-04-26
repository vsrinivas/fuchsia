// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/protocol/sdio.h>
#include <fbl/vector.h>
#include <lib/fzl/vmo-mapper.h>
#include <zxtest/zxtest.h>

namespace mock_sdio {

// This class mocks an SDIO device by providing an sdio_protocol_t. Users can set expectations that
// either return specified data on read or verify data on write. After the test, use VerifyAndClear
// to reset the object and verify that all expectations were satisfied. See the following example
// test:
//
// mock_sdio::MockSdio sdio;
// sdio
//     .ExpectReadByte(SDIO_FN_1, 0x10, 0xab)
//     .ExpectFifoWrite(SDIO_FN_2, 0x20, {0x01, 0x23, 0x45, 0x67}, true)
//     .ExpectRead(SDIO_FN_1, 0x00, {0x89, 0xab});
//
// SomeDriver dut(sdio.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES(sdio.VerifyAndClear());

class MockSdio : ddk::SdioProtocol<MockSdio> {
public:
    MockSdio() : proto_{&sdio_protocol_ops_, this} {}

    const sdio_protocol_t* GetProto() { return &proto_; }

    MockSdio& ExpectReadByte(uint8_t fn_idx, uint32_t addr, uint8_t byte) {
        SdioRwExpectation exp{
            .fn_idx = fn_idx,
            .addr = addr,
            .incr = false,
            .write = false,
            .data = {byte},
            .exact = true
        };
        expectations_.push_back(std::move(exp));
        return *this;
    }

    MockSdio& ExpectWriteByte(uint8_t fn_idx, uint32_t addr, uint8_t byte) {
        SdioRwExpectation exp{
            .fn_idx = fn_idx,
            .addr = addr,
            .incr = false,
            .write = true,
            .data = {byte},
            .exact = true
        };
        expectations_.push_back(std::move(exp));
        return *this;
    }

    MockSdio& ExpectFifoRead(uint8_t fn_idx, uint32_t addr, fbl::Vector<uint8_t> buf, bool exact) {
        SdioRwExpectation exp{
            .fn_idx = fn_idx,
            .addr = addr,
            .incr = false,
            .write = false,
            .data = std::move(buf),
            .exact = exact
        };
        expectations_.push_back(std::move(exp));
        return *this;
    }

    MockSdio& ExpectFifoWrite(uint8_t fn_idx, uint32_t addr, fbl::Vector<uint8_t> buf, bool exact) {
        SdioRwExpectation exp{
            .fn_idx = fn_idx,
            .addr = addr,
            .incr = false,
            .write = true,
            .data = std::move(buf),
            .exact = exact
        };
        expectations_.push_back(std::move(exp));
        return *this;
    }

    MockSdio& ExpectRead(uint8_t fn_idx, uint32_t addr, fbl::Vector<uint8_t> buf, bool exact) {
        SdioRwExpectation exp{
            .fn_idx = fn_idx,
            .addr = addr,
            .incr = true,
            .write = false,
            .data = std::move(buf),
            .exact = exact
        };
        expectations_.push_back(std::move(exp));
        return *this;
    }

    MockSdio& ExpectWrite(uint8_t fn_idx, uint32_t addr, fbl::Vector<uint8_t> buf, bool exact) {
        SdioRwExpectation exp{
            .fn_idx = fn_idx,
            .addr = addr,
            .incr = true,
            .write = true,
            .data = std::move(buf),
            .exact = exact
        };
        expectations_.push_back(std::move(exp));
        return *this;
    }

    MockSdio& ExpectGetInBandIntr(uint8_t fn_idx, const zx::interrupt& interrupt) {
        ExpectGetInBandIntrHelper(fn_idx, interrupt);
        return *this;
    }

    void VerifyAndClear() {
        EXPECT_EQ(expectations_index_, expectations_.size(), "More transactions are expected");
        expectations_.reset();
        expectations_index_ = 0;
    }

    // These are used by ddk::SdioProtocol but are not intended for use by tests.
    zx_status_t SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // TODO(bradenkell): Add support for testing these.
    zx_status_t SdioEnableFn(uint8_t fn_idx) {
        return ZX_OK;
    }

    zx_status_t SdioDisableFn(uint8_t fn_idx) {
        return ZX_OK;
    }

    zx_status_t SdioEnableFnIntr(uint8_t fn_idx) {
        return ZX_OK;
    }

    zx_status_t SdioDisableFnIntr(uint8_t fn_idx) {
        return ZX_OK;
    }

    zx_status_t SdioUpdateBlockSize(uint8_t fn_idx, uint16_t blk_sz, bool deflt) {
        return ZX_OK;
    }

    zx_status_t SdioGetBlockSize(uint8_t fn_idx, uint16_t* out_cur_blk_size) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t SdioDoRwTxn(uint8_t fn_idx, sdio_rw_txn_t* txn) {
        DoRwTxnHelper(fn_idx, txn);
        return ZX_OK;
    }

    zx_status_t SdioDoRwByte(bool write, uint8_t fn_idx, uint32_t addr, uint8_t write_byte,
                             uint8_t* out_read_byte) {
        DoRwByteHelper(write, fn_idx, addr, write_byte, out_read_byte);
        return ZX_OK;
    }

    zx_status_t SdioGetInBandIntr(uint8_t fn_idx, zx::interrupt* out_irq) {
        GetInBandIntrHelper(fn_idx, out_irq);
        return ZX_OK;
    }

private:
    struct SdioRwExpectation {
        uint8_t fn_idx;
        uint32_t addr;
        bool incr;
        bool write;
        fbl::Vector<uint8_t> data;
        bool exact;
    };

    void ExpectGetInBandIntrHelper(uint8_t fn_idx, const zx::interrupt& interrupt) {
        ASSERT_LT(fn_idx, countof(interrupts_));
        ASSERT_FALSE(interrupts_[fn_idx].is_valid(), "Interrupt has already been set");
        EXPECT_OK(interrupt.duplicate(ZX_RIGHT_SAME_RIGHTS, &interrupts_[fn_idx]),
                  "Failed to duplicate interrupt");
    }

    void DoRwHelper(uint8_t fn_idx, uint32_t addr, bool incr, bool write, uint8_t* buffer,
                    uint32_t size) {
        ASSERT_LT(expectations_index_, expectations_.size(), "No more transactions are expected");

        const SdioRwExpectation& exp = expectations_[expectations_index_++];
        EXPECT_EQ(exp.fn_idx, fn_idx, "Transaction function mismatch");
        EXPECT_EQ(exp.addr, addr, "Transaction address mismatch");
        EXPECT_EQ(exp.incr, incr, "Transaction FIFO mismatch");
        ASSERT_EQ(exp.write, write, "Transaction read/write mismatch");

        if (exp.exact) {
            ASSERT_EQ(exp.data.size(), size, "Transaction size mismatch");
        } else {
            // The expected message must not be larger than the provided buffer.
            ASSERT_LE(exp.data.size(), size, "Transaction size mismatch");
        }

        if (write) {
            EXPECT_BYTES_EQ(exp.data.get(), buffer, exp.data.size());
        } else {
            memcpy(buffer, exp.data.get(), exp.data.size());
        }
    }

    void DoRwTxnHelper(uint8_t fn_idx, sdio_rw_txn_t* txn) {
        ASSERT_NOT_NULL(txn, "Transaction struct is null");

        uint8_t* buffer = reinterpret_cast<uint8_t*>(txn->virt_buffer) + txn->buf_offset;

        fzl::VmoMapper mapper;
        if (txn->use_dma) {
            zx::vmo vmo(txn->dma_vmo);
            zx_status_t status =
                mapper.Map(vmo, 0, txn->data_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);

            __UNUSED zx_handle_t handle = vmo.release();

            ASSERT_OK(status, "Failed to map DMA VMO");

            buffer = reinterpret_cast<uint8_t*>(mapper.start()) + txn->buf_offset;
        }

        DoRwHelper(fn_idx, txn->addr, txn->incr, txn->write, buffer, txn->data_size);
    }

    void DoRwByteHelper(bool write, uint8_t fn_idx, uint32_t addr, uint8_t write_byte,
                        uint8_t* out_read_byte) {
        DoRwHelper(fn_idx, addr, false, write, write ? &write_byte : out_read_byte, 1);
    }

    void GetInBandIntrHelper(uint8_t fn_idx, zx::interrupt* out_irq) {
        ASSERT_LT(fn_idx, SDIO_MAX_FUNCS);
        ASSERT_TRUE(interrupts_[fn_idx].is_valid(), "No interrupt has been set");
        ASSERT_NOT_NULL(out_irq, "Out interrupt is null");

        *out_irq = std::move(interrupts_[fn_idx]);
    }

    const sdio_protocol_t proto_;
    zx::interrupt interrupts_[SDIO_MAX_FUNCS];
    fbl::Vector<SdioRwExpectation> expectations_;
    size_t expectations_index_ = 0;
};

}  // namespace mock_sdio
