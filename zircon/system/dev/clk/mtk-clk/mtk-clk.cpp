// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-clk.h"

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/pdev.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/clock/c/fidl.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-clk.h>

namespace clk {

struct MtkClkGateRegs {
    const zx_off_t set;
    const zx_off_t clr;
};

struct MtkClkGate {
    const MtkClkGateRegs regs;
    const uint8_t bit;
};

constexpr MtkClkGateRegs kClkGatingCtrl0 = {.set = 0x50, .clr = 0x80};
constexpr MtkClkGateRegs kClkGatingCtrl1 = {.set = 0x54, .clr = 0x84};
constexpr MtkClkGateRegs kClkGatingCtrl8 = {.set = 0xa0, .clr = 0xb0};

constexpr MtkClkGate kMtkClkGates[] = {
    [board_mt8167::kClkThermal] = {.regs = kClkGatingCtrl1, .bit = 1},
    [board_mt8167::kClkI2c0] = {.regs = kClkGatingCtrl1, .bit = 3},
    [board_mt8167::kClkI2c1] = {.regs = kClkGatingCtrl1, .bit = 4},
    [board_mt8167::kClkI2c2] = {.regs = kClkGatingCtrl1, .bit = 16},
    [board_mt8167::kClkPmicWrapAp] = {.regs = kClkGatingCtrl1, .bit = 20},
    [board_mt8167::kClkPmicWrap26M] = {.regs = kClkGatingCtrl1, .bit = 29},
    [board_mt8167::kClkAuxAdc] = {.regs = kClkGatingCtrl1, .bit = 30},
    [board_mt8167::kClkSlowMfg] = {.regs = kClkGatingCtrl8, .bit = 7},
    [board_mt8167::kClkAxiMfg] = {.regs = kClkGatingCtrl8, .bit = 6},
    [board_mt8167::kClkMfgMm] = {.regs = kClkGatingCtrl0, .bit = 2},
    [board_mt8167::kClkAud1] = {.regs = kClkGatingCtrl8, .bit = 8},
    [board_mt8167::kClkAud2] = {.regs = kClkGatingCtrl8, .bit = 9},
    [board_mt8167::kClkAudEngen1] = {.regs = kClkGatingCtrl8, .bit = 10},
    [board_mt8167::kClkAudEngen2] = {.regs = kClkGatingCtrl8, .bit = 11},
};
struct clock_info {
    uint32_t idx;
    const char* name;
};

static struct clock_info clks[] = {
    {.idx = 1, .name = "mainpll_div8"},
    {.idx = 2, .name = "mainpll_div11"},
    {.idx = 3, .name = "mainpll_div12"},
    {.idx = 4, .name = "mainpll_div20"},
    {.idx = 5, .name = "mainpll_div7"},
    {.idx = 6, .name = "univpll_div16"},
    {.idx = 7, .name = "univpll_div24"},
    {.idx = 8, .name = "nfix2"},
    {.idx = 9, .name = "whpll"},
    {.idx = 10, .name = "wpll"},
    {.idx = 11, .name = "26mhz"},
    {.idx = 18, .name = "mfg"},
    {.idx = 45, .name = "axi_mfg"},
    {.idx = 46, .name = "slow_mfg"},
    {.idx = 47, .name = "aud1"},
    {.idx = 48, .name = "aud2"},
    {.idx = 49, .name = "aud engen1"},
    {.idx = 50, .name = "aud engen2"},
    {.idx = 67, .name = "mmpll"},
    {.idx = 69, .name = "aud1pll"},
    {.idx = 70, .name = "aud2pll"},
};

zx_status_t fidl_clk_measure(void* ctx, uint32_t clk, fidl_txn_t* txn) {
    auto dev = static_cast<MtkClk*>(ctx);
    fuchsia_hardware_clock_FrequencyInfo info;

    dev->ClkMeasure(clk, &info);

    return fuchsia_hardware_clock_DeviceMeasure_reply(txn, &info);
}

zx_status_t fidl_clk_get_count(void* ctx, fidl_txn_t* txn) {
    auto dev = static_cast<MtkClk*>(ctx);

    return fuchsia_hardware_clock_DeviceGetCount_reply(txn, dev->GetClkCount());
}

static const fuchsia_hardware_clock_Device_ops_t fidl_ops_ = {
    .Measure = fidl_clk_measure,
    .GetCount = fidl_clk_get_count,
};

namespace {

class FrequencyMeterControl : public hwreg::RegisterBase<FrequencyMeterControl, uint32_t> {
public:
    enum {
        kFixClk26Mhz = 0,
        kFixClk32Khz = 2,
    };

    DEF_FIELD(29, 28, ck_div);
    DEF_FIELD(24, 24, fixclk_sel);
    DEF_FIELD(22, 16, monclk_sel);
    DEF_BIT(15, enable);
    DEF_BIT(14, reset);
    DEF_FIELD(11, 0, window);

