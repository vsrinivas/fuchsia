// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "composite-device.h"

#include <utility>
#include "binding-internal.h"

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

// CompositeDeviceComponent methods

CompositeDeviceComponent::CompositeDeviceComponent(CompositeDevice* composite, uint32_t index,
                                                   fbl::Array<const ComponentPartDescriptor> parts)
    : composite_(composite), index_(index), parts_(std::move(parts)) {
    // TODO(teisenbe): Remove this when the index is used elsewhere.  Clang and
    // GCC do not agree on whether this is unused, so either tagging it as used
    // or unused causes one of them to error.
    (void)bound_device_;
}

CompositeDeviceComponent::~CompositeDeviceComponent() = default;

} // namespace devmgr
