// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arm-isp.h"
#include "arm-isp-regs.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <memory>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/types.h>

namespace camera {

namespace {

constexpr uint32_t kHiu = 0;
constexpr uint32_t kPowerDomain = 1;
constexpr uint32_t kMemoryDomain = 2;
constexpr uint32_t kReset = 3;

} // namespace

void ArmIspDevice::IspHWReset(bool reset) {
    if (reset) {
        reset_mmio_->ClearBits32(1 << 1, RESET4_LEVEL);
    } else {
        reset_mmio_->SetBits32(1 << 1, RESET4_LEVEL);
    }
    // Reference code has a sleep in this path.
    // TODO(braval@) Double check to look into if
    // this sleep is really necessary.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
}

void ArmIspDevice::PowerUpIsp() {
    // set bit[18-19]=0
    power_mmio_->ClearBits32(1 << 18 | 1 << 19, AO_RTI_GEN_PWR_SLEEP0);
    // TODO(braval@) Double check to look into if
    // this sleep is really necessary.
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));

    // set bit[18-19]=0
    power_mmio_->ClearBits32(1 << 18 | 1 << 19, AO_RTI_GEN_PWR_ISO0);

    // MEM_PD_REG0 set 0
    memory_pd_mmio_->Write32(0, HHI_ISP_MEM_PD_REG0);
    // MEM_PD_REG1 set 0
    memory_pd_mmio_->Write32(0, HHI_ISP_MEM_PD_REG1);

    // Refer to reference source code
    hiu_mmio_->Write32(0x5b446585, HHI_CSI_PHY_CNTL0);
    hiu_mmio_->Write32(0x803f4321, HHI_CSI_PHY_CNTL1);
}

// Interrupt handler for the ISP.
int ArmIspDevice::IspIrqHandler() {
    zxlogf(INFO, "%s start\n", __func__);
    zx_status_t status = ZX_OK;

    while (running_.load()) {
        status = isp_irq_.wait(NULL);
        if (status != ZX_OK) {
            return status;
        }

        // Handle the Interrupt here.
    }
    return status;
}

zx_status_t ArmIspDevice::InitIsp() {
    // The ISP and MIPI module is in same power domain.
    // So if we don't call the power sequence of ISP, the mipi module
    // won't work and it will block accesses to the  mipi register block.
    PowerUpIsp();

    IspHWReset(true);

    // Start ISP Interrupt Handling Thread.
    auto start_thread = [](void* arg) -> int {
        return static_cast<ArmIspDevice*>(arg)->IspIrqHandler();
    };

    running_.store(true);
    int rc = thrd_create_with_name(&irq_thread_,
                                   start_thread,
                                   this,
                                   "isp_irq_thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    IspHWReset(false);

    return ZX_OK;
}

zx_status_t ArmIspDevice::InitPdev(zx_device_t* parent) {
    if (!pdev_.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    zx_status_t status = pdev_.MapMmio(kHiu, &hiu_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    status = pdev_.MapMmio(kPowerDomain, &power_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    status = pdev_.MapMmio(kMemoryDomain, &memory_pd_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    status = pdev_.MapMmio(kReset, &reset_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.MapMmio failed %d\n", __func__, status);
        return status;
    }

    status = pdev_.GetInterrupt(0, &isp_irq_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_.GetInterrupt failed %d\n", __func__, status);
        return status;
    }

    return status;
}

// static
zx_status_t ArmIspDevice::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto isp_device = std::unique_ptr<ArmIspDevice>(new (&ac) ArmIspDevice(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = isp_device->InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }

    status = isp_device->DdkAdd("arm-isp");
    if (status != ZX_OK) {
        zxlogf(ERROR, "arm-isp: Could not create arm-isp device: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "arm-isp: Added arm-isp device\n");
    }

    // isp_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = isp_device.release();

    return status;
}

ArmIspDevice::~ArmIspDevice() {
    running_.store(false);
    thrd_join(irq_thread_, NULL);
    isp_irq_.destroy();
}

void ArmIspDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void ArmIspDevice::DdkRelease() {
    delete this;
}

void ArmIspDevice::ShutDown() {
}

} // namespace camera
