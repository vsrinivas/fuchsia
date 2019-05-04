// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-gpio.h"

#include <fbl/algorithm.h>
#include <lib/mock-function/mock-function.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace gpio {

class As370GpioTest : public As370Gpio {
public:
    As370GpioTest()
        : As370Gpio(nullptr, ddk::MmioBuffer({}), ddk::MmioBuffer({}), ddk::MmioBuffer({})),
          mock_pinmux_regs_(pinmux_reg_array_, sizeof(uint32_t), fbl::count_of(pinmux_reg_array_)),
          mock_gpio1_regs_(gpio1_reg_array_, sizeof(uint32_t), fbl::count_of(gpio1_reg_array_)),
          mock_gpio2_regs_(gpio2_reg_array_, sizeof(uint32_t), fbl::count_of(gpio2_reg_array_)) {
        pinmux_mmio_ = ddk::MmioBuffer(mock_pinmux_regs_.GetMmioBuffer());
        gpio1_mmio_ = ddk::MmioBuffer(mock_gpio1_regs_.GetMmioBuffer());
        gpio2_mmio_ = ddk::MmioBuffer(mock_gpio2_regs_.GetMmioBuffer());
    }

    void VerifyAll() {
        mock_pinmux_regs_.VerifyAll();
        mock_gpio1_regs_.VerifyAll();
        mock_gpio2_regs_.VerifyAll();
        mock_gpio_impl_write_.VerifyAndClear();
    }

    ddk_mock::MockMmioRegRegion& mock_pinmux_regs() { return mock_pinmux_regs_; }
    ddk_mock::MockMmioRegRegion& mock_gpio1_regs() { return mock_gpio1_regs_; }
    ddk_mock::MockMmioRegRegion& mock_gpio2_regs() { return mock_gpio2_regs_; }

    auto& mock_GpioImplWrite() { return mock_gpio_impl_write_; }

    zx_status_t GpioImplWrite(uint32_t index, uint8_t value) override {
        if (mock_gpio_impl_write_.HasExpectations()) {
            return mock_gpio_impl_write_.Call(index, value);
        } else {
            return As370Gpio::GpioImplWrite(index, value);
        }
    }

private:
    ddk_mock::MockMmioReg pinmux_reg_array_[64];
    ddk_mock::MockMmioReg gpio1_reg_array_[128];
    ddk_mock::MockMmioReg gpio2_reg_array_[128];

    ddk_mock::MockMmioRegRegion mock_pinmux_regs_;
    ddk_mock::MockMmioRegRegion mock_gpio1_regs_;
    ddk_mock::MockMmioRegRegion mock_gpio2_regs_;

    mock_function::MockFunction<zx_status_t, uint32_t, uint8_t> mock_gpio_impl_write_;
};

TEST(As370GpioTest, ConfigIn) {
    As370GpioTest dut;

    dut.mock_gpio1_regs()[0x04]
        .ExpectRead(0xdeadbeef)
        .ExpectWrite(0xdeadbeee)
        .ExpectRead(0xabcd1234)
        .ExpectWrite(0xabcd0234)
        .ExpectRead(0xfedc1234)
        .ExpectWrite(0x7edc1234);

    dut.mock_gpio2_regs()[0x04]
        .ExpectRead(0xabcd4321)
        .ExpectWrite(0xabcd4320)
        .ExpectRead(0xcc7a2c98)
        .ExpectWrite(0xc47a2c98)
        .ExpectRead(0x89ab0123)
        .ExpectWrite(0x09ab0123);

    EXPECT_OK(dut.GpioImplConfigIn(0, GPIO_NO_PULL));
    EXPECT_OK(dut.GpioImplConfigIn(12, GPIO_NO_PULL));
    EXPECT_OK(dut.GpioImplConfigIn(31, GPIO_NO_PULL));

    EXPECT_OK(dut.GpioImplConfigIn(32, GPIO_NO_PULL));
    EXPECT_OK(dut.GpioImplConfigIn(59, GPIO_NO_PULL));
    EXPECT_OK(dut.GpioImplConfigIn(63, GPIO_NO_PULL));

    EXPECT_NE(ZX_OK, dut.GpioImplConfigIn(64, GPIO_NO_PULL));

    dut.VerifyAll();
}

