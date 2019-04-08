// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53-clk.h"

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clockimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/pdev.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/clock/c/fidl.h>
#include <hwreg/bitfields.h>
#include <soc/msm8x53/msm8x53-clock.h>

#include <ddktl/protocol/platform/bus.h>

#include "msm8x53-clk-regs.h"

namespace clk {

typedef struct msm_clk_gate {
    uint32_t reg;
    uint32_t bit;
    uint32_t delay_us;
} msm_clk_gate_t;

struct msm_clk_branch {
    uint32_t reg;
};

struct msm_clk_voter {
    uint32_t cbcr_reg;
    uint32_t vote_reg;
    uint32_t bit;
};

namespace {

const char kMsmClkName[] = "msm-clk";

constexpr msm_clk_gate_t kMsmClkGates[] = {
    [msm8x53::MsmClkIndex(msm8x53::kQUsbRefClk)] = {.reg = 0x41030, .bit = 0, .delay_us = 0},
    [msm8x53::MsmClkIndex(msm8x53::kUsbSSRefClk)] = {.reg = 0x5e07c, .bit = 0, .delay_us = 0},
    [msm8x53::MsmClkIndex(msm8x53::kUsb3PipeClk)] = {.reg = 0x5e040, .bit = 0, .delay_us = 50},
};

constexpr uint32_t kBranchEnable = (0x1u << 0);
constexpr struct msm_clk_branch kMsmClkBranches[] = {
    [msm8x53::MsmClkIndex(msm8x53::kApc0DroopDetectorGpll0Clk)] = {
        .reg = msm8x53::kApc0VoltageDroopDetectorGpll0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kApc1DroopDetectorGpll0Clk)] = {
        .reg = msm8x53::kApc1VoltageDroopDetectorGpll0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup1I2cAppsClk)] = {.reg = msm8x53::kBlsp1Qup1I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup1SpiAppsClk)] = {.reg = msm8x53::kBlsp1Qup1SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup2I2cAppsClk)] = {.reg = msm8x53::kBlsp1Qup2I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup2SpiAppsClk)] = {.reg = msm8x53::kBlsp1Qup2SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup3I2cAppsClk)] = {.reg = msm8x53::kBlsp1Qup3I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup3SpiAppsClk)] = {.reg = msm8x53::kBlsp1Qup3SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup4I2cAppsClk)] = {.reg = msm8x53::kBlsp1Qup4I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup4SpiAppsClk)] = {.reg = msm8x53::kBlsp1Qup4SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart1AppsClk)] = {.reg = msm8x53::kBlsp1Uart1AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart2AppsClk)] = {.reg = msm8x53::kBlsp1Uart2AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup1I2cAppsClk)] = {.reg = msm8x53::kBlsp2Qup1I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup1SpiAppsClk)] = {.reg = msm8x53::kBlsp2Qup1SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup2I2cAppsClk)] = {.reg = msm8x53::kBlsp2Qup2I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup2SpiAppsClk)] = {.reg = msm8x53::kBlsp2Qup2SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup3I2cAppsClk)] = {.reg = msm8x53::kBlsp2Qup3I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup3SpiAppsClk)] = {.reg = msm8x53::kBlsp2Qup3SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup4I2cAppsClk)] = {.reg = msm8x53::kBlsp2Qup4I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup4SpiAppsClk)] = {.reg = msm8x53::kBlsp2Qup4SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Uart1AppsClk)] = {.reg = msm8x53::kBlsp2Uart1AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Uart2AppsClk)] = {.reg = msm8x53::kBlsp2Uart2AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBimcGpuClk)] = {.reg = msm8x53::kBimcGpuCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCciAhbClk)] = {.reg = msm8x53::kCamssCciAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCciClk)] = {.reg = msm8x53::kCamssCciCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppAhbClk)] = {.reg = msm8x53::kCamssCppAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppAxiClk)] = {.reg = msm8x53::kCamssCppAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppClk)] = {.reg = msm8x53::kCamssCppCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0AhbClk)] = {.reg = msm8x53::kCamssCsi0AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0Clk)] = {.reg = msm8x53::kCamssCsi0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0Csiphy3pClk)] = {
        .reg = msm8x53::kCamssCsi0Csiphy3pCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0phyClk)] = {.reg = msm8x53::kCamssCsi0phyCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0pixClk)] = {.reg = msm8x53::kCamssCsi0pixCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0rdiClk)] = {.reg = msm8x53::kCamssCsi0rdiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1AhbClk)] = {.reg = msm8x53::kCamssCsi1AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1Clk)] = {.reg = msm8x53::kCamssCsi1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1Csiphy3pClk)] = {
        .reg = msm8x53::kCamssCsi1Csiphy3pCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1phyClk)] = {.reg = msm8x53::kCamssCsi1phyCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1pixClk)] = {.reg = msm8x53::kCamssCsi1pixCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1rdiClk)] = {.reg = msm8x53::kCamssCsi1rdiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2AhbClk)] = {.reg = msm8x53::kCamssCsi2AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2Clk)] = {.reg = msm8x53::kCamssCsi2Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2Csiphy3pClk)] = {
        .reg = msm8x53::kCamssCsi2Csiphy3pCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2phyClk)] = {.reg = msm8x53::kCamssCsi2phyCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2pixClk)] = {.reg = msm8x53::kCamssCsi2pixCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2rdiClk)] = {.reg = msm8x53::kCamssCsi2rdiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsiVfe0Clk)] = {.reg = msm8x53::kCamssCsiVfe0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsiVfe1Clk)] = {.reg = msm8x53::kCamssCsiVfe1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssGp0Clk)] = {.reg = msm8x53::kCamssGp0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssGp1Clk)] = {.reg = msm8x53::kCamssGp1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssIspifAhbClk)] = {.reg = msm8x53::kCamssIspifAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpeg0Clk)] = {.reg = msm8x53::kCamssJpeg0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpegAhbClk)] = {.reg = msm8x53::kCamssJpegAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpegAxiClk)] = {.reg = msm8x53::kCamssJpegAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk0Clk)] = {.reg = msm8x53::kCamssMclk0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk1Clk)] = {.reg = msm8x53::kCamssMclk1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk2Clk)] = {.reg = msm8x53::kCamssMclk2Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk3Clk)] = {.reg = msm8x53::kCamssMclk3Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMicroAhbClk)] = {.reg = msm8x53::kCamssMicroAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0phytimerClk)] = {
        .reg = msm8x53::kCamssCsi0phytimerCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1phytimerClk)] = {
        .reg = msm8x53::kCamssCsi1phytimerCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2phytimerClk)] = {
        .reg = msm8x53::kCamssCsi2phytimerCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssAhbClk)] = {.reg = msm8x53::kCamssAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssTopAhbClk)] = {.reg = msm8x53::kCamssTopAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe0Clk)] = {.reg = msm8x53::kCamssVfe0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfeAhbClk)] = {.reg = msm8x53::kCamssVfeAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfeAxiClk)] = {.reg = msm8x53::kCamssVfeAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1AhbClk)] = {.reg = msm8x53::kCamssVfe1AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1AxiClk)] = {.reg = msm8x53::kCamssVfe1AxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1Clk)] = {.reg = msm8x53::kCamssVfe1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kDccClk)] = {.reg = msm8x53::kDccCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kGp1Clk)] = {.reg = msm8x53::kGp1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kGp2Clk)] = {.reg = msm8x53::kGp2Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kGp3Clk)] = {.reg = msm8x53::kGp3Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssAhbClk)] = {.reg = msm8x53::kMdssAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssAxiClk)] = {.reg = msm8x53::kMdssAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssByte0Clk)] = {.reg = msm8x53::kMdssByte0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssByte1Clk)] = {.reg = msm8x53::kMdssByte1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssEsc0Clk)] = {.reg = msm8x53::kMdssEsc0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssEsc1Clk)] = {.reg = msm8x53::kMdssEsc1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssMdpClk)] = {.reg = msm8x53::kMdssMdpCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssPclk0Clk)] = {.reg = msm8x53::kMdssPclk0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssPclk1Clk)] = {.reg = msm8x53::kMdssPclk1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssVsyncClk)] = {.reg = msm8x53::kMdssVsyncCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMssCfgAhbClk)] = {.reg = msm8x53::kMssCfgAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMssQ6BimcAxiClk)] = {.reg = msm8x53::kMssQ6BimcAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBimcGfxClk)] = {.reg = msm8x53::kBimcGfxCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kOxiliAhbClk)] = {.reg = msm8x53::kOxiliAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kOxiliAonClk)] = {.reg = msm8x53::kOxiliAonCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kOxiliGfx3dClk)] = {.reg = msm8x53::kOxiliGfx3dCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kOxiliTimerClk)] = {.reg = msm8x53::kOxiliTimerCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kPcnocUsb3AxiClk)] = {.reg = msm8x53::kPcnocUsb3AxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kPdm2Clk)] = {.reg = msm8x53::kPdm2Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kPdmAhbClk)] = {.reg = msm8x53::kPdmAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kRbcprGfxClk)] = {.reg = msm8x53::kRbcprGfxCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1AhbClk)] = {.reg = msm8x53::kSdcc1AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1AppsClk)] = {.reg = msm8x53::kSdcc1AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1IceCoreClk)] = {.reg = msm8x53::kSdcc1IceCoreCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc2AhbClk)] = {.reg = msm8x53::kSdcc2AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc2AppsClk)] = {.reg = msm8x53::kSdcc2AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsb30MasterClk)] = {.reg = msm8x53::kUsb30MasterCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsb30MockUtmiClk)] = {.reg = msm8x53::kUsb30MockUtmiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsb30SleepClk)] = {.reg = msm8x53::kUsb30SleepCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsb3AuxClk)] = {.reg = msm8x53::kUsb3AuxCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsbPhyCfgAhbClk)] = {.reg = msm8x53::kUsbPhyCfgAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kVenus0AhbClk)] = {.reg = msm8x53::kVenus0AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kVenus0AxiClk)] = {.reg = msm8x53::kVenus0AxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kVenus0Core0Vcodec0Clk)] = {
        .reg = msm8x53::kVenus0Core0Vcodec0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kVenus0Vcodec0Clk)] = {.reg = msm8x53::kVenus0Vcodec0Cbcr},
};

