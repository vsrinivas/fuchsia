// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "composite_device.h"

#include <lib/fidl/cpp/wire/arena.h>
#include <zircon/status.h>

#include <string_view>
#include <utility>

#include "src/devices/bin/driver_manager/binding.h"
#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/lib/log/log.h"

namespace fdm = fuchsia_device_manager;

namespace {

fbl::Array<StrProperty> ConvertStringProperties(
    fidl::VectorView<fdm::wire::DeviceStrProperty> str_props) {
  fbl::Array<StrProperty> str_properties(new StrProperty[str_props.count()], str_props.count());
  for (size_t i = 0; i < str_props.count(); i++) {
    str_properties[i].key = str_props[i].key.get();
    if (str_props[i].value.is_int_value()) {
      str_properties[i].value.emplace<StrPropValueType::Integer>(str_props[i].value.int_value());
    } else if (str_props[i].value.is_str_value()) {
      str_properties[i].value.emplace<StrPropValueType::String>(
          std::string(str_props[i].value.str_value().get()));
    } else if (str_props[i].value.is_bool_value()) {
      str_properties[i].value.emplace<StrPropValueType::Bool>(str_props[i].value.bool_value());
    } else if (str_props[i].value.is_enum_value()) {
      str_properties[i].value.emplace<StrPropValueType::Enum>(
          std::string(str_props[i].value.enum_value().get()));
    }
  }

  return str_properties;
}

}  // namespace

// CompositeDevice methods

CompositeDevice::CompositeDevice(fbl::String name, fbl::Array<const zx_device_prop_t> properties,
                                 fbl::Array<const StrProperty> str_properties,
                                 uint32_t fragments_count, uint32_t primary_fragment_index,
                                 bool spawn_colocated,
                                 fbl::Array<std::unique_ptr<Metadata>> metadata,
                                 bool from_driver_index)
    : name_(std::move(name)),
      properties_(std::move(properties)),
      str_properties_(std::move(str_properties)),
      fragments_count_(fragments_count),
      primary_fragment_index_(primary_fragment_index),
      spawn_colocated_(spawn_colocated),
      metadata_(std::move(metadata)),
      from_driver_index_(from_driver_index) {}

CompositeDevice::~CompositeDevice() = default;

zx_status_t CompositeDevice::Create(std::string_view name,
                                    fdm::wire::CompositeDeviceDescriptor comp_desc,
                                    std::unique_ptr<CompositeDevice>* out) {
  fbl::String name_obj(name);
  fbl::Array<zx_device_prop_t> properties(new zx_device_prop_t[comp_desc.props.count() + 1],
                                          comp_desc.props.count() + 1);
  memcpy(properties.data(), comp_desc.props.data(),
         comp_desc.props.count() * sizeof(comp_desc.props.data()[0]));

  // Set a property unique to composite devices.
  properties[comp_desc.props.count()].id = BIND_COMPOSITE;
  properties[comp_desc.props.count()].value = 1;

  fbl::Array<std::unique_ptr<Metadata>> metadata(
      new std::unique_ptr<Metadata>[comp_desc.metadata.count()], comp_desc.metadata.count());

  fbl::Array<StrProperty> str_properties = ConvertStringProperties(comp_desc.str_props);

  for (size_t i = 0; i < comp_desc.metadata.count(); i++) {
    std::unique_ptr<Metadata> md;
    zx_status_t status = Metadata::Create(comp_desc.metadata[i].data.count(), &md);
    if (status != ZX_OK) {
      return status;
    }

    md->type = comp_desc.metadata[i].key;
    md->length = static_cast<uint32_t>(comp_desc.metadata[i].data.count());
    memcpy(md->Data(), comp_desc.metadata[i].data.data(), md->length);
    metadata[i] = std::move(md);
  }

  auto dev = std::make_unique<CompositeDevice>(
      std::move(name), std::move(properties), std::move(str_properties),
      comp_desc.fragments.count(), comp_desc.primary_fragment_index, comp_desc.spawn_colocated,
      std::move(metadata), false);
  for (uint32_t i = 0; i < comp_desc.fragments.count(); ++i) {
    const auto& fidl_fragment = comp_desc.fragments[i];
    size_t parts_count = fidl_fragment.parts.count();
    if (parts_count != 1) {
      LOGF(ERROR, "Composite fragments with multiple parts are deprecated. %s has %zd parts.",
           name.data(), parts_count);
      return ZX_ERR_INVALID_ARGS;
    }

    const auto& fidl_part = fidl_fragment.parts[0];
    size_t program_count = fidl_part.match_program.count();
    fbl::Array<zx_bind_inst_t> bind_rules(new zx_bind_inst_t[program_count], program_count);
    for (size_t j = 0; j < program_count; ++j) {
      bind_rules[j] = zx_bind_inst_t{
          .op = fidl_part.match_program[j].op,
          .arg = fidl_part.match_program[j].arg,
      };
    }
    std::string name(fidl_fragment.name.data(), fidl_fragment.name.size());
    auto fragment =
        std::make_unique<CompositeDeviceFragment>(dev.get(), name, i, std::move(bind_rules));
    dev->fragments_.push_back(std::move(fragment));
  }
  *out = std::move(dev);
  return ZX_OK;
}

