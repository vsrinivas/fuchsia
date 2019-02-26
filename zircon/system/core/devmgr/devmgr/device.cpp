// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include "../shared/log.h"
#include "coordinator.h"

namespace devmgr {

Device::Device(Coordinator* coord)
    : coordinator(coord), publish_task([this] { coordinator->HandleNewDevice(this); }) {}

// Handle inbound messages from devhost to devices
void Device::HandleRpc(Device* dev, async_dispatcher_t* dispatcher, async::WaitBase* wait,
                       zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        log(ERROR, "devcoord: Device::HandleRpc aborting, saw status %d\n", status);
        return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
        zx_status_t r;
        if ((r = dev->coordinator->HandleDeviceRead(dev)) < 0) {
            if (r != ZX_ERR_STOP) {
                log(ERROR, "devcoord: device %p name='%s' rpc status: %d\n", dev, dev->name.data(),
                    r);
            }
            dev->coordinator->RemoveDevice(dev, true);
            // Do not start waiting again on this device's channel again
            return;
        }
        Device::BeginWait(dev, dispatcher);
        return;
    }
    if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
        log(ERROR, "devcoord: device %p name='%s' disconnected!\n", dev, dev->name.data());
        dev->coordinator->RemoveDevice(dev, true);
        // Do not start waiting again on this device's channel again
        return;
    }
    log(ERROR, "devcoord: no work? %08x\n", signal->observed);
    Device::BeginWait(dev, dispatcher);
}

zx_status_t Device::SetProps(fbl::Array<const zx_device_prop_t> props) {
    // This function should only be called once
    ZX_DEBUG_ASSERT(props_.get() == nullptr);

    props_ = std::move(props);
    topo_prop_ = nullptr;

    for (const auto prop : props_) {
        if (prop.id >= BIND_TOPO_START && prop.id <= BIND_TOPO_END) {
            if (topo_prop_ != nullptr) {
                return ZX_ERR_INVALID_ARGS;
            }
            topo_prop_ = &prop;
        }
    }
    return ZX_OK;
}

} // namespace devmgr
