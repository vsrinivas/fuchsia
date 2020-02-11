// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-gpio.h"

#include <lib/mock-function/mock-function.h>

#include <fbl/algorithm.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace gpio {

class TestAs370Gpio : public As370Gpio {
 public:
  TestAs370Gpio(ddk::MmioBuffer pinmux_mmio, ddk::MmioBuffer gpio1_mmio, ddk::MmioBuffer gpio2_mmio)
      : As370Gpio(nullptr, std::move(pinmux_mmio), std::move(gpio1_mmio), std::move(gpio2_mmio),
                  zx::interrupt()) {}

  TestAs370Gpio(ddk::MmioBuffer pinmux_mmio, ddk::MmioBuffer gpio1_mmio, ddk::MmioBuffer gpio2_mmio,
                zx::interrupt gpio1_irq)
      : As370Gpio(nullptr, std::move(pinmux_mmio), std::move(gpio1_mmio), std::move(gpio2_mmio),
                  std::move(gpio1_irq)) {}

  void VerifyAll() { mock_gpio_impl_write_.VerifyAndClear(); }

  auto& mock_GpioImplWrite() { return mock_gpio_impl_write_; }

  zx_status_t GpioImplWrite(uint32_t index, uint8_t value) override {
    if (mock_gpio_impl_write_.HasExpectations()) {
      return mock_gpio_impl_write_.Call(index, value);
    } else {
      return As370Gpio::GpioImplWrite(index, value);
    }
  }

 private:
  mock_function::MockFunction<zx_status_t, uint32_t, uint8_t> mock_gpio_impl_write_;
};

class As370GpioTest : public zxtest::Test {
 public:
  As370GpioTest()
      : zxtest::Test(),
        mock_pinmux_regs_(pinmux_reg_array_, sizeof(uint32_t), fbl::count_of(pinmux_reg_array_)),
        mock_gpio1_regs_(gpio1_reg_array_, sizeof(uint32_t), fbl::count_of(gpio1_reg_array_)),
        mock_gpio2_regs_(gpio2_reg_array_, sizeof(uint32_t), fbl::count_of(gpio2_reg_array_)) {}

  void TearDown() override {
    mock_pinmux_regs_.VerifyAll();
    mock_gpio1_regs_.VerifyAll();
    mock_gpio2_regs_.VerifyAll();
  }

 protected:
  ddk_mock::MockMmioReg pinmux_reg_array_[96];
  ddk_mock::MockMmioReg gpio1_reg_array_[128];
  ddk_mock::MockMmioReg gpio2_reg_array_[128];

  ddk_mock::MockMmioRegRegion mock_pinmux_regs_;
  ddk_mock::MockMmioRegRegion mock_gpio1_regs_;
  ddk_mock::MockMmioRegRegion mock_gpio2_regs_;
};

TEST_F(As370GpioTest, ConfigIn) {
  TestAs370Gpio dut(mock_pinmux_regs_.GetMmioBuffer(), mock_gpio1_regs_.GetMmioBuffer(),
                    mock_gpio2_regs_.GetMmioBuffer());

  mock_gpio1_regs_[0x04]
      .ExpectRead(0xdeadbeef)
      .ExpectWrite(0xdeadbeee)
      .ExpectRead(0xabcd1234)
      .ExpectWrite(0xabcd0234)
      .ExpectRead(0xfedc1234)
      .ExpectWrite(0x7edc1234);

  mock_gpio2_regs_[0x04]
      .ExpectRead(0xabcd4321)
      .ExpectWrite(0xabcd4320)
      .ExpectRead(0xcc7a2c98)
      .ExpectWrite(0xc47a2c98)
      .ExpectRead(0x89ab0123)
      .ExpectWrite(0x09ab0123);

  mock_pinmux_regs_[0x060].ExpectRead(0b0100).ExpectWrite(0b1000);
  mock_pinmux_regs_[0x090].ExpectRead(0b1100).ExpectWrite(0b0000);
  mock_pinmux_regs_[0x0fc].ExpectRead(0b0000).ExpectWrite(0b0100);
  mock_pinmux_regs_[0x100].ExpectRead(0b0100).ExpectWrite(0b0000);
  mock_pinmux_regs_[0x16c].ExpectRead(0b0000).ExpectWrite(0b0100);
  mock_pinmux_regs_[0x17c].ExpectRead(0b1100).ExpectWrite(0b1000);
  mock_pinmux_regs_[0x0b0].ExpectRead(0b0000).ExpectWrite(0b1000);
  mock_pinmux_regs_[0x0c0].ExpectRead(0b1000).ExpectWrite(0b0100);

  EXPECT_OK(dut.GpioImplConfigIn(0, GPIO_PULL_UP));
  EXPECT_OK(dut.GpioImplConfigIn(12, GPIO_NO_PULL));
  EXPECT_OK(dut.GpioImplConfigIn(31, GPIO_PULL_DOWN));

  EXPECT_OK(dut.GpioImplConfigIn(32, GPIO_NO_PULL));
  EXPECT_OK(dut.GpioImplConfigIn(59, GPIO_PULL_DOWN));
  EXPECT_OK(dut.GpioImplConfigIn(63, GPIO_PULL_UP));

  EXPECT_OK(dut.GpioImplConfigIn(66, GPIO_PULL_UP));
  EXPECT_OK(dut.GpioImplConfigIn(70, GPIO_PULL_DOWN));

  EXPECT_NOT_OK(dut.GpioImplConfigIn(72, GPIO_NO_PULL));

  dut.VerifyAll();
}