constexpr struct msm_clk_voter kMsmClkVoters[] = {
    [msm8x53::MsmClkIndex(msm8x53::kApssAhbClk)] = {
        .cbcr_reg = msm8x53::kApssAhbCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 14)
    },
    [msm8x53::MsmClkIndex(msm8x53::kApssAxiClk)] = {
        .cbcr_reg = msm8x53::kApssAxiCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 13)
    },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1AhbClk)] = {
        .cbcr_reg = msm8x53::kBlsp1AhbCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 10)
    },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2AhbClk)] = {
        .cbcr_reg = msm8x53::kBlsp2AhbCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 20)
    },
    [msm8x53::MsmClkIndex(msm8x53::kBootRomAhbClk)] = {
        .cbcr_reg = msm8x53::kBootRomAhbCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 7)
    },
    [msm8x53::MsmClkIndex(msm8x53::kCryptoAhbClk)] = {
        .cbcr_reg = msm8x53::kCryptoAhbCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 0)
    },
    [msm8x53::MsmClkIndex(msm8x53::kCryptoAxiClk)] = {
        .cbcr_reg = msm8x53::kCryptoAxiCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 1)
    },
    [msm8x53::MsmClkIndex(msm8x53::kCryptoClk)] = {
        .cbcr_reg = msm8x53::kCryptoCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 2)
    },
    [msm8x53::MsmClkIndex(msm8x53::kQdssDapClk)] = {
        .cbcr_reg = msm8x53::kQdssDapCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 11)
    },
    [msm8x53::MsmClkIndex(msm8x53::kPrngAhbClk)] = {
        .cbcr_reg = msm8x53::kPrngAhbCbcr,
        .vote_reg = msm8x53::kApcsClockBranchEnaVote,
        .bit = (1 << 8)
    },
    [msm8x53::MsmClkIndex(msm8x53::kApssTcuAsyncClk)] = {
        .cbcr_reg = msm8x53::kApssTcuAsyncCbcr,
        .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
        .bit = (1 << 1)
    },
    [msm8x53::MsmClkIndex(msm8x53::kCppTbuClk)] = {
        .cbcr_reg = msm8x53::kCppTbuCbcr,
        .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
        .bit = (1 << 14)
    },
    [msm8x53::MsmClkIndex(msm8x53::kJpegTbuClk)] = {
        .cbcr_reg = msm8x53::kJpegTbuCbcr,
        .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
        .bit = (1 << 10)
    },
    [msm8x53::MsmClkIndex(msm8x53::kMdpTbuClk)] = {
        .cbcr_reg = msm8x53::kMdpTbuCbcr,
        .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
        .bit = (1 << 4)
    },
    [msm8x53::MsmClkIndex(msm8x53::kSmmuCfgClk)] = {
        .cbcr_reg = msm8x53::kSmmuCfgCbcr,
        .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
        .bit = (1 << 12)
    },
    [msm8x53::MsmClkIndex(msm8x53::kVenusTbuClk)] = {
        .cbcr_reg = msm8x53::kVenusTbuCbcr,
        .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
        .bit = (1 << 5)
    },
    [msm8x53::MsmClkIndex(msm8x53::kVfe1TbuClk)] = {
        .cbcr_reg = msm8x53::kVfe1TbuCbcr,
        .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
        .bit = (1 << 17)
    },
    [msm8x53::MsmClkIndex(msm8x53::kVfeTbuClk)] = {
        .cbcr_reg = msm8x53::kVfeTbuCbcr,
        .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
        .bit = (1 << 9)
    },
};

} // namespace

