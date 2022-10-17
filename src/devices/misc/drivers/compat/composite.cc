// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/composite.h"

#include <lib/ddk/binding_priv.h>
#include <lib/ddk/device.h>

namespace compat {

namespace {

fuchsia_device_manager::wire::DeviceProperty convert_device_prop(const zx_device_prop_t& prop) {
  return fuchsia_device_manager::wire::DeviceProperty{
      .id = prop.id,
      .reserved = prop.reserved,
      .value = prop.value,
  };
}

bool property_value_type_valid(uint32_t value_type) {
  return value_type > ZX_DEVICE_PROPERTY_VALUE_UNDEFINED &&
         value_type <= ZX_DEVICE_PROPERTY_VALUE_ENUM;
}

fuchsia_device_manager::wire::DeviceStrProperty convert_device_str_prop(
    const zx_device_str_prop_t& prop, fidl::AnyArena& allocator) {
  ZX_ASSERT(property_value_type_valid(prop.property_value.data_type));

  auto str_property = fuchsia_device_manager::wire::DeviceStrProperty{
      .key = fidl::StringView(allocator, prop.key),
  };

  switch (prop.property_value.data_type) {
    case ZX_DEVICE_PROPERTY_VALUE_INT: {
      str_property.value = fuchsia_device_manager::wire::PropertyValue::WithIntValue(
          prop.property_value.data.int_val);
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_STRING: {
      str_property.value = fuchsia_device_manager::wire::PropertyValue::WithStrValue(
          allocator, allocator, prop.property_value.data.str_val);
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_BOOL: {
      str_property.value = fuchsia_device_manager::wire::PropertyValue::WithBoolValue(
          prop.property_value.data.bool_val);
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_ENUM: {
      str_property.value = fuchsia_device_manager::wire::PropertyValue::WithEnumValue(
          fidl::ObjectView<fidl::StringView>(allocator, allocator,
                                             prop.property_value.data.enum_val));
      break;
    }
  }

  return str_property;
}

}  // namespace

zx::result<fuchsia_device_manager::wire::CompositeDeviceDescriptor> CreateComposite(
    fidl::AnyArena& arena, const composite_device_desc_t* comp_desc) {
  fidl::VectorView<fuchsia_device_manager::wire::DeviceFragment> compvec(
      arena, comp_desc->fragments_count);
  for (size_t i = 0; i < comp_desc->fragments_count; i++) {
    fuchsia_device_manager::wire::DeviceFragment dc;
    dc.name = ::fidl::StringView::FromExternal(comp_desc->fragments[i].name,
                                               strnlen(comp_desc->fragments[i].name, 32));
    dc.parts.Allocate(arena, comp_desc->fragments[i].parts_count);

    for (uint32_t j = 0; j < comp_desc->fragments[i].parts_count; j++) {
      dc.parts[j].match_program.Allocate(arena, comp_desc->fragments[i].parts[j].instruction_count);

      for (uint32_t k = 0; k < comp_desc->fragments[i].parts[j].instruction_count; k++) {
        dc.parts[j].match_program[k] = fuchsia_device_manager::wire::BindInstruction{
            .op = comp_desc->fragments[i].parts[j].match_program[k].op,
            .arg = comp_desc->fragments[i].parts[j].match_program[k].arg,
            .debug = comp_desc->fragments[i].parts[j].match_program[k].debug,
        };
      }
    }
    compvec[i] = std::move(dc);
  }

  fidl::VectorView<fuchsia_device_manager::wire::DeviceMetadata> metadata(
      arena, comp_desc->metadata_count);
  for (size_t i = 0; i < comp_desc->metadata_count; i++) {
    auto meta = fuchsia_device_manager::wire::DeviceMetadata{
        .key = comp_desc->metadata_list[i].type,
        .data = fidl::VectorView<uint8_t>::FromExternal(
            reinterpret_cast<uint8_t*>(const_cast<void*>(comp_desc->metadata_list[i].data)),
            comp_desc->metadata_list[i].length)};
    metadata[i] = std::move(meta);
  }

  fidl::VectorView<fuchsia_device_manager::wire::DeviceProperty> props(arena,
                                                                       comp_desc->props_count);
  for (size_t i = 0; i < comp_desc->props_count; i++) {
    props[i] = convert_device_prop(comp_desc->props[i]);
  }

  fidl::VectorView<fuchsia_device_manager::wire::DeviceStrProperty> str_props(
      arena, comp_desc->str_props_count);
  for (size_t i = 0; i < comp_desc->str_props_count; i++) {
    str_props[i] = convert_device_str_prop(comp_desc->str_props[i], arena);
  }

  uint32_t primary_fragment_index = UINT32_MAX;
  for (size_t i = 0; i < comp_desc->fragments_count; i++) {
    if (strcmp(comp_desc->primary_fragment, comp_desc->fragments[i].name) == 0) {
      primary_fragment_index = static_cast<uint32_t>(i);
      break;
    }
  }
  if (primary_fragment_index == UINT32_MAX) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  fuchsia_device_manager::wire::CompositeDeviceDescriptor comp_dev = {
      .props = props,
      .str_props = str_props,
      .fragments = compvec,
      .primary_fragment_index = primary_fragment_index,
      .spawn_colocated = comp_desc->spawn_colocated,
      .metadata = metadata};
  return zx::ok(std::move(comp_dev));
}

}  // namespace compat