std::unique_ptr<CompositeDevice> CompositeDevice::CreateFromDriverIndex(
    MatchedCompositeDriverInfo driver, uint32_t primary_index,
    fbl::Array<std::unique_ptr<Metadata>> metadata) {
  fbl::String name(driver.composite.name);
  auto dev = std::make_unique<CompositeDevice>(
      std::move(name), fbl::Array<const zx_device_prop_t>(), fbl::Array<const StrProperty>(),
      driver.composite.num_nodes, primary_index, driver.driver_info.colocate, std::move(metadata),
      true);

  for (uint32_t i = 0; i < driver.composite.num_nodes; ++i) {
    std::string name = driver.composite.node_names[i];
    auto fragment = std::make_unique<CompositeDeviceFragment>(dev.get(), std::string(name), i,
                                                              fbl::Array<const zx_bind_inst_t>());
    dev->fragments_.push_back(std::move(fragment));
  }
  dev->driver_index_driver_ = driver.driver_info;
  return dev;
}

bool CompositeDevice::IsFragmentMatch(const fbl::RefPtr<Device>& dev, size_t* index_out) const {
  if (from_driver_index_) {
    return false;
  }

  // Check bound fragments for ambiguous binds.
  for (auto& fragment : fragments_) {
    if (!fragment.IsBound() || !fragment.TryMatch(dev)) {
      continue;
    }
    LOGF(ERROR, "Ambiguous bind for composite device %p '%s': device 1 '%s', device 2 '%s'", this,
         name_.data(), fragment.bound_device()->name().data(), dev->name().data());
    return false;
  }

  // Check unbound fragments for matches.
  for (auto& fragment : fragments_) {
    if (fragment.IsBound() || !fragment.TryMatch(dev)) {
      continue;
    }
    VLOGF(1, "Found a match for composite device %p '%s': device '%s'", this, name_.data(),
          dev->name().data());
    *index_out = fragment.index();
    return true;
  }

  VLOGF(1, "No match for composite device %p '%s': device '%s'", this, name_.data(),
        dev->name().data());
  return false;
}

zx_status_t CompositeDevice::TryMatchBindFragments(const fbl::RefPtr<Device>& dev) {
  size_t index;
  if (!IsFragmentMatch(dev, &index)) {
    return ZX_OK;
  }

  if (dev->name() == "sysmem-fidl" || dev->name() == "sysmem-banjo") {
    VLOGF(1, "Device '%s' matched fragment %zu of composite '%s'", dev->name().data(), index,
          name().data());
  } else {
    LOGF(INFO, "Device '%s' matched fragment %zu of composite '%s'", dev->name().data(), index,
         name().data());
  }
  auto status = BindFragment(index, dev);
  if (status != ZX_OK) {
    LOGF(ERROR, "Device '%s' failed to bind fragment %zu of composite '%s': %s", dev->name().data(),
         index, name().data(), zx_status_get_string(status));
  }
  return status;
}

