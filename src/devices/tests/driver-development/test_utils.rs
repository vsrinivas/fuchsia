// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_driver_development as fdd,
    std::collections::{HashMap, HashSet},
};

/// Represents either a DFv1 device or DFv2 node.
#[derive(Debug, PartialEq)]
pub struct DeviceNode {
    pub topological_path: Option<String>,
    pub moniker: Option<String>,
    pub bound_driver_libname: Option<String>,
    pub bound_driver_url: Option<String>,
    pub child_nodes: Vec<DeviceNode>,
    /// The number of child devices a device has. This can be different than
    /// `child_nodes.len()` if, for example, the call to GetDeviceInfo passes a
    /// filter that excludes a device's children from being returned.
    pub num_children: usize,
}

impl DeviceNode {
    pub fn new(
        topological_path: Option<String>,
        moniker: Option<String>,
        bound_driver_libname: Option<String>,
        bound_driver_url: Option<String>,
        child_nodes: Vec<DeviceNode>,
        num_children: usize,
    ) -> Self {
        Self {
            topological_path,
            moniker,
            bound_driver_libname,
            bound_driver_url,
            child_nodes,
            num_children,
        }
    }
}

// Used internally for quick lookup of DFv1 devices or DFv2 nodes within a
// hashmap.
struct DeviceVertex {
    parent_ids: HashSet<u64>,
    child_ids: HashSet<u64>,
    topological_path: Option<String>,
    moniker: Option<String>,
    bound_driver_libname: Option<String>,
    bound_driver_url: Option<String>,
}

impl DeviceVertex {
    fn new(
        parent_ids: HashSet<u64>,
        child_ids: HashSet<u64>,
        topological_path: Option<String>,
        moniker: Option<String>,
        bound_driver_libname: Option<String>,
        bound_driver_url: Option<String>,
    ) -> Self {
        Self {
            parent_ids,
            child_ids,
            topological_path,
            moniker,
            bound_driver_libname,
            bound_driver_url,
        }
    }
}

fn device_vertex_to_device_node(
    id: u64,
    device_vertices: &HashMap<u64, DeviceVertex>,
) -> DeviceNode {
    let vertex = device_vertices.get(&id).expect("Invalid vertex ID");
    // If two devices share the same child device then multiple nodes will
    // be made for that child.
    let child_nodes = vertex
        .child_ids
        .iter()
        .filter_map(|child_id| {
            // Child device may or may not exist depending on the filter
            // sent in GetDeviceInfo.
            if device_vertices.contains_key(child_id) {
                Some(device_vertex_to_device_node(*child_id, device_vertices))
            } else {
                None
            }
        })
        .collect();
    DeviceNode::new(
        vertex.topological_path.clone(),
        vertex.moniker.clone(),
        vertex.bound_driver_libname.clone(),
        vertex.bound_driver_url.clone(),
        child_nodes,
        vertex.child_ids.len(),
    )
}

// DFS approach to detect cycles.
// `vertices` acts similar to an adjacency matrix.
fn is_cyclic(vertices: &HashMap<u64, DeviceVertex>) -> bool {
    enum State {
        Unprocessed,
        Processing,
        Processed,
    }

    // Process a vertex and its children for cycles.
    // Returns true if there's a cycle and false if there is not.
    fn process_vertex(
        id: u64,
        vertices: &HashMap<u64, DeviceVertex>,
        states: &mut HashMap<u64, State>,
    ) -> bool {
        states.insert(id, State::Processing);
        if let Some(vertex) = vertices.get(&id) {
            for child_id in vertex.child_ids.iter() {
                match states.get(child_id) {
                    // We found another Node that's processing which means there's a cycle.
                    Some(State::Processing) => {
                        return true;
                    }
                    Some(State::Unprocessed) => {
                        // Check our unprocessed child for cycles and return if we find one.
                        if process_vertex(*child_id, vertices, states) {
                            return true;
                        }
                    }
                    _ => {}
                }
            }
        }

        // We have not found any cycles so mark Node as processed.
        states.insert(id, State::Processed);
        false
    }

    // Key: ID of device vertex
    let mut states: HashMap<u64, State> =
        vertices.keys().map(|id| (*id, State::Unprocessed)).collect();
    for id in vertices.keys() {
        if let Some(State::Unprocessed) = states.get(id) {
            if process_vertex(*id, &vertices, &mut states) {
                return true;
            }
        }
    }
    false
}

