// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "composite-device.h"

#include <utility>
#include "../shared/log.h"
#include "binding-internal.h"
#include "coordinator.h"

namespace devmgr {

// CompositeDevice methods

CompositeDevice::CompositeDevice(fbl::String name, fbl::Array<const zx_device_prop_t> properties,
                                 uint32_t coresident_device_index)
    : name_(std::move(name)), properties_(std::move(properties)),
      coresident_device_index_(coresident_device_index) {
    // TODO(teisenbe): Remove this when the index is used elsewhere.  Clang and
    // GCC do not agree on whether this is unused, so either tagging it as used
    // or unused causes one of them to error.
    (void)coresident_device_index_;
}

CompositeDevice::~CompositeDevice() = default;

zx_status_t CompositeDevice::Create(const fbl::StringPiece& name,
                                    const zx_device_prop_t* props_data, size_t props_count,
                                    const fuchsia_device_manager_DeviceComponent* components,
                                    size_t components_count, uint32_t coresident_device_index,
                                    std::unique_ptr<CompositeDevice>* out) {
    fbl::String name_obj(name);
    fbl::Array<zx_device_prop_t> properties(new zx_device_prop_t[props_count], props_count);
    memcpy(properties.get(), props_data, props_count * sizeof(props_data[0]));

    auto dev = std::make_unique<CompositeDevice>(std::move(name), std::move(properties),
                                                 coresident_device_index);
    for (size_t i = 0; i < components_count; ++i) {
        const auto& fidl_component = components[i];
        size_t parts_count = fidl_component.parts_count;
        fbl::Array<ComponentPartDescriptor> parts(new ComponentPartDescriptor[parts_count],
                                                  parts_count);
        for (size_t j = 0; j < parts_count; ++j) {
            const auto& fidl_part = fidl_component.parts[j];
            size_t program_count = fidl_part.match_program_count;
            fbl::Array<zx_bind_inst_t> match_program(new zx_bind_inst_t[program_count],
                                                    program_count);
            static_assert(sizeof(zx_bind_inst_t) == sizeof(fidl_part.match_program[0]));
            memcpy(match_program.get(), fidl_part.match_program,
                   sizeof(zx_bind_inst_t) * program_count);
            parts[j] = { std::move(match_program) };
        }

        auto component = std::make_unique<CompositeDeviceComponent>(dev.get(), i, std::move(parts));
        dev->unbound_.push_back(std::move(component));
    }
    *out = std::move(dev);
    return ZX_OK;
}

bool CompositeDevice::TryMatchComponents(const fbl::RefPtr<Device>& dev, size_t* index_out) {
    for (auto itr = bound_.begin(); itr != bound_.end(); ++itr) {
        if (itr->TryMatch(dev)) {
            log(ERROR,
                "devcoordinator: ambiguous composite bind! composite='%s', dev1='%s', dev2='%s'\n",
                name_.data(), itr->bound_device()->name.data(), dev->name.data());
            return false;
        }
    }
    for (auto itr = unbound_.begin(); itr != unbound_.end(); ++itr) {
        if (itr->TryMatch(dev)) {
            log(SPEW, "devcoordinator: found match for composite='%s', dev='%s'\n",
                name_.data(), dev->name.data());
            *index_out = itr->index();
           return true;
        }
    }
    log(SPEW, "devcoordinator: no match for composite='%s', dev='%s'\n",
        name_.data(), dev->name.data());
    return false;
}

zx_status_t CompositeDevice::BindComponent(size_t index, const fbl::RefPtr<Device>& dev) {
    // Find the component we're binding
    CompositeDeviceComponent* component = nullptr;
    for (auto& unbound_component : unbound_) {
        if (unbound_component.index() == index) {
            component = &unbound_component;
            break;
        }
    }
    ZX_ASSERT_MSG(component != nullptr, "Attempted to bind component that wasn't unbound!\n");

    zx_status_t status = component->Bind(dev);
    if (status != ZX_OK) {
        return status;
    }
    bound_.push_back(unbound_.erase(*component));
    return ZX_OK;
}

zx_status_t CompositeDevice::TryAssemble() {
    if (!unbound_.is_empty()) {
        return ZX_ERR_SHOULD_WAIT;
    }

    Devhost* devhost = nullptr;
    for (auto& component : bound_) {
        // Find the devhost to put everything in (if we don't find one, nullptr
        // means "a new devhost").
        if (component.index() == coresident_device_index_) {
            devhost = component.bound_device()->host();
        }
        // Make sure the component driver has created its device
        if (component.component_device() == nullptr) {
            return ZX_ERR_SHOULD_WAIT;
        }
    }

    // Create all of the proxies for the component devices, in the same process
    for (auto& component : bound_) {
        const fbl::RefPtr<Device>& dev = component.component_device();
        // Double check that we haven't ended up in a state where the proxies
        // would need to be in different processes.
        if (devhost != nullptr && dev->proxy != nullptr && dev->proxy->host() != nullptr &&
            dev->proxy->host() != devhost) {
            log(ERROR, "devcoordinator: cannot create composite, proxies in different processes\n");
            return ZX_ERR_BAD_STATE;
        }
        zx_status_t status = dev->coordinator->PrepareProxy(dev, devhost);
        if (status != ZX_OK) {
            return status;
        }
        // If we hadn't picked a devhost, use the one that was created just now.
        if (devhost == nullptr) {
            devhost = dev->proxy->host();
            ZX_ASSERT(devhost != nullptr);
        }
    }
    // TODO: Create the composite device and wire everything up
    return ZX_ERR_NOT_SUPPORTED;
}

// CompositeDeviceComponent methods

CompositeDeviceComponent::CompositeDeviceComponent(CompositeDevice* composite, uint32_t index,
                                                   fbl::Array<const ComponentPartDescriptor> parts)
    : composite_(composite), index_(index), parts_(std::move(parts)) {
}

CompositeDeviceComponent::~CompositeDeviceComponent() = default;

bool CompositeDeviceComponent::TryMatch(const fbl::RefPtr<Device>& dev) {
    if (parts_.size() > UINT32_MAX) {
        return false;
    }
    auto match = ::devmgr::internal::MatchParts(dev, parts_.get(),
                                                static_cast<uint32_t>(parts_.size()));
    if (match != ::devmgr::internal::Match::One) {
        return false;
    }
    return true;
}

zx_status_t CompositeDeviceComponent::Bind(const fbl::RefPtr<Device>& dev) {
    ZX_ASSERT(bound_device_ == nullptr);

    zx_status_t status = dev->coordinator->BindDriverToDevice(
            dev, dev->coordinator->component_driver(), true /* autobind */);
    if (status != ZX_OK) {
        return status;
    }

    bound_device_ = dev;
    dev->set_component(this);
    return ZX_OK;
}

void CompositeDeviceComponent::Unbind() {
    ZX_ASSERT(bound_device_ != nullptr);
    // Drop our reference to the device added by the component driver
    component_device_ = nullptr;
    bound_device_->set_component(nullptr);
    bound_device_ = nullptr;
}

} // namespace devmgr
