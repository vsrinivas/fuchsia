// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>

#include <utility>

#include "metadata.h"

namespace devmgr {

struct Device;
struct Devhost;
struct Devnode;
class SuspendContext;

struct Work {
    Work() : owner(nullptr) { }
    explicit Work(Device* dev) : owner(dev) { }

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
    } op = Op::kIdle;
    uint32_t arg = 0;
    Device* owner;
};

class PendingOperation {
public:
    enum struct Op : uint32_t {
        kBind = 1,
        kSuspend = 2,
    };

    PendingOperation(Op op, SuspendContext* context) : op_(op), context_(context) {}

    struct Node {
        static fbl::DoublyLinkedListNodeState<fbl::unique_ptr<PendingOperation>>& node_state(
            PendingOperation& obj) {
            return obj.node_;
        }
    };

    Op op() const { return op_; }
    SuspendContext* context() const { return context_; }

private:
    fbl::DoublyLinkedListNodeState<fbl::unique_ptr<PendingOperation>> node_;

    Op op_;
    SuspendContext* context_;
};

#define DEV_HOST_DYING 1
#define DEV_HOST_SUSPEND 2

struct Device {
    Device();
    ~Device();

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
    static void HandleRpc(Device* dev, async_dispatcher_t* dispatcher,
                          async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);

    zx::channel hrpc;
    uint32_t flags;

    async::WaitMethod<Device, &Device::HandleRpcEntry> wait{this};

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
    fbl::DoublyLinkedList<fbl::unique_ptr<PendingOperation>, PendingOperation::Node> pending;

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

    async::Wait wait;
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

class SuspendContext {
public:
    enum struct Flags : uint32_t {
        kRunning = 0u,
        kSuspend = 1u,
    };

    SuspendContext() {
    }

    SuspendContext(Flags flags, uint32_t sflags, zx::socket socket,
                   zx::vmo kernel = zx::vmo(),
                   zx::vmo bootdata = zx::vmo()) :
        flags_(flags), sflags_(sflags), socket_(std::move(socket)),
        kernel_(std::move(kernel)), bootdata_(std::move(bootdata)) {
    }

    ~SuspendContext() {
        devhosts_.clear();
    }

    SuspendContext(SuspendContext&&) = default;
    SuspendContext& operator=(SuspendContext&&) = default;

    zx_status_t status() const { return status_; }
    void set_status(zx_status_t status) { status_ = status; }
    Flags flags() const { return flags_; }
    void set_flags(Flags flags) { flags_ = flags; }
    uint32_t sflags() const { return sflags_; }

    Devhost* dh() const { return dh_; }
    void set_dh(Devhost* dh) { dh_ = dh; }

    using DevhostList = fbl::DoublyLinkedList<Devhost*, Devhost::SuspendNode>;
    DevhostList& devhosts() { return devhosts_; }
    const DevhostList& devhosts() const { return devhosts_; }

    const zx::vmo& kernel() const { return kernel_; }
    const zx::vmo& bootdata() const { return bootdata_; }

    // Close the socket whose ownership was handed to this SuspendContext.
    void CloseSocket() {
        socket_.reset();
    }

    // The AddRef and Release functions follow the contract for fbl::RefPtr.
    void AddRef() const {
        ++count_;
    }

    // Returns true when the last message reference has been released.
    bool Release() const {
        const int32_t rc = count_;
        --count_;
        return rc == 1;
    }

private:
    zx_status_t status_ = ZX_OK;
    Flags flags_ = Flags::kRunning;

    // suspend flags
    uint32_t sflags_ = 0u;
    // outstanding msgs
    mutable uint32_t count_ = 0u;
    // next devhost to process
    Devhost* dh_ = nullptr;
    fbl::DoublyLinkedList<Devhost*, Devhost::SuspendNode> devhosts_;

    // socket to notify on for 'dm reboot' and 'dm poweroff'
    zx::socket socket_;

    // mexec arguments
    zx::vmo kernel_;
    zx::vmo bootdata_;
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

// Access the devcoordinator's async event loop
async::Loop* DcAsyncLoop();

zx_status_t devfs_publish(Device* parent, Device* dev);
void devfs_unpublish(Device* dev);
void devfs_advertise(Device* dev);
void devfs_advertise_modified(Device* dev);

Device* coordinator_init(const zx::job& root_job);

// Setup and begin executing the coordinator's loop.
// |driver_search_path| specifies which directory drivers should be loaded from
// |sys_device_driver| specifies which driver should be used for the sys device.
// Either may be nullptr, in which case default values will be used.
void coordinator(const char* driver_search_path, const char* sys_device_driver);

void load_driver(const char* path,
                 void (*func)(Driver* drv, const char* version));
void find_loadable_drivers(const char* path,
                           void (*func)(Driver* drv, const char* version));

bool dc_is_bindable(const Driver* drv, uint32_t protocol_id,
                    zx_device_prop_t* props, size_t prop_count,
                    bool autobind);

extern bool dc_asan_drivers;
extern bool dc_launched_first_devhost;

// Methods for composing FIDL RPCs to the devhosts
zx_status_t dh_send_remove_device(const Device* dev);
zx_status_t dh_send_create_device(Device* dev, Devhost* dh, zx::channel rpc, zx::vmo driver,
                                  const char* args, zx::handle rpc_proxy);
zx_status_t dh_send_create_device_stub(Devhost* dh, zx::channel rpc, uint32_t protocol_id);
zx_status_t dh_send_bind_driver(Device* dev, const char* libname, zx::vmo driver);
zx_status_t dh_send_connect_proxy(const Device* dev, zx::channel proxy);
zx_status_t dh_send_suspend(const Device* dev, uint32_t flags);

} // namespace devmgr