TEST_F(As370GpioTest, ConfigOut) {
  TestAs370Gpio dut(mock_pinmux_regs_.GetMmioBuffer(), mock_gpio1_regs_.GetMmioBuffer(),
                    mock_gpio2_regs_.GetMmioBuffer());

  dut.mock_GpioImplWrite()
      .ExpectCall(ZX_OK, 0, 0)
      .ExpectCall(ZX_OK, 20, 1)
      .ExpectCall(ZX_OK, 31, 0)
      .ExpectCall(ZX_OK, 32, 1)
      .ExpectCall(ZX_OK, 39, 0)
      .ExpectCall(ZX_OK, 63, 1);

  mock_gpio1_regs_[0x04]
      .ExpectRead(0xc8e4dc3c)
      .ExpectWrite(0xc8e4dc3d)
      .ExpectRead(0x89226125)
      .ExpectWrite(0x89326125)
      .ExpectRead(0x19b21f13)
      .ExpectWrite(0x99b21f13);

  mock_gpio2_regs_[0x04]
      .ExpectRead(0x9f5f0d82)
      .ExpectWrite(0x9f5f0d83)
      .ExpectRead(0x4b012478)
      .ExpectWrite(0x4b0124f8)
      .ExpectRead(0x468529a9)
      .ExpectWrite(0xc68529a9);

  EXPECT_OK(dut.GpioImplConfigOut(0, 0));
  EXPECT_OK(dut.GpioImplConfigOut(20, 1));
  EXPECT_OK(dut.GpioImplConfigOut(31, 0));

  EXPECT_OK(dut.GpioImplConfigOut(32, 1));
  EXPECT_OK(dut.GpioImplConfigOut(39, 0));
  EXPECT_OK(dut.GpioImplConfigOut(63, 1));

  EXPECT_NOT_OK(dut.GpioImplConfigOut(64, 0));

  dut.VerifyAll();
}

TEST_F(As370GpioTest, SetAltFunction) {
  TestAs370Gpio dut(mock_pinmux_regs_.GetMmioBuffer(), mock_gpio1_regs_.GetMmioBuffer(),
                    mock_gpio2_regs_.GetMmioBuffer());

  mock_pinmux_regs_[0x40].ExpectRead(0x7a695363).ExpectWrite(0x7a695367);
  mock_pinmux_regs_[0x44].ExpectRead(0x647b8955).ExpectWrite(0x649b8955);
  mock_pinmux_regs_[0x48].ExpectRead(0xac20b39d).ExpectWrite(0xac2cb39d);
  mock_pinmux_regs_[0x54].ExpectRead(0x2bfc508b).ExpectWrite(0x2b1c508b);
  mock_pinmux_regs_[0x48].ExpectRead(0x833d4afc).ExpectWrite(0x833d4b7c);
  mock_pinmux_regs_[0x48].ExpectRead(0xcd0f533b).ExpectWrite(0xcd0cd33b);

  EXPECT_OK(dut.GpioImplSetAltFunction(0, 7));
  EXPECT_OK(dut.GpioImplSetAltFunction(17, 4));
  EXPECT_OK(dut.GpioImplSetAltFunction(18, 3));
  EXPECT_OK(dut.GpioImplSetAltFunction(49, 0));
  EXPECT_OK(dut.GpioImplSetAltFunction(68, 5));
  EXPECT_OK(dut.GpioImplSetAltFunction(71, 1));

  EXPECT_NOT_OK(dut.GpioImplSetAltFunction(72, 0));
  EXPECT_NOT_OK(dut.GpioImplSetAltFunction(0, 8));

  dut.VerifyAll();
}

TEST_F(As370GpioTest, Read) {
  TestAs370Gpio dut(mock_pinmux_regs_.GetMmioBuffer(), mock_gpio1_regs_.GetMmioBuffer(),
                    mock_gpio2_regs_.GetMmioBuffer());

  mock_gpio1_regs_[0x50].ExpectRead(0x833d4b7c).ExpectRead(0xa66346fe).ExpectRead(0x2962e9ab);

  mock_gpio2_regs_[0x50].ExpectRead(0x7054a9e7).ExpectRead(0xe5770561).ExpectRead(0xbd4bfdec);

  uint8_t value;

  EXPECT_OK(dut.GpioImplRead(0, &value));
  EXPECT_EQ(0, value);
  EXPECT_OK(dut.GpioImplRead(17, &value));
  EXPECT_EQ(1, value);
  EXPECT_OK(dut.GpioImplRead(31, &value));
  EXPECT_EQ(0, value);
  EXPECT_OK(dut.GpioImplRead(32, &value));
  EXPECT_EQ(1, value);
  EXPECT_OK(dut.GpioImplRead(55, &value));
  EXPECT_EQ(0, value);
  EXPECT_OK(dut.GpioImplRead(63, &value));
  EXPECT_EQ(1, value);

  EXPECT_NOT_OK(dut.GpioImplRead(64, &value));

  dut.VerifyAll();
}

