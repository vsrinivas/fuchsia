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
#include <variant>

#include "metadata.h"
#include "../shared/async-loop-ref-counted-rpc-handler.h"

namespace devmgr {

class CompositeDevice;
class CompositeDeviceComponent;
class Coordinator;
class Devhost;
struct Devnode;
class SuspendContext;
class SuspendTask;

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
    Device(Coordinator* coord, fbl::RefPtr<Device> parent, uint32_t protocol_id);
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
    static zx_status_t CreateComposite(Coordinator* coordinator, Devhost* devhost,
                                       const CompositeDevice& composite, zx::channel rpc,
                                       fbl::RefPtr<Device>* device);
    zx_status_t CreateProxy();

    static void HandleRpc(fbl::RefPtr<Device>&& dev, async_dispatcher_t* dispatcher,
                          async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);

    // Node for entry in device child list
    struct Node {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(Device& obj) {
            return obj.node_;
        }
    };

    // This iterator provides access to a list of devices that does not provide
    // mechanisms for mutating that list.  With this, a user can get mutable
    // access to a device in the list.  This is achieved by making the linked
    // list iterator opaque. It is not safe to modify the underlying list while
    // this iterator is in use.
    template <typename IterType, typename ValueType>
    class ChildListIterator {
    public:
        explicit ChildListIterator(IterType itr) : itr_(std::move(itr)) {}

        ChildListIterator& operator++() { ++itr_; return *this; }
        ChildListIterator operator++(int) {
            auto other = *this;
            ++*this;
            return other;
        }
        ValueType& operator*() const { return *itr_; }
        bool operator==(const ChildListIterator& other) const { return itr_ == other.itr_; }
        bool operator!=(const ChildListIterator& other) const { return itr_ != other.itr_; }
    private:
        IterType itr_;
    };

    // This class exists to allow consumers of the Device class to write
    //   for (auto& child : dev->children())
    // and get mutable access to the children without getting mutable access to
    // the list.
    template <typename ListType, typename IterType>
    class ChildListIteratorFactory {
    public:
        explicit ChildListIteratorFactory(ListType& list) : list_(list) {}

        IterType begin() const { return IterType(list_.begin()); }
        IterType end() const { return IterType(list_.end()); }

        bool is_empty() const { return begin() == end(); }
    private:
        ListType& list_;
    };

    using NonConstChildListIterator =
            ChildListIterator<fbl::DoublyLinkedList<Device*, Node>::iterator, Device>;
    using ConstChildListIterator =
            ChildListIterator<fbl::DoublyLinkedList<Device*, Node>::const_iterator, const Device>;
    using NonConstChildListIteratorFactory = ChildListIteratorFactory<
            fbl::DoublyLinkedList<Device*, Node>, NonConstChildListIterator>;
    using ConstChildListIteratorFactory = ChildListIteratorFactory<
            const fbl::DoublyLinkedList<Device*, Node>, ConstChildListIterator>;
    // We do not want to expose the list itself for mutation, even if the
    // children are allowed to be mutated.  We manage this by making the
    // iterator opaque.
    NonConstChildListIteratorFactory children() {
        return NonConstChildListIteratorFactory(children_);
    }
    ConstChildListIteratorFactory children() const {
        return ConstChildListIteratorFactory(children_);
    }

    // Signal that this device is ready for bind to happen.  This should happen
    // either immediately after the device is created, if it's created visible,
    // or after it becomes visible.
    zx_status_t SignalReadyForBind(zx::duration delay = zx::sec(0));

    using SuspendCompletion = fit::function<void(zx_status_t)>;
    // Issue a Suspend request to this device.  When the response comes in, the
    // given completion will be invoked.
    zx_status_t SendSuspend(uint32_t flags, SuspendCompletion completion);

    // Run the completion for the outstanding suspend, if any.  This method is
    // only exposed currently because RemoveDevice is on Coordinator instead of
    // Device.
    void CompleteSuspend(zx_status_t status);

    Coordinator* coordinator;
    uint32_t flags = 0;

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

    // listnode for this device in its devhost's
    // list-of-devices
    fbl::DoublyLinkedListNodeState<Device*> dhnode;
    struct DevhostNode {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(Device& obj) {
            return obj.dhnode;
        }
    };

