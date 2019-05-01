// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>
#include "mt-dsi-host.h"
#include "lcd.h"
#include "mt-sysconfig.h"
#include <ddktl/protocol/platform/device.h>
#include <ddktl/pdev.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
namespace mt8167s_display {

namespace {
constexpr uint32_t kDsiHostRegNum = 132;
constexpr uint32_t kSyscfgRegNum = 336;
constexpr uint32_t kMutexRegNum = 48;
}

/********************************/
/* LCD Unit Tests */
/********************************/

TEST(LcdTest, PowerOn) {
    // get the dsi and gpio protocols
    ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
    ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
    Lcd lcd(&dsi, &gpio, 0);
    lcd.PowerOn();
}

TEST(LcdTest, PowerOff) {
    ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
    ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
    Lcd lcd(&dsi, &gpio, 0);
    lcd.PowerOff();
}

TEST(DsiHostTest, IsDsiHostOn) {
    pdev_protocol_t pdev = {};
    MtDsiHost dsi_host(&pdev, 0, 0, 0);

    ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
    ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
    ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
    fbl::AllocChecker ac;
    fbl::unique_ptr<Lcd> lcd = fbl::make_unique_checked<mt8167s_display::Lcd>(&ac,
                                                                              &dsi,
                                                                              &gpio,
                                                                              uint8_t(0));
    EXPECT_TRUE(ac.check());
    ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
    ddk_mock::MockMmioRegRegion mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> mmio;
    mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());
    // This will simulate the HOST being ON
    mmio->Write32(0x1, 0x50);
    EXPECT_OK(dsi_host.Init(std::move(mmio), std::move(lcd), &dsi, &gpio, &power));
    EXPECT_TRUE(dsi_host.IsHostOn());
}

TEST(DsiHostTest, IsDsiHostOff) {
    pdev_protocol_t pdev = {};
    MtDsiHost dsi_host(&pdev, 0, 0, 0);
    ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
    ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
    ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
    fbl::AllocChecker ac;
    fbl::unique_ptr<Lcd> lcd = fbl::make_unique_checked<mt8167s_display::Lcd>(&ac,
                                                                              &dsi,
                                                                              &gpio,
                                                                              uint8_t(0));
    EXPECT_TRUE(ac.check());
    ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
    ddk_mock::MockMmioRegRegion mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> mmio;
    mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());
    // This will simulate the HOST being OFF
    mmio->Write32(0x0, 0x50);
    EXPECT_OK(dsi_host.Init(std::move(mmio), std::move(lcd), &dsi, &gpio, &power));
    EXPECT_FALSE(dsi_host.IsHostOn());
}