zx_status_t CompositeDevice::BindFragment(size_t index, const fbl::RefPtr<Device>& dev) {
  // Find the fragment we're binding
  CompositeDeviceFragment* fragment = nullptr;
  for (auto& f : fragments_) {
    if (f.index() == index) {
      fragment = &f;
      break;
    }
  }

  if (!fragment || fragment->IsBound()) {
    LOGF(ERROR, "Attempted to bind bound fragment %zu in composite device %p", index, name_.data());
    return ZX_OK;
  }

  zx_status_t status = fragment->Bind(dev);
  if (status != ZX_OK) {
    return status;
  }
  if (dev->has_outgoing_directory()) {
    status = TryAssemble();
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      LOGF(ERROR, "Failed to assemble composite device: %s", zx_status_get_string(status));
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t CompositeDevice::TryAssemble() {
  ZX_ASSERT(device_ == nullptr);

  for (auto& fragment : fragments_) {
    if (!fragment.IsReady()) {
      return ZX_ERR_SHOULD_WAIT;
    }
  }

  // Find or create the driver_host to put everything in.
  Coordinator* coordinator = primary_fragment()->bound_device()->coordinator;
  fbl::RefPtr<DriverHost> driver_host;
  if (spawn_colocated_) {
    if (const CompositeDeviceFragment* fragment = primary_fragment(); fragment != nullptr) {
      driver_host = fragment->bound_device()->host();
    }
  } else {
    zx_status_t status = coordinator->NewDriverHost("driver_host:composite", &driver_host);
    if (status != ZX_OK) {
      return status;
    }
  }

  fidl::Arena allocator;
  fidl::VectorView<fdm::wire::Fragment> fragments(allocator, fragments_.size_slow());

  // Create all of the proxies for the fragment devices, in the same process
  for (auto& fragment : fragments_) {
    zx_status_t status = fragment.CreateProxy(driver_host);
    if (status != ZX_OK) {
      return status;
    }
    // Stash the local ID after the proxy has been created
    fragments[fragment.index()].name = fidl::StringView::FromExternal(fragment.name());
    fragments[fragment.index()].id = fragment.proxy_device()->local_id();
  }

  auto coordinator_endpoints = fidl::CreateEndpoints<fdm::Coordinator>();
  if (coordinator_endpoints.is_error()) {
    return coordinator_endpoints.error_value();
  }

  auto device_controller_endpoints = fidl::CreateEndpoints<fdm::DeviceController>();
  if (device_controller_endpoints.is_error()) {
    return device_controller_endpoints.error_value();
  }

  fbl::RefPtr<Device> new_device;
  auto status = Device::CreateComposite(
      coordinator, driver_host, *this, std::move(coordinator_endpoints->server),
      std::move(device_controller_endpoints->client), &new_device);
  if (status != ZX_OK) {
    return status;
  }
  coordinator->device_manager()->AddToDevices(new_device);

  // Create the composite device in the driver_host
  fdm::wire::CompositeDevice composite{fragments, fidl::StringView::FromExternal(name())};
  auto type = fdm::wire::DeviceType::WithComposite(allocator, composite);

  driver_host->controller()
      ->CreateDevice(std::move(coordinator_endpoints->client),
                     std::move(device_controller_endpoints->server), std::move(type),
                     new_device->local_id())
      .ThenExactlyOnce(
          [](fidl::WireUnownedResult<fdm::DriverHostController::CreateDevice>& result) {
            if (!result.ok()) {
              LOGF(ERROR, "Failed to create composite device: %s",
                   result.error().FormatDescription().c_str());
              return;
            }
            if (result.value().status != ZX_OK) {
              LOGF(ERROR, "Failed to create composite device: %s",
                   zx_status_get_string(result.value().status));
            }
          });

  device_ = std::move(new_device);

  // Add metadata
  for (size_t i = 0; i < metadata_.size(); i++) {
    // Making a copy of metadata, instead of transfering ownership, so that
    // metadata can be added again if device is recreated
    status = coordinator->AddMetadata(device_, metadata_[i]->type, metadata_[i]->Data(),
                                      metadata_[i]->length);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to add metadata to device %p '%s': %s", device_.get(),
           device_->name().data(), zx_status_get_string(status));
      return status;
    }
  }

  status = device_->SignalReadyForBind();
  if (status != ZX_OK) {
    return status;
  }
  if (from_driver_index_) {
    zx_status_t status = coordinator->AttemptBind(driver_index_driver_, device_);
    if (status != ZX_OK) {
      LOGF(ERROR, "%s: Failed to bind composite driver '%s' to device '%s': %s", __func__,
           driver_index_driver_.name(), device_->name().data(), zx_status_get_string(status));
    }
    return status;
  }

  return ZX_OK;
}

void CompositeDevice::UnbindFragment(CompositeDeviceFragment* fragment) {
  // If the composite was fully instantiated, disassociate from it.  It will be
  // reinstantiated when this fragment is re-bound.
  if (device_ != nullptr) {
    Remove();
  }
  ZX_ASSERT(device_ == nullptr);
  ZX_ASSERT(fragment->composite() == this);
}

