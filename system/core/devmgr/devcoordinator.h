// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/listnode.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/new.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/vmo.h>
#include <port/port.h>

namespace devmgr {

struct Devhost;
struct Devnode;

struct Work {
    fbl::DoublyLinkedListNodeState<Work*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<Work*>& node_state(
            Work& obj) {
            return obj.node;
        }
    };

    enum struct Op : uint32_t {
        kIdle = 0,
        kDeviceAdded = 1,
        kDriverAdded = 2,
    } op;
    uint32_t arg;
    void* ptr;
};

struct Pending {
    Pending();

    fbl::DoublyLinkedListNodeState<Pending*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<Pending*>& node_state(
            Pending& obj) {
            return obj.node;
        }
    };

    void* ctx;
    enum struct Op : uint32_t {
        kBind = 1,
        kSuspend = 2,
    } op;
};

struct Metadata {
    fbl::DoublyLinkedListNodeState<fbl::unique_ptr<Metadata>> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<fbl::unique_ptr<Metadata>>& node_state(
            Metadata& obj) {
            return obj.node;
        }
    };

    uint32_t type;
    uint32_t length;
    bool has_path;      // zero terminated string starts at data[length]

    char* Data() {
        return reinterpret_cast<char*>(this + 1);
    }

    const char* Data() const {
        return reinterpret_cast<const char*>(this + 1);
    }

    static zx_status_t Create(size_t data_len, fbl::unique_ptr<Metadata>* out) {
        uint8_t* buf = new uint8_t[sizeof(Metadata) + data_len];
        if (!buf) {
            return ZX_ERR_NO_MEMORY;
        }
        new (buf) Metadata();

        out->reset(reinterpret_cast<Metadata*>(buf));
        return ZX_OK;
    }

    // Implement a custom delete to deal with the allocation mechanism used in
    // Create().  Since the ctor is private, all Metadata* will come from
    // Create().
    void operator delete(void* ptr) {
        delete [] reinterpret_cast<uint8_t*>(ptr);
    }

 private:
    Metadata() = default;

    Metadata(const Metadata&) = delete;
    Metadata& operator=(const Metadata&) = delete;

    Metadata(Metadata&&) = delete;
    Metadata& operator=(Metadata&&) = delete;
};

#define DEV_HOST_DYING 1
#define DEV_HOST_SUSPEND 2

struct Device {
    Device();
    ~Device();

    zx_handle_t hrpc;
    uint32_t flags;

    port_handler_t ph;

    Devhost* host;
    const char* name;
    const char* libname;
    fbl::unique_ptr<const char[]> args;
    Work work;
    mutable int32_t refcount_;
    uint32_t protocol_id;
    uint32_t prop_count;
    Devnode* self;
    Devnode* link;
    Device* parent;
    Device* proxy;

    // listnode for this device in its parent's
    // list-of-children
    fbl::DoublyLinkedListNodeState<Device*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(
            Device& obj) {
            return obj.node;
        }
    };

    // listnode for this device in its devhost's
    // list-of-devices
    fbl::DoublyLinkedListNodeState<Device*> dhnode;
    struct DevhostNode {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(
            Device& obj) {
            return obj.dhnode;
        }
    };

    // list of all child devices of this device
    fbl::DoublyLinkedList<Device*, Node> children;

    // list of outstanding requests from the devcoord
    // to this device's devhost, awaiting a response
    fbl::DoublyLinkedList<Pending*, Pending::Node> pending;

    // listnode for this device in the all devices list
    fbl::DoublyLinkedListNodeState<Device*> anode;
    struct AllDevicesNode {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(
            Device& obj) {
            return obj.anode;
        }
    };

    // Metadata entries associated to this device.
    fbl::DoublyLinkedList<fbl::unique_ptr<Metadata>, Metadata::Node> metadata;

    fbl::unique_ptr<zx_device_prop_t[]> props;

    // Allocation backing |name| and |libname|
    fbl::unique_ptr<char[]> name_alloc_;

    // The AddRef and Release functions follow the contract for fbl::RefPtr.
    void AddRef() const {
        ++refcount_;
    }

    // Returns true when the last reference has been released.
    bool Release() const {
        const int32_t rc = refcount_;
        --refcount_;
        return rc == 1;
    }
};

struct Devhost {
    Devhost();

    port_handler_t ph;
    zx_handle_t hrpc;
    zx::process proc;
    zx_koid_t koid;
    mutable int32_t refcount_;
    uint32_t flags;
    Devhost* parent;

    // list of all devices on this devhost
    fbl::DoublyLinkedList<Device*, Device::DevhostNode> devices;

    // listnode for this devhost in the all devhosts list
    fbl::DoublyLinkedListNodeState<Devhost*> anode;
    struct AllDevhostsNode {
        static fbl::DoublyLinkedListNodeState<Devhost*>& node_state(
            Devhost& obj) {
            return obj.anode;
        }
    };

    // listnode for this devhost in the order-to-suspend list
    fbl::DoublyLinkedListNodeState<Devhost*> snode;
    struct SuspendNode {
        static fbl::DoublyLinkedListNodeState<Devhost*>& node_state(
            Devhost& obj) {
            return obj.snode;
        }
    };

    // listnode for this devhost in its parent devhost's list-of-children
    fbl::DoublyLinkedListNodeState<Devhost*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<Devhost*>& node_state(
            Devhost& obj) {
            return obj.node;
        }
    };

    // list of all child devhosts of this devhost
    fbl::DoublyLinkedList<Devhost*, Node> children;

    // The AddRef and Release functions follow the contract for fbl::RefPtr.
    void AddRef() const {
        ++refcount_;
    }

    // Returns true when the last reference has been released.
    bool Release() const {
        const int32_t rc = refcount_;
        --refcount_;
        return rc == 1;
    }
};

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

struct Driver {
    Driver() = default;

    fbl::String name;
    fbl::unique_ptr<const zx_bind_inst_t[]> binding;
    // Binding size in number of bytes, not number of entries
    // TODO: Change it to number of entries
    uint32_t binding_size = 0;
    uint32_t flags = 0;
    zx::vmo dso_vmo;

    fbl::DoublyLinkedListNodeState<Driver*> node;
    struct Node {
        static fbl::DoublyLinkedListNodeState<Driver*>& node_state(
            Driver& obj) {
            return obj.node;
        }
    };

    fbl::String libname;
};

#define DRIVER_NAME_LEN_MAX 64

zx_status_t devfs_publish(Device* parent, Device* dev);
void devfs_unpublish(Device* dev);
void devfs_advertise(Device* dev);
void devfs_advertise_modified(Device* dev);

Device* coordinator_init(const zx::job& root_job);
void coordinator();

void load_driver(const char* path,
                 void (*func)(Driver* drv, const char* version));
void find_loadable_drivers(const char* path,
                           void (*func)(Driver* drv, const char* version));

bool dc_is_bindable(const Driver* drv, uint32_t protocol_id,
                    zx_device_prop_t* props, size_t prop_count,
                    bool autobind);

extern bool dc_asan_drivers;
extern bool dc_launched_first_devhost;

} // namespace devmgr
