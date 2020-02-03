// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "composite-device.h"

#include <zircon/status.h>

#include <utility>

#include "binding-internal.h"
#include "coordinator.h"
#include "fidl.h"
#include "log.h"

// CompositeDevice methods

CompositeDevice::CompositeDevice(fbl::String name, fbl::Array<const zx_device_prop_t> properties,
                                 uint32_t components_count, uint32_t coresident_device_index,
                                 fbl::Array<std::unique_ptr<Metadata>> metadata)
    : name_(std::move(name)),
      properties_(std::move(properties)),
      components_count_(components_count),
      coresident_device_index_(coresident_device_index),
      metadata_(std::move(metadata)) {}

CompositeDevice::~CompositeDevice() = default;

zx_status_t CompositeDevice::Create(
    const fbl::StringPiece& name,
    llcpp::fuchsia::device::manager::CompositeDeviceDescriptor comp_desc,
    std::unique_ptr<CompositeDevice>* out) {
  fbl::String name_obj(name);
  fbl::Array<zx_device_prop_t> properties(new zx_device_prop_t[comp_desc.props.count()],
                                          comp_desc.props.count());
  memcpy(properties.data(), comp_desc.props.data(),
         comp_desc.props.count() * sizeof(comp_desc.props.data()[0]));

  fbl::Array<std::unique_ptr<Metadata>> metadata(
      new std::unique_ptr<Metadata>[comp_desc.metadata.count()], comp_desc.metadata.count());

  for (size_t i = 0; i < comp_desc.metadata.count(); i++) {
    std::unique_ptr<Metadata> md;
    zx_status_t status = Metadata::Create(comp_desc.metadata[i].data.count(), &md);
    if (status != ZX_OK) {
      return status;
    }

    md->type = comp_desc.metadata[i].key;
    md->length = comp_desc.metadata[i].data.count();
    memcpy(md->Data(), comp_desc.metadata[i].data.data(), md->length);
    metadata[i] = std::move(md);
  }

  auto dev = std::make_unique<CompositeDevice>(
      std::move(name), std::move(properties), comp_desc.components.count(),
      comp_desc.coresident_device_index, std::move(metadata));
  for (uint32_t i = 0; i < comp_desc.components.count(); ++i) {
    const auto& fidl_component = comp_desc.components[i];
    size_t parts_count = fidl_component.parts_count;
    fbl::Array<ComponentPartDescriptor> parts(new ComponentPartDescriptor[parts_count],
                                              parts_count);
    for (size_t j = 0; j < parts_count; ++j) {
      const auto& fidl_part = fidl_component.parts[j];
      size_t program_count = fidl_part.match_program_count;
      fbl::Array<zx_bind_inst_t> match_program(new zx_bind_inst_t[program_count], program_count);
      for (size_t k = 0; k < program_count; ++k) {
        match_program[k] = zx_bind_inst_t{
            .op = fidl_part.match_program[k].op,
            .arg = fidl_part.match_program[k].arg,
        };
      }
      parts[j] = {std::move(match_program)};
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
      log(ERROR, "driver_manager: ambiguous composite bind! composite='%s', dev1='%s', dev2='%s'\n",
          name_.data(), itr->bound_device()->name().data(), dev->name().data());
      return false;
    }
  }
  for (auto itr = unbound_.begin(); itr != unbound_.end(); ++itr) {
    if (itr->TryMatch(dev)) {
      log(SPEW, "driver_manager: found match for composite='%s', dev='%s'\n", name_.data(),
          dev->name().data());
      *index_out = itr->index();
      return true;
    }
  }
  log(SPEW, "driver_manager: no match for composite='%s', dev='%s'\n", name_.data(),
      dev->name().data());
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
  ZX_ASSERT(device_ == nullptr);
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

  Coordinator* coordinator = nullptr;
  uint64_t component_local_ids[fuchsia_device_manager_COMPONENTS_MAX] = {};

  // Create all of the proxies for the component devices, in the same process
  for (auto& component : bound_) {
    const auto& component_dev = component.component_device();
    auto bound_dev = component.bound_device();
    coordinator = component_dev->coordinator;

    // If the device we're bound to is proxied, we care about its proxy
    // rather than it, since that's the side that we communicate with.
    if (bound_dev->proxy()) {
      bound_dev = bound_dev->proxy();
    }

    // Check if we need to use the proxy.  If not, share a reference straight
    // to the target device rather than the instance of the component device
    // that bound to it.
    if (bound_dev->host() == devhost) {
      component_local_ids[component.index()] = bound_dev->local_id();
      continue;
    }

    // We need to create it.  Double check that we haven't ended up in a state
    // where the proxies would need to be in different processes.
    if (devhost != nullptr && component_dev->proxy() != nullptr &&
        component_dev->proxy()->host() != nullptr && component_dev->proxy()->host() != devhost) {
      log(ERROR, "driver_manager: cannot create composite, proxies in different processes\n");
      return ZX_ERR_BAD_STATE;
    }

    zx_status_t status = coordinator->PrepareProxy(component_dev, devhost);
    if (status != ZX_OK) {
      return status;
    }
    // If we hadn't picked a devhost, use the one that was created just now.
    if (devhost == nullptr) {
      devhost = component_dev->proxy()->host();
      ZX_ASSERT(devhost != nullptr);
    }
    // Stash the local ID after the proxy has been created
    component_local_ids[component.index()] = component_dev->proxy()->local_id();
  }

  zx::channel coordinator_rpc_local, coordinator_rpc_remote;
  zx_status_t status = zx::channel::create(0, &coordinator_rpc_local, &coordinator_rpc_remote);
  if (status != ZX_OK) {
    return status;
  }

  zx::channel device_controller_rpc_local, device_controller_rpc_remote;
  status = zx::channel::create(0, &device_controller_rpc_local, &device_controller_rpc_remote);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<Device> new_device;
  status = Device::CreateComposite(coordinator, devhost, *this, std::move(coordinator_rpc_local),
                                   std::move(device_controller_rpc_remote), &new_device);
  if (status != ZX_OK) {
    return status;
  }
  coordinator->devices().push_back(new_device);

  // Create the composite device in the devhost
  status = dh_send_create_composite_device(devhost, new_device.get(), *this, component_local_ids,
                                           std::move(coordinator_rpc_remote),
                                           std::move(device_controller_rpc_local));
  if (status != ZX_OK) {
    log(ERROR, "driver_manager: create composite device request failed: %s\n",
        zx_status_get_string(status));
    return status;
  }

  device_ = std::move(new_device);
  device_->set_composite(this);

  // Add metadata
  for (size_t i = 0; i < metadata_.size(); i++) {
    // Making a copy of metadata, instead of transfering ownership, so that
    // metadata can be added again if device is recreated
    status = coordinator->AddMetadata(device_, metadata_[i]->type, metadata_[i]->Data(),
                                      metadata_[i]->length);
    if (status != ZX_OK) {
      log(ERROR, "driver_manager: Failed to add metadata: %s\n", zx_status_get_string(status));
      return status;
    }
  }

  status = device_->SignalReadyForBind();
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

void CompositeDevice::UnbindComponent(CompositeDeviceComponent* component) {
  // If the composite was fully instantiated, diassociate from it.  It will be
  // reinstantiated when this component is re-bound.
  if (device_ != nullptr) {
    Remove();
  }
  ZX_ASSERT(device_ == nullptr);
  ZX_ASSERT(component->composite() == this);
  unbound_.push_back(bound_.erase(*component));
}

void CompositeDevice::Remove() {
  device_->disassociate_from_composite();
  device_ = nullptr;
}

// CompositeDeviceComponent methods

CompositeDeviceComponent::CompositeDeviceComponent(CompositeDevice* composite, uint32_t index,
                                                   fbl::Array<const ComponentPartDescriptor> parts)
    : composite_(composite), index_(index), parts_(std::move(parts)) {}

CompositeDeviceComponent::~CompositeDeviceComponent() = default;

bool CompositeDeviceComponent::TryMatch(const fbl::RefPtr<Device>& dev) {
  if (parts_.size() > UINT32_MAX) {
    return false;
  }
  auto match = ::internal::MatchParts(dev, parts_.data(), static_cast<uint32_t>(parts_.size()));
  if (match != ::internal::Match::One) {
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
  dev->push_component(this);

  return ZX_OK;
}

void CompositeDeviceComponent::Unbind() {
  ZX_ASSERT(bound_device_ != nullptr);
  composite_->UnbindComponent(this);
  // Drop our reference to the device added by the component driver
  component_device_ = nullptr;
  bound_device_->disassociate_from_composite();
  bound_device_ = nullptr;
}
