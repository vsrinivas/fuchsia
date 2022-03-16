// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::match_common::node_to_device_property, bind::interpreter::match_bind::DeviceProperties,
    fidl_fuchsia_driver_framework as fdf, fuchsia_zircon::zx_status_t,
};

pub struct Node {
    name: String,
    properties: DeviceProperties,
}

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
                        properties: node_to_device_property(&node.properties)?,
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
    for (key, value) in node.properties.iter() {
        if device_properties.get(key) != Some(value) {
            return false;
        }
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    use bind::compiler::Symbol;
    use bind::interpreter::match_bind::PropertyKey;
    use fuchsia_async as fasync;
    use std::collections::HashMap;

    #[fasync::run_singlethreaded(test)]
    async fn test_property_match() {
        let node_properties_1 = vec![
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(1)),
                value: Some(fdf::NodePropertyValue::IntValue(200)),
                ..fdf::NodeProperty::EMPTY
            },
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(3)),
                value: Some(fdf::NodePropertyValue::BoolValue(true)),
                ..fdf::NodeProperty::EMPTY
            },
        ];

        let node_properties_2 = vec![
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::StringValue("killdeer".to_string())),
                value: Some(fdf::NodePropertyValue::StringValue("plover".to_string())),
                ..fdf::NodeProperty::EMPTY
            },
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::StringValue("Moon".to_string())),
                value: Some(fdf::NodePropertyValue::EnumValue("Moon.Half".to_string())),
                ..fdf::NodeProperty::EMPTY
            },
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::StringValue("yellowlegs".to_string())),
                value: Some(fdf::NodePropertyValue::BoolValue(false)),
                ..fdf::NodeProperty::EMPTY
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
            Symbol::StringValue("plover".to_string()),
        );
        device_properties_2.insert(
            PropertyKey::StringKey("Moon".to_string()),
            Symbol::EnumValue("Moon.Half".to_string()),
        );
        device_properties_2.insert(PropertyKey::NumberKey(3), Symbol::BoolValue(true));

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
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(1)),
                value: Some(fdf::NodePropertyValue::IntValue(200)),
                ..fdf::NodeProperty::EMPTY
            },
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::IntValue(3)),
                value: Some(fdf::NodePropertyValue::BoolValue(true)),
                ..fdf::NodeProperty::EMPTY
            },
        ];

        let node_properties_2 = vec![
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::StringValue("killdeer".to_string())),
                value: Some(fdf::NodePropertyValue::StringValue("plover".to_string())),
                ..fdf::NodeProperty::EMPTY
            },
            fdf::NodeProperty {
                key: Some(fdf::NodePropertyKey::StringValue("yellowlegs".to_string())),
                value: Some(fdf::NodePropertyValue::BoolValue(false)),
                ..fdf::NodeProperty::EMPTY
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
            .insert(PropertyKey::StringKey("yellowlegs".to_string()), Symbol::BoolValue(true));
        device_properties.insert(
            PropertyKey::StringKey("killdeer".to_string()),
            Symbol::StringValue("plover".to_string()),
        );

        assert_eq!(None, device_group.matches(&device_properties));
    }
}
