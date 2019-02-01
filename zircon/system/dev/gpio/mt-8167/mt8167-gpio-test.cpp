// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167-gpio.h"

#include <lib/mmio/mmio.h>
#include <fbl/auto_call.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

namespace {
constexpr size_t kGpioRegCount = MT8167_GPIO_SIZE / sizeof(uint16_t);
constexpr size_t kIoCfgRegCount = MT8167_IOCFG_SIZE / sizeof(uint16_t);
} // namespace

namespace gpio {

class Mt8167GpioDeviceTest : public Mt8167GpioDevice {
public:
    explicit Mt8167GpioDeviceTest(ddk_mock::MockMmioRegRegion& gpio_regs,
                                  ddk_mock::MockMmioRegRegion& registers1,
                                  ddk_mock::MockMmioRegRegion& registers2)
        : Mt8167GpioDevice(nullptr,
                           gpio_regs.GetMmioBuffer(),
                           registers1.GetMmioBuffer(),
                           registers2.GetMmioBuffer()) {
        // Fake interrupts fooling GPIO's checks, not used in these tests.
        interrupts_ = fbl::Array(new zx::interrupt[MT8167_GPIO_EINT_MAX], MT8167_GPIO_EINT_MAX);
    }
};

bool TestNoPull0() {
    BEGIN_TEST;

    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegCount], kGpioRegCount);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint16_t), kGpioRegCount);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), kGpioRegCount);
    Mt8167GpioDeviceTest gpio(gpios_mock, unused, unused);

    gpios_mock[0x300].ExpectRead(0xFFFF).ExpectWrite(0xFFF8); // 3 GPIO_MODE1 bits for GPIO mode.
    gpios_mock[0x000].ExpectRead(0xFFFF).ExpectWrite(0xFFFE); // GPIO_DIR1 bit 0 to input (0).
    gpios_mock[0x500].ExpectRead(0xFFFF).ExpectWrite(0xFFFE); // GPIO_PULLEN1 bit 0 to disabled (0).
    EXPECT_EQ(gpio.GpioImplConfigIn(0, GPIO_NO_PULL), ZX_OK);
    gpios_mock.VerifyAll();

    END_TEST;
}

bool TestNoPullMid() {
    BEGIN_TEST;

    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegCount], kGpioRegCount);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint16_t), kGpioRegCount);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), kGpioRegCount);
    Mt8167GpioDeviceTest gpio(gpios_mock, unused, unused);

    gpios_mock[0x3D0].ExpectRead(0xFFFF).ExpectWrite(0xFE3F); // 3 GPIO_MODEE bits for GPIO mode.
    gpios_mock[0x040].ExpectRead(0xFFFF).ExpectWrite(0xFFF7); // GPIO_DIR5 bit 0 to input (0).
    gpios_mock[0x540].ExpectRead(0xFFFF).ExpectWrite(0xFFF7); // GPIO_PULLEN5 bit 0 to disabled (0).
    EXPECT_EQ(gpio.GpioImplConfigIn(67, GPIO_NO_PULL), ZX_OK);
    gpios_mock.VerifyAll();

    END_TEST;
}

bool TestNoPullHigh() {
    BEGIN_TEST;

    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegCount], kGpioRegCount);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint16_t), kGpioRegCount);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), kGpioRegCount);
    Mt8167GpioDeviceTest gpio(gpios_mock, unused, unused);

    gpios_mock[0x480].ExpectRead(0xFFFF).ExpectWrite(0x8FFF); // 3 GPIO_MODE19 bits for GPIO mode.
    gpios_mock[0x070].ExpectRead(0xFFFF).ExpectWrite(0xEFFF); // GPIO_DIR8 bit 0 to input (0).
    gpios_mock[0x570].ExpectRead(0xFFFF).ExpectWrite(0xEFFF); // GPIO_PULLEN8 bit 0 to disabled (0).
    EXPECT_EQ(gpio.GpioImplConfigIn(124, GPIO_NO_PULL), ZX_OK);
    gpios_mock.VerifyAll();

    END_TEST;
}

bool TestOutOfRange() {
    BEGIN_TEST;

    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), kGpioRegCount);
    Mt8167GpioDeviceTest gpio(unused, unused, unused);

    EXPECT_EQ(gpio.GpioImplConfigIn(kGpioRegCount, GPIO_NO_PULL), ZX_ERR_INVALID_ARGS);

    END_TEST;
}

bool TestPullUp() {
    BEGIN_TEST;

    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegCount], kGpioRegCount);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint16_t), kGpioRegCount);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), kGpioRegCount);
    Mt8167GpioDeviceTest gpio(gpios_mock, unused, unused);

    gpios_mock[0x360].ExpectRead(0xFFFF).ExpectWrite(0xF1FF); // 3 GPIO_MODE7 bits for GPIO mode.
    gpios_mock[0x020].ExpectRead(0xFFFF).ExpectWrite(0xFFFD); // GPIO_DIR3 bit 0 to input (0).
    gpios_mock[0x520].ExpectRead(0x0000).ExpectWrite(0x0002); // GPIO_PULLEN3 bit 0 to enable (1).
    gpios_mock[0x620].ExpectRead(0x0000).ExpectWrite(0x0002); // GPIO_PULLSEL3 bit 0 to up (1).
    EXPECT_EQ(gpio.GpioImplConfigIn(33, GPIO_PULL_UP), ZX_OK);
    gpios_mock.VerifyAll();

    END_TEST;
}