zx_status_t Msm8x53Clk::Create(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    std::unique_ptr<Msm8x53Clk> device(new Msm8x53Clk(parent));

    status = device->Init();
    if (status != ZX_OK) {
        zxlogf(ERROR, "msm-clk: failed to initialize, st = %d\n", status);
        return status;
    }

    status = device->DdkAdd(kMsmClkName);
    if (status != ZX_OK) {
        zxlogf(ERROR, "msm-clk: DdkAdd failed, st = %d\n", status);
        return status;
    }

    // Intentially leak, devmgr owns the memory now.
    __UNUSED auto* unused = device.release();

    return ZX_OK;
}

zx_status_t Msm8x53Clk::Init() {
    ddk::PDev pdev(parent());
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "msm-clk: failed to get pdev protocol\n");
        return ZX_ERR_NO_RESOURCES;
    }

    zx_status_t status = pdev.MapMmio(0, &mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "msm-clk: failed to map cc_base mmio, st = %d\n", status);
        return status;
    }

    status = RegisterClockProtocol();
    if (status != ZX_OK) {
        zxlogf(ERROR, "msm-clk: failed to register clock impl protocol, st = %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t Msm8x53Clk::ClockImplEnable(uint32_t index) {
    // Extract the index and the type of the clock from the argument.
    const uint32_t clock_id = msm8x53::MsmClkIndex(index);
    const msm8x53::msm_clk_type clock_type = msm8x53::MsmClkType(index);

    switch (clock_type) {
    case msm8x53::msm_clk_type::kGate:
        return GateClockEnable(clock_id);
    case msm8x53::msm_clk_type::kBranch:
        return BranchClockEnable(clock_id);
    case msm8x53::msm_clk_type::kVoter:
        return VoterClockEnable(clock_id);
    }

    // Unimplemented clock type?
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplDisable(uint32_t index) {
    // Extract the index and the type of the clock from the argument.
    const uint32_t clock_id = msm8x53::MsmClkIndex(index);
    const msm8x53::msm_clk_type clock_type = msm8x53::MsmClkType(index);

    switch (clock_type) {
    case msm8x53::msm_clk_type::kGate:
        return GateClockDisable(clock_id);
    case msm8x53::msm_clk_type::kBranch:
        return BranchClockDisable(clock_id);
    case msm8x53::msm_clk_type::kVoter:
        return VoterClockDisable(clock_id);
    }

    // Unimplemented clock type?
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplRequestRate(uint32_t id, uint64_t hz) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::AwaitBranchClock(AwaitBranchClockStatus status,
                                         const uint32_t cbcr_reg) {
    // In case the status check register and the clock control register cross
    // a boundary.
    hw_mb();

    // clang-format off
    constexpr uint32_t kReadyMask             = 0xf0000000;
    constexpr uint32_t kBranchEnableVal       = 0x0;
    constexpr uint32_t kBranchDisableVal      = 0x80000000;
    constexpr uint32_t kBranchNocFsmEnableVal = 0x20000000;
    // clang-format on

    constexpr uint32_t kMaxAttempts = 500;
    for (uint32_t attempts = 0; attempts < kMaxAttempts; attempts++) {
        const uint32_t val = mmio_->Read32(cbcr_reg) & kReadyMask;

        switch (status) {
        case AwaitBranchClockStatus::Enabled:
            if ((val == kBranchEnableVal) || (val == kBranchNocFsmEnableVal)) {
                return ZX_OK;
            }
            break;
        case AwaitBranchClockStatus::Disabled:
            if (val == kBranchDisableVal) {
                return ZX_OK;
            }
            break;
        }

        zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
    }

    return ZX_ERR_TIMED_OUT;
}

zx_status_t Msm8x53Clk::VoterClockEnable(uint32_t index) {
    if (unlikely(index >= countof(kMsmClkVoters))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const struct clk::msm_clk_voter& clk = kMsmClkVoters[index];

    lock_.Acquire();
    mmio_->SetBits32(clk.bit, clk.vote_reg);
    lock_.Release();

    return AwaitBranchClock(AwaitBranchClockStatus::Enabled, clk.cbcr_reg);
}

zx_status_t Msm8x53Clk::VoterClockDisable(uint32_t index) {
    if (unlikely(index >= countof(kMsmClkVoters))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const struct clk::msm_clk_voter& clk = kMsmClkVoters[index];

    lock_.Acquire();
    mmio_->ClearBits32(clk.bit, clk.vote_reg);
    lock_.Release();

    return ZX_OK;
}

zx_status_t Msm8x53Clk::BranchClockEnable(uint32_t index) {
    if (unlikely(index >= countof(kMsmClkBranches))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const struct clk::msm_clk_branch& clk = kMsmClkBranches[index];

    lock_.Acquire();
    mmio_->SetBits32(kBranchEnable, clk.reg);
    lock_.Release();

    return AwaitBranchClock(AwaitBranchClockStatus::Enabled, clk.reg);
}

zx_status_t Msm8x53Clk::BranchClockDisable(uint32_t index) {
    if (unlikely(index >= countof(kMsmClkBranches))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const struct msm_clk_branch& clk = kMsmClkBranches[index];

    lock_.Acquire();
    mmio_->ClearBits32(kBranchEnable, clk.reg);
    lock_.Release();

    return AwaitBranchClock(AwaitBranchClockStatus::Disabled, clk.reg);
}

zx_status_t Msm8x53Clk::GateClockEnable(uint32_t index) {
    if (unlikely(index >= countof(kMsmClkGates))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const msm_clk_gate_t& clk = kMsmClkGates[index];

    lock_.Acquire();
    mmio_->SetBits32(clk.bit, clk.reg);
    lock_.Release();

    if (clk.delay_us) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(clk.delay_us)));
    }

    return ZX_OK;
}
zx_status_t Msm8x53Clk::GateClockDisable(uint32_t index) {
    if (unlikely(index > countof(kMsmClkGates))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const msm_clk_gate_t& clk = kMsmClkGates[index];

    lock_.Acquire();
    mmio_->ClearBits32(clk.bit, clk.reg);
    lock_.Release();

    if (clk.delay_us) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(clk.delay_us)));
    }

    return ZX_OK;
}

zx_status_t Msm8x53Clk::Bind() {
    return ZX_OK;
}
void Msm8x53Clk::DdkUnbind() {
    // Hazard! Always acquire locks in the order that they were defined in the
    // header.
    fbl::AutoLock lock(&lock_);

    mmio_.reset();

    DdkRemove();
}

void Msm8x53Clk::DdkRelease() {
    delete this;
}

zx_status_t Msm8x53Clk::RegisterClockProtocol() {
    zx_status_t st;

    ddk::PBusProtocolClient pbus(parent());
    if (!pbus.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    clock_impl_protocol_t clk_proto = {
        .ops = &clock_impl_protocol_ops_,
        .ctx = this,
    };

    st = pbus.RegisterProtocol(ZX_PROTOCOL_CLOCK_IMPL, &clk_proto, sizeof(clk_proto));
    if (st != ZX_OK) {
        zxlogf(ERROR, "msm-clk: pbus_register_protocol failed, st = %d\n", st);
        return st;
    }

    return ZX_OK;
}

} // namespace clk

static zx_driver_ops_t msm8x53_clk_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = clk::Msm8x53Clk::Create;
    return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(msm8x53_clk, msm8x53_clk_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QUALCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_QUALCOMM_CLOCK),
ZIRCON_DRIVER_END(msm8x53_clk)
