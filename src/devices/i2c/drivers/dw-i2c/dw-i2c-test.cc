// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dw-i2c.h"

#include <lib/device-protocol/platform-device.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sync/completion.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2cimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <hw/reg.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace dw_i2c {

class DwI2cTester {
 public:
  static constexpr uint32_t kBufferDepth = 0x80;
  DwI2cTester()
      : mock_i2c_regs_(i2c_reg_array_, kRegSize, kRegBytes),
        mmio_buffer_(mock_i2c_regs_.GetMmioBuffer()) {
    SetupRegisters();

    fbl::AllocChecker ac;
    /* Create DwI2cBus instance */
    auto dw_i2c_bus = fbl::make_unique_checked<DwI2cBus>(&ac, std::move(mmio_buffer_), irq_);
    ASSERT_TRUE(ac.check());
    ASSERT_OK(dw_i2c_bus->Init());

    fbl::Vector<std::unique_ptr<DwI2cBus>> bus_list;
    bus_list.push_back(std::move(dw_i2c_bus), &ac);
    ASSERT_TRUE(ac.check());

    /* Create DwI2c instance */
    dw_i2c_ = fbl::make_unique_checked<DwI2c>(&ac, fake_ddk::kFakeParent, std::move(bus_list));
    ASSERT_TRUE(ac.check());
  }

  void SetupRegisters() {
    mock_i2c_regs_[CompTypeReg::Get().addr()].ReadReturns(DwI2cBus::kDwCompTypeNum);
    auto comp_param = CompParam1Reg::Get().FromValue(0);
    comp_param.set_rx_buffer_depth(kBufferDepth);
    comp_param.set_tx_buffer_depth(kBufferDepth);
    mock_i2c_regs_[comp_param.reg_addr()].ReadReturns(comp_param.reg_value());
  }

  auto GetDUT() { return std::move(dw_i2c_); }

  void VerifyAll() {}

  fake_ddk::Bind ddk;

 private:
  static constexpr uint32_t kRegSize = sizeof(uint32_t);
  static constexpr uint32_t kRegBytes = 0x100;
  static constexpr uint32_t kRegCount = kRegBytes / kRegSize;
  std::unique_ptr<DwI2c> dw_i2c_;
  ddk_mock::MockMmioReg i2c_reg_array_[kRegCount];
  ddk_mock::MockMmioRegRegion mock_i2c_regs_;
  ddk::MmioBuffer mmio_buffer_;
  zx::interrupt irq_;
};

TEST(DwI2cTest, DdkLifecyle) {
  DwI2cTester tester;
  auto dut = tester.GetDUT();
  ASSERT_OK(dut->DdkAdd("dw-i2c"));
  ASSERT_OK(dut->Init());
  dut->DdkAsyncRemove();
  ASSERT_TRUE(tester.ddk.Ok());
}

TEST(DwI2cTest, I2cImplGetBusCount) {
  DwI2cTester tester;
  auto dut = tester.GetDUT();
  ASSERT_OK(dut->Init());
  EXPECT_EQ(dut->I2cImplGetBusCount(), 1, "");
  tester.VerifyAll();
}

TEST(DwI2cTest, I2cImplGetMaxTransferSize) {
  DwI2cTester tester;
  auto dut = tester.GetDUT();
  ASSERT_OK(dut->Init());
  size_t out_size;
  ASSERT_OK(dut->I2cImplGetMaxTransferSize(0, &out_size));
  EXPECT_TRUE(out_size == DwI2cTester::kBufferDepth);
  tester.VerifyAll();
}

}  // namespace dw_i2c
