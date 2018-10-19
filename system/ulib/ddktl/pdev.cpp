// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/debug.h>
#include <ddktl/pdev.h>
#include <fbl/alloc_checker.h>
#include <zircon/assert.h>

namespace ddk {

void Pdev::ShowInfo() {
    zxlogf(INFO, "VID:PID:DID         = %04x:%04x:%04x\n", pdev_info_.vid,
           pdev_info_.pid,
           pdev_info_.did);
    zxlogf(INFO, "mmio count          = %d\n", pdev_info_.mmio_count);
    zxlogf(INFO, "irq count           = %d\n", pdev_info_.irq_count);
    zxlogf(INFO, "gpio count          = %d\n", pdev_info_.gpio_count);
    zxlogf(INFO, "i2c channel count   = %d\n", pdev_info_.i2c_channel_count);
    zxlogf(INFO, "clk count           = %d\n", pdev_info_.clk_count);
    zxlogf(INFO, "bti count           = %d\n", pdev_info_.bti_count);
}

zx_status_t Pdev::GetMmio(uint32_t index, fbl::optional<MmioBuffer>* mmio) {
    pdev_mmio_t pdev_mmio;

    zx_status_t status = pdev_get_mmio(&pdev_, index, &pdev_mmio);
    if (status != ZX_OK) {
        return status;
    }
    return MmioBuffer::Create(pdev_mmio.offset, pdev_mmio.size, zx::vmo(pdev_mmio.vmo),
                              ZX_CACHE_POLICY_UNCACHED_DEVICE, mmio);
}

I2cChannel Pdev::GetI2cChan(uint32_t index) {
    if (index >= pdev_info_.i2c_channel_count) {
        return I2cChannel();
    }

    i2c_protocol_t i2c;
    //Note: Pdev is a friend class of I2cChannel
    size_t actual;
    zx_status_t res = pdev_get_protocol(&pdev_, ZX_PROTOCOL_I2C, index, &i2c, sizeof(i2c), &actual);
    if (res != ZX_OK || actual != sizeof(i2c)) {
        return I2cChannel();
    }
    return I2cChannel(index, i2c);
}

GpioPin Pdev::GetGpio(uint32_t index) {
    if (index >= pdev_info_.gpio_count) {
        return GpioPin();
    }

    gpio_protocol_t gpio;
    //Note: Pdev is a friend class of GpioPin
    size_t actual;
    zx_status_t res = pdev_get_protocol(&pdev_, ZX_PROTOCOL_GPIO, index, &gpio, sizeof(gpio),
                                        &actual);
    if (res != ZX_OK || actual != sizeof(gpio)) {
        return GpioPin();
    }
    return GpioPin(gpio);
}

fbl::RefPtr<Pdev> Pdev::Create(zx_device_t* parent) {

    fbl::AllocChecker ac;

    auto pdev = fbl::AdoptRef(new (&ac) Pdev(parent));
    if (!ac.check()) {
        return nullptr;
    }

    zx_status_t status = device_get_protocol(parent,
                                             ZX_PROTOCOL_PDEV,
                                             &pdev->pdev_);
    if (status != ZX_OK) {
        return nullptr;
    }

    status = pdev_get_device_info(&pdev->pdev_, &pdev->pdev_info_);
    if (status != ZX_OK) {
        return nullptr;
    }

    return fbl::move(pdev);
}

} // namespace ddk
