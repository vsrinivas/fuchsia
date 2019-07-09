// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syn-clk.h"

#include <numeric>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clockimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hwreg/bitfields.h>
#include <lib/device-protocol/pdev.h>
#include <soc/as370/as370-audio-regs.h>
#include <soc/as370/as370-clk-regs.h>
#include <soc/as370/as370-clk.h>

namespace clk {

zx_status_t SynClk::Create(void* ctx, zx_device_t* parent) {
    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s: failed to get pdev\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    std::optional<ddk::MmioBuffer> global_mmio, avio_mmio;
    auto status = pdev.MapMmio(0, &global_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to map mmio index 0 %d\n", __FILE__, status);
        return status;
    }
    status = pdev.MapMmio(1, &avio_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to map mmio index 1 %d\n", __FILE__, status);
        return status;
    }

    std::unique_ptr<SynClk> device(new SynClk(parent, *std::move(global_mmio),
                                              *std::move(avio_mmio)));

    status = device->Init();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to initialize %d\n", __FILE__, status);
        return status;
    }

    status = device->DdkAdd("synaptics-clk");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed %d\n", __FILE__, status);
        return status;
    }

    // Intentially leak, devmgr owns the memory now.
    __UNUSED auto* unused = device.release();

    return ZX_OK;
}

zx_status_t SynClk::Init() {
    auto status = RegisterClockProtocol();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to register clock impl protocol %d\n", __FILE__, status);
        return status;
    }

    return ZX_OK;
}

zx_status_t SynClk::AvpllClkEnable(bool avpll0, bool enable) {
    uint32_t id = avpll0 ? 0 : 1;
    fbl::AutoLock lock(&lock_);
    // TODO(andresoportus): Manage dependencies between AVPLLs, avioSysClk and SYSPLL.
    // For now make sure things get enabled.
    if (enable) {
        // Enable/disable AVIO clk and keep SYSPLL DIV3 as source.
        avioSysClk_ctrl::Get().ReadFrom(&global_mmio_).set_ClkEn(enable).WriteTo(&global_mmio_);

        // Enable sysPll by disabling power down (or vice versa).
        sysPll_ctrl::Get().ReadFrom(&global_mmio_).set_PD(!enable).WriteTo(&global_mmio_);
    }

    if (avpll0) {
        // Enable/disable AVPLL0.
        avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).
            set_ctrl_AVPLL0(enable).
            WriteTo(&avio_mmio_);
    } else {
        // Enable/disable AVPLL1.
        avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).
            set_ctrl_AVPLL1(enable).
            WriteTo(&avio_mmio_);
    }
    // Enable/disable AVPLLx clock.
    avioGbl_AVPLLx_WRAP_AVPLL_CLK1_CTRL::Get(id).ReadFrom(&avio_mmio_).
        set_clkEn(enable).
        WriteTo(&avio_mmio_);

    return ZX_OK;
}