void CompositeDevice::Remove() {
  if (device_ != nullptr) {
    device_->disassociate_from_composite();
    device_ = nullptr;
  }
}

// CompositeDeviceFragment methods

CompositeDeviceFragment::CompositeDeviceFragment(CompositeDevice* composite, std::string name,
                                                 uint32_t index,
                                                 fbl::Array<const zx_bind_inst_t> bind_rules)
    : composite_(composite), name_(name), index_(index), bind_rules_(std::move(bind_rules)) {}

CompositeDeviceFragment::~CompositeDeviceFragment() = default;

bool CompositeDeviceFragment::TryMatch(const fbl::RefPtr<Device>& dev) const {
  internal::BindProgramContext ctx;
  ctx.props = &dev->props();
  ctx.protocol_id = dev->protocol_id();
  ctx.binding = bind_rules_.data();
  ctx.binding_size = bind_rules_.size() * sizeof(bind_rules_[0]);
  ctx.name = "composite_binder";
  ctx.autobind = true;
  return internal::EvaluateBindProgram(&ctx);
}

zx_status_t CompositeDeviceFragment::Bind(const fbl::RefPtr<Device>& dev) {
  ZX_ASSERT(bound_device_ == nullptr);

  if (!dev->has_outgoing_directory()) {
    zx_status_t status = dev->coordinator->AttemptBind(
        MatchedDriverInfo{.driver = dev->coordinator->fragment_driver(), .colocate = true}, dev);
    if (status != ZX_OK) {
      return status;
    }
    bound_device_ = dev;
    dev->push_fragment(this);
  } else {
    bound_device_ = dev;
    dev->push_fragment(this);
    dev->flags |= DEV_CTX_BOUND;
  }

  return ZX_OK;
}

bool CompositeDeviceFragment::IsReady() {
  if (!IsBound()) {
    return false;
  }

  return fragment_device() != nullptr || bound_device()->has_outgoing_directory();
}

zx_status_t CompositeDeviceFragment::CreateProxy(fbl::RefPtr<DriverHost> driver_host) {
  if (!IsReady()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  // If we've already created one, then don't redo work.
  if (proxy_device_) {
    return ZX_OK;
  }

  fbl::RefPtr<Device> parent = bound_device();

  // If the device we're bound to is proxied, we care about its proxy
  // rather than it, since that's the side that we communicate with.
  if (bound_device()->proxy()) {
    parent = bound_device()->proxy();
  }

  // Check if we need to create a proxy. If not, share a reference to
  // the instance of the fragment device.
  // We always use a proxy when there is an outgoing directory involved.
  if (parent->host() == driver_host && !parent->has_outgoing_directory()) {
    proxy_device_ = fragment_device_;
    return ZX_OK;
  }

  // Create a FIDL proxy.
  if (parent->has_outgoing_directory()) {
    VLOGF(1, "Preparing FIDL proxy for %s", parent->name().data());
    fbl::RefPtr<Device> fidl_proxy;
    zx_status_t status = parent->coordinator->PrepareFidlProxy(parent, driver_host, &fidl_proxy);
    if (status != ZX_OK) {
      return status;
    }
    proxy_device_ = fidl_proxy;
    return ZX_OK;
  }

  // Create a Banjo proxy.

  // Double check that we haven't ended up in a state
  // where the proxies would need to be in different processes.
  if (driver_host != nullptr && fragment_device() != nullptr &&
      fragment_device()->proxy() != nullptr && fragment_device()->proxy()->host() != nullptr &&
      fragment_device()->proxy()->host() != driver_host) {
    LOGF(ERROR, "Cannot create composite device, device proxies are in different driver_hosts");
    return ZX_ERR_BAD_STATE;
  }

  VLOGF(1, "Preparing Banjo proxy for %s", fragment_device()->name().data());
  zx_status_t status = bound_device()->coordinator->PrepareProxy(fragment_device(), driver_host);
  if (status != ZX_OK) {
    return status;
  }
  proxy_device_ = fragment_device()->proxy();
  return ZX_OK;
}

void CompositeDeviceFragment::Unbind() {
  ZX_ASSERT(bound_device_ != nullptr);
  composite_->UnbindFragment(this);
  // Drop our reference to any devices we've created.
  proxy_device_ = nullptr;
  fragment_device_ = nullptr;

  bound_device_->disassociate_from_composite();
  bound_device_ = nullptr;
}
