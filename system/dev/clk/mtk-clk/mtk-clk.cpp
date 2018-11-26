// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-clk.h"

#include <ddk/protocol/platform/device.h>
#include <ddktl/pdev.h>
#include <fbl/alloc_checker.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>
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

constexpr MtkClkGateRegs kClkGatingCtrl1 = { .set = 0x54, .clr = 0x84 };

constexpr MtkClkGate kMtkClkGates[] = {
    [board_mt8167::kClkI2c0] = { .regs = kClkGatingCtrl1, .bit = 3 },
    [board_mt8167::kClkI2c1] = { .regs = kClkGatingCtrl1, .bit = 4 },
    [board_mt8167::kClkI2c2] = { .regs = kClkGatingCtrl1, .bit = 16 },
};

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
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 failed\n", __FILE__);
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<MtkClk> device(new (&ac) MtkClk(parent, std::move(*mmio)));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: MtkClk alloc failed\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device->DdkAdd("mtk-clk")) != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
        return status;
    }

    __UNUSED auto* dummy = device.release();

    return ZX_OK;
}

zx_status_t MtkClk::ClkEnable(uint32_t index) {
    if (index >= fbl::count_of(kMtkClkGates)) {
        return ZX_ERR_INVALID_ARGS;
    }

    const MtkClkGate& gate = kMtkClkGates[index];
    mmio_.Write32(1 << gate.bit, gate.regs.clr);
    return ZX_OK;
}

zx_status_t MtkClk::ClkDisable(uint32_t index) {
    if (index >= fbl::count_of(kMtkClkGates)) {
        return ZX_ERR_INVALID_ARGS;
    }

    const MtkClkGate& gate = kMtkClkGates[index];
    mmio_.Write32(1 << gate.bit, gate.regs.set);
    return ZX_OK;
}

}  // namespace clk

extern "C" zx_status_t mtk_clk_bind(void* ctx, zx_device_t* parent) {
    return clk::MtkClk::Create(parent);
}
