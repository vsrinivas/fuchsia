// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-gpio.h"

#include <lib/mock-function/mock-function.h>

#include <fbl/algorithm.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace {

constexpr synaptics::PinmuxMetadata kVs680PinmuxMetadata = {
    .muxes = 3,
    .pinmux_map = {
        // AVIO GPIOs
        [ 0] = { synaptics::PinmuxEntry::kGpio, 1, 21, },  // I2S3_DI
        [ 1] = { synaptics::PinmuxEntry::kGpio, 1, 18, },  // I2S3_DO
        [ 2] = { synaptics::PinmuxEntry::kGpio, 1, 20, },  // I2S3_BCLKIO
        [ 3] = { synaptics::PinmuxEntry::kGpio, 1, 19, },  // I2S3_LRCKIO
        [ 4] = { synaptics::PinmuxEntry::kGpio, 1,  7, },  // SPDIFI
        [ 5] = { synaptics::PinmuxEntry::kGpio, 1, 17, },  // TX_EDDC_SDA
        [ 6] = { synaptics::PinmuxEntry::kGpio, 1, 16, },  // TX_EDDC_SCL
        [ 7] = { synaptics::PinmuxEntry::kGpio, 1, 15, },  // I2S2_MCLK
        [ 8] = { synaptics::PinmuxEntry::kGpio, 1, 13, },  // I2S2_DI3
        [ 9] = { synaptics::PinmuxEntry::kGpio, 1, 12, },  // I2S2_DI2
        [10] = { synaptics::PinmuxEntry::kGpio, 1, 11, },  // I2S2_DI1
        [11] = { synaptics::PinmuxEntry::kGpio, 1, 10, },  // I2S2_DI0
        [12] = { synaptics::PinmuxEntry::kGpio, 1,  9, },  // I2S2_BCLKIO
        [13] = { synaptics::PinmuxEntry::kGpio, 1,  8, },  // I2S2_LRCKIO
        [14] = { synaptics::PinmuxEntry::kGpio, 1,  6, },  // SPDIFO
        [15] = { synaptics::PinmuxEntry::kGpio, 1,  3, },  // I2S1_DO3
        [16] = { synaptics::PinmuxEntry::kGpio, 1,  2, },  // I2S1_DO2
        [17] = { synaptics::PinmuxEntry::kGpio, 1,  1, },  // I2S1_DO1
        [18] = { synaptics::PinmuxEntry::kGpio, 1, 14, },  // I2S1_MCLK
        [19] = { synaptics::PinmuxEntry::kGpio, 1,  0, },  // I2S1_DO0
        [20] = { synaptics::PinmuxEntry::kGpio, 1,  5, },  // I2S1_BCLKIO
        [21] = { synaptics::PinmuxEntry::kGpio, 1,  4, },  // I2S1_LRCKIO

        // SoC GPIOs
        [22] = { synaptics::PinmuxEntry::kGpio, 0, 27, },  // RGMII_TXCTL
        [23] = { synaptics::PinmuxEntry::kGpio, 0, 22, },  // RGMII_TXC
        [24] = { synaptics::PinmuxEntry::kGpio, 0, 26, },  // RGMII_TXD3
        [25] = { synaptics::PinmuxEntry::kGpio, 0, 25, },  // RGMII_TXD2
        [26] = { synaptics::PinmuxEntry::kGpio, 0, 24, },  // RGMII_TXD1
        [27] = { synaptics::PinmuxEntry::kGpio, 0, 23, },  // RGMII_TXD0
        [28] = { synaptics::PinmuxEntry::kGpio, 0, 21, },  // RGMII_MDIO
        [29] = { synaptics::PinmuxEntry::kGpio, 0, 20, },  // RGMII_MDC
        [30] = { synaptics::PinmuxEntry::kGpio, 0, 33, },  // RGMII_RXCTL
        [31] = { synaptics::PinmuxEntry::kGpio, 0, 28, },  // RGMII_RXC
        [32] = { synaptics::PinmuxEntry::kGpio, 0, 32, },  // RGMII_RXD3
        [33] = { synaptics::PinmuxEntry::kGpio, 0, 31, },  // RGMII_RXD2
        [34] = { synaptics::PinmuxEntry::kGpio, 0, 30, },  // RGMII_RXD1
        [35] = { synaptics::PinmuxEntry::kGpio, 0, 29, },  // RGMII_RXD0
        [36] = { synaptics::PinmuxEntry::kGpio, 0, 18, },  // STS1_VALD
        [37] = { synaptics::PinmuxEntry::kGpio, 0, 17, },  // STS1_SD
        [38] = { synaptics::PinmuxEntry::kGpio, 0, 16, },  // STS1_SOP
        [39] = { synaptics::PinmuxEntry::kGpio, 0, 15, },  // STS1_CLK
        [40] = { synaptics::PinmuxEntry::kGpio, 0, 14, },  // STS0_VALD
        [41] = { synaptics::PinmuxEntry::kGpio, 0, 13, },  // STS0_SD
        [42] = { synaptics::PinmuxEntry::kGpio, 0, 12, },  // STS0_SOP
        [43] = { synaptics::PinmuxEntry::kGpio, 0, 11, },  // STS0_CLK
        [44] = { synaptics::PinmuxEntry::kGpio, 0,  1, },  // SDIO_WP
        [45] = { synaptics::PinmuxEntry::kGpio, 0,  0, },  // SDIO_CDn
        [46] = { synaptics::PinmuxEntry::kGpio, 0, 10, },  // TW0_SDA
        [47] = { synaptics::PinmuxEntry::kGpio, 0,  9, },  // TW0_SCL
        [48] = { synaptics::PinmuxEntry::kGpio, 0,  8, },  // SPI1_SDI
        [49] = { synaptics::PinmuxEntry::kGpio, 0,  7, },  // SPI1_SCLK
        [50] = { synaptics::PinmuxEntry::kGpio, 0,  6, },  // SPI1_SDO
        [51] = { synaptics::PinmuxEntry::kGpio, 0,  5, },  // SPI1_SS3n
        [52] = { synaptics::PinmuxEntry::kGpio, 0,  4, },  // SPI1_SS2n
        [53] = { synaptics::PinmuxEntry::kGpio, 0,  3, },  // SPI1_SS1n
        [54] = { synaptics::PinmuxEntry::kGpio, 0,  2, },  // SPI1_SS0n
        [55] = { synaptics::PinmuxEntry::kGpio, 0, 19, },  // USB2_DRV_VBUS

        // System manager GPIOs
        [64] = { synaptics::PinmuxEntry::kGpio, 2,  0, },  // SM_TW2_SCL
        [65] = { synaptics::PinmuxEntry::kGpio, 2,  1, },  // SM_TW2_SDA
        [66] = { synaptics::PinmuxEntry::kGpio, 2,  4, },  // SM_HDMI_HPD
        [67] = { synaptics::PinmuxEntry::kGpio, 2,  5, },  // SM_HDMI_CEC
        [68] = { synaptics::PinmuxEntry::kGpio, 2,  2, },  // SM_URT1_TXD
        [69] = { synaptics::PinmuxEntry::kGpio, 2,  3, },  // SM_URT1_RXD
        [70] = { synaptics::PinmuxEntry::kGpio, 2,  6, },  // SM_TMS
        [71] = { synaptics::PinmuxEntry::kGpio, 2,  7, },  // SM_TDI
        [72] = { synaptics::PinmuxEntry::kGpio, 2,  8, },  // SM_TDO
        [73] = { synaptics::PinmuxEntry::kGpio, 2,  9, },  // SM_TW3_SCL
        [74] = { synaptics::PinmuxEntry::kGpio, 2, 10, },  // SM_TW3_SDA
        [75] = { synaptics::PinmuxEntry::kGpio, 2, 17, },  // SM_SPI2_SCLK
        [76] = { synaptics::PinmuxEntry::kGpio, 2, 16, },  // SM_SPI2_SDI
        [77] = { synaptics::PinmuxEntry::kGpio, 2, 15, },  // SM_SPI2_SDO
        [78] = { synaptics::PinmuxEntry::kGpio, 2, 14, },  // SM_SPI2_SS3n
        [79] = { synaptics::PinmuxEntry::kGpio, 2, 13, },  // SM_SPI2_SS2n
        [80] = { synaptics::PinmuxEntry::kGpio, 2, 12, },  // SM_SPI2_SS1n
        [81] = { synaptics::PinmuxEntry::kGpio, 2, 11, },  // SM_SPI2_SS0n
        [82] = { synaptics::PinmuxEntry::kGpio, 2, 18, },  // SM_URT0_TXD
        [83] = { synaptics::PinmuxEntry::kGpio, 2, 19, },  // SM_URT0_RXD
        [84] = { synaptics::PinmuxEntry::kGpio, 2, 20, },  // SM_HDMIRX_HPD
        [85] = { synaptics::PinmuxEntry::kGpio, 2, 21, },  // SM_HDMIRX_PWR5V
    },
};

}  // namespace

