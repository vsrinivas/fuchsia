// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gdc.h"
#include "gdc-regs.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <memory>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

namespace gdc {

namespace {

constexpr uint32_t kHiu = 0;
constexpr uint32_t kGdc = 1;

} // namespace

void GdcDevice::InitClocks() {
    // First reset the clocks.
    GDC_CLK_CNTL::Get()
        .ReadFrom(&clock_mmio_)
        .reset_axi()
        .reset_core()
        .WriteTo(&clock_mmio_);

    // Set the clocks to 8Mhz
    // Source XTAL
    // Clock divisor = 3
    GDC_CLK_CNTL::Get()
        .ReadFrom(&clock_mmio_)
        .set_axi_clk_div(3)
        .set_axi_clk_en(1)
        .set_axi_clk_sel(0)
        .set_core_clk_div(3)
        .set_core_clk_en(1)
        .set_core_clk_sel(0)
        .WriteTo(&clock_mmio_);

    // Enable GDC Power domain.
    GDC_MEM_POWER_DOMAIN::Get()
        .ReadFrom(&clock_mmio_)
        .set_gdc_pd(0)
        .WriteTo(&clock_mmio_);
}

zx_status_t GdcDevice::GdcInitTask(const buffer_collection_info_t* input_buffer_collection,
                                   const buffer_collection_info_t* output_buffer_collection,
                                   zx::vmo config_vmo,
                                   const gdc_callback_t* callback,
                                   uint32_t* out_task_index) {
    // TODO(braval): Implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t GdcDevice::GdcProcessFrame(uint32_t task_index, uint32_t input_buffer_index) {
    // TODO(braval): Implement this.
    return ZX_ERR_NOT_SUPPORTED;
}

void GdcDevice::GdcRemoveTask(uint32_t task_index) {
    // TODO(braval): Implement this.
}

void GdcDevice::GdcReleaseFrame(uint32_t task_index, uint32_t buffer_index) {
    // TODO(braval): Implement this.
}

// static
zx_status_t GdcDevice::Setup(void* ctx,
                             zx_device_t* parent,
                             std::unique_ptr<GdcDevice>* out) {

    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    std::optional<ddk::MmioBuffer> clk_mmio;
    zx_status_t status = pdev.MapMmio(kHiu, &clk_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    std::optional<ddk::MmioBuffer> gdc_mmio;
    status = pdev.MapMmio(kGdc, &gdc_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    zx::interrupt gdc_irq;
    status = pdev.GetInterrupt(0, &gdc_irq);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.GetInterrupt failed %d\n", __func__, status);
        return status;
    }

    zx::bti bti;
    status = pdev.GetBti(0, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not obtain bti: %d\n", __func__, status);
        return status;
    }

    fbl::AllocChecker ac;
    auto gdc_device = std::unique_ptr<GdcDevice>(new (&ac) GdcDevice(parent,
                                                                     std::move(*clk_mmio),
                                                                     std::move(*gdc_mmio),
                                                                     std::move(gdc_irq),
                                                                     std::move(bti)));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    gdc_device->InitClocks();

    *out = std::move(gdc_device);
    return status;
}

GdcDevice::~GdcDevice() {
    running_.store(false);
    thrd_join(irq_thread_, NULL);
    gdc_irq_.destroy();
}

void GdcDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void GdcDevice::DdkRelease() {
    delete this;
}

void GdcDevice::ShutDown() {
}

zx_status_t GdcBind(void* ctx, zx_device_t* device) {
    std::unique_ptr<GdcDevice> gdc_device;
    zx_status_t status = gdc::GdcDevice::Setup(ctx, device, &gdc_device);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Could not setup gdc device: %d\n", __func__, status);
        return status;
    }
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_PROTO, 0, ZX_PROTOCOL_GDC},
    };

    // Run the unit tests for this device
    // TODO(braval): CAM-44 (Run only when build flag enabled)
    // This needs to be replaced with run unittests hooks when
    // the framework is available.
    #if 0
    status = gdc::GdcDeviceTester::RunTests(gdc_device.get());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Device Unit Tests Failed \n", __func__);
        return status;
    }
    #endif

    status = gdc_device->DdkAdd("gdc", 0, props, countof(props));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Could not add gdc device: %d\n", __func__, status);
        return status;
    } else {
        zxlogf(INFO, "%s: gdc driver added\n", __func__);
    }

    // gdc device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto* dev = gdc_device.release();
    return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = GdcBind;
    return ops;
}();

} // namespace gdc

// clang-format off
ZIRCON_DRIVER_BEGIN(gdc, gdc::driver_ops, "gdc", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_ARM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GDC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ARM_MALI_IV010),
ZIRCON_DRIVER_END(gdc)