    // listnode for this device in the all devices list
    fbl::DoublyLinkedListNodeState<Device*> anode;
    struct AllDevicesNode {
        static fbl::DoublyLinkedListNodeState<Device*>& node_state(Device& obj) {
            return obj.anode;
        }
    };

    // Break the relationship between this device object and its parent
    void DetachFromParent();

    // Metadata entries associated to this device.
    fbl::DoublyLinkedList<fbl::unique_ptr<Metadata>, Metadata::Node> metadata;

    // Sets the properties of this device.  Returns an error if the properties
    // array contains more than one property from the BIND_TOPO_* range.
    zx_status_t SetProps(fbl::Array<const zx_device_prop_t> props);
    const fbl::Array<const zx_device_prop_t>& props() const { return props_; }
    const zx_device_prop_t* topo_prop() const { return topo_prop_; }

    const fbl::RefPtr<Device>& parent() { return parent_; }
    fbl::RefPtr<const Device> parent() const { return parent_; }

    uint32_t protocol_id() const { return protocol_id_; }

    bool is_bindable() const {
        return !(flags & (DEV_CTX_BOUND | DEV_CTX_DEAD | DEV_CTX_INVISIBLE));
    }

    // If the device was bound as a component of a composite, this returns the
    // component's description.
    CompositeDeviceComponent* component() const {
        auto val = std::get_if<CompositeDeviceComponent*>(&composite_);
        return val ? *val : nullptr;

    }
    void set_component(CompositeDeviceComponent* component) {
        ZX_ASSERT(std::holds_alternative<UnassociatedWithComposite>(composite_));
        composite_ = component;
    }

    // If the device was created as a composite, this returns its description.
    CompositeDevice* composite() const {
        auto val = std::get_if<CompositeDevice*>(&composite_);
        return val ? *val : nullptr;

    }
    void set_composite(CompositeDevice* composite) {
        ZX_ASSERT(std::holds_alternative<UnassociatedWithComposite>(composite_));
        composite_ = composite;
    }
    void disassociate_from_composite() {
        composite_ = UnassociatedWithComposite{};
    }

    void set_host(Devhost* host);
    Devhost* host() const { return host_; }
    uint64_t local_id() const { return local_id_; }

    // TODO(teisenbe): We probably want more states.  For example, the DEAD flag
    // should probably move in to here.
    enum class State {
        kActive,
        kSuspended,
    };

    State state() const { return state_; }

    // Creates a new suspend task if necessary and returns a reference to it.
    // If one is already in-progress, a reference to it is returned instead
    fbl::RefPtr<SuspendTask> RequestSuspendTask(uint32_t suspend_flags);
private:
    zx_status_t HandleRead();

    fbl::RefPtr<Device> parent_;
    const uint32_t protocol_id_;

    fbl::Array<const zx_device_prop_t> props_;
    // If the device has a topological property in |props|, this points to it.
    const zx_device_prop_t* topo_prop_ = nullptr;

    async::TaskClosure publish_task_;

    // listnode for this device in its parent's list-of-children
    fbl::DoublyLinkedListNodeState<Device*> node_;

    // list of all child devices of this device
    fbl::DoublyLinkedList<Device*, Node> children_;

    // - If this device is part of a composite device, this is inhabited by
    //   CompositeDeviceComponent* and it points to the component that matched it.
    //   Note that this is only set on the device that matched the component, not
    //   the "component device" added by the component driver.
    // - If this device is a composite device, this is inhabited by
    //   CompositeDevice* and it points to the composite that describes it.
    // - Otherwise, it is inhabited by UnassociatedWithComposite
    struct UnassociatedWithComposite {};
    std::variant<UnassociatedWithComposite, CompositeDeviceComponent*, CompositeDevice*>
            composite_;

    Devhost* host_ = nullptr;
    // The id of this device from the perspective of the devhost.  This can be
    // used to communicate with the devhost about this device.
    uint64_t local_id_ = 0;

    // The current state of the device
    State state_ = State::kActive;

    // If a suspend is in-progress, this task represents it.
    fbl::RefPtr<SuspendTask> active_suspend_;
    // If a suspend is in-progress, this completion will be invoked when it is
    // completed.  It will likely mark |active_suspend_| as completed and clear
    // it.
    SuspendCompletion suspend_completion_;
};

} // namespace devmgr
