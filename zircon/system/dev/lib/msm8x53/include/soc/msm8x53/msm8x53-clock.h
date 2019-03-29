// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

namespace msm8x53 {

// Branch clock control register.
class CBCR : public hwreg::RegisterBase<CBCR, uint32_t> {
public:
    DEF_BIT(0, enable);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<CBCR>(offset); }
};

// Branch clock reset register.
class BCR : public hwreg::RegisterBase<BCR, uint32_t> {
public:
    DEF_BIT(0, reset);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<BCR>(offset); }
};

// Root clock gating command register.
class RCG_CMD : public hwreg::RegisterBase<RCG_CMD, uint32_t> {
public:
    DEF_BIT(0, update);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_CMD>(offset); }
};

// Root clock gating config register.
class RCG_CFG: public hwreg::RegisterBase<RCG_CFG, uint32_t> {
public:
    DEF_FIELD(12, 11, mode);
    DEF_FIELD(8, 6, src_sel);
    DEF_FIELD(4, 0, src_div);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_CFG>(offset); }
};

// Root clock gating M-prescalar.
class RCG_M : public hwreg::RegisterBase<RCG_M, uint32_t> {
public:
    DEF_FIELD(31, 0, m);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_M>(offset); }
};

// Root clock gating N-prescalar.
class RCG_N : public hwreg::RegisterBase<RCG_N, uint32_t> {
public:
    DEF_FIELD(31, 0, n);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_N>(offset); }
};

// Root clock gating D-prescalar.
class RCG_D : public hwreg::RegisterBase<RCG_D, uint32_t> {
public:
    DEF_FIELD(31, 0, d);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_D>(offset); }
};

// Clock control registers.
static constexpr uint32_t kCcBase = 0x1800000;
static constexpr uint32_t kCcSize = 0x80000;

enum class msm_clk_type : uint16_t {
    kGate,
    kBranch,
    kVoter,
};

// Create a clock ID based on a type and an index
constexpr uint32_t MsmClkId(const uint16_t index, const msm_clk_type type) {
    // Top 16 bits are the type, bottom 16 bits are the index.
    return static_cast<uint32_t>(index) | ((static_cast<uint32_t>(type)) << 16);
}

constexpr uint16_t MsmClkIndex(const uint32_t clk_id) {
    return clk_id & 0x0000ffff;
}

constexpr msm_clk_type MsmClkType(const uint32_t clk_id) {
    return static_cast<msm_clk_type>(clk_id >> 16);
}

// The following is a list of Clock IDs that can be used as parameters to
// ClockImplEnable/ClockImplDisable functions.
// Each ID refers to a distinct clock in the system.

// MSM Gate Clocks
constexpr uint32_t kQUsbRefClk = MsmClkId(0, msm_clk_type::kGate);
constexpr uint32_t kUsbSSRefClk = MsmClkId(1, msm_clk_type::kGate);
constexpr uint32_t kUsb3PipeClk = MsmClkId(2, msm_clk_type::kGate);

