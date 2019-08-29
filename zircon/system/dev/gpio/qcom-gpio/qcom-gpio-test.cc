// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qcom-gpio.h"

#include <fbl/auto_call.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

namespace {
constexpr size_t kGpioRegSize = 0x00300000 / sizeof(uint32_t);  // in 32 bits chunks.
}  // namespace

namespace gpio {

class QcomGpioDeviceTest : public QcomGpioDevice {
 public:
  explicit QcomGpioDeviceTest(ddk_mock::MockMmioRegRegion& gpio_regs)
      : QcomGpioDevice(nullptr, gpio_regs.GetMmioBuffer()) {
    // Fake interrupts.
    interrupts_ = fbl::Array(new zx::interrupt[kGpioMax], kGpioMax);
  }
};

TEST(GpioTest, AltMode) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x22000].ExpectRead(0x00000000).ExpectWrite(0x0000003C);  // 4 GpioCfg bits for mode.
  EXPECT_OK(gpio.GpioImplSetAltFunction(0x22, 15));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, NoPull0) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x00000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3);  // 4 GpioCfg bits for mode.
  gpios_mock[0x00000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF);  // 1 GpioCfg bits for OE.
  gpios_mock[0x00000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFC);  // bits 0 to disable pull.
  EXPECT_OK(gpio.GpioImplConfigIn(0, GPIO_NO_PULL));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, NoPullMid) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x43000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3);  // 4 GpioCfg bits for mode.
  gpios_mock[0x43000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF);  // 1 GpioCfg bits for OE.
  gpios_mock[0x43000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFC);  // bits 0 to disable pull.
  EXPECT_OK(gpio.GpioImplConfigIn(0x43, GPIO_NO_PULL));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, NoPullHigh) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x7C000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3);  // 4 GpioCfg bits for mode.
  gpios_mock[0x7C000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF);  // 1 GpioCfg bits for OE.
  gpios_mock[0x7C000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFC);  // bits 0 to disable pull.
  EXPECT_OK(gpio.GpioImplConfigIn(0x7C, GPIO_NO_PULL));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, OutOfRange) {
  ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(unused);

  EXPECT_EQ(gpio.GpioImplConfigIn(kGpioRegSize, GPIO_NO_PULL), ZX_ERR_INVALID_ARGS);
}

TEST(GpioTest, PullUp) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x21000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3);  // 4 GpioCfg bits for mode.
  gpios_mock[0x21000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF);  // 1 GpioCfg bits for OE.
  gpios_mock[0x21000].ExpectRead(0x00000000).ExpectWrite(0x00000003);  // bits to enable pull up.
  EXPECT_OK(gpio.GpioImplConfigIn(0x21, GPIO_PULL_UP));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, PullDown) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x20000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3);  // 4 GpioCfg bits for mode.
  gpios_mock[0x20000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF);  // 1 GpioCfg bits for OE.
  gpios_mock[0x20000].ExpectRead(0x00000000).ExpectWrite(0x00000001);  // bits to enable pull down.
  EXPECT_OK(gpio.GpioImplConfigIn(0x20, GPIO_PULL_DOWN));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, Out) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x19000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3);  // 4 GpioCfg bits for mode.
  gpios_mock[0x19000].ExpectRead(0x00000000).ExpectWrite(0x00000200);  // 1 GpioCfg bits for OE.
  gpios_mock[0x19004].ExpectRead(0x00000000).ExpectWrite(0x00000002);  // write 1 to out.
  EXPECT_OK(gpio.GpioImplConfigOut(0x19, 1));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, In) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x44000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3);  // 4 GpioCfg bits for mode.
  gpios_mock[0x44000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF);  // 0 GpioCfg bits for OE.
  gpios_mock[0x44000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFC);  // bits 0 to disable pull.
  gpios_mock[0x44004].ExpectRead(0x00000001);                          // read 0x01.
  gpios_mock[0x44004].ExpectRead(0x00000000);                          // read 0x00.
  gpios_mock[0x44004].ExpectRead(0x00000001);                          // read 0x01.
  EXPECT_OK(gpio.GpioImplConfigIn(0x44, GPIO_NO_PULL));
  uint8_t out_value = 0;
  EXPECT_OK(gpio.GpioImplRead(0x44, &out_value));
  EXPECT_EQ(out_value, 0x01);
  EXPECT_OK(gpio.GpioImplRead(0x44, &out_value));
  EXPECT_EQ(out_value, 0x00);
  EXPECT_OK(gpio.GpioImplRead(0x44, &out_value));
  EXPECT_EQ(out_value, 0x01);
  gpios_mock.VerifyAll();
}

