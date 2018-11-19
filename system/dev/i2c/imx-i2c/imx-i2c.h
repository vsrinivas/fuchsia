// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>

#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/i2cimpl.h>

#include <fbl/optional.h>

namespace imx_i2c {

class ImxI2cDevice;
using DeviceType = ddk::Device<ImxI2cDevice, ddk::Unbindable>;

class ImxI2cDevice : public DeviceType,
                     public ddk::I2cImplProtocol<ImxI2cDevice> {
public:
    ImxI2cDevice(zx_device_t* parent, int dev_cnt)
        : DeviceType(parent), dev_cnt_(dev_cnt) {}

    zx_status_t Bind(int id);

    // Methods required by the ddk mixins
    void DdkUnbind();
    void DdkRelease();
    uint32_t I2cImplGetBusCount();
    zx_status_t I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size);
    zx_status_t I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate);
    zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* ops, size_t count);

private:
    enum class Wait {
        kBusy,
        kIdle,
        kInterruptPending
    };
    static constexpr const char* WaitStr(Wait type) {
        switch (type) {
        case Wait::kBusy:
            return "BUSY";
        case Wait::kIdle:
            return "IDLE";
        case Wait::kInterruptPending:
            return "INTERRUPT_PENDING";
        }
        return "UNKNOWN";
    }
    const uint32_t dev_cnt_;
    thrd_t thread_;
    fbl::optional<ddk::MmioBuffer> mmio_;

    void Reset();
    zx_status_t Read(uint8_t addr, void* buf, size_t len, bool stop);
    zx_status_t Write(uint8_t addr, const void* buf, size_t len, bool stop);
    zx_status_t Start();
    void Stop();
    zx_status_t RxData(uint8_t* buf, size_t length, bool stop);
    zx_status_t TxData(const uint8_t* buf, size_t length, bool stop);
    zx_status_t TxAddress(uint8_t addr, bool is_read);
    zx_status_t WaitFor(Wait type);
    int Thread();
    void ShutDown();
};
} // namespace imx_i2c