impl From<&fdd::DeviceInfo> for DeviceVertex {
    fn from(device_info: &fdd::DeviceInfo) -> Self {
        let mut parent_ids = HashSet::<u64>::new();
        if let Some(ids) = device_info.parent_ids.as_ref() {
            parent_ids = ids.iter().map(|id| *id).collect();
            assert_eq!(
                ids.len(),
                parent_ids.len(),
                "Device has the same parent ID listed multiple times"
            );
        };

        let mut child_ids = HashSet::<u64>::new();
        if let Some(ids) = device_info.child_ids.as_ref() {
            child_ids = ids.iter().map(|id| *id).collect();
            assert_eq!(
                ids.len(),
                child_ids.len(),
                "Device has the same child ID listed multiple times"
            );
        };

        Self::new(
            parent_ids,
            child_ids,
            device_info.topological_path.clone(),
            device_info.moniker.clone(),
            device_info.bound_driver_libname.clone(),
            device_info.bound_driver_url.clone(),
        )
    }
}

/// Creates zero or more tree structures that reflects the DFv1 devices' or DFv2
/// nodes' topology defined in `device_infos`. Note that if two devices/nodes
/// share a child then that child will be duplicated so that each parent has a
/// copy of the child.
///
/// # Panics
///
/// Panics if one of the resulting graphs is cyclic or if `device_infos`
/// contains conflicting information about the devices'/nodes' topology or if a
/// device's/node's ID or topological path is missing.
pub fn create_device_topology(device_infos: &Vec<fdd::DeviceInfo>) -> Vec<DeviceNode> {
    // Key: ID of the vertex
    let device_vertices: HashMap<u64, DeviceVertex> = device_infos
        .iter()
        .map(|device_info| (device_info.id.expect("Device ID missing"), device_info.into()))
        .collect();
    assert!(!is_cyclic(&device_vertices), "Device topology is cyclic");

    // Assert that the devices' children and parents make sense.
    let mut root_ids: Vec<u64> = Vec::new();
    for device_info in device_infos {
        let id = device_info.id.expect("Device ID missing");

        // Assert all device's children exist.
        if let Some(child_ids) = device_info.child_ids.as_ref() {
            for child_id in child_ids {
                // Child device may or may not exist depending on the filter
                // sent in GetDeviceInfo.
                if let Some(child_node) = device_vertices.get(child_id) {
                    assert!(child_node.parent_ids.contains(&id), "Parent device lists the ID of a child device in it's child ID's, however, the child device does not list the parent device's ID in its parent devices");
                }
            }
        }

        // Find root and assert non-root devices' parents exist.
        if let Some(parent_ids) = device_info.parent_ids.as_ref() {
            for parent_id in parent_ids {
                // Parent device may or may not exist depending on the filter
                // sent in GetDeviceInfo or if the current device is a root
                // device.
                if let Some(parent_node) = device_vertices.get(parent_id) {
                    assert!(parent_node.child_ids.contains(&id), "Child device lists the ID of a parent device in it's parent ID's, however, the parent device does not list the child device's ID in its child devices");
                } else {
                    root_ids.push(id);
                }
            }
        } else {
            root_ids.push(id);
        }
    }

    root_ids
        .into_iter()
        .map(|root_id| device_vertex_to_device_node(root_id, &device_vertices))
        .collect::<Vec<DeviceNode>>()
}
