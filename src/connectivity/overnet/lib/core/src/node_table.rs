// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::labels::{NodeId, NodeLinkId};
use failure::Error;
use std::collections::{BTreeMap, BinaryHeap};
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
    established: bool,
}

/// During pathfinding, collects the shortest path so far to a node
#[derive(Debug, Clone, Copy)]
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
/// Links that are owned by NodeTable should remain owned (mutable references should not be given
/// out)
#[derive(Debug)]
pub struct Link {
    /// Destination node for this link
    pub to: NodeId,
    /// Description of this link
    pub desc: LinkDescription,
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
    pending_callbacks: Vec<Box<dyn NodeStateCallback>>,
    triggered_callbacks: Vec<Box<dyn NodeStateCallback>>,
}

impl VersionTracker {
    /// New version tracker with an initial non-zero version stamp.
    fn new() -> Self {
        Self { pending_callbacks: Vec::new(), triggered_callbacks: Vec::new(), version: 1 }
    }

    /// Query for a new version (given the last version seen).
    /// Trigger on the next flush if last_version == self.version.
    fn post_query(&mut self, last_version: u64, cb: Box<dyn NodeStateCallback>) {
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
    fn take_triggered_callbacks(&mut self) -> (u64, Vec<Box<dyn NodeStateCallback>>) {
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
        table.get_or_create_node_mut(root_node).established = true;
        table
    }

    /// Convert the node table to a string representing a directed graph for graphviz visualization
    pub fn digraph_string(&self) -> String {
        let mut s = "digraph G {\n".to_string();
        s += &format!("  {} [shape=diamond];\n", self.root_node.0);
        for (id, node) in self.nodes.iter() {
            for (link_id, link) in node.links.iter() {
                s += &format!(
                    "  {} -> {} [label=\"[{}] rtt={:?}\"];\n",
                    id.0, link.to.0, link_id.0, link.desc.round_trip_time
                );
            }
        }
        s += "}\n";
        s
    }

    /// Query for a new version (given the last version seen).
    /// Trigger on the next flush if `last_version` == the current version.
    pub fn post_query(&mut self, last_version: u64, cb: Box<dyn NodeStateCallback>) {
        self.version_tracker.post_query(last_version, cb);
    }

    /// Execute any completed version watch callbacks.
    pub fn trigger_callbacks(&mut self) {
        let (version, callbacks) = self.version_tracker.take_triggered_callbacks();
        for mut cb in callbacks {
            if let Err(e) = cb.trigger(version, &self) {
                log::warn!("Node state callback failed: {:?}", e);
            }
        }
    }

    fn get_or_create_node_mut(&mut self, node_id: NodeId) -> &mut Node {
        let version_tracker = &mut self.version_tracker;
        let mut was_new = false;
        let node = self.nodes.entry(node_id).or_insert_with(|| {
            version_tracker.incr_version();
            was_new = true;
            Node { links: BTreeMap::new(), desc: NodeDescription::default(), established: false }
        });
        node
    }

    /// Collates and returns all services available
    pub fn nodes(&self) -> impl Iterator<Item = NodeId> + '_ {
        self.nodes.iter().map(|(id, _)| *id)
    }

    /// Return the services supplied by some `node_id`
    pub fn node_services(&self, node_id: NodeId) -> &[String] {
        self.nodes.get(&node_id).map(|node| node.desc.services.as_slice()).unwrap_or(&[])
    }