    static auto Get() { return hwreg::RegisterAddr<FrequencyMeterControl>(0x10); }
};

} // namespace

zx_status_t MtkClk::Bind() {
    zx_status_t status;
    pbus_protocol_t pbus;
    status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "MtkClk: failed to get ZX_PROTOCOL_PBUS, st = %d\n", status);
        return status;
    }

    clock_impl_protocol_t clk_proto = {
        .ops = &clock_impl_protocol_ops_,
        .ctx = this,
    };

    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_CLOCK_IMPL, &clk_proto, sizeof(clk_proto));
    if (status != ZX_OK) {
        zxlogf(ERROR, "MtkClk::Create: pbus_register_protocol failed, st = %d\n", status);
        return status;
    }

    return DdkAdd("mtk-clk");
}

zx_status_t MtkClk::Create(zx_device_t* parent) {
    zx_status_t status;

    pdev_protocol_t pdev_proto;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_proto)) != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
        return status;
    }

    ddk::PDev pdev(&pdev_proto);
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = pdev.MapMmio(0, &mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer failed\n", __FILE__);
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<MtkClk> device(new (&ac) MtkClk(parent, *std::move(mmio)));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: MtkClk alloc failed\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device->Bind()) != ZX_OK) {
        zxlogf(ERROR, "%s: MtkClk bind failed: %d\n", __FILE__, status);
        return status;
    }

    __UNUSED auto* dummy = device.release();

    return ZX_OK;
}

zx_status_t MtkClk::ClockImplEnable(uint32_t index) {
    if (index >= fbl::count_of(kMtkClkGates)) {
        return ZX_ERR_INVALID_ARGS;
    }

    const MtkClkGate& gate = kMtkClkGates[index];
    mmio_.Write32(1 << gate.bit, gate.regs.clr);
    return ZX_OK;
}

zx_status_t MtkClk::ClockImplDisable(uint32_t index) {
    if (index >= fbl::count_of(kMtkClkGates)) {
        return ZX_ERR_INVALID_ARGS;
    }

    const MtkClkGate& gate = kMtkClkGates[index];
    mmio_.Write32(1 << gate.bit, gate.regs.set);
    return ZX_OK;
}

zx_status_t MtkClk::ClockImplRequestRate(uint32_t id, uint64_t hz) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MtkClk::ClkMeasure(uint32_t clk, fuchsia_hardware_clock_FrequencyInfo* info) {
    if (clk >= fbl::count_of(clks)) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t max_len = sizeof(info->name);
    size_t len = strnlen(clks[clk].name, max_len);
    if (len == max_len) {
        return ZX_ERR_INVALID_ARGS;
    }

    memcpy(info->name, clks[clk].name, len + 1);

    constexpr uint32_t kWindowSize = 512;
    constexpr uint32_t kFixedClockFreqMHz = 26000000 / 1000000;
    FrequencyMeterControl::Get().FromValue(0).set_reset(true).WriteTo(&mmio_);
    FrequencyMeterControl::Get().FromValue(0).set_reset(false).WriteTo(&mmio_);
    auto ctrl = FrequencyMeterControl::Get().FromValue(0);
    ctrl.set_window(kWindowSize - 1).set_monclk_sel(clks[clk].idx);
    ctrl.set_fixclk_sel(FrequencyMeterControl::kFixClk26Mhz).set_enable(true);
    ctrl.WriteTo(&mmio_);

    hw_wmb();

    // Sleep at least kWindowSize ticks of the fixed clock.
    zx_nanosleep(zx_deadline_after(ZX_USEC(30)));

    // Assume it completed calculating.

    constexpr uint32_t kFrequencyMeterReadData = 0x14;
    uint32_t count = mmio_.Read32(kFrequencyMeterReadData);
    info->frequency = (count * kFixedClockFreqMHz) / kWindowSize;
    FrequencyMeterControl::Get().FromValue(0).set_reset(true).WriteTo(&mmio_);
    FrequencyMeterControl::Get().FromValue(0).set_reset(false).WriteTo(&mmio_);
    return ZX_OK;
}

uint32_t MtkClk::GetClkCount() {
    return static_cast<uint32_t>(fbl::count_of(clks));
}

zx_status_t MtkClk::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_hardware_clock_Device_dispatch(this, txn, msg, &fidl_ops_);
}

} // namespace clk

zx_status_t mtk_clk_bind(void* ctx, zx_device_t* parent) {
    return clk::MtkClk::Create(parent);
}

static zx_driver_ops_t mtk_clk_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = mtk_clk_bind;
    return ops;
}();

ZIRCON_DRIVER_BEGIN(mtk_clk, mtk_clk_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_CLK),
    ZIRCON_DRIVER_END(mtk_clk)