namespace gpio {

class As370GpioTest : public zxtest::Test {
 public:
  As370GpioTest()
      : zxtest::Test(),
        mock_pinmux1_regs_(pinmux1_reg_array_, sizeof(uint32_t), fbl::count_of(pinmux1_reg_array_)),
        mock_pinmux2_regs_(pinmux2_reg_array_, sizeof(uint32_t), fbl::count_of(pinmux2_reg_array_)),
        mock_pinmux3_regs_(pinmux3_reg_array_, sizeof(uint32_t), fbl::count_of(pinmux3_reg_array_)),
        mock_gpio1_regs_(gpio1_reg_array_, sizeof(uint32_t), fbl::count_of(gpio1_reg_array_)),
        mock_gpio2_regs_(gpio2_reg_array_, sizeof(uint32_t), fbl::count_of(gpio2_reg_array_)),
        mock_gpio3_regs_(gpio3_reg_array_, sizeof(uint32_t), fbl::count_of(gpio2_reg_array_)) {}

  void TearDown() override {
    mock_pinmux1_regs_.VerifyAll();
    mock_pinmux2_regs_.VerifyAll();
    mock_pinmux3_regs_.VerifyAll();
    mock_gpio1_regs_.VerifyAll();
    mock_gpio2_regs_.VerifyAll();
    mock_gpio3_regs_.VerifyAll();
  }

