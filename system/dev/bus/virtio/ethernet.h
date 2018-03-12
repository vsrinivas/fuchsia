// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device.h"
#include "ring.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <ddk/io-buffer.h>
#include <ddk/protocol/ethernet.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <virtio/net.h>
#include <zircon/compiler.h>
#include <zircon/device/ethernet.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

namespace virtio {

class EthernetDevice : public Device {
public:
    explicit EthernetDevice(zx_device_t* device, zx::bti, fbl::unique_ptr<Backend> backend);
    virtual ~EthernetDevice();

    zx_status_t Init() override TA_EXCL(state_lock_);
    void Release() override TA_EXCL(state_lock_);

    // VirtIO callbacks
    void IrqRingUpdate() override TA_EXCL(state_lock_);
    void IrqConfigChange() override TA_EXCL(state_lock_);

    // DDK protocol hooks; see ddk/protocol/ethernet.h
    zx_status_t Query(uint32_t options, ethmac_info_t* info) TA_EXCL(state_lock_);
    void Stop() TA_EXCL(state_lock_);
    zx_status_t Start(ethmac_ifc_t* ifc, void* cookie) TA_EXCL(state_lock_);
    zx_status_t QueueTx(uint32_t options, ethmac_netbuf_t* netbuf) TA_EXCL(state_lock_);

    const char* tag() const override { return "virtio-net"; }

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(EthernetDevice);

    // DDK device hooks; see ddk/device.h
    void ReleaseLocked() TA_REQ(state_lock_);

    // Mutexes to control concurrent access
    mtx_t state_lock_;
    mtx_t tx_lock_;

    // Virtqueues; see section 5.1.2 of the spec
    // This driver doesn't currently support multi-queueing, automatic
    // steering, or the control virtqueue, so only a single queue is needed in
    // each direction.
    Ring rx_;
    Ring tx_;
    fbl::unique_ptr<io_buffer_t[]> bufs_;
    size_t unkicked_ TA_GUARDED(tx_lock_);

    // Saved net device configuration out of the pci config BAR
    virtio_net_config_t config_ TA_GUARDED(state_lock_);
    size_t virtio_hdr_len_;

    // Ethmac callback interface; see ddk/protocol/ethernet.h
    ethmac_ifc_t* ifc_ TA_GUARDED(state_lock_);
    void* cookie_;
};

} // namespace virtio