TEST_F(As370GpioTest, Write) {
  TestAs370Gpio dut(mock_pinmux_regs_.GetMmioBuffer(), mock_gpio1_regs_.GetMmioBuffer(),
                    mock_gpio2_regs_.GetMmioBuffer());

  mock_gpio1_regs_[0x00]
      .ExpectRead(0xfff6b928)
      .ExpectWrite(0xfff6b929)
      .ExpectRead(0x6a246060)
      .ExpectWrite(0x6a246060)
      .ExpectRead(0xaab6b6b7)
      .ExpectWrite(0xaab6b6b7);

  mock_gpio2_regs_[0x00]
      .ExpectRead(0x8a22ff3b)
      .ExpectWrite(0x8a22ff3a)
      .ExpectRead(0x07e37cb7)
      .ExpectWrite(0x07e37db7)
      .ExpectRead(0x833d4b7c)
      .ExpectWrite(0x033d4b7c);

  EXPECT_OK(dut.GpioImplWrite(0, 0x9c));
  EXPECT_OK(dut.GpioImplWrite(12, 0x00));
  EXPECT_OK(dut.GpioImplWrite(31, 0x1e));
  EXPECT_OK(dut.GpioImplWrite(32, 0x00));
  EXPECT_OK(dut.GpioImplWrite(40, 0xba));
  EXPECT_OK(dut.GpioImplWrite(63, 0x00));

  EXPECT_NOT_OK(dut.GpioImplWrite(64, 0));

  dut.VerifyAll();
}

TEST_F(As370GpioTest, Interrupt) {
  zx::interrupt mock_irq, dup_irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &mock_irq));
  ASSERT_OK(mock_irq.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_irq));

  TestAs370Gpio dut(mock_pinmux_regs_.GetMmioBuffer(), mock_gpio1_regs_.GetMmioBuffer(),
                    mock_gpio2_regs_.GetMmioBuffer(), std::move(dup_irq));

  dut.Init();
  // Interrupt enable register
  mock_gpio1_regs_[0x30]
      .ExpectRead(0xABCD'EF80)  // Interrupt enable check
      .ExpectRead(0xABCD'EF80)  // Set pin 0 interrupt enable
      .ExpectWrite(0xABCD'EF81)
      .ExpectRead(0xABCD'EF81)  // Irq thread interrupt enable check
      .ExpectRead(0xABCD'EF81)  // Release method interrupt check
      .ExpectRead(0xABCD'EF81)  // Disable interrupt
      .ExpectWrite(0xABCD'EF80);

  // Interrupt Polarity and Level
  mock_gpio1_regs_[0x3c].ExpectRead(0xFFFE'AAA8).ExpectWrite(0xFFFE'AAA9);
  mock_gpio1_regs_[0x38].ExpectRead(0xFFFE'AAA8).ExpectWrite(0xFFFE'AAA9);

  // Interrupt Status and Clear
  mock_gpio1_regs_[0x40].ExpectRead(0x0000'0001);
  mock_gpio1_regs_[0x4c].ExpectRead(0xFFFE'AAAC).ExpectWrite(0xFFFE'AAAD);

  zx::interrupt test_irq;
  EXPECT_OK(dut.GpioImplGetInterrupt(0, ZX_INTERRUPT_MODE_EDGE_HIGH, &test_irq));

  EXPECT_OK(mock_irq.trigger(0, zx::time()));
  EXPECT_OK(test_irq.wait(nullptr));

  EXPECT_OK(dut.GpioImplReleaseInterrupt(0));

  dut.Shutdown();
  dut.VerifyAll();
}

TEST_F(As370GpioTest, SetDriveStrength) {
  TestAs370Gpio dut(mock_pinmux_regs_.GetMmioBuffer(), mock_gpio1_regs_.GetMmioBuffer(),
                    mock_gpio2_regs_.GetMmioBuffer());

  mock_pinmux_regs_[0x088].ExpectWrite(2);
  mock_pinmux_regs_[0x12c].ExpectWrite(0);
  mock_pinmux_regs_[0x0b8].ExpectWrite(3);

  EXPECT_OK(dut.GpioImplSetDriveStrength(10, 8));
  EXPECT_NOT_OK(dut.GpioImplSetDriveStrength(10, 0));
  EXPECT_OK(dut.GpioImplSetDriveStrength(43, 2));
  EXPECT_NOT_OK(dut.GpioImplSetDriveStrength(43, 10));
  EXPECT_OK(dut.GpioImplSetDriveStrength(68, 12));
  EXPECT_NOT_OK(dut.GpioImplSetDriveStrength(68, 16));

  EXPECT_NOT_OK(dut.GpioImplSetDriveStrength(72, 12));

  dut.VerifyAll();
}

}  // namespace gpio
