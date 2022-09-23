// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod json {
    use crate::processes_data::processed;
    use fuchsia_zircon_types as zx_types;
    use serde::Serialize;
    use std::collections::HashSet;

    /// A node is part of the graph, and it represents a channel.
    #[derive(Serialize, PartialEq, Debug)]
    pub struct Node {
        /// Process name
        pub name: String,
        /// Process koid
        pub id: u64,
    }

    /// A link is an edge of the graph, and it represents a channel.
    #[derive(Serialize, PartialEq, Debug, Eq, Hash)]
    pub struct Link {
        /// The koid of the process owning one end of the channel
        pub source: u64,
        /// The koid of the process owning the other end of the channel
        pub target: u64,
    }

    /// The JSON structure to be fed to fuchsia-map
    #[derive(Serialize, PartialEq, Debug)]
    pub struct Json {
        /// A vector containing all the nodes (processes)
        pub nodes: Vec<Node>,
        /// A set containing all the links (channels)
        pub links: HashSet<Link>,
    }

    /// Conversion trait from processed processes information to the
    /// JSON format data needed for generating a map (Fuchsia-Map)
    impl From<processed::ProcessesData> for Json {
        fn from(processed: processed::ProcessesData) -> Self {
            let nodes: Vec<Node> = {
                let mut nodes = vec![];
                for process in &processed.processes {
                    let n = Node { name: process.name.clone(), id: process.koid };
                    nodes.push(n);
                }
                nodes
            };
            let links: HashSet<Link> = {
                let mut links = HashSet::new();
                for process in processed.processes {
                    for objects_by_type in process.objects {
                        if objects_by_type.objects_type == zx_types::ZX_OBJ_TYPE_CHANNEL {
                            for channel in objects_by_type.objects {
                                if channel.peer_owner_koid != 0 {
                                    let l = Link {
                                        source: process.koid,
                                        target: channel.peer_owner_koid,
                                    };
                                    links.insert(l);
                                }
                            }
                        }
                    }
                }
                links
            };
            Json { nodes, links }
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::fuchsia_map::json;
    use crate::processes_data::processed;
    use std::collections::{HashMap, HashSet};

    #[test]
    fn processed_to_json_test() {
        let processed = processed::ProcessesData {
            processes_count: 2,
            processes: vec![
                processed::Process {
                    koid: 789,
                    name: "process789".to_string(),
                    objects_count: 3,
                    objects: vec![
                        processed::KernelObjectsByType {
                            objects_type: 4,
                            objects_count: 2,
                            objects: vec![
                                processed::KernelObject {
                                    koid: 78,
                                    related_koid: 79,
                                    peer_owner_koid: 987,
                                },
                                processed::KernelObject {
                                    koid: 52,
                                    related_koid: 53,
                                    peer_owner_koid: 0,
                                },
                            ],
                        },
                        processed::KernelObjectsByType {
                            objects_type: 17,
                            objects_count: 1,
                            objects: vec![processed::KernelObject {
                                koid: 36,
                                related_koid: 0,
                                peer_owner_koid: 0,
                            }],
                        },
                    ],
                },
                processed::Process {
                    koid: 987,
                    name: "process987".to_string(),
                    objects_count: 2,
                    objects: vec![
                        processed::KernelObjectsByType {
                            objects_type: 4,
                            objects_count: 1,
                            objects: vec![processed::KernelObject {
                                koid: 78,
                                related_koid: 79,
                                peer_owner_koid: 789,
                            }],
                        },
                        processed::KernelObjectsByType {
                            objects_type: 19,
                            objects_count: 1,
                            objects: vec![processed::KernelObject {
                                koid: 28,
                                related_koid: 0,
                                peer_owner_koid: 0,
                            }],
                        },
                    ],
                },
            ],
        };
        let expected_nodes = HashMap::from([
            (789, json::Node { name: "process789".to_string(), id: 789 }),
            (987, json::Node { name: "process987".to_string(), id: 987 }),
        ]);
        let expected_links = HashSet::from([
            json::Link { source: 789, target: 987 },
            json::Link { source: 987, target: 789 },
        ]);
        let json_map = json::Json::from(processed);
        for node in json_map.nodes {
            let expected = expected_nodes
                .get(&node.id)
                .expect(&format!("Data contains unexpected node {:?}", node));
            pretty_assertions::assert_eq!(node, *expected);
        }
        pretty_assertions::assert_eq!(json_map.links, expected_links);
    }
}
