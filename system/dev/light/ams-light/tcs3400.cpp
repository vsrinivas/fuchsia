// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include "tcs3400-regs.h"
#include "tcs3400.h"

namespace tcs {

zx_status_t Tcs3400Device::FillRpt() {
    struct Regs {
        uint16_t* out;
        uint8_t reg_h;
        uint8_t reg_l;
    } regs[] = {
        {&tcs_rpt_.illuminance, TCS_I2C_CDATAH, TCS_I2C_CDATAL}
        // TODO(andresoportus): Instead of raw clear value, aproximate Lux from RGBC?
    };
    for (auto i : regs) {
        uint8_t buf_h, buf_l;
        zx_status_t status;
        // Read lower byte first, the device holds upper byte of a sample in a shadow register after
        // a lower byte read
        status = i2c_transact_sync(&i2c_, kI2cIndex, &i.reg_l, 1, &buf_l, 1);
        if (status != ZX_OK) {
            zxlogf(ERROR, "Tcs3400Device::I2cRead: i2c_transact_sync failed: %d\n", status);
            return status;
        }
        status = i2c_transact_sync(&i2c_, kI2cIndex, &i.reg_h, 1, &buf_h, 1);
        if (status != ZX_OK) {
            zxlogf(ERROR, "Tcs3400Device::I2cRead: i2c_transact_sync failed: %d\n", status);
            return status;
        }
        *i.out = static_cast<uint16_t>(((buf_h & 0xFF) << 8) | (buf_l & 0xFF));
    }
    return ZX_OK;
}

int Tcs3400Device::Thread() {
    uint8_t cmd[] = {TCS_I2C_ENABLE, TCS_I2C_ENABLE_POWER_ON | TCS_I2C_ENABLE_ADC_ENABLE};
    zx_status_t status = i2c_transact_sync(&i2c_, kI2cIndex, &cmd, sizeof(cmd), NULL, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::Thread: i2c_transact_sync failed: %d\n", status);
        return thrd_error;
    }

    // TODO(andresoportus): Interrupt support pending on interface with upper layers via HID
    while (1) {
        if (!running_.load()) {
            return thrd_success;
        }
        {
            fbl::AutoLock lock(&proxy_lock_);
            if (proxy_.is_valid()) {
                tcs_rpt_.rpt_id = AMBIENT_LIGHT_RPT_ID_SIMPLE_POLL;
                status = FillRpt();
                if (status == ZX_OK) {
                    proxy_.IoQueue(reinterpret_cast<uint8_t*>(&tcs_rpt_), sizeof(ambient_light_data_t));
                }
            }
        }
        sleep(TCS3400_POLL_SLEEP_SECS);
    }
    return thrd_success;
}

// TODO(andresoportus): Only for testing, will send data up the stack via HID
zx_status_t Tcs3400Device::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
    if (count == 0) return ZX_OK;
    uint8_t* p = reinterpret_cast<uint8_t*>(buf);
    uint8_t addr;
    zx_status_t status;
    // Read lower byte first, the device holds upper byte of a sample in a shadow register after a
    // lower byte read
    addr = TCS_I2C_CDATAL;
    status = i2c_transact_sync(&i2c_, kI2cIndex, &addr, 1, p + 1, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::DdkRead: i2c_transact_sync failed: %d\n", status);
        return status;
    }
    if (count == 1) {
        zxlogf(INFO, "TCS-3400 clear light read: 0x%02X\n", *p);
        *actual = 1;
        return ZX_OK;
    }
    addr = TCS_I2C_CDATAH;
    status = i2c_transact_sync(&i2c_, kI2cIndex, &addr, 1, p, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::DdkRead: i2c_transact_sync failed: %d\n", status);
        return status;
    }
    zxlogf(INFO, "TCS-3400 clear light read: 0x%02X%02X\n", *p, *(p + 1));
    *actual = 2;
    return ZX_OK;
}

zx_status_t Tcs3400Device::HidBusStart(ddk::HidBusIfcProxy proxy) {
    fbl::AutoLock lock(&proxy_lock_);
    if (proxy_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    } else {
        proxy_ = proxy;
    }
    return ZX_OK;
}

zx_status_t Tcs3400Device::HidBusQuery(uint32_t options, hid_info_t* info) {
    if (!info) {
        return ZX_ERR_INVALID_ARGS;
    }
    info->dev_num = 0;
    info->dev_class = HID_DEV_CLASS_OTHER;
    info->boot_device = false;

    return ZX_OK;
}

void Tcs3400Device::HidBusStop() {
}

zx_status_t Tcs3400Device::HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {
    const uint8_t* desc_ptr;
    uint8_t* buf;
    *len = get_ambient_light_report_desc(&desc_ptr);
    fbl::AllocChecker ac;
    buf = new (&ac) uint8_t[*len];
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(buf, desc_ptr, *len);
    *data = buf;
    return ZX_OK;
}

zx_status_t Tcs3400Device::HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                           size_t len, size_t* out_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                           size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidBusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidBusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidBusGetProtocol(uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidBusSetProtocol(uint8_t protocol) {
    return ZX_OK;
}

zx_status_t Tcs3400Device::Bind() {
    if (device_get_protocol(parent(), ZX_PROTOCOL_I2C, &i2c_) != ZX_OK) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

    running_.store(true);
    int rc = thrd_create_with_name(&thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<Tcs3400Device*>(arg)->Thread();
                                   },
                                   reinterpret_cast<void*>(this),
                                   "tcs-3400-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    zx_status_t status = DdkAdd("tcs-3400");
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::Bind: DdkAdd failed: %d\n", status);
        return status;
    }

    cleanup.cancel();
    return ZX_OK;
}

void Tcs3400Device::ShutDown() {
    running_.store(false);
    thrd_join(thread_, NULL);
    {
        fbl::AutoLock lock(&proxy_lock_);
        proxy_.clear();
    }
}

void Tcs3400Device::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void Tcs3400Device::DdkRelease() {
    delete this;
}

} // namespace tcs

extern "C" zx_status_t tcs3400_bind(void* ctx, zx_device_t* parent) {
    auto dev = fbl::make_unique<tcs::Tcs3400Device>(parent);
    auto status = dev->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
