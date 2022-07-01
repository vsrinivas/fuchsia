// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bind::compiler::Symbol,
    bind::interpreter::match_bind::{DeviceProperties, PropertyKey},
    fidl_fuchsia_driver_framework as fdf, fidl_fuchsia_driver_index as fdi,
    fuchsia_zircon::{zx_status_t, Status},
    std::collections::{BTreeMap, HashMap, HashSet},
};

#[derive(Debug, Eq, Hash, PartialEq)]
pub struct DeviceGroupNodePropertyCondition {
    condition: fdf::Condition,
    values: Vec<Symbol>,
}

type DeviceGroupNodeProperties = BTreeMap<PropertyKey, DeviceGroupNodePropertyCondition>;

// The DeviceGroupManager struct is responsible of managing a list of device groups
// for matching.
pub struct DeviceGroupManager {
    // This maps a list of device groups to the nodes that they belong to.
    pub device_group_nodes: HashMap<DeviceGroupNodeProperties, Vec<fdi::MatchedDeviceGroupInfo>>,

    // Contains all the topological path of all device groups that have been added. This
    // list is to ensure that we don't add multiple device groups with the same topological
    // path.
    pub device_group_list: HashSet<String>,
}

impl DeviceGroupManager {
    pub fn new() -> Self {
        DeviceGroupManager { device_group_nodes: HashMap::new(), device_group_list: HashSet::new() }
    }

    pub fn add_device_group(
        &mut self,
        topological_path: String,
        nodes: Vec<fdf::DeviceGroupNode>,
    ) -> fdi::DriverIndexAddDeviceGroupResult {
        if self.device_group_list.contains(&topological_path) {
            return Err(Status::ALREADY_EXISTS.into_raw());
        }

        if nodes.is_empty() {
            return Err(Status::INVALID_ARGS.into_raw());
        }

        // Collect device group nodes in a separate vector before adding them to the device group
        // manager. This is to ensure that we add the nodes after they're all verified to be valid.
        let mut device_group_nodes: Vec<(DeviceGroupNodeProperties, fdi::MatchedDeviceGroupInfo)> =
            vec![];
        for (node_idx, node) in nodes.iter().enumerate() {
            let properties = convert_to_device_properties(&node.properties)?;

            let device_group_info = fdi::MatchedDeviceGroupInfo {
                topological_path: Some(topological_path.clone()),
                node_index: Some(node_idx as u32),
                ..fdi::MatchedDeviceGroupInfo::EMPTY
            };

            device_group_nodes.push((properties, device_group_info));
        }

        // Add to the device group list.
        self.device_group_list.insert(topological_path.clone());

        // Add each node and its device group to the node map.
        for (properties, group_info) in device_group_nodes {
            self.device_group_nodes
                .entry(properties)
                .and_modify(|device_groups| device_groups.push(group_info.clone()))
                .or_insert(vec![group_info]);
        }

        Ok(())
    }

    // Match the given device properties to all the nodes. Returns a list of device groups for all the
    // nodes that match.
    pub fn match_device_group_nodes(
        &self,
        properties: &DeviceProperties,
    ) -> Option<fdi::MatchedDriver> {
        let mut device_groups: Vec<fdi::MatchedDeviceGroupInfo> = vec![];
        for (node_props, group_list) in self.device_group_nodes.iter() {
            if match_node(&node_props, properties) {
                device_groups.extend_from_slice(group_list.as_slice());
            }
        }

        if device_groups.is_empty() {
            return None;
        }

        Some(fdi::MatchedDriver::DeviceGroupNode(fdi::MatchedDeviceGroupNodeInfo {
            device_groups: Some(device_groups.to_vec()),
            ..fdi::MatchedDeviceGroupNodeInfo::EMPTY
        }))
    }
}

