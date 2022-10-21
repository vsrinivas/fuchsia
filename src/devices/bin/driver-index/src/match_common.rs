// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resolved_driver::ResolvedDriver,
    bind::ddk_bind_constants::BIND_PROTOCOL,
    bind::interpreter::decode_bind_rules::DecodedCompositeBindRules,
    bind::interpreter::match_bind::{DeviceProperties, PropertyKey},
    fidl_fuchsia_driver_framework as fdf,
    fuchsia_zircon::{zx_status_t, Status},
};

pub fn node_to_device_property(
    node_properties: &Vec<fdf::NodeProperty>,
) -> Result<DeviceProperties, zx_status_t> {
    let mut device_properties = DeviceProperties::new();

    for property in node_properties {
        if property.key.is_none() || property.value.is_none() {
            return Err(Status::INVALID_ARGS.into_raw());
        }

        let key = match property.key.as_ref().unwrap() {
            fdf::NodePropertyKey::IntValue(i) => PropertyKey::NumberKey(i.clone().into()),
            fdf::NodePropertyKey::StringValue(s) => PropertyKey::StringKey(s.clone()),
        };

        let value = match property.value.as_ref().unwrap() {
            fdf::NodePropertyValue::IntValue(i) => {
                bind::compiler::Symbol::NumberValue(i.clone().into())
            }
            fdf::NodePropertyValue::StringValue(s) => {
                bind::compiler::Symbol::StringValue(s.clone())
            }
            fdf::NodePropertyValue::EnumValue(s) => bind::compiler::Symbol::EnumValue(s.clone()),
            fdf::NodePropertyValue::BoolValue(b) => bind::compiler::Symbol::BoolValue(b.clone()),
        };

        // TODO(fxb/93937): Platform bus devices may contain two different BIND_PROTOCOL values.
        // The duplicate key needs to be fixed since this is incorrect and is working by luck.
        if key != PropertyKey::NumberKey(BIND_PROTOCOL.into()) {
            if device_properties.contains_key(&key) && device_properties.get(&key) != Some(&value) {
                return Err(Status::INVALID_ARGS.into_raw());
            }
        }

        device_properties.insert(key, value);
    }
    Ok(device_properties)
}

pub fn collect_node_names_from_composite_rules(
    composite_rules: &DecodedCompositeBindRules,
) -> Vec<String> {
    let mut node_names = vec![];
    node_names.push(composite_rules.symbol_table[&composite_rules.primary_node.name_id].clone());
    for node in &composite_rules.additional_nodes {
        node_names.push(composite_rules.symbol_table[&node.name_id].clone());
    }

    for node in &composite_rules.optional_nodes {
        node_names.push(composite_rules.symbol_table[&node.name_id].clone());
    }

    return node_names;
}

pub fn get_composite_rules_from_composite_driver<'a>(
    composite_driver: &'a ResolvedDriver,
) -> Result<&'a DecodedCompositeBindRules, i32> {
    match &composite_driver.bind_rules {
        bind::interpreter::decode_bind_rules::DecodedRules::Normal(_) => {
            log::error!("Cannot extract composite bind rules from a non-composite driver.");
            Err(Status::INTERNAL.into_raw())
        }
        bind::interpreter::decode_bind_rules::DecodedRules::Composite(rules) => Ok(rules),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn test_duplicate_properties() {
        let node_properties = vec![
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(10)),
                value: Some(fdf::NodePropertyValue::IntValue(200)),
                ..fdf::NodeProperty::EMPTY
            },
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(10)),
                value: Some(fdf::NodePropertyValue::IntValue(200)),
                ..fdf::NodeProperty::EMPTY
            },
        ];

        let mut expected_properties = DeviceProperties::new();
        expected_properties
            .insert(PropertyKey::NumberKey(10), bind::compiler::Symbol::NumberValue(200));

        let result = node_to_device_property(&node_properties).unwrap();
        assert_eq!(expected_properties, result);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_property_collision() {
        let node_properties = vec![
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(10)),
                value: Some(fdf::NodePropertyValue::IntValue(200)),
                ..fdf::NodeProperty::EMPTY
            },
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(10)),
                value: Some(fdf::NodePropertyValue::IntValue(10)),
                ..fdf::NodeProperty::EMPTY
            },
        ];

        assert_eq!(Err(Status::INVALID_ARGS.into_raw()), node_to_device_property(&node_properties));
    }

    // TODO(fxb/93937): Remove this case once the issue with multiple BIND_PROTOCOL properties
    // is resolved.
    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_bind_protocol() {
        let node_properties = vec![
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(BIND_PROTOCOL.into())),
                value: Some(fdf::NodePropertyValue::IntValue(200)),
                ..fdf::NodeProperty::EMPTY
            },
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(BIND_PROTOCOL.into())),
                value: Some(fdf::NodePropertyValue::IntValue(10)),
                ..fdf::NodeProperty::EMPTY
            },
        ];

        let mut expected_properties = DeviceProperties::new();
        expected_properties.insert(
            PropertyKey::NumberKey(BIND_PROTOCOL.into()),
            bind::compiler::Symbol::NumberValue(10),
        );

        assert_eq!(Ok(expected_properties), node_to_device_property(&node_properties));
    }
}
