// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bind::compiler::Symbol,
    bind::interpreter::match_bind::{DeviceProperties, PropertyKey},
    fidl_fuchsia_driver_framework as fdf,
    fuchsia_zircon::{zx_status_t, Status},
    std::collections::HashMap,
};

#[derive(Debug, PartialEq)]
struct DeviceGroupNodePropertyValue {
    is_equal: bool,
    values: Vec<Symbol>,
}

#[derive(Debug, PartialEq)]
pub struct Node {
    name: String,
    properties: HashMap<PropertyKey, DeviceGroupNodePropertyValue>,
}

#[derive(Debug, PartialEq)]
pub struct DeviceGroup {
    topological_path: String,
    nodes: Vec<Node>,
}

impl DeviceGroup {
    pub fn create(
        topological_path: String,
        device_group_nodes: Vec<fdf::DeviceGroupNode>,
    ) -> Result<Self, zx_status_t> {
        Ok(DeviceGroup {
            topological_path: topological_path,
            nodes: device_group_nodes
                .into_iter()
                .map(|node| {
                    Ok(Node {
                        name: node.name,
                        properties: group_node_to_device_property(&node.properties)?,
                    })
                })
                .collect::<Result<Vec<Node>, zx_status_t>>()?,
        })
    }

    pub fn matches(&self, properties: &DeviceProperties) -> Option<fdf::MatchedDriver> {
        for (index, node) in self.nodes.iter().enumerate() {
            if match_node(&node, properties) {
                let node_names = self.nodes.iter().map(|node| node.name.clone()).collect();
                return Some(fdf::MatchedDriver::DeviceGroup(fdf::MatchedDeviceGroupInfo {
                    topological_path: Some(self.topological_path.clone()),
                    node_index: Some(index as u32),
                    num_nodes: Some(self.nodes.len() as u32),
                    node_names: Some(node_names),
                    ..fdf::MatchedDeviceGroupInfo::EMPTY
                }));
            }
        }

        None
    }
}

fn match_node(node: &Node, device_properties: &DeviceProperties) -> bool {
    for (key, prop_values) in node.properties.iter() {
        let match_property = match device_properties.get(key) {
            Some(val) => prop_values.values.contains(val),
            None => {
                // If the node properties contain a false boolean
                // value, then evaluate this to true.
                prop_values.values.contains(&Symbol::BoolValue(false))
            }
        };

        if prop_values.is_equal && !match_property {
            return false;
        }

        if !prop_values.is_equal && match_property {
            return false;
        }
    }
    true
}

fn node_property_to_symbol(value: &fdf::NodePropertyValue) -> Symbol {
    match value {
        fdf::NodePropertyValue::IntValue(i) => {
            bind::compiler::Symbol::NumberValue(i.clone().into())
        }
        fdf::NodePropertyValue::StringValue(s) => bind::compiler::Symbol::StringValue(s.clone()),
        fdf::NodePropertyValue::EnumValue(s) => bind::compiler::Symbol::EnumValue(s.clone()),
        fdf::NodePropertyValue::BoolValue(b) => bind::compiler::Symbol::BoolValue(b.clone()),
    }
}

fn group_node_to_device_property(
    node_properties: &Vec<fdf::DeviceGroupProperty>,
) -> Result<HashMap<PropertyKey, DeviceGroupNodePropertyValue>, zx_status_t> {
    let mut device_properties = HashMap::new();

    for property in node_properties {
        let key = match &property.key {
            fdf::NodePropertyKey::IntValue(i) => PropertyKey::NumberKey(i.clone().into()),
            fdf::NodePropertyKey::StringValue(s) => PropertyKey::StringKey(s.clone()),
        };

        // Contains duplicate keys.
        if device_properties.contains_key(&key) {
            return Err(Status::INVALID_ARGS.into_raw());
        }

        let first_val = property.values.first().ok_or(Status::INVALID_ARGS.into_raw())?;
        let values = property
            .values
            .iter()
            .map(|val| {
                // The properties should all be the same type.
                if std::mem::discriminant(first_val) != std::mem::discriminant(val) {
                    return Err(Status::INVALID_ARGS.into_raw());
                }
                Ok(node_property_to_symbol(val))
            })
            .collect::<Result<Vec<Symbol>, zx_status_t>>()?;

        let is_equal = property.condition == fdf::Condition::Accept;
        device_properties
            .insert(key, DeviceGroupNodePropertyValue { is_equal: is_equal, values: values });
    }
    Ok(device_properties)
}

