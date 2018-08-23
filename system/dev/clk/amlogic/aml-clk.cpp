// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-clk.h"
#include "aml-axg-blocks.h"
#include "aml-gxl-blocks.h"
#include <ddk/debug.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

namespace amlogic_clock {

// MMIO Indexes
static constexpr uint32_t kHiuMmio = 0;

zx_status_t AmlClock::InitPdev(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent,
                                             ZX_PROTOCOL_PLATFORM_DEV,
                                             &pdev_);
    if (status != ZX_OK) {
        return status;
    }

    // Map the HIU registers.
    status = pdev_map_mmio_buffer(&pdev_, kHiuMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &hiu_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: could not map periph mmio: %d\n", status);
        return status;
    }

    auto cleanup = fbl::MakeAutoCall([&]() { io_buffer_release(&hiu_mmio_); });

    fbl::AllocChecker ac;
    hiu_regs_ = fbl::make_unique_checked<hwreg::RegisterIo>(&ac, reinterpret_cast<volatile void*>(
                                                                     io_buffer_virt(&hiu_mmio_)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    // Populate the clock gates.
    pdev_device_info_t info;
    status = pdev_get_device_info(&pdev_, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: pdev_get_device_info failed\n");
        return status;
    }

    // Populate the correct register blocks.
    switch (info.did) {
    case PDEV_DID_AMLOGIC_AXG_CLK:
        gates_.reset(axg_clk_gates, countof(axg_clk_gates));
        break;
    case PDEV_DID_AMLOGIC_GXL_CLK:
        gates_.reset(gxl_clk_gates, countof(gxl_clk_gates));
        break;
    default:
        zxlogf(ERROR, "aml-clk: Unsupported SOC DID %u\n", info.pid);
        return ZX_ERR_INVALID_ARGS;
    }

    platform_bus_protocol_t pbus;
    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: failed to get ZX_PROTOCOL_PLATFORM_BUS, "
                      "st = %d\n",
               status);
        return status;
    }

    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_CLK, &clk_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "meson_clk_bind: pbus_register_protocol failed, st = %d\n", status);
        return status;
    }

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t AmlClock::Create(zx_device_t* parent) {
    auto clock_device = fbl::make_unique<amlogic_clock::AmlClock>(parent);

    zx_status_t status = clock_device->InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }

    status = clock_device->DdkAdd("clocks");
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-clk: Could not create clock device: %d\n", status);
        return status;
    }

    // devmgr is now in charge of the memory for dev.
    __UNUSED auto ptr = clock_device.release();
    return ZX_OK;
}

zx_status_t AmlClock::ClkToggle(uint32_t clk, const bool enable) {
    if (clk >= gates_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    const meson_clk_gate_t* gate = &gates_[clk];
    fbl::AutoLock al(&lock_);
    uint32_t value = hiu_regs_->Read<uint32_t>(gate->reg);

    if (enable) {
        value |= (1 << gate->bit);
    } else {
        value &= ~(1 << gate->bit);
    }

    hiu_regs_->Write(gate->reg, value);
    return ZX_OK;
}

zx_status_t AmlClock::ClkEnable(uint32_t clk) {
    return ClkToggle(clk, true);
}

zx_status_t AmlClock::ClkDisable(uint32_t clk) {
    return ClkToggle(clk, false);
}

void AmlClock::ShutDown() {
    io_buffer_release(&hiu_mmio_);
}

void AmlClock::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void AmlClock::DdkRelease() {
    delete this;
}

} // namespace amlogic_clock

extern "C" zx_status_t aml_clk_bind(void* ctx, zx_device_t* parent) {
    return amlogic_clock::AmlClock::Create(parent);
}
