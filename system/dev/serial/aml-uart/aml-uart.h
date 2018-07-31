// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <ddk/io-buffer.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/serial.h>
#include <ddktl/device.h>
#include <ddktl/protocol/serial.h>

#include <fbl/function.h>
#include <fbl/mutex.h>
#include <lib/zx/interrupt.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

namespace serial {

class AmlUart;
using DeviceType = ddk::Device<AmlUart, ddk::Unbindable>;

class AmlUart : public DeviceType,
                public ddk::SerialProtocol<AmlUart> {
public:
    // Spawns device node.
    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    void DdkUnbind() {
        DdkRemove();
    }
    void DdkRelease() {
        Enable(false);
        io_buffer_release(&mmio_);
        delete this;
    }

    // Serial protocol implementation.
    zx_status_t GetInfo(serial_port_info_t* info);
    zx_status_t Config(uint32_t baud_rate, uint32_t flags);
    zx_status_t Enable(bool enable);
    zx_status_t Read(void* buf, size_t length, size_t* out_actual);
    zx_status_t Write(const void* buf, size_t length, size_t* out_actual);
    zx_status_t SetNotifyCallback(serial_notify_cb cb, void* cookie);

private:
    using Callback = fbl::Function<void(uint32_t)>;

    explicit AmlUart(zx_device_t* parent, const platform_device_protocol_t& pdev,
                     const pdev_device_info_t& info, io_buffer_t mmio)
        : DeviceType(parent), pdev_(pdev), serial_port_info_(info.serial_port_info), mmio_(mmio) {}

    // Reads the current state from the status register and calls notify_cb if it has changed.
    uint32_t ReadStateAndNotify();
    void EnableLocked(bool enable) TA_REQ(enable_lock_);
    int IrqThread();

    const platform_device_protocol_t pdev_;
    const serial_port_info_t serial_port_info_;
    io_buffer_t mmio_;
    zx::interrupt irq_;

    thrd_t irq_thread_ TA_GUARDED(enable_lock_);
    bool enabled_ TA_GUARDED(enable_lock_) = false;

    Callback notify_cb_ TA_GUARDED(status_lock_) = nullptr;
    // Last state we sent to notify_cb.
    uint32_t state_ TA_GUARDED(status_lock_) = 0;

    // Protects enabling/disabling lifecycle.
    fbl::Mutex enable_lock_;
    // Protects status register and notify_cb.
    fbl::Mutex status_lock_;
};

} // namespace serial
