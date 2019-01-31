// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device.h"
#include "ring.h"

#include <atomic>
#include <stdlib.h>

#include "backends/backend.h"
#include <lib/sync/completion.h>
#include <virtio/scsi.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>

namespace virtio {

class ScsiDevice : public Device {
public:
    enum Queue {
        CONTROL = 0,
        EVENT = 1,
        REQUEST = 2,
    };

    ScsiDevice(zx_device_t* device, zx::bti bti, fbl::unique_ptr<Backend> backend)
        : Device(device, std::move(bti), std::move(backend)) {}

    // virtio::Device overrides
    zx_status_t Init() override;
    void Unbind() override;
    void Release() override;
    // Invoked for most device interrupts.
    void IrqRingUpdate() override {}
    // Invoked on config change interrupts.
    void IrqConfigChange() override {}

    const char* tag() const override { return "virtio-scsi"; }

    static void FillLUNStructure(struct virtio_scsi_req_cmd* req, uint8_t target, uint16_t lun);

private:
    zx_status_t ExecuteCommandSync(uint8_t target, uint16_t lun, uint8_t* cdb, size_t cdb_length)
        TA_REQ(lock_);

    zx_status_t WorkerThread();

    // Latched copy of virtio-scsi device configuration.
    struct virtio_scsi_config config_ TA_GUARDED(lock_) = {};

    // DMA Memory for virtio-scsi requests/responses, events, task management functions.
    io_buffer_t request_buffers_ TA_GUARDED(lock_) = {};

    Ring control_ring_ TA_GUARDED(lock_) = {this};
    Ring request_queue_ TA_GUARDED(lock_) = {this};

    thrd_t worker_thread_;
    bool worker_thread_should_exit_ TA_GUARDED(lock_) = {};

    // Synchronizes virtio rings and worker thread control.
    fbl::Mutex lock_;
};

} // namespace virtio
