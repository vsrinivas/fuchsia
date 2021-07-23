// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/nand/drivers/intel-spi-flash/intel-spi-flash.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/device.h>
#include <lib/zx/clock.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <condition_variable>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "src/devices/nand/drivers/intel-spi-flash/flash-chips.h"
#include "src/devices/nand/drivers/intel-spi-flash/registers.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

class SpiFlashTest : public zxtest::Test {
 public:
  SpiFlashTest()
      : region_(registers_, 4, countof(registers_)), fake_parent_(MockDevice::FakeRootParent()) {}
  void SetUp() override {
    cmd_handler_thread_ = std::thread(&SpiFlashTest::CmdThread, this);

    // Set up our registers.
    auto& control_reg = GetReg(spiflash::kSpiFlashHfstsCtl);
    control_reg.SetReadCallback([this]() { return ControlRead(); });
    control_reg.SetWriteCallback([this](uint64_t v) { ControlWrite(v); });
    // Set some sensible defaults - the SPI flash is valid and some other bits
    // that match what we observe on Atlas.
    {
      std::scoped_lock lock(mutex_);
      control_.set_prr34_lockdn(1).set_flockdn(1).set_fdv(1);
    }

    for (size_t i = 0; i < spiflash::kSpiFlashFdataCount; i++) {
      auto& reg = GetReg(spiflash::kSpiFlashFdataBase + (4 * i));
      reg.SetReadCallback([this, i]() { return data_[i]; });
      reg.SetWriteCallback([this, i](uint64_t val) { data_[i] = val; });
    }

    auto& addr_reg = GetReg(spiflash::kSpiFlashFaddr);
    addr_reg.SetReadCallback([this]() { return address_.reg_value(); });
    addr_reg.SetWriteCallback([this](uint64_t v) { address_.set_reg_value(v); });

    auto device =
        std::make_unique<spiflash::SpiFlashDevice>(fake_parent_.get(), region_.GetMmioBuffer());
    ASSERT_OK(device->Bind());
    __UNUSED auto unused = device.release();
    device_ = fake_parent_->GetLatestChild();
  }

  uint64_t ControlRead() {
    std::scoped_lock lock(mutex_);
    return control_.reg_value();
  }
  void ControlWrite(uint64_t val) {
    // We shouldn't touch this register while h_scip is set (i.e. while a command is being
    // executed).
    std::scoped_lock lock(mutex_);
    ASSERT_EQ(control_.h_scip(), 0);
    control_.set_reg_value(val);
    if (control_.fcerr()) {
      control_.set_fcerr(0);
    }
    if (control_.fdone()) {
      control_.set_fdone(0);
    }
    if (control_.h_ael()) {
      control_.set_h_ael(0);
    }

    if (control_.fgo()) {
      // Reads of fgo always return 0, but fgo indicates that we should start a transaction.
      control_.set_h_scip(1);
      control_.set_fgo(0);
      condition_.notify_all();
    }
  }

  void UnbindDevice() {
    if (device_) {
      device_async_remove(device_);
      ASSERT_OK(mock_ddk::ReleaseFlaggedDevices(fake_parent_.get()));
    }
    device_ = nullptr;
  }

  void TearDown() override {
    ASSERT_NO_FATAL_FAILURES(UnbindDevice());
    {
      std::scoped_lock lock(mutex_);
      stop_ = true;
      mmio_cmd_handler_ = [](uint32_t*, spiflash::FlashControl&) {};
    }
    condition_.notify_all();
    if (cmd_handler_thread_.joinable()) {
      cmd_handler_thread_.join();
    }
  }

  ddk_fake::FakeMmioReg& GetReg(uint32_t offset) { return registers_[offset / 4]; }

  void CmdThread() {
    std::scoped_lock lock(mutex_);
    while (true) {
      condition_.wait(mutex_,
                      [this]() __TA_REQUIRES(mutex_) { return stop_ || control_.h_scip(); });
      if (stop_) {
        break;
      }
      mmio_cmd_handler_(data_, control_);
    }
  }

