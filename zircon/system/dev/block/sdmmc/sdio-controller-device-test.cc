// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdio-controller-device.h"

#include <fbl/auto_lock.h>
#include <hw/sdio.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <zxtest/zxtest.h>

#include "mock-sdmmc-device.h"

namespace sdmmc {

class Bind : public fake_ddk::Bind {
 public:
  int total_children() const { return total_children_; }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (parent == fake_ddk::kFakeParent) {
      *out = fake_ddk::kFakeDevice;
      add_called_ = true;
    } else if (parent == fake_ddk::kFakeDevice) {
      *out = kFakeChild;
      children_++;
      total_children_++;
    } else {
      *out = kUnknownDevice;
      bad_parent_ = false;
    }

    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) override {
    if (device == fake_ddk::kFakeDevice) {
      remove_called_ = true;
    } else if (device == kFakeChild) {
      // Check that all children are removed before the parent is removed.
      if (!remove_called_) {
        children_--;
      }
    } else {
      bad_device_ = true;
    }

    return ZX_OK;
  }

  void Ok() {
    EXPECT_EQ(children_, 0);
    EXPECT_TRUE(add_called_);
    EXPECT_TRUE(remove_called_);
    EXPECT_FALSE(bad_parent_);
    EXPECT_FALSE(bad_device_);
  }

 private:
  zx_device_t* kFakeChild = reinterpret_cast<zx_device_t*>(0x1234);
  zx_device_t* kUnknownDevice = reinterpret_cast<zx_device_t*>(0x5678);

  int total_children_ = 0;
  int children_ = 0;

  bool bad_parent_ = false;
  bool bad_device_ = false;
  bool add_called_ = false;
  bool remove_called_ = false;
};

class SdioControllerDeviceTest : public SdioControllerDevice {
 public:
  SdioControllerDeviceTest(MockSdmmcDevice* mock_sdmmc, const sdio_device_hw_info_t& hw_info)
      : SdioControllerDevice(fake_ddk::kFakeParent, SdmmcDevice({}, {})), mock_sdmmc_(mock_sdmmc) {
    hw_info_ = hw_info;
  }

  void SetSdioFunctionInfo(uint8_t fn_idx, const SdioFunction& info) {
    fbl::AutoLock lock(&lock_);
    funcs_[fn_idx] = info;
  }

  auto& mock_SdioDoRwByte() { return mock_sdio_do_rw_byte_; }

  void VerifyAll() { mock_sdio_do_rw_byte_.VerifyAndClear(); }

  zx_status_t SdioDoRwByteLocked(bool write, uint8_t fn_idx, uint32_t addr, uint8_t write_byte,
                                 uint8_t* out_read_byte) override TA_REQ(lock_) {
    if (mock_sdio_do_rw_byte_.HasExpectations()) {
      std::tuple<zx_status_t, uint8_t> ret =
          mock_sdio_do_rw_byte_.Call(write, fn_idx, addr, write_byte);
      if (out_read_byte != nullptr) {
        *out_read_byte = std::get<1>(ret);
      }

      return std::get<0>(ret);
    } else {
      return SdioControllerDevice::SdioDoRwByteLocked(write, fn_idx, addr, write_byte,
                                                      out_read_byte);
    }
  }

  // Registers an interrupt with the SDIO controller for the given function. The interrupt is
  // managed by this object.
  zx_status_t RegisterInterrupt(uint8_t fn_idx) {
    zx_status_t status = ZX_OK;

    if (interrupts_[fn_idx].is_valid()) {
      return status;
    } else if (!port_.is_valid()) {
      if ((status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_)) != ZX_OK) {
        return status;
      }
    }

    if ((status = SdioGetInBandIntr(fn_idx, &interrupts_[fn_idx])) != ZX_OK) {
      return status;
    }

