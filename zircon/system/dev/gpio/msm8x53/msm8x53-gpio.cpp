// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <soc/msm8x53/msm8x53-hw.h>

#include "msm8x53-gpio.h"

namespace gpio {

zx_status_t Msm8x53GpioDevice::GpioImplConfigIn(uint32_t index, uint32_t flags) {
    if (index >= kMsm9x53GpioMax) {
        return ZX_ERR_INVALID_ARGS;
    }
    GpioCfgReg::SetMode(&gpio_mmio_, index, GpioCfgReg::kModeGpio);
    GpioCfgReg::SetOut(&gpio_mmio_, index, false);
    const uint32_t pull_mode = flags & GPIO_PULL_MASK;

    // clang-format off
    switch (pull_mode) {
    case GPIO_NO_PULL:   GpioCfgReg::SetPullNone(&gpio_mmio_, index); break;
    case GPIO_PULL_DOWN: GpioCfgReg::SetPullDown(&gpio_mmio_, index); break;
    case GPIO_PULL_UP:   GpioCfgReg::SetPullUp  (&gpio_mmio_, index); break;
    default: return ZX_ERR_NOT_SUPPORTED;
    }
    // clang-format on
    return ZX_OK;
}

zx_status_t Msm8x53GpioDevice::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
    if (index >= kMsm9x53GpioMax) {
        return ZX_ERR_INVALID_ARGS;
    }
    GpioCfgReg::SetMode(&gpio_mmio_, index, GpioCfgReg::kModeGpio);
    GpioCfgReg::SetOut(&gpio_mmio_, index, true);
    return GpioImplWrite(index, initial_value);
}

zx_status_t Msm8x53GpioDevice::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
    if (index >= kMsm9x53GpioMax) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (function >= GpioCfgReg::kModeMax) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    GpioCfgReg::SetMode(&gpio_mmio_, index, static_cast<uint32_t>(function));
    return ZX_OK;
}

zx_status_t Msm8x53GpioDevice::GpioImplRead(uint32_t index, uint8_t* out_value) {
    if (index >= kMsm9x53GpioMax) {
        return ZX_ERR_INVALID_ARGS;
    }
    *out_value = static_cast<uint8_t>(in_.GetVal(index));
    return ZX_OK;
}

zx_status_t Msm8x53GpioDevice::GpioImplWrite(uint32_t index, uint8_t value) {
    if (index >= kMsm9x53GpioMax) {
        return ZX_ERR_INVALID_ARGS;
    }
    out_.SetVal(index, value);
    return ZX_OK;
}

zx_status_t Msm8x53GpioDevice::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                                    zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53GpioDevice::GpioImplReleaseInterrupt(uint32_t index) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Msm8x53GpioDevice::GpioImplSetPolarity(uint32_t index, uint32_t polarity) {
    return ZX_ERR_NOT_SUPPORTED;
}

void Msm8x53GpioDevice::ShutDown() {
}

void Msm8x53GpioDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void Msm8x53GpioDevice::DdkRelease() {
    delete this;
}

zx_status_t Msm8x53GpioDevice::Bind() {
    auto status = DdkAdd("msm8x53-gpio");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAdd failed %d\n", __func__, status);
        ShutDown();
        return status;
    }
    return ZX_OK;
}

zx_status_t Msm8x53GpioDevice::Init() {
    pbus_protocol_t pbus;
    auto status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d\n", __func__, status);
        return status;
    }
    gpio_impl_protocol_t gpio_proto = {
        .ops = &gpio_impl_protocol_ops_,
        .ctx = this,
    };
    const platform_proxy_cb_t kCallback = {nullptr, nullptr};
    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto),
                                    &kCallback);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_register_protocol failed %d\n", __func__, status);
        ShutDown();
        return status;
    }
    return ZX_OK;
}

zx_status_t Msm8x53GpioDevice::Create(zx_device_t* parent) {
    pdev_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s ZX_PROTOCOL_PDEV not available %d \n", __func__, status);
        return status;
    }

    mmio_buffer_t gpio_mmio;
    status = pdev_map_mmio_buffer(&pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpio_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s gpio pdev_map_mmio_buffer failed %d\n", __func__, status);
        return status;
    }

    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<gpio::Msm8x53GpioDevice>(&ac, parent, gpio_mmio);
    if (!ac.check()) {
        zxlogf(ERROR, "msm8x53_gpio_bind: ZX_ERR_NO_MEMORY\n");
        return ZX_ERR_NO_MEMORY;
    }
    status = dev->Bind();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the memory for dev
    auto ptr = dev.release();
    return ptr->Init();
}

zx_status_t msm8x53_gpio_bind(void* ctx, zx_device_t* parent) {
    return gpio::Msm8x53GpioDevice::Create(parent);
}

} // namespace gpio