bool TestPullDown() {
    BEGIN_TEST;

    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegCount], kGpioRegCount);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint16_t), kGpioRegCount);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), kGpioRegCount);
    Mt8167GpioDeviceTest gpio(gpios_mock, unused, unused);

    gpios_mock[0x360].ExpectRead(0xFFFF).ExpectWrite(0xFE3F); // 3 GPIO_MODE7 bits for GPIO mode.
    gpios_mock[0x020].ExpectRead(0xFFFF).ExpectWrite(0xFFFE); // GPIO_DIR3 bit 0 to input (0).
    gpios_mock[0x520].ExpectRead(0x0000).ExpectWrite(0x0001); // GPIO_PULLEN3 bit 0 to enable (1).
    gpios_mock[0x620].ExpectRead(0xFFFF).ExpectWrite(0xFFFE); // GPIO_PULLSEL3 bit 0 to down (0).
    EXPECT_EQ(gpio.GpioImplConfigIn(32, GPIO_PULL_DOWN), ZX_OK);
    gpios_mock.VerifyAll();

    END_TEST;
}

bool TestNoPullIoCfg() {
    BEGIN_TEST;

    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegCount], kGpioRegCount);
    fbl::Array<ddk_mock::MockMmioReg> iocfg_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kIoCfgRegCount], kIoCfgRegCount);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint16_t), kGpioRegCount);
    ddk_mock::MockMmioRegRegion iocfg_mock(iocfg_regs.get(), sizeof(uint16_t), kIoCfgRegCount);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), 1); // Fake non-zero size.
    Mt8167GpioDeviceTest gpio(gpios_mock, iocfg_mock, unused);

    gpios_mock[0x330].ExpectRead(0xFFFF).ExpectWrite(0xFFF8); // 3 GPIO_MODE4 bits for GPIO mode.
    gpios_mock[0x000].ExpectRead(0xFFFF).ExpectWrite(0x7FFF); // GPIO_DIR1 bit 15 to input (0).
    iocfg_mock[0x560].ExpectRead(0xFFFF).ExpectWrite(0xFFFC); // PUPD_CTRL6 bits 0-1 to no pull (0).
    EXPECT_EQ(gpio.GpioImplConfigIn(15, GPIO_NO_PULL), ZX_OK);
    gpios_mock.VerifyAll();
    iocfg_mock.VerifyAll();

    END_TEST;
}

bool TestPullUpIoCfg() {
    BEGIN_TEST;

    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegCount], kGpioRegCount);
    fbl::Array<ddk_mock::MockMmioReg> iocfg_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kIoCfgRegCount], kIoCfgRegCount);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint16_t), kGpioRegCount);
    ddk_mock::MockMmioRegRegion iocfg_mock(iocfg_regs.get(), sizeof(uint16_t), kIoCfgRegCount);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), 1); // Fake non-zero size.
    Mt8167GpioDeviceTest gpio(gpios_mock, iocfg_mock, unused);

    gpios_mock[0x330].ExpectRead(0xFFFF).ExpectWrite(0xFFF8); // 3 GPIO_MODE4 bits for GPIO mode.
    gpios_mock[0x000].ExpectRead(0xFFFF).ExpectWrite(0x7FFF); // GPIO_DIR1 bit 15 to input (0).
    iocfg_mock[0x560].ExpectRead(0x0000).ExpectWrite(0x0001); // PUPD_CTRL6 bits 0-1 pull 10K (1).
    iocfg_mock[0x560].ExpectRead(0xFFFF).ExpectWrite(0xFFFB); // PUPD_CTRL6 bit 2 to pull up (0).
    EXPECT_EQ(gpio.GpioImplConfigIn(15, GPIO_PULL_UP), ZX_OK);
    gpios_mock.VerifyAll();
    iocfg_mock.VerifyAll();

    END_TEST;
}

bool TestPullDownIoCfg() {
    BEGIN_TEST;

    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegCount], kGpioRegCount);
    fbl::Array<ddk_mock::MockMmioReg> iocfg_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kIoCfgRegCount], kIoCfgRegCount);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint16_t), kGpioRegCount);
    ddk_mock::MockMmioRegRegion iocfg_mock(iocfg_regs.get(), sizeof(uint16_t), kIoCfgRegCount);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint16_t), 1); // Fake non-zero size.
    Mt8167GpioDeviceTest gpio(gpios_mock, iocfg_mock, unused);

    gpios_mock[0x330].ExpectRead(0xFFFF).ExpectWrite(0xFFF8); // 3 GPIO_MODE4 bits for GPIO mode.
    gpios_mock[0x000].ExpectRead(0xFFFF).ExpectWrite(0x7FFF); // GPIO_DIR1 bit 15 to input (0).
    iocfg_mock[0x560].ExpectRead(0x0000).ExpectWrite(0x0001); // PUPD_CTRL6 bits 0-1 pull 10K (1).
    iocfg_mock[0x560].ExpectRead(0x0000).ExpectWrite(0x0004); // PUPD_CTRL6 bit 2 to pull down (1).
    EXPECT_EQ(gpio.GpioImplConfigIn(15, GPIO_PULL_DOWN), ZX_OK);
    gpios_mock.VerifyAll();
    iocfg_mock.VerifyAll();

    END_TEST;
}
} // namespace gpio

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : 1;
}

BEGIN_TEST_CASE(MtkGpioTests)
RUN_TEST_SMALL(gpio::TestNoPull0)
RUN_TEST_SMALL(gpio::TestNoPullMid)
RUN_TEST_SMALL(gpio::TestNoPullHigh)
RUN_TEST_SMALL(gpio::TestOutOfRange)
RUN_TEST_SMALL(gpio::TestPullUp)
RUN_TEST_SMALL(gpio::TestPullDown)
RUN_TEST_SMALL(gpio::TestNoPullIoCfg)
RUN_TEST_SMALL(gpio::TestPullUpIoCfg)
RUN_TEST_SMALL(gpio::TestPullDownIoCfg)
END_TEST_CASE(MtkGpioTests)
