// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BUS_TESTING_MOCK_SDIO_INCLUDE_LIB_MOCK_SDIO_MOCK_SDIO_H_
#define SRC_DEVICES_BUS_TESTING_MOCK_SDIO_INCLUDE_LIB_MOCK_SDIO_MOCK_SDIO_H_

#include <fuchsia/hardware/sdio/cpp/banjo.h>
#include <lib/fzl/vmo-mapper.h>

#include <fbl/vector.h>
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
// ASSERT_NO_FATAL_FAILURE(sdio.VerifyAndClear());

class MockSdio : ddk::SdioProtocol<MockSdio> {
 public:
  MockSdio() : proto_{&sdio_protocol_ops_, this} {}

  const sdio_protocol_t* GetProto() { return &proto_; }

  MockSdio& ExpectReadByte(uint32_t addr, uint8_t byte) {
    SdioRwExpectation exp{
        .addr = addr, .incr = false, .write = false, .data = {byte}, .exact = true};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  MockSdio& ExpectWriteByte(uint32_t addr, uint8_t byte) {
    SdioRwExpectation exp{
        .addr = addr, .incr = false, .write = true, .data = {byte}, .exact = true};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  MockSdio& ExpectFifoRead(uint32_t addr, fbl::Vector<uint8_t> buf, bool exact) {
    SdioRwExpectation exp{
        .addr = addr, .incr = false, .write = false, .data = std::move(buf), .exact = exact};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  MockSdio& ExpectFifoWrite(uint32_t addr, fbl::Vector<uint8_t> buf, bool exact) {
    SdioRwExpectation exp{
        .addr = addr, .incr = false, .write = true, .data = std::move(buf), .exact = exact};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  MockSdio& ExpectRead(uint32_t addr, fbl::Vector<uint8_t> buf, bool exact) {
    SdioRwExpectation exp{
        .addr = addr, .incr = true, .write = false, .data = std::move(buf), .exact = exact};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  MockSdio& ExpectWrite(uint32_t addr, fbl::Vector<uint8_t> buf, bool exact) {
    SdioRwExpectation exp{
        .addr = addr, .incr = true, .write = true, .data = std::move(buf), .exact = exact};
    expectations_.push_back(std::move(exp));
    return *this;
  }

  MockSdio& ExpectGetInBandIntr(const zx::interrupt& interrupt) {
    ExpectGetInBandIntrHelper(interrupt);
    return *this;
  }

  void VerifyAndClear() {
    EXPECT_EQ(expectations_index_, expectations_.size(), "More transactions are expected");
    expectations_.reset();
    expectations_index_ = 0;
  }

  // These are used by ddk::SdioProtocol but are not intended for use by tests.
  zx_status_t SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info) { return ZX_ERR_NOT_SUPPORTED; }

  // TODO(bradenkell): Add support for testing these.
  zx_status_t SdioEnableFn() { return ZX_OK; }

  zx_status_t SdioDisableFn() { return ZX_OK; }

  zx_status_t SdioEnableFnIntr() { return ZX_OK; }

  zx_status_t SdioDisableFnIntr() { return ZX_OK; }

  zx_status_t SdioUpdateBlockSize(uint16_t blk_sz, bool deflt) { return ZX_OK; }

  zx_status_t SdioGetBlockSize(uint16_t* out_cur_blk_size) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t SdioDoRwTxn(sdio_rw_txn_t* txn) {
    DoRwTxnHelper(txn);
    return ZX_OK;
  }

  zx_status_t SdioDoRwByte(bool write, uint32_t addr, uint8_t write_byte, uint8_t* out_read_byte) {
    DoRwByteHelper(write, addr, write_byte, out_read_byte);
    return ZX_OK;
  }

  zx_status_t SdioGetInBandIntr(zx::interrupt* out_irq) {
    GetInBandIntrHelper(out_irq);
    return ZX_OK;
  }

  zx_status_t SdioIoAbort() { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t SdioIntrPending(bool* out_pending) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t SdioDoVendorControlRwByte(bool write, uint8_t addr, uint8_t write_byte,
                                        uint8_t* out_read_byte) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t SdioRegisterVmo(uint32_t vmo_id, zx::vmo vmo, uint64_t offset, uint64_t size,
                              uint32_t vmo_rights) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t SdioUnregisterVmo(uint32_t vmo_id, zx::vmo* out_vmo) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t SdioDoRwTxnNew(const sdio_rw_txn_new_t* txn) { return ZX_ERR_NOT_SUPPORTED; }
  void SdioRunDiagnostics() {}

 private:
  struct SdioRwExpectation {
    uint32_t addr;
    bool incr;
    bool write;
    fbl::Vector<uint8_t> data;
    bool exact;
  };

  void ExpectGetInBandIntrHelper(const zx::interrupt& interrupt) {
    ASSERT_FALSE(interrupt_.is_valid(), "Interrupt has already been set");
    EXPECT_OK(interrupt.duplicate(ZX_RIGHT_SAME_RIGHTS, &interrupt_),
              "Failed to duplicate interrupt");
  }

  void DoRwHelper(uint32_t addr, bool incr, bool write, uint8_t* buffer, uint32_t size) {
    ASSERT_LT(expectations_index_, expectations_.size(), "No more transactions are expected");

    const SdioRwExpectation& exp = expectations_[expectations_index_++];
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
      EXPECT_BYTES_EQ(exp.data.data(), buffer, exp.data.size());
    } else {
      memcpy(buffer, exp.data.data(), exp.data.size());
    }
  }

  void DoRwTxnHelper(sdio_rw_txn_t* txn) {
    ASSERT_NOT_NULL(txn, "Transaction struct is null");

    uint8_t* buffer = reinterpret_cast<uint8_t*>(txn->virt_buffer) + txn->buf_offset;

    fzl::VmoMapper mapper;
    if (txn->use_dma) {
      zx::vmo vmo(txn->dma_vmo);
      zx_status_t status = mapper.Map(vmo, 0, txn->data_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);

      __UNUSED zx_handle_t handle = vmo.release();

      ASSERT_OK(status, "Failed to map DMA VMO");

      buffer = reinterpret_cast<uint8_t*>(mapper.start()) + txn->buf_offset;
    }

    DoRwHelper(txn->addr, txn->incr, txn->write, buffer, txn->data_size);
  }

  void DoRwByteHelper(bool write, uint32_t addr, uint8_t write_byte, uint8_t* out_read_byte) {
    DoRwHelper(addr, false, write, write ? &write_byte : out_read_byte, 1);
  }

  void GetInBandIntrHelper(zx::interrupt* out_irq) {
    ASSERT_TRUE(interrupt_.is_valid(), "No interrupt has been set");
    ASSERT_NOT_NULL(out_irq, "Out interrupt is null");

    *out_irq = std::move(interrupt_);
  }

  const sdio_protocol_t proto_;
  zx::interrupt interrupt_;
  fbl::Vector<SdioRwExpectation> expectations_;
  size_t expectations_index_ = 0;
};

}  // namespace mock_sdio

#endif  // SRC_DEVICES_BUS_TESTING_MOCK_SDIO_INCLUDE_LIB_MOCK_SDIO_MOCK_SDIO_H_
