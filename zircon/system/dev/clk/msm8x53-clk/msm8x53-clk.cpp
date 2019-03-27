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

namespace clk {

namespace {

const char kMsmClkName[] = "msm-clk";

typedef struct msm_clk_gate {
    uint32_t reg;
    uint32_t bit;
    uint32_t delay_us;
} msm_clk_gate_t;

constexpr msm_clk_gate_t kMsmClkGates[] = {
    [msm8x53::kQUsbRefClk] = {.reg = 0x41030, .bit = 0, .delay_us = 0},
    [msm8x53::kUsbSSRefClk] = {.reg = 0x5e07c, .bit = 0, .delay_us = 0},
    [msm8x53::kUsb3PipeClk] = {.reg = 0x5e040, .bit = 0, .delay_us = 50},
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
    if (unlikely(index > countof(kMsmClkGates))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const msm_clk_gate_t& clk = kMsmClkGates[index];

    local_clock_mutex_.Acquire();
    mmio_->SetBits32(clk.bit, clk.reg);
    local_clock_mutex_.Release();

    if (clk.delay_us) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(clk.delay_us)));
    }

    return ZX_OK;
}
zx_status_t Msm8x53Clk::ClockImplDisable(uint32_t index) {
    if (unlikely(index > countof(kMsmClkGates))) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const msm_clk_gate_t& clk = kMsmClkGates[index];

    local_clock_mutex_.Acquire();
    mmio_->ClearBits32(clk.bit, clk.reg);
    local_clock_mutex_.Release();

    if (clk.delay_us) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(clk.delay_us)));
    }

    return ZX_OK;
}

zx_status_t Msm8x53Clk::Bind() {
    return ZX_OK;
}
void Msm8x53Clk::DdkUnbind() {
    fbl::AutoLock lock(&local_clock_mutex_);

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
