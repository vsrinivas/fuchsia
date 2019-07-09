// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syn-clk.h"

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/as370/as370-hw.h>
#include <soc/as370/as370-clk.h>

namespace clk {

class SynClkTest : public SynClk {
public:
    SynClkTest(ddk_mock::MockMmioRegRegion& global_mmio, ddk_mock::MockMmioRegRegion& audio_mmio)
        : SynClk(nullptr, ddk::MmioBuffer(global_mmio.GetMmioBuffer()),
                 ddk::MmioBuffer(audio_mmio.GetMmioBuffer())) {}
};

TEST(ClkSynTest, AvpllClkEnable) {
    auto global_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kGlobalSize/4);
    auto audio_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioGlobalSize/4);
    ddk_mock::MockMmioRegRegion global_region(global_regs.get(), 4, as370::kGlobalSize/4);
    ddk_mock::MockMmioRegRegion audio_region(audio_regs.get(), 4, as370::kAudioGlobalSize/4);
    SynClkTest test(global_region, audio_region);

    global_regs[0x0530/4].ExpectRead(0x00000000).ExpectWrite(0x00000001); // Enable AVIO clock.
    global_regs[0x0088/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffe); // Not sysPll power down.
    audio_regs[0x0044/4].ExpectRead(0x00000000).ExpectWrite(0x00000004); // Enable AVPLL.
    audio_regs[0x0000/4].ExpectRead(0x00000000).ExpectWrite(0x00000020); // Enable AVPLL Clock.

    EXPECT_OK(test.ClockImplEnable(0));

    global_region.VerifyAll();
    audio_region.VerifyAll();
}

TEST(ClkSynTest, AvpllClkDisable) {
    auto global_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kGlobalSize/4);
    auto audio_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioGlobalSize/4);
    ddk_mock::MockMmioRegRegion global_region(global_regs.get(), 4, as370::kGlobalSize/4);
    ddk_mock::MockMmioRegRegion audio_region(audio_regs.get(), 4, as370::kAudioGlobalSize/4);
    SynClkTest test(global_region, audio_region);

    audio_regs[0x0044/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffb); // Disable AVPLL.
    audio_regs[0x0000/4].ExpectRead(0xffffffff).ExpectWrite(0xffffffdf); // Disable AVPLL Clock.

    EXPECT_OK(test.ClockImplDisable(0));

    global_region.VerifyAll();
    audio_region.VerifyAll();
}

TEST(ClkSynTest, AvpllClkDisablePll1) {
    auto global_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kGlobalSize/4);
    auto audio_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioGlobalSize/4);
    ddk_mock::MockMmioRegRegion global_region(global_regs.get(), 4, as370::kGlobalSize/4);
    ddk_mock::MockMmioRegRegion audio_region(audio_regs.get(), 4, as370::kAudioGlobalSize/4);
    SynClkTest test(global_region, audio_region);

    audio_regs[0x0044/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffff7); // Disable AVPLL 1.
    audio_regs[0x0020/4].ExpectRead(0xffffffff).ExpectWrite(0xffffffdf); // Disable AVPLL Clock.

    EXPECT_OK(test.ClockImplDisable(1));

    global_region.VerifyAll();
    audio_region.VerifyAll();
}

TEST(ClkSynTest, AvpllSetRateBad) {
    auto global_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kGlobalSize/4);
    auto audio_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioGlobalSize/4);
    ddk_mock::MockMmioRegRegion global_region(global_regs.get(), 4, as370::kGlobalSize/4);
    ddk_mock::MockMmioRegRegion audio_region(audio_regs.get(), 4, as370::kAudioGlobalSize/4);
    SynClkTest test(global_region, audio_region);

    EXPECT_NOT_OK(test.ClockImplSetRate(0, 3'200'000'001)); // Too high.
}

TEST(ClkSynTest, AvpllSetRateGood) {
    auto global_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kGlobalSize/4);
    auto audio_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioGlobalSize/4);
    ddk_mock::MockMmioRegRegion global_region(global_regs.get(), 4, as370::kGlobalSize/4);
    ddk_mock::MockMmioRegRegion audio_region(audio_regs.get(), 4, as370::kAudioGlobalSize/4);
    SynClkTest test(global_region, audio_region);

    audio_regs[0x0044/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffb); // Clock disable.
    audio_regs[0x0018/4].ExpectRead(0x00000000).ExpectWrite(0x00000001); // Bypass.
    audio_regs[0x0014/4].ExpectRead(0x00000000).ExpectWrite(0x01000000); // Power down DP.

    // 800 MHz = 25 MHz * 32 so dn = 32 and dm = 1.
    audio_regs[0x0008/4].ExpectRead(0x00000000).ExpectWrite(0x00002004); // dn 32 dm 1.
    audio_regs[0x0014/4].ExpectRead(0x00000000).ExpectWrite(0x02000000); // dp 1.

    audio_regs[0x0014/4].ExpectRead(0xffffffff).ExpectWrite(0xfeffffff); // Power up DP.
    audio_regs[0x0018/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffe); // Remove bypass.
    audio_regs[0x0044/4].ExpectRead(0x00000000).ExpectWrite(0x00000004); // Clock enable.

    EXPECT_OK(test.ClockImplSetRate(0, 800'000'000));

    audio_region.VerifyAll();
}

