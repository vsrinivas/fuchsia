// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_GROUP_DESC_UTIL_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_GROUP_DESC_UTIL_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/ddk/device.h>

zx::result<fuchsia_driver_framework::wire::BindRule> ConvertBindRuleToFidl(
    fidl::AnyArena& allocator, device_group_bind_rule_t bind_rule) {
  fuchsia_driver_framework::wire::NodePropertyKey property_key;

  switch (bind_rule.key.key_type) {
    case DEVICE_BIND_PROPERTY_KEY_INT: {
      property_key =
          fuchsia_driver_framework::wire::NodePropertyKey::WithIntValue(bind_rule.key.data.int_key);
      break;
    }
    case DEVICE_BIND_PROPERTY_KEY_STRING: {
      property_key = fuchsia_driver_framework::wire::NodePropertyKey::WithStringValue(
          allocator, allocator, bind_rule.key.data.str_key);
      break;
    }
    default: {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  auto bind_rule_values = fidl::VectorView<fuchsia_driver_framework::wire::NodePropertyValue>(
      allocator, bind_rule.values_count);
  for (size_t i = 0; i < bind_rule.values_count; i++) {
    auto bind_rule_val = bind_rule.values[i];
    switch (bind_rule_val.data_type) {
      case ZX_DEVICE_PROPERTY_VALUE_INT: {
        bind_rule_values[i] = fuchsia_driver_framework::wire::NodePropertyValue::WithIntValue(
            bind_rule_val.data.int_value);
        break;
      }
      case ZX_DEVICE_PROPERTY_VALUE_STRING: {
        auto str_val =
            fidl::ObjectView<fidl::StringView>(allocator, allocator, bind_rule_val.data.str_value);
        bind_rule_values[i] =
            fuchsia_driver_framework::wire::NodePropertyValue::WithStringValue(str_val);
        break;
      }
      case ZX_DEVICE_PROPERTY_VALUE_BOOL: {
        bind_rule_values[i] = fuchsia_driver_framework::wire::NodePropertyValue::WithBoolValue(
            bind_rule_val.data.bool_value);
        break;
      }
      case ZX_DEVICE_PROPERTY_VALUE_ENUM: {
        auto enum_val =
            fidl::ObjectView<fidl::StringView>(allocator, allocator, bind_rule_val.data.enum_value);
        bind_rule_values[i] =
            fuchsia_driver_framework::wire::NodePropertyValue::WithEnumValue(enum_val);
        break;
      }
      default: {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
    }
  }

  fuchsia_driver_framework::wire::Condition condition;
  switch (bind_rule.condition) {
    case DEVICE_BIND_RULE_CONDITION_ACCEPT: {
      condition = fuchsia_driver_framework::wire::Condition::kAccept;
      break;
    }
    case DEVICE_BIND_RULE_CONDITION_REJECT: {
      condition = fuchsia_driver_framework::wire::Condition::kReject;
      break;
    }
    default: {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  return zx::ok(fuchsia_driver_framework::wire::BindRule{
      .key = property_key,
      .condition = condition,
      .values = bind_rule_values,
  });
}

zx::result<fuchsia_driver_framework::wire::NodeProperty> ConvertBindPropToFidl(
    fidl::AnyArena& allocator, const device_bind_prop_t& bind_prop) {
  auto node_property = fuchsia_driver_framework::wire::NodeProperty::Builder(allocator);

  switch (bind_prop.key.key_type) {
    case DEVICE_BIND_PROPERTY_KEY_INT: {
      node_property.key(fuchsia_driver_framework::wire::NodePropertyKey::WithIntValue(
          bind_prop.key.data.int_key));
      break;
    }
    case DEVICE_BIND_PROPERTY_KEY_STRING: {
      node_property.key(fuchsia_driver_framework::wire::NodePropertyKey::WithStringValue(
          allocator, allocator, bind_prop.key.data.str_key));
      break;
    }
    default: {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  switch (bind_prop.value.data_type) {
    case ZX_DEVICE_PROPERTY_VALUE_INT: {
      node_property.value(fuchsia_driver_framework::wire::NodePropertyValue::WithIntValue(
          bind_prop.value.data.int_value));
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_STRING: {
      node_property.value(fuchsia_driver_framework::wire::NodePropertyValue::WithStringValue(
          allocator, allocator, bind_prop.value.data.str_value));
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_BOOL: {
      node_property.value(fuchsia_driver_framework::wire::NodePropertyValue::WithBoolValue(
          bind_prop.value.data.bool_value));
      break;
    }
    case ZX_DEVICE_PROPERTY_VALUE_ENUM: {
      node_property.value(fuchsia_driver_framework::wire::NodePropertyValue::WithEnumValue(
          fidl::ObjectView<fidl::StringView>(allocator, allocator,
                                             bind_prop.value.data.enum_value)));
      break;
    }
    default: {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  return zx::ok(node_property.Build());
}

zx::result<fuchsia_driver_framework::wire::DeviceGroupNode> ConvertDeviceGroupNode(
    fidl::AnyArena& allocator, device_group_node_t node) {
  fidl::VectorView<fuchsia_driver_framework::wire::BindRule> bind_rules(allocator,
                                                                        node.bind_rule_count);
  for (size_t i = 0; i < node.bind_rule_count; i++) {
    auto bind_rule_result = ConvertBindRuleToFidl(allocator, node.bind_rules[i]);
    if (!bind_rule_result.is_ok()) {
      return bind_rule_result.take_error();
    }

    bind_rules[i] = std::move(bind_rule_result.value());
  }

  fidl::VectorView<fuchsia_driver_framework::wire::NodeProperty> bind_props(
      allocator, node.bind_property_count);
  for (size_t i = 0; i < node.bind_property_count; i++) {
    auto bind_prop_result = ConvertBindPropToFidl(allocator, node.bind_properties[i]);
    if (!bind_prop_result.is_ok()) {
      return bind_prop_result.take_error();
    }

    bind_props[i] = std::move(bind_prop_result.value());
  }

  return zx::ok(fuchsia_driver_framework::wire::DeviceGroupNode{
      .bind_rules = bind_rules,
      .bind_properties = bind_props,
  });
}

#endif  //   SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_GROUP_DESC_UTIL_H_
