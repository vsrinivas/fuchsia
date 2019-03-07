// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fbl/array.h>
#include <fbl/string.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/channel.h>

#include "metadata.h"

namespace devmgr {

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

// Device has been removed but its rpc channel is not
// torn down yet.  The rpc transport will call remove
// when it notices at which point the device will leave
// the zombie state and drop the reference associated
// with the rpc channel, allowing complete destruction.
#define DEV_CTX_ZOMBIE        0x20

// Device is a proxy -- its "parent" is the device it's
// a proxy to.
#define DEV_CTX_PROXY         0x40

// Device is not visible in devfs or bindable.
// Devices may be created in this state, but may not
// return to this state once made visible.
#define DEV_CTX_INVISIBLE     0x80

// clang-format on

struct Device {
    explicit Device(Coordinator* coord);

    // Begins waiting in |dispatcher| on |dev->wait|.  This transfers a
    // reference of |dev| to the dispatcher.  The dispatcher returns ownership
    // when the of that reference when the handler is invoked.
    // TODO(teisenbe/kulakowski): Make this take a RefPtr
    static zx_status_t BeginWait(Device* dev, async_dispatcher_t* dispatcher) {
        // TODO(teisenbe/kulakowski): Once this takes a refptr, we should leak a
        // ref in the success case (to present the ref owned by the dispatcher).
        return dev->wait.Begin(dispatcher);
    }

    // Entrypoint for the RPC handler that captures the pointer ownership
    // semantics.
    void HandleRpcEntry(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
        // TODO(teisenbe/kulakowski): Perform the appropriate dance to construct
        // a RefPtr from |this| without a net-increase in refcount, to represent
        // the dispatcher passing ownership of its reference to the handler
        HandleRpc(this, dispatcher, wait, status, signal);
    }

    // TODO(teisenbe/kulakowski): Make this take a RefPtr
    static void HandleRpc(Device* dev, async_dispatcher_t* dispatcher, async::WaitBase* wait,
                          zx_status_t status, const zx_packet_signal_t* signal);

    Coordinator* coordinator;
    zx::channel hrpc;
    uint32_t flags = 0;

    async::WaitMethod<Device, &Device::HandleRpcEntry> wait{this};
    async::TaskClosure publish_task;

    Devhost* host = nullptr;
    fbl::String name;
    fbl::String libname;
    fbl::String args;
    // The backoff between each driver retry. This grows exponentially.
    zx::duration backoff = zx::msec(250);
    // The number of retries left for the driver.
    uint32_t retries = 4;
    mutable int32_t refcount_ = 0;
    Devnode* self = nullptr;
    Devnode* link = nullptr;
    Device* proxy = nullptr;

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

    // The AddRef and Release functions follow the contract for fbl::RefPtr.
    void AddRef() const { ++refcount_; }

    // Returns true when the last reference has been released.
    bool Release() const {
        const int32_t rc = refcount_;
        --refcount_;
        return rc == 1;
    }

    // Sets the properties of this device.  Returns an error if the properties
    // array contains more than one property from the BIND_TOPO_* range.
    zx_status_t SetProps(fbl::Array<const zx_device_prop_t> props);
    const fbl::Array<const zx_device_prop_t>& props() const { return props_; }
    const zx_device_prop_t* topo_prop() const { return topo_prop_; }

    Device* parent() { return parent_; }
    const Device* parent() const { return parent_; }
    // TODO: Remove set_parent once this class is further encapsulated.  It
    // should be unnecessary.
    void set_parent(Device* parent) { parent_ = parent; }

    uint32_t protocol_id() const { return protocol_id_; }
    // TODO: Remove set_protocol_id once this class is further encapsulated.  It
    // should be unnecessary.
    void set_protocol_id(uint32_t protocol_id) { protocol_id_ = protocol_id; }

    bool is_bindable() const {
        return !(flags & (DEV_CTX_BOUND | DEV_CTX_DEAD | DEV_CTX_ZOMBIE | DEV_CTX_INVISIBLE));
    }
private:
    Device* parent_ = nullptr;
    uint32_t protocol_id_ = 0;

    fbl::Array<const zx_device_prop_t> props_;
    // If the device has a topological property in |props|, this points to it.
    const zx_device_prop_t* topo_prop_ = nullptr;
};

} // namespace devmgr