/* The following test will simulate DSI Host Shutdown if the DSI IP is already off */
TEST(DsiHostTest, DsiHostShutdown_OFF) {
    pdev_protocol_t pdev = {};
    MtDsiHost dsi_host(&pdev, 0, 0, 0);

    ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
    ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
    ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
    fbl::AllocChecker ac;
    fbl::unique_ptr<Lcd> lcd = fbl::make_unique_checked<mt8167s_display::Lcd>(&ac,
                                                                              &dsi,
                                                                              &gpio,
                                                                              uint8_t(0));
    EXPECT_TRUE(ac.check());
    ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
    ddk_mock::MockMmioRegRegion dsi_mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> dsi_mmio;
    dsi_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, dsi_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    // This will simulate the HOST being OFF
    dsi_mmio->Write32(0x0, 0x50);
    EXPECT_OK(dsi_host.Init(std::move(dsi_mmio), std::move(lcd), &dsi, &gpio, &power));

    ddk_mock::MockMmioReg syscfg_reg_array[kSyscfgRegNum];
    ddk_mock::MockMmioRegRegion syscfg_mock_regs(syscfg_reg_array, sizeof(uint32_t), kSyscfgRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> syscfg_mmio;
    syscfg_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, syscfg_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    ddk_mock::MockMmioReg mutex_reg_array[kMutexRegNum];
    ddk_mock::MockMmioRegRegion mutex_mock_regs(mutex_reg_array, sizeof(uint32_t), kMutexRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> mutex_mmio;
    mutex_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mutex_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    fbl::unique_ptr<MtSysConfig> syscfg;
    syscfg = fbl::make_unique_checked<MtSysConfig>(&ac);
    EXPECT_TRUE(ac.check());
    EXPECT_OK(syscfg->Init(std::move(syscfg_mmio), std::move(mutex_mmio)));
    EXPECT_OK(dsi_host.Shutdown(syscfg));
}

/* The following test will simulate DSI Host Shutdown if the DSI IP is already ON */
TEST(DsiHostTest, DsiHostShutdown_ON) {
    pdev_protocol_t pdev = {};
    MtDsiHost dsi_host(&pdev, 0, 0, 0);

    ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
    ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
    ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
    fbl::AllocChecker ac;
    fbl::unique_ptr<Lcd> lcd = fbl::make_unique_checked<mt8167s_display::Lcd>(&ac,
                                                                              &dsi,
                                                                              &gpio,
                                                                              uint8_t(0));
    EXPECT_TRUE(ac.check());

    ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
    ddk_mock::MockMmioRegRegion dsi_mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> dsi_mmio;
    dsi_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, dsi_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    // This will simulate the HOST being OFF
    dsi_mmio->Write32(0x1, 0x50);
    EXPECT_OK(dsi_host.Init(std::move(dsi_mmio), std::move(lcd), &dsi, &gpio, &power));

    ddk_mock::MockMmioReg syscfg_reg_array[kSyscfgRegNum];
    ddk_mock::MockMmioRegRegion syscfg_mock_regs(syscfg_reg_array, sizeof(uint32_t), kSyscfgRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> syscfg_mmio;
    syscfg_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, syscfg_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    ddk_mock::MockMmioReg mutex_reg_array[kMutexRegNum];
    ddk_mock::MockMmioRegRegion mutex_mock_regs(mutex_reg_array, sizeof(uint32_t), kMutexRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> mutex_mmio;
    mutex_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mutex_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    fbl::unique_ptr<MtSysConfig> syscfg;
    syscfg = fbl::make_unique_checked<MtSysConfig>(&ac);
    EXPECT_TRUE(ac.check());
    EXPECT_OK(syscfg->Init(std::move(syscfg_mmio), std::move(mutex_mmio)));
    EXPECT_OK(dsi_host.Shutdown(syscfg));
}

/* The following test will simulate DSI Host Shutdown if the DSI IP is already off */
TEST(DsiHostTest, DsiHostPowerOn) {
    pdev_protocol_t pdev = {};
    MtDsiHost dsi_host(&pdev, 0, 0, 0);

    ddk::GpioProtocolClient gpio = ddk::GpioProtocolClient();
    ddk::DsiImplProtocolClient dsi = ddk::DsiImplProtocolClient();
    ddk::PowerProtocolClient power = ddk::PowerProtocolClient();
    fbl::AllocChecker ac;
    fbl::unique_ptr<Lcd> lcd = fbl::make_unique_checked<mt8167s_display::Lcd>(&ac,
                                                                              &dsi,
                                                                              &gpio,
                                                                              uint8_t(0));
    EXPECT_TRUE(ac.check());

    ddk_mock::MockMmioReg dsi_reg_array[kDsiHostRegNum];
    ddk_mock::MockMmioRegRegion dsi_mock_regs(dsi_reg_array, sizeof(uint32_t), kDsiHostRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> dsi_mmio;
    dsi_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, dsi_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    // This will simulate the HOST being OFF
    dsi_mmio->Write32(0x0, 0x50);
    EXPECT_OK(dsi_host.Init(std::move(dsi_mmio), std::move(lcd), &dsi, &gpio, &power));

    ddk_mock::MockMmioReg syscfg_reg_array[kSyscfgRegNum];
    ddk_mock::MockMmioRegRegion syscfg_mock_regs(syscfg_reg_array, sizeof(uint32_t), kSyscfgRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> syscfg_mmio;
    syscfg_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, syscfg_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    ddk_mock::MockMmioReg mutex_reg_array[kMutexRegNum];
    ddk_mock::MockMmioRegRegion mutex_mock_regs(mutex_reg_array, sizeof(uint32_t), kMutexRegNum);
    fbl::unique_ptr<ddk::MmioBuffer> mutex_mmio;
    mutex_mmio = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mutex_mock_regs.GetMmioBuffer());
    EXPECT_TRUE(ac.check());

    fbl::unique_ptr<MtSysConfig> syscfg;
    syscfg = fbl::make_unique_checked<MtSysConfig>(&ac);
    EXPECT_TRUE(ac.check());
    EXPECT_OK(syscfg->Init(std::move(syscfg_mmio), std::move(mutex_mmio)));
    EXPECT_OK(dsi_host.PowerOn(syscfg));
}

} // namespace mt8167s_display