zx_status_t SynClk::AvpllSetRate(bool avpll0, uint32_t rate) {
    constexpr uint32_t min_rate = 800'000'000;
    constexpr uint32_t max_rate = 3'200'000'000;
    uint32_t id = avpll0 ? 0 : 1;

    if (rate > max_rate) {
        return ZX_ERR_INVALID_ARGS;
    }

    // All rates are converted to MHz.
    rate /= 1'000'000;
    // OSC at 25 MHz, TODO(andresoportus): Make rate setting relative to parent.
    constexpr uint32_t parent_rate = 25'000'000 / 1'000'000;

    uint32_t dp = 1;
    constexpr uint32_t max_dp = 0x7;
    if (rate < min_rate / 1'000'000) {
        dp = min_rate / 1'000'000 / rate + 1;
        if (dp > max_dp) {
            return ZX_ERR_INTERNAL;
        }
        rate = rate * dp;
    }

    uint32_t div = std::gcd(rate, parent_rate);
    uint32_t dn = rate / div;
    uint32_t dm = parent_rate / div;
    uint32_t frac = 0;
    constexpr uint32_t max_dn = 0x7ff;
    constexpr uint32_t max_dm = 0x3f;

    if ((dm > max_dm) || (dn > max_dn)) {
        // Fractional mode.
        dn /= dm;
        if (dn > max_dn) {
            return ZX_ERR_INTERNAL;
        }
        frac = dn % dm;
        frac = (frac << 24) / dm + 1;
        dm = 1;
    }
    zxlogf(TRACE, "%s: frac %u  dn %u  dm %u  dp %u\n", __FILE__, frac, dn, dm, dp);

    fbl::AutoLock lock(&lock_);

    if (avpll0) {
        avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).
            set_ctrl_AVPLL0(0).
            WriteTo(&avio_mmio_);
    } else {
        avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).
            set_ctrl_AVPLL1(0).
            WriteTo(&avio_mmio_);
    }

    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl4::Get(id).ReadFrom(&avio_mmio_).
        set_BYPASS(1).
        WriteTo(&avio_mmio_);

    // PLL power down.
    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3::Get(id).ReadFrom(&avio_mmio_).
        set_PDDP(1).
        WriteTo(&avio_mmio_);

    if (frac != 0) {
        avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl::Get(id).ReadFrom(&avio_mmio_).
            set_RESETN(0).
            WriteTo(&avio_mmio_);
        avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl1::Get(id).ReadFrom(&avio_mmio_).
            set_FRAC(frac).
            WriteTo(&avio_mmio_);
    }

    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl::Get(id).ReadFrom(&avio_mmio_).
        set_DN(dn).
        set_DM(dm).
        WriteTo(&avio_mmio_);
    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3::Get(id).ReadFrom(&avio_mmio_).
        set_DP(dp).
        WriteTo(&avio_mmio_);
    zx_nanosleep(zx_deadline_after(ZX_USEC(2)));

    if (frac != 0) {
        avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl::Get(id).ReadFrom(&avio_mmio_).
            set_RESETN(1).
            WriteTo(&avio_mmio_);
    }

    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl3::Get(id).ReadFrom(&avio_mmio_).
        set_PDDP(0).
        WriteTo(&avio_mmio_);
    // TODO(andresoportus): Wait for PLL lock instead of arbitrary delay.
    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

    avioGbl_AVPLLx_WRAP_AVPLL_vsipll_ctrl4::Get(id).ReadFrom(&avio_mmio_).
        set_BYPASS(0).
        WriteTo(&avio_mmio_);
    if (avpll0) {
        avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).
            set_ctrl_AVPLL0(1).
            WriteTo(&avio_mmio_);
    } else {
        avioGbl_AVPLLA_CLK_EN::Get().ReadFrom(&avio_mmio_).
            set_ctrl_AVPLL1(1).
            WriteTo(&avio_mmio_);
    }
    return ZX_OK;
}

zx_status_t SynClk::ClockImplEnable(uint32_t index) {
    switch (index) {
    case as370::kClkAvpll0:
        return AvpllClkEnable(true, true);
    case as370::kClkAvpll1:
        return AvpllClkEnable(false, true);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SynClk::ClockImplDisable(uint32_t index) {
    switch (index) {
    case as370::kClkAvpll0:
        return AvpllClkEnable(true, false);
    case as370::kClkAvpll1:
        return AvpllClkEnable(false, false);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SynClk::ClockImplIsEnabled(uint32_t id, bool* out_enabled) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SynClk::ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate, uint64_t* out_best_rate) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SynClk::ClockImplGetRate(uint32_t id, uint64_t* out_current_rate) {
    return ZX_ERR_NOT_SUPPORTED;
}


zx_status_t SynClk::ClockImplSetRate(uint32_t index, uint64_t hz) {
    switch (index) {
    case as370::kClkAvpll0:
        return AvpllSetRate(true, static_cast<uint32_t>(hz));
    case as370::kClkAvpll1:
        return AvpllSetRate(false, static_cast<uint32_t>(hz));
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t SynClk::Bind() {
    return ZX_OK;
}

void SynClk::DdkUnbind() {
    fbl::AutoLock lock(&lock_);

    global_mmio_.reset();
    avio_mmio_.reset();

    DdkRemove();
}

void SynClk::DdkRelease() {
    delete this;
}

zx_status_t SynClk::RegisterClockProtocol() {
    ddk::PBusProtocolClient pbus(parent());
    if (!pbus.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    clock_impl_protocol_t clk_proto = {
        .ops = &clock_impl_protocol_ops_,
        .ctx = this,
    };
//#define TEST_DAI_CLOCKS
#ifdef TEST_DAI_CLOCKS
    ClockImplEnable(as370::kClkAvpll0);
    ClockImplSetRate(as370::kClkAvpll0, static_cast<uint32_t>(48000) * 64 * 512);
#endif

    auto status = pbus.RegisterProtocol(ZX_PROTOCOL_CLOCK_IMPL, &clk_proto, sizeof(clk_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_register_protocol failed %d\n", __FILE__, status);
        return status;
    }
    return ZX_OK;
}

} // namespace clk

static constexpr zx_driver_ops_t syn_clk_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = clk::SynClk::Create;
    return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(syn_clk, syn_clk_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AS370_CLOCK),
ZIRCON_DRIVER_END(syn_clk)
// clang-format on
