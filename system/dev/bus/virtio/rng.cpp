// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rng.h"

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>
#include <limits.h>

namespace virtio {

RngDevice::RngDevice(zx_device_t* bus_device, zx::bti bti, fbl::unique_ptr<Backend> backend)
    : Device(bus_device, fbl::move(bti), fbl::move(backend)) {
}

RngDevice::~RngDevice() {
    // TODO: clean up allocated physical memory
}

zx_status_t RngDevice::Init() {
    // reset the device
    DeviceReset();

    // ack and set the driver status bit
    DriverStatusAck();

    // allocate the main vring
    auto err = vring_.Init(kRingIndex, kRingSize);
    if (err < 0) {
        zxlogf(ERROR, "%s: failed to allocate vring\n", tag());
        return err;
    }

    // allocate the entropy buffer
    static_assert(kBufferSize <= PAGE_SIZE, "");
    zx_status_t rc = io_buffer_init(&buf_, bti_.get(), kBufferSize,
                                    IO_BUFFER_RO | IO_BUFFER_CONTIG);
    if (rc != ZX_OK) {
        zxlogf(ERROR, "%s: cannot allocate entropy buffer: %d\n", tag(), rc);
        return rc;
    }

    zxlogf(SPEW, "%s: allocated entropy buffer at %p, physical address %#" PRIxPTR "\n",
           tag(), io_buffer_virt(&buf_), io_buffer_phys(&buf_));

    // start the interrupt thread
    StartIrqThread();

    // set DRIVER_OK
    DriverStatusOk();

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-rng";
    args.ctx = nullptr;
    args.ops = &device_ops_;

    auto status = device_add(bus_device_, &args, &device_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: device_add failed %d\n", tag(), status);
        device_ = nullptr;
        return status;
    }

    // TODO(SEC-29): The kernel should trigger entropy requests, instead of relying on this
    // userspace thread to push entropy whenever it wants to. As a temporary hack, this thread
    // pushes entropy to the kernel every 300 seconds instead.
    thrd_create_with_name(&seed_thread_, RngDevice::SeedThreadEntry, this,
                          "virtio-rng-seed-thread");
    thrd_detach(seed_thread_);

    zxlogf(INFO, "%s: initialization succeeded\n", tag());

    return ZX_OK;
}

void RngDevice::IrqRingUpdate() {
    zxlogf(TRACE, "%s: Got irq ring update\n", tag());

    // parse our descriptor chain, add back to the free queue
    auto free_chain = [this](vring_used_elem* used_elem) {
        uint32_t i = (uint16_t)used_elem->id;
        struct vring_desc* desc = vring_.DescFromIndex((uint16_t)i);

        if (desc->addr != io_buffer_phys(&buf_) || desc->len != kBufferSize) {
            zxlogf(ERROR, "%s: entropy response with unexpected buffer\n", tag());
        } else {
            zxlogf(SPEW, "%s: received entropy; adding to kernel pool\n", tag());
            zx_status_t rc = zx_cprng_add_entropy(io_buffer_virt(&buf_), kBufferSize);
            if (rc != ZX_OK) {
                zxlogf(ERROR, "%s: add_entropy failed (%d)\n", tag(), rc);
            }
        }

        vring_.FreeDesc((uint16_t)i);
    };

    // tell the ring to find free chains and hand it back to our lambda
    vring_.IrqRingUpdate(free_chain);
}

void RngDevice::IrqConfigChange() {
    zxlogf(TRACE, "%s: Got irq config change (ignoring)\n", tag());
}

int RngDevice::SeedThreadEntry(void* arg) {
    RngDevice* d = static_cast<RngDevice*>(arg);
    for (;;) {
        zx_status_t rc = d->Request();
        zxlogf(SPEW, "virtio-rng-seed-thread: RngDevice::Request() returned %d\n", rc);
        zx_nanosleep(zx_deadline_after(ZX_SEC(300)));
    }
}

zx_status_t RngDevice::Request() {
    zxlogf(TRACE, "%s: sending entropy request\n", tag());
    fbl::AutoLock lock(&lock_);
    uint16_t i;
    vring_desc* desc = vring_.AllocDescChain(1, &i);
    if (!desc) {
        zxlogf(ERROR, "%s: failed to allocate descriptor chain of length 1\n", tag());
        return ZX_ERR_NO_RESOURCES;
    }

    desc->addr = io_buffer_phys(&buf_);
    desc->len = kBufferSize;
    desc->flags = VRING_DESC_F_WRITE;
    zxlogf(SPEW, "%s: allocated descriptor chain desc %p, i %u\n", tag(), desc, i);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        virtio_dump_desc(desc);
    }

    vring_.SubmitChain(i);
    vring_.Kick();

    zxlogf(SPEW, "%s: kicked off entropy request\n", tag());

    return ZX_OK;
}

} // namespace virtio