TEST(ClkSynTest, AvpllSetRateMax) {
    auto global_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kGlobalSize/4);
    auto audio_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioGlobalSize/4);
    ddk_mock::MockMmioRegRegion global_region(global_regs.get(), 4, as370::kGlobalSize/4);
    ddk_mock::MockMmioRegRegion audio_region(audio_regs.get(), 4, as370::kAudioGlobalSize/4);
    SynClkTest test(global_region, audio_region);

    audio_regs[0x0044/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffb); // Clock disable.
    audio_regs[0x0018/4].ExpectRead(0x00000000).ExpectWrite(0x00000001); // Bypass.
    audio_regs[0x0014/4].ExpectRead(0x00000000).ExpectWrite(0x01000000); // Power down DP.

    // 3200 MHz.
    audio_regs[0x0008/4].ExpectRead(0x00000000).ExpectWrite(0x00008004); // dn, dm.
    audio_regs[0x0014/4].ExpectRead(0x00000000).ExpectWrite(0x02000000); // dp.

    audio_regs[0x0014/4].ExpectRead(0xffffffff).ExpectWrite(0xfeffffff); // Power up DP.
    audio_regs[0x0018/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffe); // Remove bypass.
    audio_regs[0x0044/4].ExpectRead(0x00000000).ExpectWrite(0x00000004); // Clock enable.

    EXPECT_OK(test.ClockImplSetRate(0, 3'200'000'000));

    audio_region.VerifyAll();
}

TEST(ClkSynTest, AvpllSetRateFractional) {
    auto global_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kGlobalSize/4);
    auto audio_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioGlobalSize/4);
    ddk_mock::MockMmioRegRegion global_region(global_regs.get(), 4, as370::kGlobalSize/4);
    ddk_mock::MockMmioRegRegion audio_region(audio_regs.get(), 4, as370::kAudioGlobalSize/4);
    SynClkTest test(global_region, audio_region);

    audio_regs[0x0044/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffb); // Clock disable.
    audio_regs[0x0018/4].ExpectRead(0x00000000).ExpectWrite(0x00000001); // Bypass.
    audio_regs[0x0014/4].ExpectRead(0x00000000).ExpectWrite(0x01000000); // Power down DP.

    audio_regs[0x0008/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffd); // Reset.
    audio_regs[0x000c/4].ExpectRead(0x00000000).ExpectWrite(0x005c28f6); // Fractional.
    audio_regs[0x0008/4].ExpectRead(0x00000000).ExpectWrite(0x00005404); // dn, dm.
    audio_regs[0x0014/4].ExpectRead(0x00000000).ExpectWrite(0x02000000); // dp.
    audio_regs[0x0008/4].ExpectRead(0x00000000).ExpectWrite(0x00000002); // Not reset.

    audio_regs[0x0014/4].ExpectRead(0xffffffff).ExpectWrite(0xfeffffff); // Power up DP.
    audio_regs[0x0018/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffe); // Remove bypass.
    audio_regs[0x0044/4].ExpectRead(0x00000000).ExpectWrite(0x00000004); // Clock enable.

    EXPECT_OK(test.ClockImplSetRate(0, 2'123'000'000));

    audio_region.VerifyAll();
}

TEST(ClkSynTest, AvpllSetRatePll1) {
    auto global_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kGlobalSize/4);
    auto audio_regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioGlobalSize/4);
    ddk_mock::MockMmioRegRegion global_region(global_regs.get(), 4, as370::kGlobalSize/4);
    ddk_mock::MockMmioRegRegion audio_region(audio_regs.get(), 4, as370::kAudioGlobalSize/4);
    SynClkTest test(global_region, audio_region);

    audio_regs[0x0044/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffff7); // Clock disable.
    audio_regs[0x0038/4].ExpectRead(0x00000000).ExpectWrite(0x00000001); // Bypass.
    audio_regs[0x0034/4].ExpectRead(0x00000000).ExpectWrite(0x01000000); // Power down DP.

    // 800 MHz = 25 MHz * 32 so dn = 32 and dm = 1.
    audio_regs[0x0028/4].ExpectRead(0x00000000).ExpectWrite(0x00002004); // dn 32 dm 1.
    audio_regs[0x0034/4].ExpectRead(0x00000000).ExpectWrite(0x02000000); // dp 1.

    audio_regs[0x0034/4].ExpectRead(0xffffffff).ExpectWrite(0xfeffffff); // Power up DP.
    audio_regs[0x0038/4].ExpectRead(0xffffffff).ExpectWrite(0xfffffffe); // Remove bypass.
    audio_regs[0x0044/4].ExpectRead(0x00000000).ExpectWrite(0x00000008); // Clock enable.

    EXPECT_OK(test.ClockImplSetRate(1, 800'000'000));

    audio_region.VerifyAll();
}

} // namespace clk
