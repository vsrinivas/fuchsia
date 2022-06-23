// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "binding_internal.h"

#include "src/devices/lib/bind/ffi_bindings.h"
#include "src/devices/lib/log/log.h"
#include "src/lib/fxl/strings/utf_codecs.h"

bool can_driver_bind(const Driver* drv, uint32_t protocol_id,
                     const fbl::Array<const zx_device_prop_t>& props,
                     const fbl::Array<const StrProperty>& str_props, bool autobind) {
  if (drv->bytecode_version == 1) {
    auto* binding = std::get_if<std::unique_ptr<zx_bind_inst_t[]>>(&drv->binding);
    if (!binding && drv->binding_size > 0) {
      return false;
    }

    internal::BindProgramContext ctx;
    ctx.props = &props;
    ctx.protocol_id = protocol_id;
    ctx.binding = binding ? binding->get() : nullptr;
    ctx.binding_size = drv->binding_size;
    ctx.name = drv->name.c_str();
    ctx.autobind = autobind ? 1 : 0;
    return internal::EvaluateBindProgram(&ctx);
  }

  if (drv->bytecode_version == 2) {
    auto* bytecode = std::get_if<std::unique_ptr<uint8_t[]>>(&drv->binding);
    if (!bytecode && drv->binding_size > 0) {
      return false;
    }

    fbl::Array<device_property_t> properties(new device_property_t[props.size()], props.size());
    for (size_t i = 0; i < props.size(); i++) {
      properties[i] = device_property_t{.key = props[i].id, .value = props[i].value};
    }

    fbl::Array<device_str_property_t> str_properties(new device_str_property_t[str_props.size()],
                                                     str_props.size());
    for (size_t i = 0; i < str_props.size(); i++) {
      if (!fxl::IsStringUTF8(str_props[i].key)) {
        LOGF(ERROR, "String property key is not in UTF-8 encoding");
        return false;
      }

      if (str_props[i].value.valueless_by_exception()) {
        LOGF(ERROR, "String property value is not set");
        return false;
      }

      switch (str_props[i].value.index()) {
        case StrPropValueType::Integer: {
          const auto prop_val = std::get<StrPropValueType::Integer>(str_props[i].value);
          str_properties[i] = str_property_with_int(str_props[i].key.c_str(), prop_val);
          break;
        }
        case StrPropValueType::String: {
          auto* prop_val = std::get_if<StrPropValueType::String>(&str_props[i].value);
          if (prop_val && !fxl::IsStringUTF8(*prop_val)) {
            LOGF(ERROR, "String property value is not in UTF-8 encoding");
            return false;
          }
          str_properties[i] = str_property_with_string(str_props[i].key.c_str(), prop_val->c_str());
          break;
        }
        case StrPropValueType::Bool: {
          const auto prop_val = std::get<StrPropValueType::Bool>(str_props[i].value);
          str_properties[i] = str_property_with_bool(str_props[i].key.c_str(), prop_val);
          break;
        }
        case StrPropValueType::Enum: {
          auto* prop_val = std::get_if<StrPropValueType::Enum>(&str_props[i].value);
          if (prop_val && !fxl::IsStringUTF8(*prop_val)) {
            LOGF(ERROR, "Enum property value is not in UTF-8 encoding");
            return false;
          }
          str_properties[i] = str_property_with_enum(str_props[i].key.c_str(), prop_val->c_str());
          break;
        }
        default: {
          LOGF(ERROR, "String property value type is invalid.");
          return false;
        }
      }
    }

    return match_bind_rules(bytecode ? bytecode->get() : nullptr, drv->binding_size,
                            properties.get(), props.size(), str_properties.get(),
                            str_properties.size(), protocol_id, autobind);
  }

  LOGF(ERROR, "Invalid bytecode version: %i", drv->bytecode_version);
  return false;
}
