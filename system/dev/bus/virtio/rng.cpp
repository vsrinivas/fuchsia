// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rng.h"

#include <ddk/debug.h>
#include <inttypes.h>
#include <fbl/auto_lock.h>

namespace virtio {

RngDevice::RngDevice(mx_device_t* bus_device)
    : Device(bus_device) {
}

RngDevice::~RngDevice() {
    // TODO: clean up allocated physical memory
}

mx_status_t RngDevice::Init() {
    // reset the device
    Reset();

    // ack and set the driver status bit
    StatusAcknowledgeDriver();

    // allocate the main vring
    auto err = vring_.Init(kRingIndex, kRingSize);
    if (err < 0) {
        dprintf(ERROR, "virtio-rng: failed to allocate vring\n");
        return err;
    }

    // allocate the entropy buffer
    mx_status_t rc = io_buffer_init(&buf_, kBufferSize, IO_BUFFER_RO);
    if (rc != MX_OK) {
        dprintf(ERROR, "virtio-rng: cannot allocate entropy buffer: %d\n", rc);
        return rc;
    }

    dprintf(SPEW, "virtio-rng: allocated entropy buffer at %p, physical address %#" PRIxPTR "\n",
            io_buffer_virt(&buf_), io_buffer_phys(&buf_));

    // start the interrupt thread
    StartIrqThread();

    // set DRIVER_OK
    StatusDriverOK();

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "virtio-rng";
    args.ctx = nullptr;
    args.ops = &device_ops_;

    auto status = device_add(bus_device_, &args, &device_);
    if (status < 0) {
        dprintf(ERROR, "virtio-rng: device_add failed %d\n", status);
        device_ = nullptr;
        return status;
    }

    // TODO(SEC-29): The kernel should trigger entropy requests, instead of relying on this
    // userspace thread to push entropy whenever it wants to. As a temporary hack, this thread
    // pushes entropy to the kernel every 300 seconds instead.
    thrd_create_with_name(&seed_thread_, RngDevice::SeedThreadEntry, this,
                          "virtio-rng-seed-thread");
    thrd_detach(seed_thread_);

    dprintf(INFO, "virtio-rng: initialization succeeded\n");

    return MX_OK;
}

void RngDevice::IrqRingUpdate() {
    dprintf(TRACE, "virtio-rng: Got irq ring update\n");

    // parse our descriptor chain, add back to the free queue
    auto free_chain = [this](vring_used_elem* used_elem) {
        uint32_t i = (uint16_t)used_elem->id;
        struct vring_desc* desc = vring_.DescFromIndex((uint16_t)i);

        if (desc->addr != io_buffer_phys(&buf_) || desc->len != kBufferSize) {
            dprintf(ERROR, "virtio-rng: entropy response with unexpected buffer\n");
        } else {
            dprintf(SPEW, "virtio-rng: received entropy; adding to kernel pool\n");
            mx_status_t rc = mx_cprng_add_entropy(io_buffer_virt(&buf_), kBufferSize);
            if (rc != MX_OK) {
                dprintf(ERROR, "virtio-rng: add_entropy failed (%d)\n", rc);
            }
        }

        vring_.FreeDesc((uint16_t)i);
    };

    // tell the ring to find free chains and hand it back to our lambda
    vring_.IrqRingUpdate(free_chain);
}

void RngDevice::IrqConfigChange() {
    dprintf(TRACE, "virtio-rng: Got irq config change (ignoring)\n");
}

int RngDevice::SeedThreadEntry(void* arg) {
    RngDevice* d = static_cast<RngDevice*>(arg);
    for(;;) {
        mx_status_t rc = d->Request();
        dprintf(SPEW, "virtio-rng-seed-thread: RngDevice::Request() returned %d\n", rc);
        mx_nanosleep(mx_deadline_after(MX_SEC(300)));
    }
}

mx_status_t RngDevice::Request() {
    dprintf(TRACE, "virtio-rng: sending entropy request\n");
    fbl::AutoLock lock(&lock_);
    uint16_t i;
    vring_desc* desc = vring_.AllocDescChain(1, &i);
    if (!desc) {
        dprintf(ERROR, "virtio-rng: failed to allocate descriptor chain of length 1\n");
        return MX_ERR_NO_RESOURCES;
    }

    desc->addr = io_buffer_phys(&buf_);
    desc->len = kBufferSize;
    desc->flags = VRING_DESC_F_WRITE;
    dprintf(SPEW, "virtio-rng: allocated descriptor chain desc %p, i %u\n", desc, i);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        virtio_dump_desc(desc);
    }

    vring_.SubmitChain(i);
    vring_.Kick();

    dprintf(SPEW, "virtio-rng: kicked off entropy request\n");

    return MX_OK;
}

} // namespace virtio