    return interrupts_[fn_idx].bind(port_, fn_idx, 0);
  }

  // Wait for count interrupts to be received for any combination of functions. Upon return the
  // bits in mask represent the different functions which had interrupts triggered.
  zx_status_t WaitForInterrupts(uint32_t count, uint8_t* mask) {
    *mask = 0;

    for (uint32_t i = 0; i < count; i++) {
      zx_port_packet_t packet;
      zx_status_t status = port_.wait(zx::time::infinite(), &packet);
      if (status != ZX_OK) {
        return status;
      }

      *mask |= static_cast<uint8_t>(1 << packet.key);
      interrupts_[packet.key].ack();
    }

    return ZX_OK;
  }

  zx_status_t ProcessCccrLocked() TA_EXCL(lock_) {
    fbl::AutoLock lock(&lock_);
    return ProcessCccr();
  }

  zx_status_t ProcessCisLocked(uint8_t fn_idx) TA_EXCL(lock_) {
    fbl::AutoLock lock(&lock_);
    return ProcessCis(fn_idx);
  }

  zx_status_t ProcessFbrLocked(uint8_t fn_idx) TA_EXCL(lock_) {
    fbl::AutoLock lock(&lock_);
    return ProcessFbr(fn_idx);
  }

  const SdioFunction& func(uint8_t func) { return funcs_[func]; }
  const sdio_device_hw_info_t& hw_info() { return hw_info_; }

 private:
  SdmmcDevice& sdmmc() override { return *mock_sdmmc_; }

  MockSdmmcDevice* mock_sdmmc_;
  mock_function::MockFunction<std::tuple<zx_status_t, uint8_t>, bool, uint8_t, uint32_t, uint8_t>
      mock_sdio_do_rw_byte_;
  zx::port port_;
  zx::interrupt interrupts_[SDIO_MAX_FUNCS];
};

