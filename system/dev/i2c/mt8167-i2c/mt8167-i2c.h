// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/i2c-impl.h>

#include <fbl/optional.h>
#include <fbl/vector.h>

#include <lib/zx/event.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>

#include <threads.h>

#include "mt8167-i2c-regs.h"

namespace mt8167_i2c {

class Mt8167I2c;
using DeviceType = ddk::Device<Mt8167I2c, ddk::Unbindable>;

class Mt8167I2c : public DeviceType,
                  public ddk::I2cImplProtocol<Mt8167I2c> {
public:
    explicit Mt8167I2c(zx_device_t* parent)
        : DeviceType(parent) {}
    zx_status_t Bind();
    zx_status_t Init();
    static zx_status_t Create(zx_device_t* parent);

    // Methods required by the ddk mixins.
    void DdkUnbind();
    void DdkRelease();
    uint32_t I2cImplGetBusCount();
    zx_status_t I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size);
    zx_status_t I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate);
    zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* ops, size_t count);

private:
    struct Key {
        ddk::MmioBuffer mmio;
        zx::interrupt irq;
        zx::event event;
    };

    void ClockEnable(uint32_t id, bool enable);
    int IrqThread();
    int TestThread();
    zx_status_t Transact(bool is_read, uint32_t id, uint8_t addr, void* buf, size_t len, bool stop);
    void DataMove(bool is_read, uint32_t id, void* buf, size_t len);
    void Reset(uint32_t id);
    void ShutDown();

    uint32_t bus_count_;
    fbl::optional<XoRegs> xo_regs_;
    fbl::Vector<Key> keys_;
    zx::port irq_port_;
    thrd_t irq_thread_;
};

} // namespace mt8167_i2c
