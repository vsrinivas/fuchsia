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

#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include "mt8167-gpio-regs.h"
#include "mt8167-gpio.h"

namespace gpio {

int Mt8167GpioDevice::Thread() {
    while (1) {
        zx_port_packet_t packet;
        zx_status_t status = port_.wait(zx::time::infinite(), &packet);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s port wait failed: %d\n", __FUNCTION__, status);
            return thrd_error;
        }
        uint32_t index = eint_.GetNextInterrupt(0);
        while (index != ExtendedInterruptReg::kInvalidInterruptIdx &&
               index < interrupts_.size() &&
               interrupts_[index].is_valid()) {
            zxlogf(TRACE, "%s msg on port key %lu  EINT %u\n", __FUNCTION__, packet.key, index);
            if (eint_.IsEnabled(index)) {
                zxlogf(TRACE, "%s zx_interrupt_trigger for %u\n", __FUNCTION__, index);
                status = interrupts_[index].trigger(0, zx::time(packet.interrupt.timestamp));
                if (status != ZX_OK) {
                    zxlogf(ERROR, "%s zx_interrupt_trigger failed %d \n", __FUNCTION__, status);
                }
            }
            eint_.AckInterrupt(index);
            index = eint_.GetNextInterrupt(index + 1);
        }
        int_.ack();
    }
}

