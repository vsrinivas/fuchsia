// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/driver_development.h"

#include "src/devices/bin/driver_manager/composite_device.h"

namespace fdd = fuchsia_driver_development;
namespace fdm = fuchsia_device_manager;

zx::status<std::vector<fuchsia_driver_development::wire::DriverInfo>> GetDriverInfo(
    fidl::AnyArena& allocator, const std::vector<const Driver*>& drivers) {
  std::vector<fdd::wire::DriverInfo> driver_info_vec;
  // TODO(fxbug.dev/80033): Support base drivers.
  for (const auto& driver : drivers) {
    fdd::wire::DriverInfo driver_info(allocator);
    driver_info.set_name(allocator,
                         fidl::StringView(allocator, {driver->name.data(), driver->name.size()}));
    driver_info.set_url(
        allocator, fidl::StringView(allocator, {driver->libname.data(), driver->libname.size()}));

    if (driver->bytecode_version == 1) {
      auto* binding = std::get_if<std::unique_ptr<zx_bind_inst_t[]>>(&driver->binding);
      if (!binding) {
        return zx::error(ZX_ERR_NOT_FOUND);
      }
      auto binding_insts = binding->get();

      uint32_t count = 0;
      if (driver->binding_size > 0) {
        count = driver->binding_size / sizeof(binding_insts[0]);
      }
      if (count > fdm::wire::kBindRulesInstructionsMax) {
        return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
      }

      using fdm::wire::BindInstruction;
      fidl::VectorView<BindInstruction> instructions(allocator, count);
      for (uint32_t i = 0; i < count; i++) {
        instructions[i] = BindInstruction{
            .op = binding_insts[i].op,
            .arg = binding_insts[i].arg,
            .debug = binding_insts[i].debug,
        };
      }
      driver_info.set_bind_rules(
          allocator, fdd::wire::BindRulesBytecode::WithBytecodeV1(allocator, instructions));

    } else if (driver->bytecode_version == 2) {
      auto* binding = std::get_if<std::unique_ptr<uint8_t[]>>(&driver->binding);
      if (!binding) {
        return zx::error(ZX_ERR_NOT_FOUND);
      }

      fidl::VectorView<uint8_t> bytecode(allocator, driver->binding_size);
      for (uint32_t i = 0; i < driver->binding_size; i++) {
        bytecode[i] = binding->get()[i];
      }

      driver_info.set_bind_rules(allocator,
                                 fdd::wire::BindRulesBytecode::WithBytecodeV2(allocator, bytecode));
    } else {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    driver_info_vec.push_back(std::move(driver_info));
  }

  return zx::ok(std::move(driver_info_vec));
}

zx::status<std::vector<fdd::wire::DeviceInfo>> GetDeviceInfo(
    fidl::AnyArena& allocator, const std::vector<fbl::RefPtr<Device>>& devices) {
  std::vector<fdd::wire::DeviceInfo> device_info_vec;
  for (const auto& device : devices) {
    if (device->props().size() > fdm::wire::kPropertiesMax) {
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    }
    if (device->str_props().size() > fdm::wire::kPropertiesMax) {
      return zx::error(ZX_ERR_BUFFER_TOO_SMALL);
    }

    fdd::wire::DeviceInfo device_info(allocator);

    // id leaks internal pointers, but since this is a development only API, it shouldn't be
    // a big deal.
    device_info.set_id(allocator, reinterpret_cast<uint64_t>(device.get()));

    // TODO(fxbug.dev/80094): Handle multiple parents case.
    fidl::VectorView<uint64_t> parent_ids(allocator, 1);
    parent_ids[0] = reinterpret_cast<uint64_t>(device->parent().get());
    device_info.set_parent_ids(allocator, parent_ids);

    size_t child_count = 0;
    for (const auto& child __attribute__((unused)) : device->children()) {
      child_count++;
    }
    if (child_count > 0) {
      fidl::VectorView<uint64_t> child_ids(allocator, child_count);
      size_t i = 0;
      for (const auto& child : device->children()) {
        child_ids[i++] = reinterpret_cast<uint64_t>(&child);
      }
      device_info.set_child_ids(allocator, child_ids);
    }

    if (device->host()) {
      device_info.set_driver_host_koid(allocator, device->host()->koid());
    }

    char path[fdm::wire::kDevicePathMax + 1];
    if (auto status = Coordinator::GetTopologicalPath(device, path, sizeof(path));
        status != ZX_OK) {
      return zx::error(status);
    }

    device_info.set_topological_path(allocator, fidl::StringView(allocator, {path, strlen(path)}));

    device_info.set_bound_driver_libname(
        allocator,
        fidl::StringView(allocator, {device->libname().data(), device->libname().size()}));

    fidl::VectorView<fdm::wire::DeviceProperty> props(allocator, device->props().size());
    for (size_t i = 0; i < device->props().size(); i++) {
      const auto& prop = device->props()[i];
      props[i] = fdm::wire::DeviceProperty{
          .id = prop.id,
          .reserved = prop.reserved,
          .value = prop.value,
      };
    }

    fidl::VectorView<fdm::wire::DeviceStrProperty> str_props(allocator, device->str_props().size());
    for (size_t i = 0; i < device->str_props().size(); i++) {
      const auto& str_prop = device->str_props()[i];
      if (str_prop.value.valueless_by_exception()) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }

      auto fidl_str_prop = fdm::wire::DeviceStrProperty{
          .key = fidl::StringView(allocator, str_prop.key),
      };

      switch (str_prop.value.index()) {
        case StrPropValueType::Integer: {
          const auto prop_val = std::get<StrPropValueType::Integer>(str_prop.value);
          fidl_str_prop.value = fdm::wire::PropertyValue::WithIntValue(prop_val);
          break;
        }
        case StrPropValueType::String: {
          const auto prop_val = std::get<StrPropValueType::String>(str_prop.value);
          fidl_str_prop.value = fdm::wire::PropertyValue::WithStrValue(
              allocator, fidl::StringView(allocator, prop_val));
          break;
        }
        case StrPropValueType::Bool: {
          const auto prop_val = std::get<StrPropValueType::Bool>(str_prop.value);
          fidl_str_prop.value = fdm::wire::PropertyValue::WithBoolValue(prop_val);
          break;
        }
        case StrPropValueType::Enum: {
          const auto prop_val = std::get<StrPropValueType::Enum>(str_prop.value);
          fidl_str_prop.value = fdm::wire::PropertyValue::WithEnumValue(
              allocator, fidl::StringView(allocator, prop_val));
          break;
        }
      }

      str_props[i] = fidl_str_prop;
    }

    device_info.set_property_list(allocator, fdm::wire::DevicePropertyList{
                                                 .props = props,
                                                 .str_props = str_props,
                                             });

    device_info.set_flags(fdd::wire::DeviceFlags::TruncatingUnknown(device->flags));

    device_info_vec.push_back(std::move(device_info));
  }
  return zx::ok(std::move(device_info_vec));
}