#[cfg(test)]
mod tests {
    use super::*;
    use bind::compiler::Symbol;
    use bind::interpreter::match_bind::PropertyKey;
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn test_property_match() {
        let node_properties_1 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(1),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::IntValue(200)],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(3),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("killdeer".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::StringValue("plover".to_string())],
            },
        ];

        let node_properties_2 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("killdeer".to_string()),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::StringValue("plover".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("Moon".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::EnumValue("Moon.Half".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("yellowlegs".to_string()),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
        ];

        let device_group = DeviceGroup::create(
            "test/path".to_string(),
            vec![
                fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties_1.clone(),
                },
                fdf::DeviceGroupNode {
                    name: "godwit".to_string(),
                    properties: node_properties_2.clone(),
                },
            ],
        )
        .unwrap();

        // Match node 1.
        let mut device_properties_1: DeviceProperties = HashMap::new();
        device_properties_1.insert(PropertyKey::NumberKey(1), Symbol::NumberValue(200));
        device_properties_1.insert(
            PropertyKey::StringKey("kingfisher".to_string()),
            Symbol::StringValue("kookaburra".to_string()),
        );
        device_properties_1.insert(PropertyKey::NumberKey(3), Symbol::BoolValue(true));
        device_properties_1.insert(
            PropertyKey::StringKey("killdeer".to_string()),
            Symbol::StringValue("plover".to_string()),
        );

        assert_eq!(
            Some(fdf::MatchedDriver::DeviceGroup(fdf::MatchedDeviceGroupInfo {
                topological_path: Some("test/path".to_string()),
                node_index: Some(0),
                num_nodes: Some(2),
                node_names: Some(vec!["whimbrel".to_string(), "godwit".to_string()]),
                ..fdf::MatchedDeviceGroupInfo::EMPTY
            })),
            device_group.matches(&device_properties_1)
        );

        // Match node 2.
        let mut device_properties_2: DeviceProperties = HashMap::new();
        device_properties_2
            .insert(PropertyKey::StringKey("yellowlegs".to_string()), Symbol::BoolValue(false));
        device_properties_2.insert(
            PropertyKey::StringKey("killdeer".to_string()),
            Symbol::StringValue("lapwing".to_string()),
        );
        device_properties_2.insert(
            PropertyKey::StringKey("Moon".to_string()),
            Symbol::EnumValue("Moon.Half".to_string()),
        );

        assert_eq!(
            Some(fdf::MatchedDriver::DeviceGroup(fdf::MatchedDeviceGroupInfo {
                topological_path: Some("test/path".to_string()),
                node_index: Some(1),
                num_nodes: Some(2),
                node_names: Some(vec!["whimbrel".to_string(), "godwit".to_string()]),
                ..fdf::MatchedDeviceGroupInfo::EMPTY
            })),
            device_group.matches(&device_properties_2)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_property_mismatch() {
        let node_properties_1 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(1),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::IntValue(200)],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(3),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("killdeer".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::StringValue("plover".to_string())],
            },
        ];

        let node_properties_2 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("killdeer".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::StringValue("plover".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("yellowlegs".to_string()),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::BoolValue(false)],
            },
        ];

        let device_group = DeviceGroup::create(
            "test/path".to_string(),
            vec![
                fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties_1.clone(),
                },
                fdf::DeviceGroupNode {
                    name: "godwit".to_string(),
                    properties: node_properties_2.clone(),
                },
            ],
        )
        .unwrap();

        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(1), Symbol::NumberValue(200));
        device_properties.insert(
            PropertyKey::StringKey("kingfisher".to_string()),
            Symbol::StringValue("bee-eater".to_string()),
        );
        device_properties
            .insert(PropertyKey::StringKey("yellowlegs".to_string()), Symbol::BoolValue(false));
        device_properties.insert(
            PropertyKey::StringKey("killdeer".to_string()),
            Symbol::StringValue("plover".to_string()),
        );

        assert_eq!(None, device_group.matches(&device_properties));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_property_match_list() {
        let node_properties_1 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(10),
                condition: fdf::Condition::Reject,
                values: vec![
                    fdf::NodePropertyValue::IntValue(200),
                    fdf::NodePropertyValue::IntValue(150),
                ],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("plover".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![
                    fdf::NodePropertyValue::StringValue("killdeer".to_string()),
                    fdf::NodePropertyValue::StringValue("lapwing".to_string()),
                ],
            },
        ];

        let node_properties_2 = vec![fdf::DeviceGroupProperty {
            key: fdf::NodePropertyKey::IntValue(11),
            condition: fdf::Condition::Reject,
            values: vec![
                fdf::NodePropertyValue::IntValue(20),
                fdf::NodePropertyValue::IntValue(10),
            ],
        }];

        let device_group = DeviceGroup::create(
            "test/path".to_string(),
            vec![
                fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties_1.clone(),
                },
                fdf::DeviceGroupNode {
                    name: "godwit".to_string(),
                    properties: node_properties_2.clone(),
                },
            ],
        )
        .unwrap();

        // Match node 1.
        let mut device_properties_1: DeviceProperties = HashMap::new();
        device_properties_1.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(20));
        device_properties_1.insert(
            PropertyKey::StringKey("plover".to_string()),
            Symbol::StringValue("lapwing".to_string()),
        );
        assert_eq!(
            Some(fdf::MatchedDriver::DeviceGroup(fdf::MatchedDeviceGroupInfo {
                topological_path: Some("test/path".to_string()),
                node_index: Some(0),
                num_nodes: Some(2),
                node_names: Some(vec!["whimbrel".to_string(), "godwit".to_string()]),
                ..fdf::MatchedDeviceGroupInfo::EMPTY
            })),
            device_group.matches(&device_properties_1)
        );

        // Match node 2.
        let mut device_properties_2: DeviceProperties = HashMap::new();
        device_properties_2.insert(PropertyKey::NumberKey(5), Symbol::NumberValue(20));

        assert_eq!(
            Some(fdf::MatchedDriver::DeviceGroup(fdf::MatchedDeviceGroupInfo {
                topological_path: Some("test/path".to_string()),
                node_index: Some(1),
                num_nodes: Some(2),
                node_names: Some(vec!["whimbrel".to_string(), "godwit".to_string()]),
                ..fdf::MatchedDeviceGroupInfo::EMPTY
            })),
            device_group.matches(&device_properties_2)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_property_mismatch_list() {
        let node_properties_1 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(10),
                condition: fdf::Condition::Reject,
                values: vec![
                    fdf::NodePropertyValue::IntValue(200),
                    fdf::NodePropertyValue::IntValue(150),
                ],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("plover".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![
                    fdf::NodePropertyValue::StringValue("killdeer".to_string()),
                    fdf::NodePropertyValue::StringValue("lapwing".to_string()),
                ],
            },
        ];

        let node_properties_2 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(11),
                condition: fdf::Condition::Reject,
                values: vec![
                    fdf::NodePropertyValue::IntValue(20),
                    fdf::NodePropertyValue::IntValue(10),
                ],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(2),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
        ];

        let device_group = DeviceGroup::create(
            "test/path".to_string(),
            vec![
                fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties_1.clone(),
                },
                fdf::DeviceGroupNode {
                    name: "godwit".to_string(),
                    properties: node_properties_2.clone(),
                },
            ],
        )
        .unwrap();

        // Match node 1.
        let mut device_properties_1: DeviceProperties = HashMap::new();
        device_properties_1.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(200));
        device_properties_1.insert(
            PropertyKey::StringKey("plover".to_string()),
            Symbol::StringValue("lapwing".to_string()),
        );
        assert_eq!(None, device_group.matches(&device_properties_1));

        // Match node 2.
        let mut device_properties_2: DeviceProperties = HashMap::new();
        device_properties_2.insert(PropertyKey::NumberKey(11), Symbol::NumberValue(10));
        device_properties_2.insert(PropertyKey::NumberKey(2), Symbol::BoolValue(true));

        assert_eq!(None, device_group.matches(&device_properties_2));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_property_multiple_value_types() {
        let node_properties_1 = vec![fdf::DeviceGroupProperty {
            key: fdf::NodePropertyKey::IntValue(10),
            condition: fdf::Condition::Reject,
            values: vec![
                fdf::NodePropertyValue::IntValue(200),
                fdf::NodePropertyValue::BoolValue(false),
            ],
        }];

        assert_eq!(
            Err(Status::INVALID_ARGS.into_raw()),
            DeviceGroup::create(
                "test/path".to_string(),
                vec![fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties_1.clone(),
                }],
            )
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_property_duplicate_key() {
        let node_properties_1 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(10),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::IntValue(200)],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(10),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::IntValue(10)],
            },
        ];

        assert_eq!(
            Err(Status::INVALID_ARGS.into_raw()),
            DeviceGroup::create(
                "test/path".to_string(),
                vec![fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties_1.clone(),
                },],
            )
        );
    }
}