// MSM Branch Clocks
constexpr uint32_t kApc0DroopDetectorGpll0Clk = MsmClkId(0, msm_clk_type::kBranch);
constexpr uint32_t kApc1DroopDetectorGpll0Clk = MsmClkId(1, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Qup1I2cAppsClk = MsmClkId(2, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Qup1SpiAppsClk = MsmClkId(3, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Qup2I2cAppsClk = MsmClkId(4, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Qup2SpiAppsClk = MsmClkId(5, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Qup3I2cAppsClk = MsmClkId(6, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Qup3SpiAppsClk = MsmClkId(7, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Qup4I2cAppsClk = MsmClkId(8, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Qup4SpiAppsClk = MsmClkId(9, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Uart1AppsClk = MsmClkId(10, msm_clk_type::kBranch);
constexpr uint32_t kBlsp1Uart2AppsClk = MsmClkId(11, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Qup1I2cAppsClk = MsmClkId(12, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Qup1SpiAppsClk = MsmClkId(13, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Qup2I2cAppsClk = MsmClkId(14, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Qup2SpiAppsClk = MsmClkId(15, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Qup3I2cAppsClk = MsmClkId(16, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Qup3SpiAppsClk = MsmClkId(17, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Qup4I2cAppsClk = MsmClkId(18, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Qup4SpiAppsClk = MsmClkId(19, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Uart1AppsClk = MsmClkId(20, msm_clk_type::kBranch);
constexpr uint32_t kBlsp2Uart2AppsClk = MsmClkId(21, msm_clk_type::kBranch);
constexpr uint32_t kBimcGpuClk = MsmClkId(22, msm_clk_type::kBranch);
constexpr uint32_t kCamssCciAhbClk = MsmClkId(23, msm_clk_type::kBranch);
constexpr uint32_t kCamssCciClk = MsmClkId(24, msm_clk_type::kBranch);
constexpr uint32_t kCamssCppAhbClk = MsmClkId(25, msm_clk_type::kBranch);
constexpr uint32_t kCamssCppAxiClk = MsmClkId(26, msm_clk_type::kBranch);
constexpr uint32_t kCamssCppClk = MsmClkId(27, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi0AhbClk = MsmClkId(28, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi0Clk = MsmClkId(29, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi0Csiphy3pClk = MsmClkId(30, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi0phyClk = MsmClkId(31, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi0pixClk = MsmClkId(32, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi0rdiClk = MsmClkId(33, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi1AhbClk = MsmClkId(34, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi1Clk = MsmClkId(35, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi1Csiphy3pClk = MsmClkId(36, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi1phyClk = MsmClkId(37, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi1pixClk = MsmClkId(38, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi1rdiClk = MsmClkId(39, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi2AhbClk = MsmClkId(40, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi2Clk = MsmClkId(41, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi2Csiphy3pClk = MsmClkId(42, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi2phyClk = MsmClkId(43, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi2pixClk = MsmClkId(44, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi2rdiClk = MsmClkId(45, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsiVfe0Clk = MsmClkId(46, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsiVfe1Clk = MsmClkId(47, msm_clk_type::kBranch);
constexpr uint32_t kCamssGp0Clk = MsmClkId(48, msm_clk_type::kBranch);
constexpr uint32_t kCamssGp1Clk = MsmClkId(49, msm_clk_type::kBranch);
constexpr uint32_t kCamssIspifAhbClk = MsmClkId(50, msm_clk_type::kBranch);
constexpr uint32_t kCamssJpeg0Clk = MsmClkId(51, msm_clk_type::kBranch);
constexpr uint32_t kCamssJpegAhbClk = MsmClkId(52, msm_clk_type::kBranch);
constexpr uint32_t kCamssJpegAxiClk = MsmClkId(53, msm_clk_type::kBranch);
constexpr uint32_t kCamssMclk0Clk = MsmClkId(54, msm_clk_type::kBranch);
constexpr uint32_t kCamssMclk1Clk = MsmClkId(55, msm_clk_type::kBranch);
constexpr uint32_t kCamssMclk2Clk = MsmClkId(56, msm_clk_type::kBranch);
constexpr uint32_t kCamssMclk3Clk = MsmClkId(57, msm_clk_type::kBranch);
constexpr uint32_t kCamssMicroAhbClk = MsmClkId(58, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi0phytimerClk = MsmClkId(59, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi1phytimerClk = MsmClkId(60, msm_clk_type::kBranch);
constexpr uint32_t kCamssCsi2phytimerClk = MsmClkId(61, msm_clk_type::kBranch);
constexpr uint32_t kCamssAhbClk = MsmClkId(62, msm_clk_type::kBranch);
constexpr uint32_t kCamssTopAhbClk = MsmClkId(63, msm_clk_type::kBranch);
constexpr uint32_t kCamssVfe0Clk = MsmClkId(64, msm_clk_type::kBranch);
constexpr uint32_t kCamssVfeAhbClk = MsmClkId(65, msm_clk_type::kBranch);
constexpr uint32_t kCamssVfeAxiClk = MsmClkId(66, msm_clk_type::kBranch);
constexpr uint32_t kCamssVfe1AhbClk = MsmClkId(67, msm_clk_type::kBranch);
constexpr uint32_t kCamssVfe1AxiClk = MsmClkId(68, msm_clk_type::kBranch);
constexpr uint32_t kCamssVfe1Clk = MsmClkId(69, msm_clk_type::kBranch);
constexpr uint32_t kDccClk = MsmClkId(70, msm_clk_type::kBranch);
constexpr uint32_t kGp1Clk = MsmClkId(71, msm_clk_type::kBranch);
constexpr uint32_t kGp2Clk = MsmClkId(72, msm_clk_type::kBranch);
constexpr uint32_t kGp3Clk = MsmClkId(73, msm_clk_type::kBranch);
constexpr uint32_t kMdssAhbClk = MsmClkId(74, msm_clk_type::kBranch);
constexpr uint32_t kMdssAxiClk = MsmClkId(75, msm_clk_type::kBranch);
constexpr uint32_t kMdssByte0Clk = MsmClkId(76, msm_clk_type::kBranch);
constexpr uint32_t kMdssByte1Clk = MsmClkId(77, msm_clk_type::kBranch);
constexpr uint32_t kMdssEsc0Clk = MsmClkId(78, msm_clk_type::kBranch);
constexpr uint32_t kMdssEsc1Clk = MsmClkId(79, msm_clk_type::kBranch);
constexpr uint32_t kMdssMdpClk = MsmClkId(80, msm_clk_type::kBranch);
constexpr uint32_t kMdssPclk0Clk = MsmClkId(81, msm_clk_type::kBranch);
constexpr uint32_t kMdssPclk1Clk = MsmClkId(82, msm_clk_type::kBranch);
constexpr uint32_t kMdssVsyncClk = MsmClkId(83, msm_clk_type::kBranch);
constexpr uint32_t kMssCfgAhbClk = MsmClkId(84, msm_clk_type::kBranch);
constexpr uint32_t kMssQ6BimcAxiClk = MsmClkId(85, msm_clk_type::kBranch);
constexpr uint32_t kBimcGfxClk = MsmClkId(86, msm_clk_type::kBranch);
constexpr uint32_t kOxiliAhbClk = MsmClkId(87, msm_clk_type::kBranch);
constexpr uint32_t kOxiliAonClk = MsmClkId(88, msm_clk_type::kBranch);
constexpr uint32_t kOxiliGfx3dClk = MsmClkId(89, msm_clk_type::kBranch);
constexpr uint32_t kOxiliTimerClk = MsmClkId(90, msm_clk_type::kBranch);
constexpr uint32_t kPcnocUsb3AxiClk = MsmClkId(91, msm_clk_type::kBranch);
constexpr uint32_t kPdm2Clk = MsmClkId(92, msm_clk_type::kBranch);
constexpr uint32_t kPdmAhbClk = MsmClkId(93, msm_clk_type::kBranch);
constexpr uint32_t kRbcprGfxClk = MsmClkId(94, msm_clk_type::kBranch);
constexpr uint32_t kSdcc1AhbClk = MsmClkId(95, msm_clk_type::kBranch);
constexpr uint32_t kSdcc1AppsClk = MsmClkId(96, msm_clk_type::kBranch);
constexpr uint32_t kSdcc1IceCoreClk = MsmClkId(97, msm_clk_type::kBranch);
constexpr uint32_t kSdcc2AhbClk = MsmClkId(98, msm_clk_type::kBranch);
constexpr uint32_t kSdcc2AppsClk = MsmClkId(99, msm_clk_type::kBranch);
constexpr uint32_t kUsb30MasterClk = MsmClkId(100, msm_clk_type::kBranch);
constexpr uint32_t kUsb30MockUtmiClk = MsmClkId(101, msm_clk_type::kBranch);
constexpr uint32_t kUsb30SleepClk = MsmClkId(102, msm_clk_type::kBranch);
constexpr uint32_t kUsb3AuxClk = MsmClkId(103, msm_clk_type::kBranch);
constexpr uint32_t kUsbPhyCfgAhbClk = MsmClkId(104, msm_clk_type::kBranch);
constexpr uint32_t kVenus0AhbClk = MsmClkId(105, msm_clk_type::kBranch);
constexpr uint32_t kVenus0AxiClk = MsmClkId(106, msm_clk_type::kBranch);
constexpr uint32_t kVenus0Core0Vcodec0Clk = MsmClkId(107, msm_clk_type::kBranch);
constexpr uint32_t kVenus0Vcodec0Clk = MsmClkId(108, msm_clk_type::kBranch);

// MSM Local Voter Clocks
constexpr uint32_t kApssAhbClk = MsmClkId(0, msm_clk_type::kVoter);
constexpr uint32_t kApssAxiClk = MsmClkId(1, msm_clk_type::kVoter);
constexpr uint32_t kBlsp1AhbClk = MsmClkId(2, msm_clk_type::kVoter);
constexpr uint32_t kBlsp2AhbClk = MsmClkId(3, msm_clk_type::kVoter);
constexpr uint32_t kBootRomAhbClk = MsmClkId(4, msm_clk_type::kVoter);
constexpr uint32_t kCryptoAhbClk = MsmClkId(5, msm_clk_type::kVoter);
constexpr uint32_t kCryptoAxiClk = MsmClkId(6, msm_clk_type::kVoter);
constexpr uint32_t kCryptoClk = MsmClkId(7, msm_clk_type::kVoter);
constexpr uint32_t kQdssDapClk = MsmClkId(8, msm_clk_type::kVoter);
constexpr uint32_t kPrngAhbClk = MsmClkId(9, msm_clk_type::kVoter);
constexpr uint32_t kApssTcuAsyncClk = MsmClkId(10, msm_clk_type::kVoter);
constexpr uint32_t kCppTbuClk = MsmClkId(11, msm_clk_type::kVoter);
constexpr uint32_t kJpegTbuClk = MsmClkId(12, msm_clk_type::kVoter);
constexpr uint32_t kMdpTbuClk = MsmClkId(13, msm_clk_type::kVoter);
constexpr uint32_t kSmmuCfgClk = MsmClkId(14, msm_clk_type::kVoter);
constexpr uint32_t kVenusTbuClk = MsmClkId(15, msm_clk_type::kVoter);
constexpr uint32_t kVfe1TbuClk = MsmClkId(16, msm_clk_type::kVoter);
constexpr uint32_t kVfeTbuClk = MsmClkId(17, msm_clk_type::kVoter);

} // namespace msm8x53
