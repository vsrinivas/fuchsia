// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include "../shared/log.h"
#include "coordinator.h"
#include "devfs.h"

namespace devmgr {

Device::Device(Coordinator* coord)
    : coordinator(coord),
      publish_task([this] { coordinator->HandleNewDevice(fbl::WrapRefPtr(this)); }) {}

Device::~Device() {
    // Ideally we'd assert here that immortal devices are never destroyed, but
    // they're destroyed when the Coordinator object is cleaned up in tests.
    // We can probably get rid of the IMMORTAL flag, since if the Coordinator is
    // holding a reference we shouldn't be able to hit that check, in which case
    // the flag is only used to modify the proxy library loading behavior.

    log(DEVLC, "devcoord: destroy dev %p name='%s'\n", this, this->name.data());

    devfs_unpublish(this);

    fbl::unique_ptr<Metadata> md;
    while ((md = this->metadata.pop_front()) != nullptr) {
        if (md->has_path) {
            // return to published_metadata list
            coordinator->AppendPublishedMetadata(std::move(md));
        } else {
            // metadata was attached directly to this device, so we release it now
        }
    }

    // TODO: cancel any pending rpc responses
    // TODO: Have dtor assert that DEV_CTX_IMMORTAL set on flags
}

// Handle inbound messages from devhost to devices
void Device::HandleRpc(fbl::RefPtr<Device>&& dev, async_dispatcher_t* dispatcher,
                       async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        log(ERROR, "devcoord: Device::HandleRpc aborting, saw status %d\n", status);
        return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
        zx_status_t r;
        if ((r = dev->coordinator->HandleDeviceRead(dev)) < 0) {
            if (r != ZX_ERR_STOP) {
                log(ERROR, "devcoord: device %p name='%s' rpc status: %d\n", dev.get(),
                    dev->name.data(), r);
            }
            dev->coordinator->RemoveDevice(dev, true);
            // Do not start waiting again on this device's channel again
            return;
        }
        Device::BeginWait(std::move(dev), dispatcher);
        return;
    }
    if (signal->observed & ZX_CHANNEL_PEER_CLOSED) {
        log(ERROR, "devcoord: device %p name='%s' disconnected!\n", dev.get(), dev->name.data());
        dev->coordinator->RemoveDevice(dev, true);
        // Do not start waiting again on this device's channel again
        return;
    }
    log(ERROR, "devcoord: no work? %08x\n", signal->observed);
    Device::BeginWait(std::move(dev), dispatcher);
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
