// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <memory>

namespace devmgr {

// Forward declaration
class CompositeDevice;
struct Device;

// Describes a device on the path to a component of a composite device
struct ComponentPartDescriptor {
    fbl::Array<const zx_bind_inst_t> match_program;
};

// A single device that is part of a composite device.
// TODO(teisenbe): Should this just be an inner-class of CompositeDevice?
class CompositeDeviceComponent {
public:
    CompositeDeviceComponent(CompositeDevice* composite, uint32_t index,
                             fbl::Array<const ComponentPartDescriptor> parts);

    CompositeDeviceComponent(CompositeDeviceComponent&&) = delete;
    CompositeDeviceComponent& operator=(CompositeDeviceComponent&&) = delete;

    CompositeDeviceComponent(const CompositeDeviceComponent&) = delete;
    CompositeDeviceComponent& operator=(const CompositeDeviceComponent&) = delete;

    ~CompositeDeviceComponent();

    uint32_t index() const { return index_; }
    CompositeDevice* composite() const { return composite_; }

    // Used for embedding a component in the CompositeDevice's bound and unbound
    // lists.
    struct Node {
        static fbl::DoublyLinkedListNodeState<std::unique_ptr<CompositeDeviceComponent>>&
                node_state(CompositeDeviceComponent& obj) { return obj.node_; }
    };
private:
    // The CompositeDevice that this is a part of
    CompositeDevice* const composite_;

    // The index of this component within its CompositeDevice
    const uint32_t index_;

    // A description of the devices from the root of the tree to the component
    // itself.
    const fbl::Array<const ComponentPartDescriptor> parts_;

    // If this component has been bound to a device, this points to that device.
    Device* bound_device_ = nullptr;

    fbl::DoublyLinkedListNodeState<std::unique_ptr<CompositeDeviceComponent>> node_;
};

// A device composed of other devices.
class CompositeDevice {
public:
    // Only public because of make_unique.  You probably want Create().
    CompositeDevice(fbl::String name, fbl::Array<const zx_device_prop_t> properties,
                    uint32_t coresident_device_index);

    CompositeDevice(CompositeDevice&&) = delete;
    CompositeDevice& operator=(CompositeDevice&&) = delete;

    CompositeDevice(const CompositeDevice&) = delete;
    CompositeDevice& operator=(const CompositeDevice&) = delete;

    ~CompositeDevice();

    static zx_status_t Create(const fbl::StringPiece& name,
                              const zx_device_prop_t* props_data, size_t props_count,
                              const fuchsia_device_manager_DeviceComponent* components,
                              size_t components_count, uint32_t coresident_device_index,
                              std::unique_ptr<CompositeDevice>* out);

    const fbl::String& name() const {
        return name_;
    }
    const fbl::Array<const zx_device_prop_t>& properties() const {
        return properties_;
    }

    // Node for list of composite devices the coordinator knows about
    struct Node {
        static fbl::DoublyLinkedListNodeState<std::unique_ptr<CompositeDevice>>&
                node_state(CompositeDevice& obj) { return obj.node_; }
    };
private:
    using ComponentList = fbl::DoublyLinkedList<std::unique_ptr<CompositeDeviceComponent>,
          CompositeDeviceComponent::Node>;

    const fbl::String name_;
    const fbl::Array<const zx_device_prop_t> properties_;
    const uint32_t coresident_device_index_;

    ComponentList unbound_;
    ComponentList bound_;

    fbl::DoublyLinkedListNodeState<std::unique_ptr<CompositeDevice>> node_;
};

} // namespace devmgr