TEST(SdioControllerDeviceTest, MultiplexInterrupts) {
  MockSdmmcDevice mock_sdmmc({});
  SdioControllerDeviceTest dut(&mock_sdmmc, {});

  ASSERT_OK(dut.StartSdioIrqThread());

  ASSERT_OK(dut.RegisterInterrupt(1));
  ASSERT_OK(dut.RegisterInterrupt(2));
  ASSERT_OK(dut.RegisterInterrupt(4));
  ASSERT_OK(dut.RegisterInterrupt(7));

  dut.mock_SdioDoRwByte()
      .ExpectCall({ZX_OK, 0b0000'0010}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b1111'1110}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b1010'0010}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b0011'0110}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0);

  uint8_t mask;

  dut.InBandInterruptCallback();
  EXPECT_OK(dut.WaitForInterrupts(1, &mask));
  EXPECT_EQ(mask, 0b0000'0010);

  dut.InBandInterruptCallback();
  EXPECT_OK(dut.WaitForInterrupts(4, &mask));
  EXPECT_EQ(mask, 0b1001'0110);

  dut.InBandInterruptCallback();
  EXPECT_OK(dut.WaitForInterrupts(2, &mask));
  EXPECT_EQ(mask, 0b1000'0010);

  dut.InBandInterruptCallback();
  EXPECT_OK(dut.WaitForInterrupts(3, &mask));
  EXPECT_EQ(mask, 0b0001'0110);

  dut.StopSdioIrqThread();

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

TEST(SdioControllerDeviceTest, SdioDoRwTxn) {
  MockSdmmcDevice mock_sdmmc(
      {.caps = 0, .max_transfer_size = 16, .max_transfer_size_non_dma = 16, .prefs = 0});
  SdioControllerDeviceTest dut(&mock_sdmmc, {});
  dut.SetSdioFunctionInfo(
      3, {.hw_info = {}, .cur_blk_size = 8, .enabled = true, .intr_enabled = false});

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

  sdio_rw_txn_t txn = {.addr = 0xabcd0008,
                       .data_size = 36,
                       .incr = false,
                       .write = true,
                       .use_dma = false,
                       .dma_vmo = ZX_HANDLE_INVALID,
                       .virt_buffer = nullptr,
                       .virt_size = 0,
                       .buf_offset = 16};
  EXPECT_OK(dut.SdioDoRwTxn(3, &txn));

  txn = {.addr = 0x12340008,
         .data_size = 36,
         .incr = true,
         .write = false,
         .use_dma = false,
         .dma_vmo = ZX_HANDLE_INVALID,
         .virt_buffer = nullptr,
         .virt_size = 0,
         .buf_offset = 16};
  EXPECT_OK(dut.SdioDoRwTxn(3, &txn));

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

TEST(SdioControllerDeviceTest, SdioDoRwTxnMultiBlock) {
  MockSdmmcDevice mock_sdmmc(
      {.caps = 0, .max_transfer_size = 32, .max_transfer_size_non_dma = 32, .prefs = 0});
  SdioControllerDeviceTest dut(
      &mock_sdmmc, {.num_funcs = 0, .sdio_vsn = 0, .cccr_vsn = 0, .caps = SDIO_CARD_MULTI_BLOCK});
  dut.SetSdioFunctionInfo(
      7, {.hw_info = {}, .cur_blk_size = 8, .enabled = true, .intr_enabled = false});

  mock_sdmmc.mock_SdioIoRwExtended()
      .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, false, 7, 0xabcd0008, false, 4, 8, 64)
      .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, false, 7, 0xabcd0008, false, 4, 8, 96)
      .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, false, 7, 0xabcd0008, false, 1, 4, 128)
      .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, true, 7, 0x12340008, true, 4, 8, 64)
      .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, true, 7, 0x12340028, true, 4, 8, 96)
      .ExpectCall(ZX_OK, SDIO_CARD_MULTI_BLOCK, true, 7, 0x12340048, true, 1, 4, 128);

  sdio_rw_txn_t txn = {.addr = 0xabcd0008,
                       .data_size = 68,
                       .incr = false,
                       .write = false,
                       .use_dma = false,
                       .dma_vmo = ZX_HANDLE_INVALID,
                       .virt_buffer = nullptr,
                       .virt_size = 0,
                       .buf_offset = 64};
  EXPECT_OK(dut.SdioDoRwTxn(7, &txn));

  txn = {.addr = 0x12340008,
         .data_size = 68,
         .incr = true,
         .write = true,
         .use_dma = false,
         .dma_vmo = ZX_HANDLE_INVALID,
         .virt_buffer = nullptr,
         .virt_size = 0,
         .buf_offset = 64};
  EXPECT_OK(dut.SdioDoRwTxn(7, &txn));

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

TEST(SdioControllerDeviceTest, DdkLifecycle) {
  MockSdmmcDevice mock_sdmmc({});
  SdioControllerDeviceTest dut(&mock_sdmmc,
                               {.num_funcs = 5, .sdio_vsn = 0, .cccr_vsn = 0, .caps = 0});

  Bind ddk;
  EXPECT_OK(dut.AddDevice());
  dut.DdkUnbind();
  dut.StopSdioIrqThread();

  ddk.Ok();
  EXPECT_EQ(ddk.total_children(), 4);
}

TEST(SdioControllerDeviceTest, SdioIntrPending) {
  MockSdmmcDevice mock_sdmmc({});
  SdioControllerDeviceTest dut(&mock_sdmmc, {});

  dut.mock_SdioDoRwByte()
      .ExpectCall({ZX_OK, 0b0011'0010}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b0010'0010}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b1000'0000}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b0000'0000}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b0000'1110}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b0000'1110}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0)
      .ExpectCall({ZX_OK, 0b0000'1110}, false, 0, SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, 0);

  bool pending;

  EXPECT_OK(dut.SdioIntrPending(4, &pending));
  EXPECT_TRUE(pending);

  EXPECT_OK(dut.SdioIntrPending(4, &pending));
  EXPECT_FALSE(pending);

  EXPECT_OK(dut.SdioIntrPending(7, &pending));
  EXPECT_TRUE(pending);

  EXPECT_OK(dut.SdioIntrPending(7, &pending));
  EXPECT_FALSE(pending);

  EXPECT_OK(dut.SdioIntrPending(1, &pending));
  EXPECT_TRUE(pending);

  EXPECT_OK(dut.SdioIntrPending(2, &pending));
  EXPECT_TRUE(pending);

  EXPECT_OK(dut.SdioIntrPending(3, &pending));
  EXPECT_TRUE(pending);

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

TEST(SdioControllerDeviceTest, EnableDisableFnIntr) {
  MockSdmmcDevice mock_sdmmc({});
  SdioControllerDeviceTest dut(&mock_sdmmc, {});

  dut.mock_SdioDoRwByte()
      .ExpectCall({ZX_OK, 0b0000'0000}, false, 0, 0x04, 0b0000'0000)
      .ExpectCall({ZX_OK, 0b0000'0000}, true, 0, 0x04, 0b0001'0001)
      .ExpectCall({ZX_OK, 0b0001'0001}, false, 0, 0x04, 0b0000'0000)
      .ExpectCall({ZX_OK, 0b0000'0000}, true, 0, 0x04, 0b1001'0001)
      .ExpectCall({ZX_OK, 0b1001'0001}, false, 0, 0x04, 0b0000'0000)
      .ExpectCall({ZX_OK, 0b0000'0000}, true, 0, 0x04, 0b1000'0001)
      .ExpectCall({ZX_OK, 0b1000'0001}, false, 0, 0x04, 0b0000'0000)
      .ExpectCall({ZX_OK, 0b0000'0000}, true, 0, 0x04, 0b0000'0000);

  EXPECT_OK(dut.SdioEnableFnIntr(4));
  EXPECT_OK(dut.SdioEnableFnIntr(7));
  EXPECT_OK(dut.SdioEnableFnIntr(4));
  EXPECT_OK(dut.SdioDisableFnIntr(4));
  EXPECT_OK(dut.SdioDisableFnIntr(7));
  EXPECT_NOT_OK(dut.SdioDisableFnIntr(7));

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

TEST(SdioControllerDeviceTest, ProcessCccr) {
  MockSdmmcDevice mock_sdmmc({});
  SdioControllerDeviceTest dut(&mock_sdmmc, {});

  dut.mock_SdioDoRwByte()
      // CCCR/SDIO revision.
      .ExpectCall({ZX_OK, 0x43}, false, 0, 0x00, 0)
      // Card compatibility.
      .ExpectCall({ZX_OK, 0xc2}, false, 0, 0x08, 0)
      // Bus speed select.
      .ExpectCall({ZX_OK, 0xa9}, false, 0, 0x13, 0)
      // UHS-I support.
      .ExpectCall({ZX_OK, 0x3f}, false, 0, 0x14, 0)
      // Driver strength.
      .ExpectCall({ZX_OK, 0xb7}, false, 0, 0x15, 0)
      .ExpectCall({ZX_OK, 0x43}, false, 0, 0x00, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x08, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x13, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x14, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x15, 0)
      .ExpectCall({ZX_OK, 0x41}, false, 0, 0x00, 0)
      .ExpectCall({ZX_OK, 0x33}, false, 0, 0x00, 0);

  EXPECT_OK(dut.ProcessCccrLocked());
  EXPECT_EQ(dut.hw_info().caps,
            SDIO_CARD_MULTI_BLOCK | SDIO_CARD_LOW_SPEED | SDIO_CARD_FOUR_BIT_BUS |
                SDIO_CARD_HIGH_SPEED | SDIO_CARD_UHS_SDR50 | SDIO_CARD_UHS_SDR104 |
                SDIO_CARD_UHS_DDR50 | SDIO_CARD_TYPE_A | SDIO_CARD_TYPE_B | SDIO_CARD_TYPE_D);

  EXPECT_OK(dut.ProcessCccrLocked());
  EXPECT_EQ(dut.hw_info().caps, 0);

  EXPECT_NOT_OK(dut.ProcessCccrLocked());
  EXPECT_NOT_OK(dut.ProcessCccrLocked());

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

TEST(SdioControllerDeviceTest, ProcessCis) {
  MockSdmmcDevice mock_sdmmc({});
  SdioControllerDeviceTest dut(&mock_sdmmc, {});

  dut.mock_SdioDoRwByte()
      // CIS pointer.
      .ExpectCall({ZX_OK, 0xa2}, false, 0, 0x00'05'09, 0)
      .ExpectCall({ZX_OK, 0xc2}, false, 0, 0x00'05'0a, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'05'0b, 0)
      // Manufacturer ID tuple.
      .ExpectCall({ZX_OK, 0x20}, false, 0, 0x00'c2'a2, 0)
      // Manufacturer ID tuple size.
      .ExpectCall({ZX_OK, 0x04}, false, 0, 0x00'c2'a3, 0)
      // Manufacturer code.
      .ExpectCall({ZX_OK, 0x01}, false, 0, 0x00'c2'a4, 0)
      .ExpectCall({ZX_OK, 0xc0}, false, 0, 0x00'c2'a5, 0)
      // Manufacturer information (part number/revision).
      .ExpectCall({ZX_OK, 0xce}, false, 0, 0x00'c2'a6, 0)
      .ExpectCall({ZX_OK, 0xfa}, false, 0, 0x00'c2'a7, 0)
      // Null tuple.
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'a8, 0)
      // Function extensions tuple.
      .ExpectCall({ZX_OK, 0x22}, false, 0, 0x00'c2'a9, 0)
      // Function extensions tuple size.
      .ExpectCall({ZX_OK, 0x2a}, false, 0, 0x00'c2'aa, 0)
      // Type of extended data.
      .ExpectCall({ZX_OK, 0x01}, false, 0, 0x00'c2'ab, 0)
      // Stuff we don't use.
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'ac, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'ad, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'ae, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'af, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b0, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b1, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b2, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b3, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b4, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b5, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b6, 0)
      // Function block size.
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b7, 0)
      .ExpectCall({ZX_OK, 0x01}, false, 0, 0x00'c2'b8, 0)
      // More stuff we don't use.
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'b9, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'ba, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'bb, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'bc, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'bd, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'be, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'bf, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c0, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c1, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c2, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c3, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c4, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c5, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c6, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c7, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c8, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'c9, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'ca, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'cb, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'cc, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'cd, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'ce, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'cf, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'd0, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'd1, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'd2, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'd3, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x00'c2'd4, 0)
      // End-of-chain tuple.
      .ExpectCall({ZX_OK, 0xff}, false, 0, 0x00'c2'd5, 0);

  EXPECT_OK(dut.ProcessCisLocked(5));
  EXPECT_EQ(dut.func(5).hw_info.max_blk_size, 256);
  EXPECT_EQ(dut.func(5).hw_info.manufacturer_id, 0xc001);
  EXPECT_EQ(dut.func(5).hw_info.product_id, 0xface);

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

TEST(SdioControllerDeviceTest, ProcessCisFunction0) {
  MockSdmmcDevice mock_sdmmc(
      {.caps = 0, .max_transfer_size = 1024, .max_transfer_size_non_dma = 1024, .prefs = 0});
  SdioControllerDeviceTest dut(&mock_sdmmc, {});

  dut.mock_SdioDoRwByte()
      // CIS pointer.
      .ExpectCall({ZX_OK, 0xf5}, false, 0, 0x00'00'09, 0)
      .ExpectCall({ZX_OK, 0x61}, false, 0, 0x00'00'0a, 0)
      .ExpectCall({ZX_OK, 0x01}, false, 0, 0x00'00'0b, 0)
      // Function extensions tuple.
      .ExpectCall({ZX_OK, 0x22}, false, 0, 0x01'61'f5, 0)
      // Function extensions tuple size.
      .ExpectCall({ZX_OK, 0x04}, false, 0, 0x01'61'f6, 0)
      // Type of extended data.
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x01'61'f7, 0)
      // Function 0 block size.
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x01'61'f8, 0)
      .ExpectCall({ZX_OK, 0x02}, false, 0, 0x01'61'f9, 0)
      // Max transfer speed.
      .ExpectCall({ZX_OK, 0x32}, false, 0, 0x01'61'fa, 0)
      // Null tuple.
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x01'61'fb, 0)
      // Manufacturer ID tuple.
      .ExpectCall({ZX_OK, 0x20}, false, 0, 0x01'61'fc, 0)
      // Manufacturer ID tuple size.
      .ExpectCall({ZX_OK, 0x04}, false, 0, 0x01'61'fd, 0)
      // Manufacturer code.
      .ExpectCall({ZX_OK, 0xef}, false, 0, 0x01'61'fe, 0)
      .ExpectCall({ZX_OK, 0xbe}, false, 0, 0x01'61'ff, 0)
      // Manufacturer information (part number/revision).
      .ExpectCall({ZX_OK, 0xfe}, false, 0, 0x01'62'00, 0)
      .ExpectCall({ZX_OK, 0xca}, false, 0, 0x01'62'01, 0)
      // End-of-chain tuple.
      .ExpectCall({ZX_OK, 0xff}, false, 0, 0x01'62'02, 0);

  EXPECT_OK(dut.ProcessCisLocked(0));
  EXPECT_EQ(dut.func(0).hw_info.max_blk_size, 512);
  EXPECT_EQ(dut.func(0).hw_info.max_tran_speed, 25000);
  EXPECT_EQ(dut.func(0).hw_info.manufacturer_id, 0xbeef);
  EXPECT_EQ(dut.func(0).hw_info.product_id, 0xcafe);

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

TEST(SdioControllerDeviceTest, ProcessFbr) {
  MockSdmmcDevice mock_sdmmc({});
  SdioControllerDeviceTest dut(&mock_sdmmc, {});

  dut.mock_SdioDoRwByte()
      .ExpectCall({ZX_OK, 0x83}, false, 0, 0x100, 0)
      .ExpectCall({ZX_OK, 0x00}, false, 0, 0x500, 0)
      .ExpectCall({ZX_OK, 0x4e}, false, 0, 0x700, 0)
      .ExpectCall({ZX_OK, 0xcf}, false, 0, 0x600, 0)
      .ExpectCall({ZX_OK, 0xab}, false, 0, 0x601, 0);

  EXPECT_OK(dut.ProcessFbrLocked(1));
  EXPECT_EQ(dut.func(1).hw_info.fn_intf_code, 0x03);

  EXPECT_OK(dut.ProcessFbrLocked(5));
  EXPECT_EQ(dut.func(5).hw_info.fn_intf_code, 0x00);

  EXPECT_OK(dut.ProcessFbrLocked(7));
  EXPECT_EQ(dut.func(7).hw_info.fn_intf_code, 0x0e);

  EXPECT_OK(dut.ProcessFbrLocked(6));
  EXPECT_EQ(dut.func(6).hw_info.fn_intf_code, 0xab);

  dut.VerifyAll();
  mock_sdmmc.VerifyAll();
}

}  // namespace sdmmc
