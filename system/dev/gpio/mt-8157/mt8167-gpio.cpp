// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio-impl.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>

#include <hw/reg.h>

#include <soc/mt8167/mt8167-hw.h>

#include <zircon/types.h>

#include "mt8167-gpio-regs.h"
#include "mt8167-gpio.h"

namespace gpio {

// TODO(andresoportus): pull-up/down (EN, SEL) registers

zx_status_t Mt8167GpioDevice::GpioImplConfigIn(uint32_t index, uint32_t flags) {
    GpioDirReg::SetDir(mmio_.vaddr, index, false);
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
    GpioDirReg::SetDir(mmio_.vaddr, index, true);
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
    if (function >= GpioModeReg::GetModeMax()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    GpioModeReg::SetMode(mmio_.vaddr, index, static_cast<uint16_t>(function));
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplRead(uint32_t index, uint8_t* out_value) {
    *out_value = GpioInReg::GetVal(mmio_.vaddr, index);
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplWrite(uint32_t index, uint8_t value) {
    GpioOutReg::SetVal(mmio_.vaddr, index, static_cast<bool>(value));
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                                   zx_handle_t* out_handle) {
    // TODO(andresoportus): Implement
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Mt8167GpioDevice::GpioImplReleaseInterrupt(uint32_t index) {
    // TODO(andresoportus): Implement
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Mt8167GpioDevice::GpioImplSetPolarity(uint32_t index, uint32_t polarity) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Mt8167GpioDevice::ShutDown() {
    mmio_buffer_release(&mmio_);
}

void Mt8167GpioDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void Mt8167GpioDevice::DdkRelease() {
    delete this;
}

zx_status_t Mt8167GpioDevice::Bind() {
    zx_status_t status;

    status = device_get_protocol(parent(), ZX_PROTOCOL_PLATFORM_DEV, &pdev_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_DEV not available %d \n", __FUNCTION__, status);
        return status;
    }

    status = device_get_protocol(parent(), ZX_PROTOCOL_PLATFORM_BUS, &pbus_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_BUS not available %d\n", __FUNCTION__, status);
        return status;
    }

    status = pdev_map_mmio_buffer2(&pdev_, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                   &mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 gpio failed %d\n", __FUNCTION__, status);
        return status;
    }

    auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

    gpio_impl_protocol_t gpio_proto = {
        .ops = &ops_,
        .ctx = this,
    };
    status = pbus_register_protocol(&pbus_, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, NULL, NULL);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_register_protocol failed %d\n", __FUNCTION__, status);
        return status;
    }

    status = DdkAdd("mt8167-gpio");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed %d\n", __FUNCTION__, status);
        return status;
    }
    cleanup.cancel();
    return ZX_OK;
}

} // namespace gpio

extern "C" zx_status_t mt8167_gpio_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<gpio::Mt8167GpioDevice>(&ac, parent);
    if (!ac.check()) {
        zxlogf(ERROR, "mt8167_gpio_bind: ZX_ERR_NO_MEMORY\n");
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = dev->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