  static void DefaultMmioCmdHandler(uint32_t* data, spiflash::FlashControl& ctrl) {
    ctrl.set_h_scip(0);
    switch (ctrl.fcycle()) {
      case spiflash::FlashControl::kReadJedecId: {
        data[0] = (spiflash::kVendorGigadevice << 24);
        uint16_t device = spiflash::kDeviceGigadeviceGD25Q127C;
        data[0] |= device & 0xff00;
        data[0] |= (device & 0xff) << 16;
        ctrl.set_fdone(1);
        break;
      }
      default: {
        ctrl.set_fcerr(1);
        break;
      }
    }
  }

 protected:
  ddk_fake::FakeMmioReg registers_[(spiflash::kSpiFlashSbrs / 4) + 1];
  ddk_fake::FakeMmioRegRegion region_;
  spiflash::FlashControl control_ __TA_GUARDED(mutex_);
  spiflash::FlashAddress address_;
  uint32_t data_[64 / sizeof(uint32_t)];
  std::shared_ptr<MockDevice> fake_parent_;
  MockDevice* device_;

  std::mutex mutex_;
  std::condition_variable_any condition_;
  bool stop_ = false;
  std::thread cmd_handler_thread_;
  std::function<void(uint32_t* data, spiflash::FlashControl& ctrl)> mmio_cmd_handler_ =
      DefaultMmioCmdHandler;
};

TEST_F(SpiFlashTest, TestCreateAndTearDown) {
  // Nothing to do.
}

TEST_F(SpiFlashTest, TestSimpleRead) {
  mmio_cmd_handler_ = [](uint32_t* data, spiflash::FlashControl& ctrl) {
    ASSERT_EQ(ctrl.fcycle(), spiflash::FlashControl::kRead);
    ASSERT_EQ(ctrl.fdbc(), 63);
    memset(data, 0xab, sizeof(data_));
    ctrl.set_h_scip(0).set_fdone(1);
  };

  ddk::NandProtocolClient nand(device_);
  nand_info_t info;
  uint64_t op_size;

  nand.Query(&info, &op_size);
  ASSERT_EQ(info.page_size, 256);
  ASSERT_EQ(op_size, sizeof(nand_operation_t));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(info.page_size, 0, &vmo));
  nand_operation_t read{.rw = {
                            .command = NAND_OP_READ,
                            .data_vmo = vmo.get(),
                            .length = 1,
                            .offset_nand = 0,
                            .offset_data_vmo = 0,
                        }};
  sync_completion_t waiter;
  nand.Queue(
      &read,
      [](void* cookie, zx_status_t result, nand_operation_t* op) {
        ASSERT_OK(result);
        sync_completion_signal(static_cast<sync_completion_t*>(cookie));
      },
      &waiter);
  sync_completion_wait(&waiter, ZX_TIME_INFINITE);

  std::vector<uint8_t> buffer(info.page_size);
  std::vector<uint8_t> expected(info.page_size, 0xab);
  ASSERT_OK(vmo.read(buffer.data(), 0, info.page_size));
  ASSERT_BYTES_EQ(buffer.data(), expected.data(), expected.size());
}

TEST_F(SpiFlashTest, TestCancelledInflightRead) {
  ddk::NandProtocolClient nand(device_);
  nand_info_t info;
  uint64_t op_size;

  nand.Query(&info, &op_size);
  ASSERT_EQ(info.page_size, 256);
  ASSERT_EQ(op_size, sizeof(nand_operation_t));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(info.page_size, 0, &vmo));
  nand_operation_t read{.rw = {
                            .command = NAND_OP_READ,
                            .data_vmo = vmo.get(),
                            .length = 1,
                            .offset_nand = 0,
                            .offset_data_vmo = 0,
                        }};

  auto* spiflash = device_->GetDeviceContext<spiflash::SpiFlashDevice>();
  spiflash->StartShutdown();
  bool ran = false;
  nand.Queue(
      &read,
      [](void* cookie, zx_status_t result, nand_operation_t* op) {
        ASSERT_STATUS(result, ZX_ERR_UNAVAILABLE);
        bool* ptr = static_cast<bool*>(cookie);
        *ptr = true;
      },
      &ran);

  UnbindDevice();
  ASSERT_TRUE(ran);
}