    /// Returns all links from a single node
    pub fn node_links(
        &self,
        node_id: NodeId,
    ) -> Option<impl Iterator<Item = (&NodeLinkId, &Link)> + '_> {
        self.nodes.get(&node_id).map(|node| node.links.iter())
    }

    /// Update a single node
    pub fn update_node(&mut self, node_id: NodeId, desc: NodeDescription) {
        let node = self.get_or_create_node_mut(node_id);
        node.desc = desc;
        self.version_tracker.incr_version();
        log::trace!("{}", self.digraph_string());
    }

    /// Is a connection established to some node?
    pub fn is_established(&self, node_id: NodeId) -> bool {
        self.nodes.get(&node_id).map_or(false, |node| node.established)
    }

    /// Mark a node as being established
    pub fn mark_established(&mut self, node_id: NodeId) {
        log::info!("{:?} mark node established: {:?}", self.root_node, node_id);
        let node = self.get_or_create_node_mut(node_id);
        if node.established {
            return;
        }
        node.established = true;
        self.version_tracker.incr_version();
    }

    /// Mention that a node exists
    pub fn mention_node(&mut self, node_id: NodeId) {
        self.get_or_create_node_mut(node_id);
    }

    /// Update a single link on a node.
    pub fn update_link(
        &mut self,
        from: NodeId,
        to: NodeId,
        link_id: NodeLinkId,
        desc: LinkDescription,
    ) {
        log::trace!(
            "update_link: from:{:?} to:{:?} link_id:{:?} desc:{:?}",
            from,
            to,
            link_id,
            desc
        );
        assert_ne!(from, to);
        self.get_or_create_node_mut(to);
        self.get_or_create_node_mut(from).links.insert(link_id, Link { to, desc });
        log::trace!("{}", self.digraph_string());
    }

    /// Build a routing table for our node based on current link data
    pub fn build_routes(&self) -> impl Iterator<Item = (NodeId, NodeLinkId)> {
        let mut todo = BinaryHeap::new();

        let mut progress = BTreeMap::<NodeId, NodeProgress>::new();
        for (link_id, link) in self.nodes.get(&self.root_node).unwrap().links.iter() {
            if link.to == self.root_node {
                continue;
            }
            todo.push(link.to);
            let new_progress = NodeProgress {
                round_trip_time: link.desc.round_trip_time,
                outgoing_link: *link_id,
            };
            progress
                .entry(link.to)
                .and_modify(|p| {
                    if p.round_trip_time > new_progress.round_trip_time {
                        *p = new_progress;
                    }
                })
                .or_insert_with(|| new_progress);
        }

        log::trace!("BUILD START: progress={:?} todo={:?}", progress, todo);

        while let Some(from) = todo.pop() {
            log::trace!("STEP {:?}: progress={:?} todo={:?}", from, progress, todo);
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
                            todo.push(link.to);
                        }
                    })
                    .or_insert_with(|| {
                        todo.push(link.to);
                        new_progress
                    });
            }
        }

        log::trace!("DONE: progress={:?} todo={:?}", progress, todo);
        progress
            .into_iter()
            .map(|(node_id, NodeProgress { outgoing_link: link_id, .. })| (node_id, link_id))
    }
}

#[cfg(test)]
mod test {

    use super::*;

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
                LinkDescription { round_trip_time: Duration::from_millis(*rtt) },
            );
        }

        node_table
    }

    fn is_outcome(mut got: Vec<(NodeId, NodeLinkId)>, outcome: &[(u64, u64)]) -> bool {
        let mut result = true;
        for (node_id, link_id) in outcome {
            if !remove_item(&((*node_id).into(), (*link_id).into()), &mut got) {
                log::trace!("Expected outcome not found: {}#{}", node_id, link_id);
                result = false;
            }
        }
        for (node_id, link_id) in got {
            log::trace!("Unexpected outcome: {}#{}", node_id.0, link_id.0);
            result = false;
        }
        result
    }

    fn builds_route_ok(links: &[(u64, u64, u64, u64)], outcome: &[(u64, u64)]) -> bool {
        log::trace!("TEST: {:?} --> {:?}", links, outcome);
        let node_table = construct_node_table_from_links(links);
        let built: Vec<(NodeId, NodeLinkId)> = node_table.build_routes().collect();
        let r = is_outcome(built.clone(), outcome);
        if !r {
            log::trace!("NODE_TABLE: {:?}", node_table.nodes);
            log::trace!("BUILT: {:?}", built);
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
