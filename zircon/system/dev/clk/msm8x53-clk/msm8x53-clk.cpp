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

#include "msm8x53-branch-clks.h"

namespace clk {

typedef struct msm_clk_gate {
    uint32_t reg;
    uint32_t bit;
    uint32_t delay_us;
} msm_clk_gate_t;

struct msm_clk_branch {
    uint32_t reg;
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
        .reg = kApc0VoltageDroopDetectorGpll0Cbcr
    },
    [msm8x53::MsmClkIndex(msm8x53::kApc1DroopDetectorGpll0Clk)] = {
        .reg = kApc1VoltageDroopDetectorGpll0Cbcr
    },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup1I2cAppsClk)] = { .reg = kBlsp1Qup1I2cAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup1SpiAppsClk)] = { .reg = kBlsp1Qup1SpiAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup2I2cAppsClk)] = { .reg = kBlsp1Qup2I2cAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup2SpiAppsClk)] = { .reg = kBlsp1Qup2SpiAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup3I2cAppsClk)] = { .reg = kBlsp1Qup3I2cAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup3SpiAppsClk)] = { .reg = kBlsp1Qup3SpiAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup4I2cAppsClk)] = { .reg = kBlsp1Qup4I2cAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup4SpiAppsClk)] = { .reg = kBlsp1Qup4SpiAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart1AppsClk)] = { .reg = kBlsp1Uart1AppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart2AppsClk)] = { .reg = kBlsp1Uart2AppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup1I2cAppsClk)] = { .reg = kBlsp2Qup1I2cAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup1SpiAppsClk)] = { .reg = kBlsp2Qup1SpiAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup2I2cAppsClk)] = { .reg = kBlsp2Qup2I2cAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup2SpiAppsClk)] = { .reg = kBlsp2Qup2SpiAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup3I2cAppsClk)] = { .reg = kBlsp2Qup3I2cAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup3SpiAppsClk)] = { .reg = kBlsp2Qup3SpiAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup4I2cAppsClk)] = { .reg = kBlsp2Qup4I2cAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup4SpiAppsClk)] = { .reg = kBlsp2Qup4SpiAppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Uart1AppsClk)] = { .reg = kBlsp2Uart1AppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Uart2AppsClk)] = { .reg = kBlsp2Uart2AppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBimcGpuClk)] = { .reg = kBimcGpuCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCciAhbClk)] = { .reg = kCamssCciAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCciClk)] = { .reg = kCamssCciCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppAhbClk)] = { .reg = kCamssCppAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppAxiClk)] = { .reg = kCamssCppAxiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppClk)] = { .reg = kCamssCppCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0AhbClk)] = { .reg = kCamssCsi0AhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0Clk)] = { .reg = kCamssCsi0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0Csiphy3pClk)] = { .reg = kCamssCsi0Csiphy3pCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0phyClk)] = { .reg = kCamssCsi0phyCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0pixClk)] = { .reg = kCamssCsi0pixCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0rdiClk)] = { .reg = kCamssCsi0rdiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1AhbClk)] = { .reg = kCamssCsi1AhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1Clk)] = { .reg = kCamssCsi1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1Csiphy3pClk)] = { .reg = kCamssCsi1Csiphy3pCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1phyClk)] = { .reg = kCamssCsi1phyCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1pixClk)] = { .reg = kCamssCsi1pixCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1rdiClk)] = { .reg = kCamssCsi1rdiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2AhbClk)] = { .reg = kCamssCsi2AhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2Clk)] = { .reg = kCamssCsi2Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2Csiphy3pClk)] = { .reg = kCamssCsi2Csiphy3pCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2phyClk)] = { .reg = kCamssCsi2phyCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2pixClk)] = { .reg = kCamssCsi2pixCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2rdiClk)] = { .reg = kCamssCsi2rdiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsiVfe0Clk)] = { .reg = kCamssCsiVfe0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsiVfe1Clk)] = { .reg = kCamssCsiVfe1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssGp0Clk)] = { .reg = kCamssGp0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssGp1Clk)] = { .reg = kCamssGp1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssIspifAhbClk)] = { .reg = kCamssIspifAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpeg0Clk)] = { .reg = kCamssJpeg0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpegAhbClk)] = { .reg = kCamssJpegAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpegAxiClk)] = { .reg = kCamssJpegAxiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk0Clk)] = { .reg = kCamssMclk0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk1Clk)] = { .reg = kCamssMclk1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk2Clk)] = { .reg = kCamssMclk2Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk3Clk)] = { .reg = kCamssMclk3Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssMicroAhbClk)] = { .reg = kCamssMicroAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0phytimerClk)] = { .reg = kCamssCsi0phytimerCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1phytimerClk)] = { .reg = kCamssCsi1phytimerCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2phytimerClk)] = { .reg = kCamssCsi2phytimerCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssAhbClk)] = { .reg = kCamssAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssTopAhbClk)] = { .reg = kCamssTopAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe0Clk)] = { .reg = kCamssVfe0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfeAhbClk)] = { .reg = kCamssVfeAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfeAxiClk)] = { .reg = kCamssVfeAxiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1AhbClk)] = { .reg = kCamssVfe1AhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1AxiClk)] = { .reg = kCamssVfe1AxiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1Clk)] = { .reg = kCamssVfe1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kDccClk)] = { .reg = kDccCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kGp1Clk)] = { .reg = kGp1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kGp2Clk)] = { .reg = kGp2Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kGp3Clk)] = { .reg = kGp3Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssAhbClk)] = { .reg = kMdssAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssAxiClk)] = { .reg = kMdssAxiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssByte0Clk)] = { .reg = kMdssByte0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssByte1Clk)] = { .reg = kMdssByte1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssEsc0Clk)] = { .reg = kMdssEsc0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssEsc1Clk)] = { .reg = kMdssEsc1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssMdpClk)] = { .reg = kMdssMdpCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssPclk0Clk)] = { .reg = kMdssPclk0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssPclk1Clk)] = { .reg = kMdssPclk1Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMdssVsyncClk)] = { .reg = kMdssVsyncCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMssCfgAhbClk)] = { .reg = kMssCfgAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kMssQ6BimcAxiClk)] = { .reg = kMssQ6BimcAxiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kBimcGfxClk)] = { .reg = kBimcGfxCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kOxiliAhbClk)] = { .reg = kOxiliAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kOxiliAonClk)] = { .reg = kOxiliAonCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kOxiliGfx3dClk)] = { .reg = kOxiliGfx3dCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kOxiliTimerClk)] = { .reg = kOxiliTimerCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kPcnocUsb3AxiClk)] = { .reg = kPcnocUsb3AxiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kPdm2Clk)] = { .reg = kPdm2Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kPdmAhbClk)] = { .reg = kPdmAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kRbcprGfxClk)] = { .reg = kRbcprGfxCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1AhbClk)] = { .reg = kSdcc1AhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1AppsClk)] = { .reg = kSdcc1AppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1IceCoreClk)] = { .reg = kSdcc1IceCoreCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kSdcc2AhbClk)] = { .reg = kSdcc2AhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kSdcc2AppsClk)] = { .reg = kSdcc2AppsCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kUsb30MasterClk)] = { .reg = kUsb30MasterCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kUsb30MockUtmiClk)] = { .reg = kUsb30MockUtmiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kUsb30SleepClk)] = { .reg = kUsb30SleepCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kUsb3AuxClk)] = { .reg = kUsb3AuxCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kUsbPhyCfgAhbClk)] = { .reg = kUsbPhyCfgAhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kVenus0AhbClk)] = { .reg = kVenus0AhbCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kVenus0AxiClk)] = { .reg = kVenus0AxiCbcr },
    [msm8x53::MsmClkIndex(msm8x53::kVenus0Core0Vcodec0Clk)] = { .reg = kVenus0Core0Vcodec0Cbcr },
    [msm8x53::MsmClkIndex(msm8x53::kVenus0Vcodec0Clk)] = { .reg = kVenus0Vcodec0Cbcr },
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

    switch(clock_type) {
    case msm8x53::msm_clk_type::kGate:
        return GateClockEnable(clock_id);
    case msm8x53::msm_clk_type::kBranch:
        return BranchClockEnable(clock_id);
    }

    // Unimplemented clock type?
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::ClockImplDisable(uint32_t index) {
    // Extract the index and the type of the clock from the argument.
    const uint32_t clock_id = msm8x53::MsmClkIndex(index);
    const msm8x53::msm_clk_type clock_type = msm8x53::MsmClkType(index);

    switch(clock_type) {
    case msm8x53::msm_clk_type::kGate:
        return GateClockDisable(clock_id);
    case msm8x53::msm_clk_type::kBranch:
        return BranchClockDisable(clock_id);
    }

    // Unimplemented clock type?
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53Clk::AwaitBranchClock(AwaitBranchClockStatus status,
                                         const struct clk::msm_clk_branch& clk) {
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
        const uint32_t val = mmio_->Read32(clk.reg) & kReadyMask;

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

zx_status_t Msm8x53Clk::BranchClockEnable(uint32_t index) {
    if (unlikely(index >= countof(kMsmClkBranches))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const struct clk::msm_clk_branch& clk = kMsmClkBranches[index];

    branch_clock_mutex_.Acquire();
    mmio_->SetBits32(kBranchEnable, clk.reg);
    branch_clock_mutex_.Release();

    return AwaitBranchClock(AwaitBranchClockStatus::Enabled, clk);
}

zx_status_t Msm8x53Clk::BranchClockDisable(uint32_t index) {
    if (unlikely(index >= countof(kMsmClkBranches))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const struct msm_clk_branch& clk = kMsmClkBranches[index];

    branch_clock_mutex_.Acquire();
    mmio_->ClearBits32(kBranchEnable, clk.reg);
    branch_clock_mutex_.Release();

    return AwaitBranchClock(AwaitBranchClockStatus::Disabled, clk);
}


zx_status_t Msm8x53Clk::GateClockEnable(uint32_t index) {
    if (unlikely(index >= countof(kMsmClkGates))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const msm_clk_gate_t& clk = kMsmClkGates[index];

    gate_clock_mutex_.Acquire();
    mmio_->SetBits32(clk.bit, clk.reg);
    gate_clock_mutex_.Release();

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

    gate_clock_mutex_.Acquire();
    mmio_->ClearBits32(clk.bit, clk.reg);
    gate_clock_mutex_.Release();

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
    fbl::AutoLock gate_lock(&gate_clock_mutex_);
    fbl::AutoLock branch_lock(&branch_clock_mutex_);

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