TEST(GpioTest, GetInterrupt) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x55008].ExpectRead(0x00000000).ExpectWrite(0x00000008);  // int detect to edge low.
  gpios_mock[0x55008].ExpectRead(0x00000000).ExpectWrite(0x00000002);  // polarity + for any edge.
  gpios_mock[0x55008].ExpectRead(0x00000000).ExpectWrite(0x00000010);  // enable raw status.
  gpios_mock[0x55008].ExpectRead(0x00000000).ExpectWrite(0x00000080);  // route to APPS.
  gpios_mock[0x55008].ExpectRead(0x00000000).ExpectWrite(0x00000001);  // enable.
  zx::interrupt out_int;
  EXPECT_OK(gpio.GpioImplGetInterrupt(0x55, ZX_INTERRUPT_MODE_EDGE_LOW, &out_int));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, ReleaseInterrupt) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x66008].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFEF);  // disable raw status.
  gpios_mock[0x66008].ExpectRead(0x00000000).ExpectWrite(0x000000E0);  // don't route.
  gpios_mock[0x66008].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFE);  // disable.
  EXPECT_OK(gpio.GpioImplReleaseInterrupt(0x66));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, InterruptSetPolarityEdge) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x77008].ExpectRead(0x00000008);                          // edge low.
  gpios_mock[0x77008].ExpectRead(0x00000002);                          // polarity +;
  gpios_mock[0x77008].ExpectRead(0x00000000).ExpectWrite(0x00000004);  // int detect to edge high.
  gpios_mock[0x77008].ExpectRead(0x00000000).ExpectWrite(0x00000002);  // polarity + for any edge.
  gpios_mock[0x77008].ExpectRead(0x00000004);                          // edge high.
  gpios_mock[0x77008].ExpectRead(0x00000002);                          // polarity +;
  gpios_mock[0x77008].ExpectRead(0x00000000).ExpectWrite(0x00000008);  // int detect to edge low.
  gpios_mock[0x77008].ExpectRead(0x00000000).ExpectWrite(0x00000002);  // polarity + for any edge.
  zx::interrupt out_int;
  EXPECT_OK(gpio.GpioImplSetPolarity(0x77, true));
  EXPECT_OK(gpio.GpioImplSetPolarity(0x77, false));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, InterruptSetPolarityLevel) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x88008].ExpectRead(0x00000000);                          // level.
  gpios_mock[0x88008].ExpectRead(0x00000002);                          // polarity +;
  gpios_mock[0x88008].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFF3);  // int detect to level.
  gpios_mock[0x88008].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFD);  // polarity -.
  gpios_mock[0x88008].ExpectRead(0x00000000);                          // level.
  gpios_mock[0x88008].ExpectRead(0x00000000);                          // polarity -;
  gpios_mock[0x88008].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFF3);  // int detect to level.
  gpios_mock[0x88008].ExpectRead(0x00000000).ExpectWrite(0x00000002);  // polarity +.
  zx::interrupt out_int;
  EXPECT_OK(gpio.GpioImplSetPolarity(0x88, false));
  EXPECT_OK(gpio.GpioImplSetPolarity(0x88, true));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, SetDriveStrength2) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x87000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFE3F);  // strength to 0x0.
  EXPECT_OK(gpio.GpioImplSetDriveStrength(0x87, 2));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, SetDriveStrength16) {
  fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
      fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
  ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.data(), sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(gpios_mock);

  gpios_mock[0x86000].ExpectRead(0x00000000).ExpectWrite(0x000001C0);  // strength to 0x7.
  EXPECT_OK(gpio.GpioImplSetDriveStrength(0x86, 16));
  gpios_mock.VerifyAll();
}

TEST(GpioTest, SetDriveStrengthOdd) {
  ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(unused);
  EXPECT_NOT_OK(gpio.GpioImplSetDriveStrength(0x85, 1));
}

TEST(GpioTest, SetDriveStrengthLarge) {
  ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
  QcomGpioDeviceTest gpio(unused);
  EXPECT_NOT_OK(gpio.GpioImplSetDriveStrength(0x84, 17));
}

}  // namespace gpio

int main(int argc, char** argv) { return RUN_ALL_TESTS(argc, argv); }
