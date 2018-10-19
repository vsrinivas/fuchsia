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

zx_status_t Mt8167GpioDevice::GpioImplConfigIn(uint32_t index, uint32_t flags) {
    GpioModeReg::SetMode(&mmio_, index, GpioModeReg::kModeGpio);
    const uint32_t pull_mode = flags & GPIO_PULL_MASK;
    zx_status_t status;
    switch(pull_mode) {
    case GPIO_NO_PULL:
        status = pull_en_.Enable(index, false);
        if (status != ZX_OK) {
            return status;
        }
        break;
    case GPIO_PULL_UP:
        status = pull_en_.Enable(index, true);
        if (status != ZX_OK) {
            return status;
        }
        pull_sel_.SetUp(index, true);
        break;
    case GPIO_PULL_DOWN:
        status = pull_en_.Enable(index, true);
        if (status != ZX_OK) {
            return status;
        }
        pull_sel_.SetUp(index, false);
        break;
    }
    dir_.SetDir(index, false);
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
    GpioModeReg::SetMode(&mmio_, index, GpioModeReg::kModeGpio);
    dir_.SetDir(index, true);
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
    if (function >= GpioModeReg::kModeMax) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    GpioModeReg::SetMode(&mmio_, index, static_cast<uint16_t>(function));
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplRead(uint32_t index, uint8_t* out_value) {
    *out_value = static_cast<uint8_t>(in_.GetVal(index));
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplWrite(uint32_t index, uint8_t value) {
    out_.SetVal(index, value);
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
}

void Mt8167GpioDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void Mt8167GpioDevice::DdkRelease() {
    delete this;
}

zx_status_t Mt8167GpioDevice::Bind() {
    auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

    gpio_impl_protocol_t gpio_proto = {
        .ops = &ops_,
        .ctx = this,
    };

    pbus_protocol_t pbus;
    zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d\n", __FUNCTION__, status);
        return status;
    }

    const platform_proxy_cb_t kCallback = {NULL, NULL};
    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto),
                                    &kCallback);
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

zx_status_t Mt8167GpioDevice::Create(zx_device_t* parent) {
    platform_device_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PLATFORM_DEV not available %d \n", __FUNCTION__, status);
        return status;
    }

    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_map_mmio_buffer2 gpio failed %d\n", __FUNCTION__, status);
        return status;
    }

    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<gpio::Mt8167GpioDevice>(&ac, parent, mmio);
    if (!ac.check()) {
        zxlogf(ERROR, "mt8167_gpio_bind: ZX_ERR_NO_MEMORY\n");
        return ZX_ERR_NO_MEMORY;
    }
    status = dev->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

} // namespace gpio

extern "C" zx_status_t mt8167_gpio_bind(void* ctx, zx_device_t* parent) {
    return gpio::Mt8167GpioDevice::Create(parent);
}
