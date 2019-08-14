// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::labels::{NodeId, NodeLinkId, VersionCounter, TOMBSTONE_VERSION};
use failure::Error;
use std::collections::{btree_map, BTreeMap, BTreeSet};
use std::time::Duration;

/// Describes a node in the overnet mesh
#[derive(Debug)]
pub struct NodeDescription {
    /// Services exposed by this node.
    pub services: Vec<String>,
}

impl Default for NodeDescription {
    fn default() -> Self {
        NodeDescription { services: Vec::new() }
    }
}

/// Collects all information about a node in one place
#[derive(Debug)]
struct Node {
    links: BTreeMap<NodeLinkId, Link>,
    desc: NodeDescription,
}

/// During pathfinding, collects the shortest path so far to a node
#[derive(Clone, Copy)]
struct NodeProgress {
    round_trip_time: Duration,
    outgoing_link: NodeLinkId,
}

/// Describes the state of a link
#[derive(Debug)]
pub struct LinkDescription {
    /// Current round trip time estimate for this link
    pub round_trip_time: Duration,
}

/// Collects all information about one link on one node
#[derive(Debug)]
struct Link {
    to: NodeId,
    version: VersionCounter,
    desc: LinkDescription,
}

/// Notification of a new version of the node table being available.
pub trait NodeStateCallback {
    /// Called when the node state version is different to the one supplied at the initiation of
    /// monitoring.
    fn trigger(&mut self, new_version: u64, node_table: &NodeTable) -> Result<(), Error>;
}

/// Tracks the current version of the node table, and callbacks that would like to be informed when
/// that changes.
struct VersionTracker {
    version: u64,
    pending_callbacks: Vec<Box<NodeStateCallback>>,
    triggered_callbacks: Vec<Box<NodeStateCallback>>,
}

impl VersionTracker {
    /// New version tracker with an initial non-zero version stamp.
    fn new() -> Self {
        Self { pending_callbacks: Vec::new(), triggered_callbacks: Vec::new(), version: 1 }
    }

    /// Query for a new version (given the last version seen).
    /// Trigger on the next flush if last_version == self.version.
    fn post_query(&mut self, last_version: u64, cb: Box<NodeStateCallback>) {
        if last_version < self.version {
            self.triggered_callbacks.push(cb);
        } else {
            self.pending_callbacks.push(cb);
        }
    }

    /// Move to the next version.
    fn incr_version(&mut self) {
        self.version += 1;
        self.triggered_callbacks.extend(self.pending_callbacks.drain(..));
    }

    /// Returns the current version and the (now flushed) triggered callbacks
    fn take_triggered_callbacks(&mut self) -> (u64, Vec<Box<NodeStateCallback>>) {
        let callbacks = std::mem::replace(&mut self.triggered_callbacks, Vec::new());
        (self.version, callbacks)
    }
}

/// Table of all nodes (and links between them) known to an instance
pub struct NodeTable {
    root_node: NodeId,
    nodes: BTreeMap<NodeId, Node>,
    version_tracker: VersionTracker,
}

impl NodeTable {
    /// Create a new node table rooted at `root_node`
    pub fn new(root_node: NodeId) -> NodeTable {
        let mut table =
            NodeTable { root_node, nodes: BTreeMap::new(), version_tracker: VersionTracker::new() };
        table.get_or_create_node_mut(root_node);
        table
    }

    /// Query for a new version (given the last version seen).
    /// Trigger on the next flush if `last_version` == the current version.
    pub fn post_query(&mut self, last_version: u64, cb: Box<NodeStateCallback>) {
        self.version_tracker.post_query(last_version, cb);
    }

    /// Execute any completed version watch callbacks.
    pub fn trigger_callbacks(&mut self) {
        let (version, callbacks) = self.version_tracker.take_triggered_callbacks();
        for mut cb in callbacks {
            if let Err(e) = cb.trigger(version, &self) {
                warn!("Node state callback failed: {:?}", e);
            }
        }
    }

    fn get_or_create_node_mut(&mut self, node_id: NodeId) -> &mut Node {
        let version_tracker = &mut self.version_tracker;
        self.nodes.entry(node_id).or_insert_with(|| {
            version_tracker.incr_version();
            Node { links: BTreeMap::new(), desc: NodeDescription::default() }
        })
    }

