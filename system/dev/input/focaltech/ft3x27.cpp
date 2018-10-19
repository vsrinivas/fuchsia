// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/i2c-lib.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/type_support.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>
#include <zircon/compiler.h>

#include <stdio.h>
#include <string.h>

#include "ft3x27.h"

namespace ft {

Ft3x27Device::Ft3x27Device(zx_device_t* device)
    : ddk::Device<Ft3x27Device, ddk::Unbindable>(device) {
}

void Ft3x27Device::ParseReport(ft3x27_finger_t* rpt, uint8_t* buf) {
    rpt->x = static_cast<uint16_t>(((buf[0] & 0x0f) << 8) + buf[1]);
    rpt->y = static_cast<uint16_t>(((buf[2] & 0x0f) << 8) + buf[3]);
    rpt->finger_id = static_cast<uint8_t>(
        ((buf[2] >> 2) & FT3X27_FINGER_ID_CONTACT_MASK) |
        (((buf[0] & 0xC0) == 0x80) ? 1 : 0));
}

int Ft3x27Device::Thread() {
    zx_status_t status;
    zxlogf(INFO, "ft3x27: entering irq thread\n");
    while (true) {
        status = irq_.wait(nullptr);
        if (!running_.load()) {
            return ZX_OK;
        }
        if (status != ZX_OK) {
            zxlogf(ERROR, "ft3x27: Interrupt error %d\n", status);
        }
        uint8_t i2c_buf[kMaxPoints * kFingerRptSize + 1];
        status = Read(FTS_REG_CURPOINT, i2c_buf, kMaxPoints * kFingerRptSize + 1);
        if (status == ZX_OK) {
            fbl::AutoLock lock(&proxy_lock_);
            ft_rpt_.rpt_id = FT3X27_RPT_ID_TOUCH;
            ft_rpt_.contact_count = i2c_buf[0];
            for (uint i = 0; i < kMaxPoints; i++) {
                ParseReport(&ft_rpt_.fingers[i], &i2c_buf[i * kFingerRptSize + 1]);
            }
            if (proxy_.is_valid()) {
                proxy_.IoQueue(reinterpret_cast<uint8_t*>(&ft_rpt_), sizeof(ft3x27_touch_t));
            }
        } else {
            zxlogf(ERROR, "ft3x27: i2c read error\n");
        }
    }
    zxlogf(INFO, "ft3x27: exiting\n");
}

zx_status_t Ft3x27Device::InitPdev() {
    pdev_protocol_t pdev;

    zx_status_t status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ft3x27: failed to acquire pdev\n");
        return status;
    }

    status = device_get_protocol(parent_, ZX_PROTOCOL_I2C, &i2c_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ft3x27: failed to acquire i2c\n");
        return status;
    }

    for (uint32_t i = 0; i < FT_PIN_COUNT; i++) {
        size_t actual;
        status = pdev_get_protocol(&pdev, ZX_PROTOCOL_GPIO, i, &gpios_[i], sizeof(gpios_[i]),
                                   &actual);
        if (status != ZX_OK) {
            return status;
        }
    }

    gpio_config_in(&gpios_[FT_INT_PIN], GPIO_NO_PULL);

    status = gpio_get_interrupt(&gpios_[FT_INT_PIN],
                                ZX_INTERRUPT_MODE_EDGE_LOW,
                                irq_.reset_and_get_address());
    if (status != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

zx_status_t Ft3x27Device::Create(zx_device_t* device) {

    zxlogf(INFO, "ft3x27: driver started...\n");

    auto ft_dev = fbl::make_unique<Ft3x27Device>(device);
    zx_status_t status = ft_dev->InitPdev();
    if (status != ZX_OK) {
        zxlogf(ERROR, "ft3x27: Driver bind failed %d\n", status);
        return status;
    }

    auto thunk = [](void* arg) -> int {
        return reinterpret_cast<Ft3x27Device*>(arg)->Thread();
    };

    auto cleanup = fbl::MakeAutoCall([&]() { ft_dev->ShutDown(); });

    ft_dev->running_.store(true);
    int ret = thrd_create_with_name(&ft_dev->thread_, thunk,
                                    reinterpret_cast<void*>(ft_dev.get()),
                                    "ft3x27-thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);

    status = ft_dev->DdkAdd("ft3x27 HidDevice\n");
    if (status != ZX_OK) {
        zxlogf(ERROR, "ft3x27: Could not create hid device: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "ft3x27: Added hid device\n");
    }

    cleanup.cancel();

    // device intentionally leaked as it is now held by DevMgr
    __UNUSED auto ptr = ft_dev.release();

    return ZX_OK;
}

zx_status_t Ft3x27Device::HidbusQuery(uint32_t options, hid_info_t* info) {
    if (!info) {
        return ZX_ERR_INVALID_ARGS;
    }
    info->dev_num = 0;
    info->device_class = HID_DEVICE_CLASS_OTHER;
    info->boot_device = false;

    return ZX_OK;
}

void Ft3x27Device::DdkRelease() {
    delete this;
}

void Ft3x27Device::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

zx_status_t Ft3x27Device::ShutDown() {
    running_.store(false);
    irq_.destroy();
    thrd_join(thread_, NULL);
    {
        fbl::AutoLock lock(&proxy_lock_);
        //proxy_.clear();
    }
    return ZX_OK;
}

zx_status_t Ft3x27Device::HidbusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {

    const uint8_t* desc_ptr;
    uint8_t* buf;
    *len = get_ft3x27_report_desc(&desc_ptr);
    fbl::AllocChecker ac;
    buf = new (&ac) uint8_t[*len];
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(buf, desc_ptr, *len);
    *data = buf;
    return ZX_OK;
}

zx_status_t Ft3x27Device::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                          size_t len, size_t* out_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ft3x27Device::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data,
                                          size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ft3x27Device::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ft3x27Device::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ft3x27Device::HidbusGetProtocol(uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Ft3x27Device::HidbusSetProtocol(uint8_t protocol) {
    return ZX_OK;
}

void Ft3x27Device::HidbusStop() {
    fbl::AutoLock lock(&proxy_lock_);
    proxy_.clear();
}

zx_status_t Ft3x27Device::HidbusStart(const hidbus_ifc_t* ifc) {
    fbl::AutoLock lock(&proxy_lock_);
    if (proxy_.is_valid()) {
        zxlogf(ERROR, "ft3x27: Already bound!\n");
        return ZX_ERR_ALREADY_BOUND;
    } else {
        ddk::HidbusIfcProxy proxy(ifc);
        proxy_ = proxy;
        zxlogf(INFO, "ft3x27: started\n");
    }
    return ZX_OK;
}

// simple i2c read for reading one register location
//  intended mostly for debug purposes
uint8_t Ft3x27Device::Read(uint8_t addr) {
    uint8_t rbuf;
    i2c_write_read_sync(&i2c_, &addr, 1, &rbuf, 1);
    return rbuf;
}

zx_status_t Ft3x27Device::Read(uint8_t addr, uint8_t* buf, uint8_t len) {

    zx_status_t status = i2c_write_read_sync(&i2c_, &addr, 1, buf, len);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to read i2c - %d\n", status);
        return status;
    }
    return ZX_OK;
}
} //namespace ft

extern "C" zx_status_t ft3x27_bind(void* ctx, zx_device_t* device, void** cookie) {
    return ft::Ft3x27Device::Create(device);
}
