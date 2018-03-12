// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <ddk/debug.h>
#include <hw/inout.h>
#include <pretty/hexdump.h>
#include <zircon/status.h>

#include "trace.h"

#define LOCAL_TRACE 0

namespace virtio {

Device::Device(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend)
    : bti_(fbl::move(bti)), backend_(fbl::move(backend)), bus_device_(bus_device) {
    LTRACE_ENTRY;
    device_ops_.version = DEVICE_OPS_VERSION;
}

Device::~Device() {
    LTRACE_ENTRY;
}

void Device::Unbind() {
    device_remove(device_);
}

void Device::Release() {
    backend_.reset();
}

void Device::IrqWorker() {
    zx_status_t rc;
    zxlogf(TRACE, "%s: starting irq worker\n", tag());

    while (backend_->InterruptValid() == ZX_OK) {
        if ((rc = backend_->WaitForInterrupt()) != ZX_OK) {
            zxlogf(SPEW, "%s: error while waiting for interrupt: %s\n",
                   tag(), zx_status_get_string(rc));
            continue;
        }

        // Read the status before completing the interrupt in case
        // another interrupt fires and changes the status.
        uint32_t irq_status = IsrStatus();

        LTRACEF_LEVEL(2, "irq_status %#x\n", irq_status);

        // Since we handle both interrupt types here it's possible for a
        // spurious interrupt if they come in sequence and we check IsrStatus
        // after both have been triggered.
        if (irq_status == 0)
            continue;

        if (irq_status & VIRTIO_ISR_QUEUE_INT) { /* used ring update */
            IrqRingUpdate();
        }
        if (irq_status & VIRTIO_ISR_DEV_CFG_INT) { /* config change */
            IrqConfigChange();
        }
    }
}

int Device::IrqThreadEntry(void* arg) {
    Device* d = static_cast<Device*>(arg);

    d->IrqWorker();

    return 0;
}

void Device::StartIrqThread() {
    thrd_create_with_name(&irq_thread_, IrqThreadEntry, this, "virtio-irq-thread");
    thrd_detach(irq_thread_);
}

zx_status_t Device::CopyDeviceConfig(void* _buf, size_t len) const {
    assert(_buf);

    for (uint16_t i = 0; i < len; i++) {
        backend_->DeviceConfigRead(i, static_cast<uint8_t*>(_buf) + i);
    }

    return ZX_OK;
}

} // namespace virtio
