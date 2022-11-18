// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bind::compiler::symbol_table::get_deprecated_key_identifier,
    fidl_fuchsia_driver_framework as fdf,
};

pub fn node_property_key_to_string(key: &fdf::NodePropertyKey) -> String {
    match key {
        fdf::NodePropertyKey::IntValue(int_key) => {
            let deprecated_key = get_deprecated_key_identifier(*int_key);
            match deprecated_key {
                Some(value) => value,
                None => format!("{:#08x}", int_key),
            }
        }
        fdf::NodePropertyKey::StringValue(str_key) => {
            format!("\"{}\"", str_key)
        }
    }
}

pub fn node_property_value_to_string(value: &fdf::NodePropertyValue) -> String {
    match value {
        fdf::NodePropertyValue::IntValue(int_val) => {
            format!("{:#08x}", int_val)
        }
        fdf::NodePropertyValue::StringValue(str_val) => {
            format!("\"{}\"", str_val)
        }
        fdf::NodePropertyValue::BoolValue(bool_val) => bool_val.to_string(),
        fdf::NodePropertyValue::EnumValue(enum_val) => {
            format!("Enum({})", enum_val)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    fn test_node_property_key_to_string() {
        assert_eq!(
            "fuchsia.BIND_PROTOCOL".to_string(),
            node_property_key_to_string(&fdf::NodePropertyKey::IntValue(0x0001))
        );

        assert_eq!(
            "0x000bbb".to_string(),
            node_property_key_to_string(&fdf::NodePropertyKey::IntValue(0x0BBB))
        );

        assert_eq!(
            "0xffffffff".to_string(),
            node_property_key_to_string(&fdf::NodePropertyKey::IntValue(0xFFFFFFFF))
        );
    }

    #[fuchsia::test]
    fn test_node_property_value_to_string() {
        assert_eq!(
            "0x000001".to_string(),
            node_property_value_to_string(&fdf::NodePropertyValue::IntValue(0x0001))
        );

        assert_eq!(
            "0x000bbb".to_string(),
            node_property_value_to_string(&fdf::NodePropertyValue::IntValue(0x0BBB))
        );

        assert_eq!(
            "0xffffffff".to_string(),
            node_property_value_to_string(&fdf::NodePropertyValue::IntValue(0xFFFFFFFF))
        );

        assert_eq!(
            "\"Hello\"".to_string(),
            node_property_value_to_string(&fdf::NodePropertyValue::StringValue(
                "Hello".to_string()
            ))
        );

        assert_eq!(
            "true".to_string(),
            node_property_value_to_string(&fdf::NodePropertyValue::BoolValue(true))
        );

        assert_eq!(
            "false".to_string(),
            node_property_value_to_string(&fdf::NodePropertyValue::BoolValue(false))
        );

        assert_eq!(
            "Enum(Hello.World)".to_string(),
            node_property_value_to_string(&fdf::NodePropertyValue::EnumValue(
                "Hello.World".to_string()
            ))
        );
    }
}