TEST(As370GpioTest, ConfigOut) {
    As370GpioTest dut;

    dut.mock_GpioImplWrite()
        .ExpectCall(ZX_OK, 0, 0)
        .ExpectCall(ZX_OK, 20, 1)
        .ExpectCall(ZX_OK, 31, 0)
        .ExpectCall(ZX_OK, 32, 1)
        .ExpectCall(ZX_OK, 39, 0)
        .ExpectCall(ZX_OK, 63, 1);

    dut.mock_gpio1_regs()[0x04]
        .ExpectRead(0xc8e4dc3c)
        .ExpectWrite(0xc8e4dc3d)
        .ExpectRead(0x89226125)
        .ExpectWrite(0x89326125)
        .ExpectRead(0x19b21f13)
        .ExpectWrite(0x99b21f13);

    dut.mock_gpio2_regs()[0x04]
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

    EXPECT_NE(ZX_OK, dut.GpioImplConfigOut(64, 0));

    dut.VerifyAll();
}

TEST(As370GpioTest, SetAltFunction) {
    As370GpioTest dut;

    dut.mock_pinmux_regs()[0x40].ExpectRead(0x7a695363).ExpectWrite(0x7a695367);
    dut.mock_pinmux_regs()[0x44].ExpectRead(0x647b8955).ExpectWrite(0x649b8955);
    dut.mock_pinmux_regs()[0x48].ExpectRead(0xac20b39d).ExpectWrite(0xac2cb39d);
    dut.mock_pinmux_regs()[0x54].ExpectRead(0x2bfc508b).ExpectWrite(0x2b1c508b);
    dut.mock_pinmux_regs()[0x48].ExpectRead(0x833d4afc).ExpectWrite(0x833d4b7c);
    dut.mock_pinmux_regs()[0x48].ExpectRead(0xcd0f533b).ExpectWrite(0xcd0cd33b);

    EXPECT_OK(dut.GpioImplSetAltFunction(0, 7));
    EXPECT_OK(dut.GpioImplSetAltFunction(17, 4));
    EXPECT_OK(dut.GpioImplSetAltFunction(18, 3));
    EXPECT_OK(dut.GpioImplSetAltFunction(49, 0));
    EXPECT_OK(dut.GpioImplSetAltFunction(68, 5));
    EXPECT_OK(dut.GpioImplSetAltFunction(71, 1));

    EXPECT_NE(ZX_OK, dut.GpioImplSetAltFunction(72, 0));
    EXPECT_NE(ZX_OK, dut.GpioImplSetAltFunction(0, 8));

    dut.VerifyAll();
}

TEST(As370GpioTest, Read) {
    As370GpioTest dut;

    dut.mock_gpio1_regs()[0x50]
        .ExpectRead(0x833d4b7c)
        .ExpectRead(0xa66346fe)
        .ExpectRead(0x2962e9ab);

    dut.mock_gpio2_regs()[0x50]
        .ExpectRead(0x7054a9e7)
        .ExpectRead(0xe5770561)
        .ExpectRead(0xbd4bfdec);

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

    EXPECT_NE(ZX_OK, dut.GpioImplRead(64, &value));

    dut.VerifyAll();
}

TEST(As370GpioTest, Write) {
    As370GpioTest dut;

    dut.mock_gpio1_regs()[0x00]
        .ExpectRead(0xfff6b928)
        .ExpectWrite(0xfff6b929)
        .ExpectRead(0x6a246060)
        .ExpectWrite(0x6a246060)
        .ExpectRead(0xaab6b6b7)
        .ExpectWrite(0xaab6b6b7);

    dut.mock_gpio2_regs()[0x00]
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

    EXPECT_NE(ZX_OK, dut.GpioImplWrite(64, 0));

    dut.VerifyAll();
}

}  // namespace gpio
