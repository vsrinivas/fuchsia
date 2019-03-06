// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fbl/array.h>
#include <fbl/ref_counted.h>
#include <fbl/string.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include "metadata.h"
#include "../shared/async-loop-ref-counted-rpc-handler.h"

namespace devmgr {

class CompositeDeviceComponent;
class Coordinator;
class Devhost;
struct Devnode;
class SuspendContext;

// clang-format off

// This device is never destroyed
#define DEV_CTX_IMMORTAL      0x01

// This device requires that children are created in a
// new devhost attached to a proxy device
#define DEV_CTX_MUST_ISOLATE  0x02

// This device may be bound multiple times
#define DEV_CTX_MULTI_BIND    0x04

// This device is bound and not eligible for binding
// again until unbound.  Not allowed on MULTI_BIND ctx.
#define DEV_CTX_BOUND         0x08

// Device has been remove()'d
#define DEV_CTX_DEAD          0x10

// Device is a proxy -- its "parent" is the device it's
// a proxy to.
#define DEV_CTX_PROXY         0x40

// Device is not visible in devfs or bindable.
// Devices may be created in this state, but may not
// return to this state once made visible.
#define DEV_CTX_INVISIBLE     0x80

// clang-format on

struct Device : public fbl::RefCounted<Device>, public AsyncLoopRefCountedRpcHandler<Device> {
    explicit Device(Coordinator* coord);
    ~Device();

    // Create a new device with the given parameters.  This sets up its
    // relationship with its parent and devhost and adds its RPC channel to the
    // coordinator's async loop.  This does not add the device to the
    // coordinator's devices_ list, or trigger publishing
    static zx_status_t Create(Coordinator* coordinator, const fbl::RefPtr<Device>& parent,
                              fbl::String name, fbl::String driver_path, fbl::String args,
                              uint32_t protocol_id, fbl::Array<zx_device_prop_t> props,
                              zx::channel rpc, bool invisible, zx::channel client_remote,
                              fbl::RefPtr<Device>* device);
    zx_status_t CreateProxy();

    static void HandleRpc(fbl::RefPtr<Device>&& dev, async_dispatcher_t* dispatcher,
                          async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);

    // Signal that this device is ready for bind to happen.  This should happen
    // either immediately after the device is created, if it's created visible,
    // or after it becomes visible.
    zx_status_t SignalReadyForBind(zx::duration delay = zx::sec(0));

    Coordinator* coordinator;
    uint32_t flags = 0;

    Devhost* host = nullptr;
    fbl::String name;
    fbl::String libname;
    fbl::String args;
    // The backoff between each driver retry. This grows exponentially.
    zx::duration backoff = zx::msec(250);
    // The number of retries left for the driver.
    uint32_t retries = 4;
    Devnode* self = nullptr;
    Devnode* link = nullptr;
    fbl::RefPtr<Device> proxy;

    // For attaching as an open connection to the proxy device,
    // or once the device becomes visible.
    zx::channel client_remote;

    // listnode for this device in its parent's
    // list-of-children
    fbl::DoublyLinkedListNodeState<Device*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(Device& obj) { return obj.node; }
    };

    // listnode for this device in its devhost's
    // list-of-devices
    fbl::DoublyLinkedListNodeState<Device*> dhnode;
    struct DevhostNode {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(Device& obj) {
            return obj.dhnode;
        }
    };

    // list of all child devices of this device
    fbl::DoublyLinkedList<Device*, Node> children;

    // listnode for this device in the all devices list
    fbl::DoublyLinkedListNodeState<Device*> anode;
    struct AllDevicesNode {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(Device& obj) {
            return obj.anode;
        }
    };

    // Metadata entries associated to this device.
    fbl::DoublyLinkedList<fbl::unique_ptr<Metadata>, Metadata::Node> metadata;

    // Sets the properties of this device.  Returns an error if the properties
    // array contains more than one property from the BIND_TOPO_* range.
    zx_status_t SetProps(fbl::Array<const zx_device_prop_t> props);
    const fbl::Array<const zx_device_prop_t>& props() const { return props_; }
    const zx_device_prop_t* topo_prop() const { return topo_prop_; }

    const fbl::RefPtr<Device>& parent() { return parent_; }
    fbl::RefPtr<const Device> parent() const { return parent_; }
    // TODO: Remove set_parent once this class is further encapsulated.  It
    // should be unnecessary.
    void set_parent(fbl::RefPtr<Device> parent) { parent_ = std::move(parent); }

    uint32_t protocol_id() const { return protocol_id_; }
    // TODO: Remove set_protocol_id once this class is further encapsulated.  It
    // should be unnecessary.
    void set_protocol_id(uint32_t protocol_id) { protocol_id_ = protocol_id; }

    bool is_bindable() const {
        return !(flags & (DEV_CTX_BOUND | DEV_CTX_DEAD | DEV_CTX_INVISIBLE));
    }

    CompositeDeviceComponent* component() const { return component_; }
    void set_component(CompositeDeviceComponent* component) {
        ZX_ASSERT(component == nullptr || component_ == nullptr);
        component_ = component;
    }

private:
    fbl::RefPtr<Device> parent_;
    uint32_t protocol_id_ = 0;

    fbl::Array<const zx_device_prop_t> props_;
    // If the device has a topological property in |props|, this points to it.
    const zx_device_prop_t* topo_prop_ = nullptr;

    async::TaskClosure publish_task_;

    // If the device is part of a composite device, this points to the component
    // that matched it.
    CompositeDeviceComponent* component_ = nullptr;
};

} // namespace devmgr
