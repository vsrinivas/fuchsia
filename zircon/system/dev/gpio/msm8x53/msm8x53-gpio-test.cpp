// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53-gpio.h"

#include <fbl/auto_call.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/msm8x53/msm8x53-gpio.h>

namespace {
constexpr size_t kGpioRegSize = kMsm9x53GpioSize / sizeof(uint32_t); // in 32 bits chunks.
} // namespace

namespace gpio {

class Msm8x53GpioDeviceTest : public Msm8x53GpioDevice {
public:
    explicit Msm8x53GpioDeviceTest(ddk_mock::MockMmioRegRegion& gpio_regs)
        : Msm8x53GpioDevice(nullptr, gpio_regs.GetMmioBuffer()) {
    }
};

TEST(GpioTest, NoPull0) {
    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint32_t), kGpioRegSize);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
    Msm8x53GpioDeviceTest gpio(gpios_mock);

    gpios_mock[0x00000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3); // 4 GpioCfg bits for mode.
    gpios_mock[0x00000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF); // 1 GpioCfg bits for OE.
    gpios_mock[0x00000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFC); // bits 0 to disable pull.
    EXPECT_OK(gpio.GpioImplConfigIn(0, GPIO_NO_PULL));
    gpios_mock.VerifyAll();
}

TEST(GpioTest, NoPullMid) {
    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint32_t), kGpioRegSize);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
    Msm8x53GpioDeviceTest gpio(gpios_mock);

    gpios_mock[0x43000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3); // 4 GpioCfg bits for mode.
    gpios_mock[0x43000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF); // 1 GpioCfg bits for OE.
    gpios_mock[0x43000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFC); // bits 0 to disable pull.
    EXPECT_OK(gpio.GpioImplConfigIn(0x43, GPIO_NO_PULL));
    gpios_mock.VerifyAll();
}

TEST(GpioTest, NoPullHigh) {
    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint32_t), kGpioRegSize);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
    Msm8x53GpioDeviceTest gpio(gpios_mock);

    gpios_mock[0x7C000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3); // 4 GpioCfg bits for mode.
    gpios_mock[0x7C000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF); // 1 GpioCfg bits for OE.
    gpios_mock[0x7C000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFC); // bits 0 to disable pull.
    EXPECT_OK(gpio.GpioImplConfigIn(0x7C, GPIO_NO_PULL));
    gpios_mock.VerifyAll();
}

TEST(GpioTest, OutOfRange) {
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
    Msm8x53GpioDeviceTest gpio(unused);

    EXPECT_EQ(gpio.GpioImplConfigIn(kGpioRegSize, GPIO_NO_PULL), ZX_ERR_INVALID_ARGS);
}

TEST(GpioTest, PullUp) {
    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint32_t), kGpioRegSize);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
    Msm8x53GpioDeviceTest gpio(gpios_mock);

    gpios_mock[0x21000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3); // 4 GpioCfg bits for mode.
    gpios_mock[0x21000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF); // 1 GpioCfg bits for OE.
    gpios_mock[0x21000].ExpectRead(0x00000000).ExpectWrite(0x00000003); // bits to enable pull up.
    EXPECT_OK(gpio.GpioImplConfigIn(0x21, GPIO_PULL_UP));
    gpios_mock.VerifyAll();
}

TEST(GpioTest, PullDown) {
    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint32_t), kGpioRegSize);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
    Msm8x53GpioDeviceTest gpio(gpios_mock);

    gpios_mock[0x20000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3); // 4 GpioCfg bits for mode.
    gpios_mock[0x20000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFDFF); // 1 GpioCfg bits for OE.
    gpios_mock[0x20000].ExpectRead(0x00000000).ExpectWrite(0x00000001); // bits to enable pull down.
    EXPECT_OK(gpio.GpioImplConfigIn(0x20, GPIO_PULL_DOWN));
    gpios_mock.VerifyAll();
}

TEST(GpioTest, Out) {
    fbl::Array<ddk_mock::MockMmioReg> gpio_regs =
        fbl::Array(new ddk_mock::MockMmioReg[kGpioRegSize], kGpioRegSize);
    ddk_mock::MockMmioRegRegion gpios_mock(gpio_regs.get(), sizeof(uint32_t), kGpioRegSize);
    ddk_mock::MockMmioRegRegion unused(nullptr, sizeof(uint32_t), kGpioRegSize);
    Msm8x53GpioDeviceTest gpio(gpios_mock);

    gpios_mock[0x19000].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFC3); // 4 GpioCfg bits for mode.
    gpios_mock[0x19000].ExpectRead(0x00000000).ExpectWrite(0x00000200); // 1 GpioCfg bits for OE.
    gpios_mock[0x19008].ExpectRead(0x00000000).ExpectWrite(0x00000002); // write 1 to out.
    EXPECT_OK(gpio.GpioImplConfigOut(0x19, 1));
    gpios_mock.VerifyAll();
}

} // namespace gpio

int main(int argc, char** argv) {
    return RUN_ALL_TESTS(argc, argv);
}