 protected:
  ddk_mock::MockMmioReg pinmux1_reg_array_[32];
  ddk_mock::MockMmioReg pinmux2_reg_array_[32];
  ddk_mock::MockMmioReg pinmux3_reg_array_[32];
  ddk_mock::MockMmioReg gpio1_reg_array_[128];
  ddk_mock::MockMmioReg gpio2_reg_array_[128];
  ddk_mock::MockMmioReg gpio3_reg_array_[128];

  ddk_mock::MockMmioRegRegion mock_pinmux1_regs_;
  ddk_mock::MockMmioRegRegion mock_pinmux2_regs_;
  ddk_mock::MockMmioRegRegion mock_pinmux3_regs_;
  ddk_mock::MockMmioRegRegion mock_gpio1_regs_;
  ddk_mock::MockMmioRegRegion mock_gpio2_regs_;
  ddk_mock::MockMmioRegRegion mock_gpio3_regs_;
};

TEST_F(As370GpioTest, ConfigIn) {
  fbl::Vector<ddk::MmioBuffer> pinmux_mmios;
  pinmux_mmios.push_back(mock_pinmux1_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux2_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux3_regs_.GetMmioBuffer());
  fbl::Vector<ddk::MmioBuffer> gpio_mmios;
  gpio_mmios.push_back(mock_gpio1_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio2_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio3_regs_.GetMmioBuffer());
  As370Gpio dut(nullptr, std::move(pinmux_mmios), std::move(gpio_mmios), {}, kVs680PinmuxMetadata);

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
      .ExpectWrite(0xcc5a2c98);

  mock_gpio3_regs_[0x04]
      .ExpectRead(0xb9e49005)
      .ExpectWrite(0xb9e49004)
      .ExpectRead(0xec6bd98f)
      .ExpectWrite(0xec6bd88f)
      .ExpectRead(0x44566443)
      .ExpectWrite(0x44566443);

  EXPECT_OK(dut.GpioImplConfigIn(0, GPIO_NO_PULL));
  EXPECT_OK(dut.GpioImplConfigIn(12, GPIO_NO_PULL));
  EXPECT_OK(dut.GpioImplConfigIn(31, GPIO_NO_PULL));

  EXPECT_OK(dut.GpioImplConfigIn(32, GPIO_NO_PULL));
  EXPECT_OK(dut.GpioImplConfigIn(53, GPIO_NO_PULL));

  EXPECT_NOT_OK(dut.GpioImplConfigIn(56, GPIO_NO_PULL));
  EXPECT_NOT_OK(dut.GpioImplConfigIn(63, GPIO_NO_PULL));

  EXPECT_OK(dut.GpioImplConfigIn(64, GPIO_NO_PULL));
  EXPECT_OK(dut.GpioImplConfigIn(72, GPIO_NO_PULL));
  EXPECT_OK(dut.GpioImplConfigIn(85, GPIO_NO_PULL));

  EXPECT_NOT_OK(dut.GpioImplConfigIn(86, GPIO_NO_PULL));
  EXPECT_NOT_OK(dut.GpioImplConfigIn(90, GPIO_NO_PULL));
}

TEST_F(As370GpioTest, ConfigOut) {
  fbl::Vector<ddk::MmioBuffer> pinmux_mmios;
  pinmux_mmios.push_back(mock_pinmux1_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux2_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux3_regs_.GetMmioBuffer());
  fbl::Vector<ddk::MmioBuffer> gpio_mmios;
  gpio_mmios.push_back(mock_gpio1_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio2_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio3_regs_.GetMmioBuffer());
  As370Gpio dut(nullptr, std::move(pinmux_mmios), std::move(gpio_mmios), {}, kVs680PinmuxMetadata);

  mock_gpio1_regs_[0x00]
      .ExpectRead(0xb9cc266b)
      .ExpectWrite(0xb9cc266a)
      .ExpectRead(0xeb4f99bd)
      .ExpectWrite(0xeb5f99bd)
      .ExpectRead(0xf2812503)
      .ExpectWrite(0x72812503);

  mock_gpio1_regs_[0x04]
      .ExpectRead(0x9b0461f0)
      .ExpectWrite(0x9b0461f1)
      .ExpectRead(0x02a63870)
      .ExpectWrite(0x02b63870)
      .ExpectRead(0x793e1b5e)
      .ExpectWrite(0xf93e1b5e);

  mock_gpio2_regs_[0x00]
      .ExpectRead(0xe3b50d68)
      .ExpectWrite(0xe3b50d69)
      .ExpectRead(0x2fec66bf)
      .ExpectWrite(0x2fec663f)
      .ExpectRead(0x7b3ab475)
      .ExpectWrite(0x7bbab475);

  mock_gpio2_regs_[0x04]
      .ExpectRead(0x6e2e14d6)
      .ExpectWrite(0x6e2e14d7)
      .ExpectRead(0x0f50524d)
      .ExpectWrite(0x0f5052cd)
      .ExpectRead(0xb61b5443)
      .ExpectWrite(0xb69b5443);

  mock_gpio3_regs_[0x00]
      .ExpectRead(0x46eea52d)
      .ExpectWrite(0x46eea52c)
      .ExpectRead(0x893e29c9)
      .ExpectWrite(0x893e39c9)
      .ExpectRead(0xaafd809d)
      .ExpectWrite(0xaadd809d);

  mock_gpio3_regs_[0x04]
      .ExpectRead(0xbb24ccb8)
      .ExpectWrite(0xbb24ccb9)
      .ExpectRead(0xef94ce58)
      .ExpectWrite(0xef94de58)
      .ExpectRead(0xde80a757)
      .ExpectWrite(0xdea0a757);

  EXPECT_OK(dut.GpioImplConfigOut(0, 0));
  EXPECT_OK(dut.GpioImplConfigOut(20, 1));
  EXPECT_OK(dut.GpioImplConfigOut(31, 0));

  EXPECT_OK(dut.GpioImplConfigOut(32, 1));
  EXPECT_OK(dut.GpioImplConfigOut(39, 0));
  EXPECT_OK(dut.GpioImplConfigOut(55, 1));

  EXPECT_NOT_OK(dut.GpioImplConfigOut(56, 0));
  EXPECT_NOT_OK(dut.GpioImplConfigOut(63, 0));

  EXPECT_OK(dut.GpioImplConfigOut(64, 0));
  EXPECT_OK(dut.GpioImplConfigOut(76, 1));
  EXPECT_OK(dut.GpioImplConfigOut(85, 0));

  EXPECT_NOT_OK(dut.GpioImplConfigOut(86, 0));
}

TEST_F(As370GpioTest, SetAltFunction) {
  fbl::Vector<ddk::MmioBuffer> pinmux_mmios;
  pinmux_mmios.push_back(mock_pinmux1_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux2_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux3_regs_.GetMmioBuffer());
  fbl::Vector<ddk::MmioBuffer> gpio_mmios;
  gpio_mmios.push_back(mock_gpio1_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio2_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio3_regs_.GetMmioBuffer());
  As370Gpio dut(nullptr, std::move(pinmux_mmios), std::move(gpio_mmios), {}, kVs680PinmuxMetadata);

  mock_pinmux2_regs_[0x08].ExpectRead(0x4e903aa0).ExpectWrite(0x4e903ab8);
  mock_pinmux2_regs_[0x00].ExpectRead(0xe48418b8).ExpectWrite(0xe48418a0);
  mock_pinmux2_regs_[0x04].ExpectRead(0xe478e89f).ExpectWrite(0xe478b89f);

  mock_pinmux1_regs_[0x0c].ExpectRead(0xa7f120c4).ExpectWrite(0xa7f120c0);
  mock_pinmux1_regs_[0x00].ExpectRead(0x93d14c05).ExpectWrite(0x93114c05);

  mock_pinmux3_regs_[0x00].ExpectRead(0x24874be9).ExpectWrite(0x24874b69);
  mock_pinmux3_regs_[0x08].ExpectRead(0x8513ed89).ExpectWrite(0x8513ed89);

  EXPECT_OK(dut.GpioImplSetAltFunction(0, 7));
  EXPECT_OK(dut.GpioImplSetAltFunction(17, 4));
  EXPECT_OK(dut.GpioImplSetAltFunction(18, 3));

  EXPECT_OK(dut.GpioImplSetAltFunction(34, 0));
  EXPECT_OK(dut.GpioImplSetAltFunction(49, 0));

  EXPECT_OK(dut.GpioImplSetAltFunction(68, 5));
  EXPECT_OK(dut.GpioImplSetAltFunction(85, 1));

  EXPECT_NOT_OK(dut.GpioImplSetAltFunction(56, 0));
  EXPECT_NOT_OK(dut.GpioImplSetAltFunction(63, 0));
  EXPECT_NOT_OK(dut.GpioImplSetAltFunction(86, 0));
  EXPECT_NOT_OK(dut.GpioImplSetAltFunction(0, 8));
}

TEST_F(As370GpioTest, Read) {
  fbl::Vector<ddk::MmioBuffer> pinmux_mmios;
  pinmux_mmios.push_back(mock_pinmux1_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux2_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux3_regs_.GetMmioBuffer());
  fbl::Vector<ddk::MmioBuffer> gpio_mmios;
  gpio_mmios.push_back(mock_gpio1_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio2_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio3_regs_.GetMmioBuffer());
  As370Gpio dut(nullptr, std::move(pinmux_mmios), std::move(gpio_mmios), {}, kVs680PinmuxMetadata);

  mock_gpio1_regs_[0x50].ExpectRead(0xc6ad7ad8).ExpectRead(0x688608c3).ExpectRead(0x46363432);
  mock_gpio2_regs_[0x50].ExpectRead(0x40cd0cb7).ExpectRead(0x124e597c).ExpectRead(0x07dc67ea);
  mock_gpio3_regs_[0x50].ExpectRead(0x4b174988).ExpectRead(0x59fd2410);

  uint8_t value;

  EXPECT_OK(dut.GpioImplRead(0, &value));
  EXPECT_EQ(0, value);
  EXPECT_OK(dut.GpioImplRead(17, &value));
  EXPECT_EQ(1, value);
  EXPECT_OK(dut.GpioImplRead(31, &value));
  EXPECT_EQ(0, value);
  EXPECT_OK(dut.GpioImplRead(32, &value));
  EXPECT_EQ(1, value);
  EXPECT_OK(dut.GpioImplRead(47, &value));
  EXPECT_EQ(0, value);
  EXPECT_OK(dut.GpioImplRead(55, &value));
  EXPECT_EQ(1, value);
  EXPECT_OK(dut.GpioImplRead(69, &value));
  EXPECT_EQ(0, value);
  EXPECT_OK(dut.GpioImplRead(85, &value));
  EXPECT_EQ(1, value);

  EXPECT_NOT_OK(dut.GpioImplRead(63, &value));
  EXPECT_NOT_OK(dut.GpioImplRead(86, &value));
}

TEST_F(As370GpioTest, Interrupt) {
  zx::interrupt mock_irq1, dup_irq1;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &mock_irq1));
  ASSERT_OK(mock_irq1.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_irq1));

  zx::interrupt mock_irq2, dup_irq2;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &mock_irq2));
  ASSERT_OK(mock_irq2.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_irq2));

  fbl::Array<zx::interrupt> gpio_interrupts(new zx::interrupt[2], 2);
  gpio_interrupts[0] = std::move(dup_irq1);
  gpio_interrupts[1] = std::move(dup_irq2);

  fbl::Vector<ddk::MmioBuffer> pinmux_mmios;
  pinmux_mmios.push_back(mock_pinmux1_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux2_regs_.GetMmioBuffer());
  pinmux_mmios.push_back(mock_pinmux3_regs_.GetMmioBuffer());
  fbl::Vector<ddk::MmioBuffer> gpio_mmios;
  gpio_mmios.push_back(mock_gpio1_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio2_regs_.GetMmioBuffer());
  gpio_mmios.push_back(mock_gpio3_regs_.GetMmioBuffer());

  As370Gpio dut(nullptr, std::move(pinmux_mmios), std::move(gpio_mmios), std::move(gpio_interrupts),
                kVs680PinmuxMetadata);

  EXPECT_OK(dut.Init());

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

  mock_gpio2_regs_[0x30]
      .ExpectRead(0xABCD'2BCD)
      .ExpectRead(0xABCD'2BCD)
      .ExpectWrite(0xABCD'ABCD)
      .ExpectRead(0xABCD'ABCD)
      .ExpectRead(0xABCD'ABCD)
      .ExpectRead(0xABCD'ABCD)
      .ExpectWrite(0xABCD'2BCD);

  mock_gpio2_regs_[0x3c].ExpectRead(0xFFFE'AAAA).ExpectWrite(0xFFFE'2AAA);
  mock_gpio2_regs_[0x38].ExpectRead(0xFFFE'AAAA).ExpectWrite(0xFFFE'2AAA);

  // Interrupt Status and Clear
  mock_gpio1_regs_[0x40].ExpectRead(0x0000'0001);
  mock_gpio1_regs_[0x4c].ExpectRead(0xFFFE'AAAC).ExpectWrite(0xFFFE'AAAD);

  mock_gpio2_regs_[0x40].ExpectRead(0x0000'8000);
  mock_gpio2_regs_[0x4c].ExpectRead(0xFFFE'2AAA).ExpectWrite(0xFFFE'AAAA);

  zx::interrupt test_irq1;
  EXPECT_OK(dut.GpioImplGetInterrupt(0, ZX_INTERRUPT_MODE_EDGE_HIGH, &test_irq1));

  zx::interrupt test_irq2;
  ASSERT_OK(dut.GpioImplGetInterrupt(47, ZX_INTERRUPT_MODE_LEVEL_LOW, &test_irq2));

  EXPECT_OK(mock_irq1.trigger(0, zx::time()));
  EXPECT_OK(test_irq1.wait(nullptr));

  EXPECT_OK(mock_irq2.trigger(0, zx::time()));
  EXPECT_OK(test_irq2.wait(nullptr));

  EXPECT_OK(dut.GpioImplReleaseInterrupt(0));
  EXPECT_OK(dut.GpioImplReleaseInterrupt(47));

  dut.Shutdown();
}

}  // namespace gpio