zx_status_t Mt8167GpioDevice::GpioImplConfigIn(uint32_t index, uint32_t flags) {
    if (index >= interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    GpioModeReg::SetMode(&gpio_mmio_, index, GpioModeReg::kModeGpio);
    dir_.SetDir(index, false);
    const uint32_t pull_mode = flags & GPIO_PULL_MASK;

    switch (pull_mode) {
    case GPIO_NO_PULL:
        if (pull_en_.PullDisable(index)) {
            return ZX_OK;
        }
        break;
    case GPIO_PULL_UP:
        if (pull_en_.PullEnable(index) && pull_sel_.SetPullUp(index)) {
            return ZX_OK;
        }
        break;
    case GPIO_PULL_DOWN:
        if (pull_en_.PullEnable(index) && pull_sel_.SetPullDown(index)) {
            return ZX_OK;
        }
        break;
    }

    // If not supported above, try IO Config.
    switch (pull_mode) {
    case GPIO_NO_PULL:
        if (iocfg_.PullDisable(index)) {
            return ZX_OK;
        }
        break;
    case GPIO_PULL_UP:
        if (iocfg_.PullEnable(index) && iocfg_.SetPullUp(index)) {
            return ZX_OK;
        }
        break;
    case GPIO_PULL_DOWN:
        if (iocfg_.PullEnable(index) && iocfg_.SetPullDown(index)) {
            return ZX_OK;
        }
        break;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Mt8167GpioDevice::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
    if (index >= interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    GpioModeReg::SetMode(&gpio_mmio_, index, GpioModeReg::kModeGpio);
    dir_.SetDir(index, true);
    return GpioImplWrite(index, initial_value);
}

zx_status_t Mt8167GpioDevice::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
    if (index >= interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (function >= GpioModeReg::kModeMax) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    GpioModeReg::SetMode(&gpio_mmio_, index, static_cast<uint16_t>(function));
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplRead(uint32_t index, uint8_t* out_value) {
    if (index >= interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    *out_value = static_cast<uint8_t>(in_.GetVal(index));
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplWrite(uint32_t index, uint8_t value) {
    if (index >= interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    out_.SetVal(index, value);
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                                   zx_handle_t* out_handle) {
    zx_status_t status;
    if (index >= interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (eint_.IsEnabled(index)) {
        zxlogf(ERROR, "%s interrupt %u already exists\n", __FUNCTION__, index);
        return ZX_ERR_ALREADY_EXISTS;
    }

    zx_handle_t handle;
    status = zx_interrupt_create(0, index, ZX_INTERRUPT_VIRTUAL, &handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s zx_interrupt_create failed %d \n", __FUNCTION__, status);
        return status;
    }
    status = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, out_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s zx_handle_duplicate failed %d \n", __FUNCTION__, status);
        return status;
    }

    switch (flags & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_EDGE_LOW:
        eint_.SetPolarity(index, false);
        eint_.SetEdge(index, true);
        break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
        eint_.SetPolarity(index, true);
        eint_.SetEdge(index, true);
        break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
        eint_.SetPolarity(index, false);
        eint_.SetEdge(index, false);
        break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
        eint_.SetPolarity(index, true);
        eint_.SetEdge(index, false);
        break;
    case ZX_INTERRUPT_MODE_EDGE_BOTH:
        return ZX_ERR_NOT_SUPPORTED;
    default:
        return ZX_ERR_INVALID_ARGS;
    }
    interrupts_[index] = zx::interrupt(handle);
    eint_.Enable(index);
    zxlogf(TRACE, "%s EINT %u enabled\n", __FUNCTION__, index);
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplReleaseInterrupt(uint32_t index) {
    if (index >= interrupts_.size() || !eint_.IsEnabled(index)) {
        return ZX_ERR_INVALID_ARGS;
    }
    eint_.Disable(index);
    interrupts_[index].destroy();
    interrupts_[index].reset();
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplSetPolarity(uint32_t index, uint32_t polarity) {
    if (index >= interrupts_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (polarity == GPIO_POLARITY_LOW) {
        eint_.SetPolarity(index, false);
        return ZX_OK;
    } else if (polarity == GPIO_POLARITY_HIGH) {
        eint_.SetPolarity(index, true);
        return ZX_OK;
    }
    return ZX_ERR_INVALID_ARGS;
}

void Mt8167GpioDevice::ShutDown() {
    int_.destroy();
    thrd_join(thread_, NULL);
}

void Mt8167GpioDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void Mt8167GpioDevice::DdkRelease() {
    delete this;
}

zx_status_t Mt8167GpioDevice::Bind() {
    pdev_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s ZX_PROTOCOL_PDEV not available %d \n", __FUNCTION__, status);
        return status;
    }

    status = pdev_map_interrupt(&pdev, 0, int_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pdev_map_interrupt failed %d\n", __FUNCTION__, status);
        return status;
    }

    status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s zx_port_create failed %d\n", __FUNCTION__, status);
        return status;
    }

    status = int_.bind(port_.get(), 0, 0 /*options*/);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s zx_interrupt_bind failed %d\n", __FUNCTION__, status);
        return status;
    }
    fbl::AllocChecker ac;
    interrupts_ = fbl::Array(new (&ac) zx::interrupt[MT8167_GPIO_EINT_MAX], MT8167_GPIO_EINT_MAX);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    for (uint32_t i = 0; i < interrupts_.size(); ++i) {
        eint_.SetDomain0(i);
        eint_.Disable(i);
    }

    auto cb = [](void* arg) -> int { return reinterpret_cast<Mt8167GpioDevice*>(arg)->Thread(); };
    int rc = thrd_create_with_name(&thread_, cb, this, "mt8167-gpio-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    status = DdkAdd("mt8167-gpio");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAdd failed %d\n", __FUNCTION__, status);
        ShutDown();
        return status;
    }
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::Init() {
    zx_status_t status;
    pbus_protocol_t pbus;
    status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d\n", __FUNCTION__, status);
        return status;
    }
    gpio_impl_protocol_t gpio_proto = {
        .ops = &ops_,
        .ctx = this,
    };
    const platform_proxy_cb_t kCallback = {nullptr, nullptr};
    status = pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto),
                                    &kCallback);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pbus_register_protocol failed %d\n", __FUNCTION__, status);
        ShutDown();
        return status;
    }
    return ZX_OK;
}

zx_status_t Mt8167GpioDevice::Create(zx_device_t* parent) {
    pdev_protocol_t pdev;
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s ZX_PROTOCOL_PDEV not available %d \n", __FUNCTION__, status);
        return status;
    }

    mmio_buffer_t gpio_mmio;
    status = pdev_map_mmio_buffer2(&pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpio_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s gpio pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }

    mmio_buffer_t iocfg_mmio;
    status = pdev_map_mmio_buffer2(&pdev, 1, ZX_CACHE_POLICY_UNCACHED_DEVICE, &iocfg_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s iocfg pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }

    mmio_buffer_t eint_mmio;
    status = pdev_map_mmio_buffer2(&pdev, 2, ZX_CACHE_POLICY_UNCACHED_DEVICE, &eint_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s eint pdev_map_mmio_buffer2 failed %d\n", __FUNCTION__, status);
        return status;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pdev_get_device_info failed %d\n", __FUNCTION__, status);
        return status;
    }

    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<gpio::Mt8167GpioDevice>(&ac, parent, gpio_mmio, iocfg_mmio,
                                                                eint_mmio);
    if (!ac.check()) {
        zxlogf(ERROR, "mt8167_gpio_bind: ZX_ERR_NO_MEMORY\n");
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

} // namespace gpio

extern "C" zx_status_t mt8167_gpio_bind(void* ctx, zx_device_t* parent) {
    return gpio::Mt8167GpioDevice::Create(parent);
}