fn convert_to_device_properties(
    node_properties: &Vec<fdf::DeviceGroupProperty>,
) -> Result<DeviceGroupNodeProperties, zx_status_t> {
    if node_properties.is_empty() {
        return Err(Status::INVALID_ARGS.into_raw());
    }

    let mut device_properties = BTreeMap::new();
    for property in node_properties {
        let key = match &property.key {
            fdf::NodePropertyKey::IntValue(i) => PropertyKey::NumberKey(i.clone().into()),
            fdf::NodePropertyKey::StringValue(s) => PropertyKey::StringKey(s.clone()),
        };

        // Check if the properties contain duplicate keys.
        if device_properties.contains_key(&key) {
            return Err(Status::INVALID_ARGS.into_raw());
        }

        let first_val = property.values.first().ok_or(Status::INVALID_ARGS.into_raw())?;
        let values = property
            .values
            .iter()
            .map(|val| {
                // Check that the properties are all the same type.
                if std::mem::discriminant(first_val) != std::mem::discriminant(val) {
                    return Err(Status::INVALID_ARGS.into_raw());
                }
                Ok(node_property_to_symbol(val))
            })
            .collect::<Result<Vec<Symbol>, zx_status_t>>()?;

        device_properties.insert(
            key,
            DeviceGroupNodePropertyCondition { condition: property.condition, values: values },
        );
    }
    Ok(device_properties)
}

