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
      publish_task_([this] { coordinator->HandleNewDevice(fbl::WrapRefPtr(this)); }) {}

Device::~Device() {
    // Ideally we'd assert here that immortal devices are never destroyed, but
    // they're destroyed when the Coordinator object is cleaned up in tests.
    // We can probably get rid of the IMMORTAL flag, since if the Coordinator is
    // holding a reference we shouldn't be able to hit that check, in which case
    // the flag is only used to modify the proxy library loading behavior.

    log(DEVLC, "devcoordinator: destroy dev %p name='%s'\n", this, this->name.data());

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

zx_status_t Device::Create(Coordinator* coordinator, const fbl::RefPtr<Device>& parent,
                           fbl::String name, fbl::String driver_path, fbl::String args,
                           uint32_t protocol_id, fbl::Array<zx_device_prop_t> props,
                           zx::channel rpc, bool invisible, zx::channel client_remote,
                           fbl::RefPtr<Device>* device) {
    auto dev = fbl::MakeRefCounted<Device>(coordinator);
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }
    zx_status_t status = dev->SetProps(std::move(props));
    if (status != ZX_OK) {
        return status;
    }

    dev->name = std::move(name);
    dev->libname = std::move(driver_path);
    dev->args = std::move(args);
    dev->set_channel(std::move(rpc));
    dev->set_protocol_id(protocol_id);
    dev->client_remote = std::move(client_remote);

    // If we have bus device args we are, by definition, a bus device.
    if (dev->args.size() > 0) {
        dev->flags |= DEV_CTX_MUST_ISOLATE;
    }

    // We exist within our parent's device host
    dev->host = parent->host;

    fbl::RefPtr<Device> real_parent;
    // If our parent is a proxy, for the purpose
    // of devicefs, we need to work with *its* parent
    // which is the device that it is proxying.
    if (parent->flags & DEV_CTX_PROXY) {
        real_parent = parent->parent();
    } else {
        real_parent = parent;
    }
    dev->set_parent(real_parent);

    // We must mark the device as invisible before publishing so
    // that we don't send "device added" notifications.
    if (invisible) {
        dev->flags |= DEV_CTX_INVISIBLE;
    }

    if ((status = devfs_publish(real_parent, dev)) < 0) {
        return status;
    }

    if ((status = Device::BeginWait(dev, coordinator->dispatcher())) != ZX_OK) {
        return status;
    }

    if (dev->host) {
        // TODO host == nullptr should be impossible
        dev->host->AddRef();
        dev->host->devices().push_back(dev.get());
    }
    real_parent->children.push_back(dev.get());
    log(DEVLC, "devcoord: dev %p name='%s' (child)\n", real_parent.get(), real_parent->name.data());

    *device = std::move(dev);
    return ZX_OK;
}

zx_status_t Device::CreateProxy() {
    ZX_ASSERT(this->proxy == nullptr);

    auto dev = fbl::MakeRefCounted<Device>(this->coordinator);
    if (dev == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    dev->name = this->name;
    dev->libname = this->libname;
    // non-immortal devices, use foo.proxy.so for
    // their proxy devices instead of foo.so
    if (!(this->flags & DEV_CTX_IMMORTAL)) {
        const char* begin = dev->libname.data();
        const char* end = strstr(begin, ".so");
        fbl::StringPiece prefix(begin, end == nullptr ? dev->libname.size() : end - begin);
        fbl::AllocChecker ac;
        dev->libname = fbl::String::Concat({prefix, ".proxy.so"}, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    dev->flags = DEV_CTX_PROXY;
    dev->set_protocol_id(protocol_id_);
    dev->set_parent(fbl::WrapRefPtr(this));
    this->proxy = std::move(dev);
    log(DEVLC, "devcoord: dev %p name='%s' (proxy)\n", this, this->name.data());
    return ZX_OK;
}

zx_status_t Device::SignalReadyForBind(zx::duration delay) {
    return publish_task_.PostDelayed(this->coordinator->dispatcher(), delay);
}

// Handle inbound messages from devhost to devices
void Device::HandleRpc(fbl::RefPtr<Device>&& dev, async_dispatcher_t* dispatcher,
                       async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        log(ERROR, "devcoordinator: Device::HandleRpc aborting, saw status %d\n", status);
        return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
        zx_status_t r;
        if ((r = dev->coordinator->HandleDeviceRead(dev)) < 0) {
            if (r != ZX_ERR_STOP) {
                log(ERROR, "devcoordinator: device %p name='%s' rpc status: %d\n", dev.get(),
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
        log(ERROR, "devcoordinator: device %p name='%s' disconnected!\n", dev.get(), dev->name.data());
        dev->coordinator->RemoveDevice(dev, true);
        // Do not start waiting again on this device's channel again
        return;
    }
    log(ERROR, "devcoordinator: no work? %08x\n", signal->observed);
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