    /// Collates and returns all services available
    pub fn nodes(&self) -> impl Iterator<Item = NodeId> + '_ {
        self.nodes.iter().map(|(id, _)| *id)
    }

    /// Return the services supplied by some `node_id`
    pub fn node_services(&self, node_id: NodeId) -> &[String] {
        self.nodes.get(&node_id).map(|node| node.desc.services.as_slice()).unwrap_or(&[])
    }

    /// Update a single node
    pub fn update_node(&mut self, node_id: NodeId, desc: NodeDescription) {
        self.get_or_create_node_mut(node_id).desc = desc;
        self.version_tracker.incr_version();
    }

    /// Mention that a node exists
    pub fn mention_node(&mut self, node_id: NodeId) {
        self.get_or_create_node_mut(node_id);
    }

    /// Update a single link on a node
    pub fn update_link(
        &mut self,
        from: NodeId,
        to: NodeId,
        link_id: NodeLinkId,
        version: VersionCounter,
        desc: LinkDescription,
    ) {
        if from == to {
            return;
        }
        self.get_or_create_node_mut(to);
        let node = self.get_or_create_node_mut(from);
        match node.links.entry(link_id) {
            btree_map::Entry::Occupied(mut o) => {
                let l = o.get_mut();
                if l.version < version {
                    l.version = version;
                    l.desc = desc;
                    l.to = to;
                }
            }
            btree_map::Entry::Vacant(v) => {
                if version != TOMBSTONE_VERSION {
                    v.insert(Link { to, version, desc });
                }
            }
        }
    }

    /// Build a routing table for our node based on current link data
    pub fn build_routes(&self) -> impl Iterator<Item = (NodeId, NodeLinkId)> {
        let mut todo = BTreeSet::new();

        let mut progress = BTreeMap::new();
        for (link_id, link) in self.nodes.get(&self.root_node).unwrap().links.iter() {
            if link.to == self.root_node {
                continue;
            }
            todo.insert(link.to);
            progress.insert(
                link.to,
                NodeProgress {
                    round_trip_time: link.desc.round_trip_time,
                    outgoing_link: *link_id,
                },
            );
        }

        while let Some(from) = todo.iter().next().copied() {
            todo.remove(&from);
            let progress_from = progress.get(&from).unwrap().clone();
            for (_, link) in self.nodes.get(&from).unwrap().links.iter() {
                if link.to == self.root_node {
                    continue;
                }
                let new_progress = NodeProgress {
                    round_trip_time: progress_from.round_trip_time + link.desc.round_trip_time,
                    outgoing_link: progress_from.outgoing_link,
                };
                progress
                    .entry(link.to)
                    .and_modify(|p| {
                        if p.round_trip_time > new_progress.round_trip_time {
                            *p = new_progress;
                            todo.insert(link.to);
                        }
                    })
                    .or_insert_with(|| {
                        todo.insert(link.to);
                        new_progress
                    });
            }
        }

        progress
            .into_iter()
            .map(|(node_id, NodeProgress { outgoing_link: link_id, .. })| (node_id, link_id))
    }
}

#[cfg(test)]
mod test {

    use super::*;
    use crate::labels::FIRST_VERSION;

    fn remove_item<T: Eq>(value: &T, from: &mut Vec<T>) -> bool {
        let len = from.len();
        for i in 0..len {
            if from[i] == *value {
                from.remove(i);
                return true;
            }
        }
        return false;
    }

    fn construct_node_table_from_links(links: &[(u64, u64, u64, u64)]) -> NodeTable {
        let mut node_table = NodeTable::new(1.into());

        for (from, to, link_id, rtt) in links {
            node_table.update_link(
                (*from).into(),
                (*to).into(),
                (*link_id).into(),
                FIRST_VERSION,
                LinkDescription { round_trip_time: Duration::from_millis(*rtt) },
            );
        }

        node_table
    }

    fn is_outcome(mut got: Vec<(NodeId, NodeLinkId)>, outcome: &[(u64, u64)]) -> bool {
        let mut result = true;
        for (node_id, link_id) in outcome {
            if !remove_item(&((*node_id).into(), (*link_id).into()), &mut got) {
                println!("Expected outcome not found: {}#{}", node_id, link_id);
                result = false;
            }
        }
        for (node_id, link_id) in got {
            println!("Unexpected outcome: {}#{}", node_id.0, link_id.0);
            result = false;
        }
        result
    }

    fn builds_route_ok(links: &[(u64, u64, u64, u64)], outcome: &[(u64, u64)]) -> bool {
        println!("TEST: {:?} --> {:?}", links, outcome);
        let node_table = construct_node_table_from_links(links);
        let built: Vec<(NodeId, NodeLinkId)> = node_table.build_routes().collect();
        let r = is_outcome(built.clone(), outcome);
        if !r {
            println!("NODE_TABLE: {:?}", node_table.nodes);
            println!("BUILT: {:?}", built);
        }
        r
    }

    #[test]
    fn test_build_routes() {
        assert!(builds_route_ok(&[(1, 2, 1, 10), (2, 1, 123, 5)], &[(2, 1)]));
        assert!(builds_route_ok(
            &[
                (1, 2, 1, 10),
                (2, 1, 123, 5),
                (1, 3, 2, 10),
                (3, 1, 133, 1),
                (2, 3, 7, 88),
                (3, 2, 334, 23)
            ],
            &[(2, 1), (3, 2)]
        ));
        assert!(builds_route_ok(
            &[
                (1, 2, 1, 10),
                (2, 1, 123, 5),
                (1, 3, 2, 1000),
                (3, 1, 133, 1),
                (2, 3, 7, 88),
                (3, 2, 334, 23)
            ],
            &[(2, 1), (3, 1)]
        ));
    }

    #[test]
    fn test_services() {
        let mut node_table = NodeTable::new(1.into());
        node_table.update_node(
            2.into(),
            NodeDescription { services: vec!["hello".to_string()], ..Default::default() },
        );
        assert_eq!(node_table.nodes().collect::<Vec<NodeId>>().as_slice(), [1.into(), 2.into()]);
        assert_eq!(node_table.node_services(2.into()), ["hello".to_string()]);
        assert_eq!(node_table.node_services(3.into()), Vec::<String>::new().as_slice());
    }

}