fn match_node(
    node_properties: &DeviceGroupNodeProperties,
    device_properties: &DeviceProperties,
) -> bool {
    for (key, node_prop_values) in node_properties.iter() {
        let dev_prop_contains_value = match device_properties.get(key) {
            Some(val) => node_prop_values.values.contains(val),
            None => false,
        };

        let evaluate_condition = match node_prop_values.condition {
            fdf::Condition::Accept => {
                // If the node property accepts a false boolean value and the property is
                // missing from the device properties, then we should evaluate the condition
                // as true.
                dev_prop_contains_value
                    || node_prop_values.values.contains(&Symbol::BoolValue(false))
            }
            fdf::Condition::Reject => !dev_prop_contains_value,
        };

        if !evaluate_condition {
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

#[cfg(test)]
mod tests {
    use super::*;
    use bind::compiler::Symbol;
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
                key: fdf::NodePropertyKey::StringValue("flycatcher".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::EnumValue("flycatcher.phoebe".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("yellowlegs".to_string()),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
        ];

        let mut device_group_manager = DeviceGroupManager::new();
        device_group_manager
            .add_device_group(
                "test/path".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "whimbrel".to_string(),
                        properties: node_properties_1,
                    },
                    fdf::DeviceGroupNode {
                        name: "godwit".to_string(),
                        properties: node_properties_2,
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

        let expected_device_group = fdi::MatchedDeviceGroupInfo {
            topological_path: Some("test/path".to_string()),
            node_index: Some(0),
            ..fdi::MatchedDeviceGroupInfo::EMPTY
        };
        assert_eq!(
            Some(fdi::MatchedDriver::DeviceGroupNode(fdi::MatchedDeviceGroupNodeInfo {
                device_groups: Some(vec![expected_device_group]),
                ..fdi::MatchedDeviceGroupNodeInfo::EMPTY
            })),
            device_group_manager.match_device_group_nodes(&device_properties_1)
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
            PropertyKey::StringKey("flycatcher".to_string()),
            Symbol::EnumValue("flycatcher.phoebe".to_string()),
        );

        let expected_device_group_2 = fdi::MatchedDeviceGroupInfo {
            topological_path: Some("test/path".to_string()),
            node_index: Some(1),
            ..fdi::MatchedDeviceGroupInfo::EMPTY
        };
        assert_eq!(
            Some(fdi::MatchedDriver::DeviceGroupNode(fdi::MatchedDeviceGroupNodeInfo {
                device_groups: Some(vec![expected_device_group_2]),
                ..fdi::MatchedDeviceGroupNodeInfo::EMPTY
            })),
            device_group_manager.match_device_group_nodes(&device_properties_2)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_property_match_bool_edgecase() {
        let node_properties = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(1),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::IntValue(200)],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(3),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::BoolValue(false)],
            },
        ];

        let mut device_group_manager = DeviceGroupManager::new();
        device_group_manager
            .add_device_group(
                "test/path".to_string(),
                vec![fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties,
                }],
            )
            .unwrap();

        // Match node.
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(1), Symbol::NumberValue(200));

        let expected_device_group = fdi::MatchedDeviceGroupInfo {
            topological_path: Some("test/path".to_string()),
            node_index: Some(0),
            ..fdi::MatchedDeviceGroupInfo::EMPTY
        };
        assert_eq!(
            Some(fdi::MatchedDriver::DeviceGroupNode(fdi::MatchedDeviceGroupNodeInfo {
                device_groups: Some(vec![expected_device_group]),
                ..fdi::MatchedDeviceGroupNodeInfo::EMPTY
            })),
            device_group_manager.match_device_group_nodes(&device_properties)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_group_match() {
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
                key: fdf::NodePropertyKey::StringValue("flycatcher".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::EnumValue("flycatcher.phoebe".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("yellowlegs".to_string()),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
        ];

        let node_properties_2_rearranged = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("flycatcher".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::EnumValue("flycatcher.phoebe".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("killdeer".to_string()),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::StringValue("plover".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("yellowlegs".to_string()),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
        ];

        let node_properties_3 = vec![fdf::DeviceGroupProperty {
            key: fdf::NodePropertyKey::StringValue("cormorant".to_string()),
            condition: fdf::Condition::Accept,
            values: vec![fdf::NodePropertyValue::BoolValue(true)],
        }];

        let mut device_group_manager = DeviceGroupManager::new();
        device_group_manager
            .add_device_group(
                "test/path".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "whimbrel".to_string(),
                        properties: node_properties_1,
                    },
                    fdf::DeviceGroupNode {
                        name: "godwit".to_string(),
                        properties: node_properties_2,
                    },
                ],
            )
            .unwrap();

        device_group_manager
            .add_device_group(
                "test/path2".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "whimbrel".to_string(),
                        properties: node_properties_2_rearranged,
                    },
                    fdf::DeviceGroupNode {
                        name: "godwit".to_string(),
                        properties: node_properties_3,
                    },
                ],
            )
            .unwrap();

        // Match node.
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties
            .insert(PropertyKey::StringKey("yellowlegs".to_string()), Symbol::BoolValue(false));
        device_properties.insert(
            PropertyKey::StringKey("killdeer".to_string()),
            Symbol::StringValue("lapwing".to_string()),
        );
        device_properties.insert(
            PropertyKey::StringKey("flycatcher".to_string()),
            Symbol::EnumValue("flycatcher.phoebe".to_string()),
        );
        let match_result =
            device_group_manager.match_device_group_nodes(&device_properties).unwrap();

        assert!(if let fdi::MatchedDriver::DeviceGroupNode(matched_node_info) = match_result {
            let matched_device_groups = matched_node_info.device_groups.unwrap();
            assert_eq!(2, matched_device_groups.len());

            assert!(matched_device_groups.contains(&fdi::MatchedDeviceGroupInfo {
                topological_path: Some("test/path".to_string()),
                node_index: Some(1),
                ..fdi::MatchedDeviceGroupInfo::EMPTY
            }));

            assert!(matched_device_groups.contains(&fdi::MatchedDeviceGroupInfo {
                topological_path: Some("test/path2".to_string()),
                node_index: Some(0),
                ..fdi::MatchedDeviceGroupInfo::EMPTY
            }));

            true
        } else {
            false
        });
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_group_nodes_match() {
        let node_properties_1 = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(1),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::IntValue(200)],
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
                key: fdf::NodePropertyKey::StringValue("flycatcher".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::EnumValue("flycatcher.phoebe".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("yellowlegs".to_string()),
                condition: fdf::Condition::Reject,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
        ];

        let node_properties_1_rearranged = vec![
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::StringValue("killdeer".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::StringValue("plover".to_string())],
            },
            fdf::DeviceGroupProperty {
                key: fdf::NodePropertyKey::IntValue(1),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::IntValue(200)],
            },
        ];

        let node_properties_3 = vec![fdf::DeviceGroupProperty {
            key: fdf::NodePropertyKey::StringValue("cormorant".to_string()),
            condition: fdf::Condition::Accept,
            values: vec![fdf::NodePropertyValue::BoolValue(true)],
        }];

        let node_properties_4 = vec![fdf::DeviceGroupProperty {
            key: fdf::NodePropertyKey::IntValue(1),
            condition: fdf::Condition::Accept,
            values: vec![
                fdf::NodePropertyValue::IntValue(10),
                fdf::NodePropertyValue::IntValue(200),
            ],
        }];

        let mut device_group_manager = DeviceGroupManager::new();
        device_group_manager
            .add_device_group(
                "test/path".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "whimbrel".to_string(),
                        properties: node_properties_1,
                    },
                    fdf::DeviceGroupNode {
                        name: "godwit".to_string(),
                        properties: node_properties_2,
                    },
                ],
            )
            .unwrap();

        device_group_manager
            .add_device_group(
                "test/path2".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "sanderling".to_string(),
                        properties: node_properties_3,
                    },
                    fdf::DeviceGroupNode {
                        name: "plover".to_string(),
                        properties: node_properties_1_rearranged,
                    },
                ],
            )
            .unwrap();

        device_group_manager
            .add_device_group(
                "test/path3".to_string(),
                vec![fdf::DeviceGroupNode {
                    name: "dunlin".to_string(),
                    properties: node_properties_4,
                }],
            )
            .unwrap();

        // Match node.
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(1), Symbol::NumberValue(200));
        device_properties.insert(
            PropertyKey::StringKey("killdeer".to_string()),
            Symbol::StringValue("plover".to_string()),
        );
        let match_result =
            device_group_manager.match_device_group_nodes(&device_properties).unwrap();

        assert!(if let fdi::MatchedDriver::DeviceGroupNode(matched_node_info) = match_result {
            let matched_device_groups = matched_node_info.device_groups.unwrap();
            assert_eq!(3, matched_device_groups.len());

            assert!(matched_device_groups.contains(&fdi::MatchedDeviceGroupInfo {
                topological_path: Some("test/path".to_string()),
                node_index: Some(0),
                ..fdi::MatchedDeviceGroupInfo::EMPTY
            }));

            assert!(matched_device_groups.contains(&fdi::MatchedDeviceGroupInfo {
                topological_path: Some("test/path2".to_string()),
                node_index: Some(1),
                ..fdi::MatchedDeviceGroupInfo::EMPTY
            }));

            assert!(matched_device_groups.contains(&fdi::MatchedDeviceGroupInfo {
                topological_path: Some("test/path3".to_string()),
                node_index: Some(0),
                ..fdi::MatchedDeviceGroupInfo::EMPTY
            }));

            true
        } else {
            false
        });
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

        let mut device_group_manager = DeviceGroupManager::new();
        device_group_manager
            .add_device_group(
                "test/path".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "whimbrel".to_string(),
                        properties: node_properties_1,
                    },
                    fdf::DeviceGroupNode {
                        name: "godwit".to_string(),
                        properties: node_properties_2,
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

        assert_eq!(None, device_group_manager.match_device_group_nodes(&device_properties));
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
                key: fdf::NodePropertyKey::StringValue("dunlin".to_string()),
                condition: fdf::Condition::Accept,
                values: vec![fdf::NodePropertyValue::BoolValue(true)],
            },
        ];

        let mut device_group_manager = DeviceGroupManager::new();
        device_group_manager
            .add_device_group(
                "test/path".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "whimbrel".to_string(),
                        properties: node_properties_1,
                    },
                    fdf::DeviceGroupNode {
                        name: "godwit".to_string(),
                        properties: node_properties_2,
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

        let expected_device_group_1 = fdi::MatchedDeviceGroupInfo {
            topological_path: Some("test/path".to_string()),
            node_index: Some(0),
            ..fdi::MatchedDeviceGroupInfo::EMPTY
        };
        assert_eq!(
            Some(fdi::MatchedDriver::DeviceGroupNode(fdi::MatchedDeviceGroupNodeInfo {
                device_groups: Some(vec![expected_device_group_1]),
                ..fdi::MatchedDeviceGroupNodeInfo::EMPTY
            })),
            device_group_manager.match_device_group_nodes(&device_properties_1)
        );

        // Match node 2.
        let mut device_properties_2: DeviceProperties = HashMap::new();
        device_properties_2.insert(PropertyKey::NumberKey(5), Symbol::NumberValue(20));
        device_properties_2
            .insert(PropertyKey::StringKey("dunlin".to_string()), Symbol::BoolValue(true));

        let expected_device_group_2 = fdi::MatchedDeviceGroupInfo {
            topological_path: Some("test/path".to_string()),
            node_index: Some(1),
            ..fdi::MatchedDeviceGroupInfo::EMPTY
        };
        assert_eq!(
            Some(fdi::MatchedDriver::DeviceGroupNode(fdi::MatchedDeviceGroupNodeInfo {
                device_groups: Some(vec![expected_device_group_2]),
                ..fdi::MatchedDeviceGroupNodeInfo::EMPTY
            })),
            device_group_manager.match_device_group_nodes(&device_properties_2)
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

        let mut device_group_manager = DeviceGroupManager::new();
        device_group_manager
            .add_device_group(
                "test/path".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "sanderling".to_string(),
                        properties: node_properties_1,
                    },
                    fdf::DeviceGroupNode {
                        name: "dunlin".to_string(),
                        properties: node_properties_2,
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
        assert_eq!(None, device_group_manager.match_device_group_nodes(&device_properties_1));

        // Match node 2.
        let mut device_properties_2: DeviceProperties = HashMap::new();
        device_properties_2.insert(PropertyKey::NumberKey(11), Symbol::NumberValue(10));
        device_properties_2.insert(PropertyKey::NumberKey(2), Symbol::BoolValue(true));

        assert_eq!(None, device_group_manager.match_device_group_nodes(&device_properties_2));
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

        let mut device_group_manager = DeviceGroupManager::new();
        assert_eq!(
            Err(Status::INVALID_ARGS.into_raw()),
            device_group_manager.add_device_group(
                "test/path".to_string(),
                vec![fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties_1,
                }],
            )
        );

        assert!(device_group_manager.device_group_nodes.is_empty());
        assert!(device_group_manager.device_group_list.is_empty());
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

        let mut device_group_manager = DeviceGroupManager::new();
        assert_eq!(
            Err(Status::INVALID_ARGS.into_raw()),
            device_group_manager.add_device_group(
                "test/path".to_string(),
                vec![fdf::DeviceGroupNode {
                    name: "whimbrel".to_string(),
                    properties: node_properties_1,
                },],
            )
        );

        assert!(device_group_manager.device_group_nodes.is_empty());
        assert!(device_group_manager.device_group_list.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_missing_node_properties() {
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

        let mut device_group_manager = DeviceGroupManager::new();
        assert_eq!(
            Err(Status::INVALID_ARGS.into_raw()),
            device_group_manager.add_device_group(
                "test/path".to_string(),
                vec![
                    fdf::DeviceGroupNode {
                        name: "whimbrel".to_string(),
                        properties: node_properties_1,
                    },
                    fdf::DeviceGroupNode { name: "curlew".to_string(), properties: vec![] },
                ],
            )
        );

        assert!(device_group_manager.device_group_nodes.is_empty());
        assert!(device_group_manager.device_group_list.is_empty());
    }
}
