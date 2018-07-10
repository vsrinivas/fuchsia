// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>
#include <unistd.h>

#include <ddk/debug.h>

#include <fbl/unique_ptr.h>

#include "tcs3400-regs.h"
#include "tcs3400.h"

namespace tcs {

int Tcs3400Device::Thread() {
    uint8_t cmd[] = {TCS_I2C_ENABLE, TCS_I2C_ENABLE_POWER_ON | TCS_I2C_ENABLE_ADC_ENABLE};
    zx_status_t status = i2c_transact_sync(&i2c_, kI2cIndex, &cmd, sizeof(cmd), NULL, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::Thread(tcs-3400): i2c_transact_sync failed: %d\n", status);
        return thrd_error;
    }
    // TODO(andresoportus): Interrupt support pending on interface with upper layers via HID
    return thrd_success;
}

// TODO(andresoportus): Only for testing, will send data up the stack via HID
zx_status_t Tcs3400Device::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
    if (count == 0) return ZX_OK;
    uint8_t* p = reinterpret_cast<uint8_t*>(buf);
    uint8_t addr = TCS_I2C_CDATAH;
    zx_status_t status = i2c_transact_sync(&i2c_, kI2cIndex, &addr, 1, p, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::DdkRead(tcs-3400): i2c_transact_sync failed: %d\n", status);
        return status;
    }
    if (count == 1) {
        zxlogf(INFO, "TCS-3400 clear light read: 0x%02X\n", *p);
        *actual = 1;
        return ZX_OK;
    }
    addr = TCS_I2C_CDATAL;
    status = i2c_transact_sync(&i2c_, kI2cIndex, &addr, 1, p + 1, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::DdkRead(tcs-3400): i2c_transact_sync failed: %d\n", status);
        return status;
    }
    zxlogf(INFO, "TCS-3400 clear light read: 0x%02X%02X\n", *p, *(p + 1));
    *actual = 2;
    return ZX_OK;
}

zx_status_t Tcs3400Device::Bind() {
    if (device_get_protocol(parent(), ZX_PROTOCOL_I2C, &i2c_) != ZX_OK) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    int rc = thrd_create_with_name(&thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<Tcs3400Device*>(arg)->Thread();
                                   },
                                   reinterpret_cast<void*>(this),
                                   "tcs-3400-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    return DdkAdd("tcs-3400");
}

void Tcs3400Device::DdkUnbind() {
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
