// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::log_errors,
    labels::{NodeId, NodeLinkId},
    router::Router,
    runtime::{maybe_wait_until, spawn},
};
use anyhow::{bail, format_err, Error};
use futures::{prelude::*, select};
use std::{
    collections::{btree_map, BTreeMap, BinaryHeap},
    rc::{Rc, Weak},
    time::{Duration, Instant},
};

/// Collects all information about a node in one place
#[derive(Debug)]
struct Node {
    links: BTreeMap<NodeLinkId, Link>,
}

/// During pathfinding, collects the shortest path so far to a node
#[derive(Debug, Clone, Copy)]
struct NodeProgress {
    round_trip_time: Duration,
    outgoing_link: NodeLinkId,
}

/// Describes the state of a link
#[derive(Debug, Clone)]
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

/// Table of all nodes (and links between them) known to an instance
struct NodeTable {
    root_node: NodeId,
    nodes: BTreeMap<NodeId, Node>,
}

impl NodeTable {
    /// Create a new node table rooted at `root_node`
    pub fn new(root_node: NodeId) -> NodeTable {
        NodeTable { root_node, nodes: BTreeMap::new() }
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

    fn get_or_create_node_mut(&mut self, node_id: NodeId) -> &mut Node {
        self.nodes.entry(node_id).or_insert_with(|| Node { links: BTreeMap::new() })
    }

    /// Update a single link on a node.
    pub fn update_link(
        &mut self,
        from: NodeId,
        to: NodeId,
        link_id: NodeLinkId,
        desc: LinkDescription,
    ) -> Result<(), Error> {
        log::trace!(
            "update_link: from:{:?} to:{:?} link_id:{:?} desc:{:?}",
            from,
            to,
            link_id,
            desc
        );
        if from == to {
            bail!("Circular link seen");
        }
        self.get_or_create_node_mut(to);
        self.get_or_create_node_mut(from).links.insert(link_id, Link { to, desc });
        log::trace!("{}", self.digraph_string());
        Ok(())
    }

    pub fn update_links(
        &mut self,
        from: NodeId,
        links: Vec<(NodeId, NodeLinkId, LinkDescription)>,
    ) -> Result<(), Error> {
        self.get_or_create_node_mut(from).links.clear();
        for (to, link_id, desc) in links.into_iter() {
            self.update_link(from, to, link_id, desc)?;
        }
        Ok(())
    }

    pub fn remove_link(&mut self, from: NodeId, to: NodeId, link_id: NodeLinkId) {
        match self.get_or_create_node_mut(from).links.entry(link_id) {
            btree_map::Entry::Occupied(o) if o.get().to == to => {
                o.remove();
            }
            _ => (),
        }
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

#[derive(Debug)]
pub enum RoutingUpdate {
    UpdateLocalLinkStatus {
        to_node_id: NodeId,
        link_id: NodeLinkId,
        description: Option<LinkDescription>,
    },
    UpdateRemoteLinkStatus {
        from_node_id: NodeId,
        status: Vec<(NodeId, NodeLinkId, LinkDescription)>,
    },
}

pub(crate) type RoutingUpdateSender = futures::channel::mpsc::Sender<RoutingUpdate>;
pub(crate) type RoutingUpdateReceiver = futures::channel::mpsc::Receiver<RoutingUpdate>;

pub fn routing_update_channel() -> (RoutingUpdateSender, RoutingUpdateReceiver) {
    futures::channel::mpsc::channel(1)
}

#[derive(Debug)]
enum Action {
    Apply(RoutingUpdate),
    UpdateRoutes,
    Quit,
}

pub(crate) fn spawn_route_planner(router: Rc<Router>, mut updates: RoutingUpdateReceiver) {
    let mut node_table = NodeTable::new(router.node_id());
    let router = Rc::downgrade(&router);
    spawn(log_errors(
        async move {
            let mut next_route_table_update = None;
            loop {
                let action = select! {
                    x = updates.next().fuse() => match x {
                        Some(x) => Action::Apply(x),
                        None => Action::Quit,
                    },
                    _ = maybe_wait_until(next_route_table_update).fuse() => Action::UpdateRoutes
                };
                log::trace!("Routing update: {:?}", action);
                match action {
                    Action::Quit => return Ok(()),
                    Action::Apply(update) => {
                        match update {
                            RoutingUpdate::UpdateLocalLinkStatus {
                                to_node_id,
                                link_id,
                                description: Some(description),
                            } => {
                                if let Err(e) = node_table.update_link(
                                    node_table.root_node,
                                    to_node_id,
                                    link_id,
                                    description,
                                ) {
                                    log::warn!("Update link failed: {:?}", e);
                                    continue;
                                }
                            }
                            RoutingUpdate::UpdateLocalLinkStatus {
                                to_node_id,
                                link_id,
                                description: None,
                            } => {
                                node_table.remove_link(node_table.root_node, to_node_id, link_id);
                            }
                            RoutingUpdate::UpdateRemoteLinkStatus { from_node_id, status } => {
                                if from_node_id == node_table.root_node {
                                    log::warn!("Attempt to update own node id links as remote");
                                    continue;
                                }
                                if let Err(e) = node_table.update_links(from_node_id, status) {
                                    log::warn!("Update links failed: {:?}", e);
                                    continue;
                                }
                            }
                        }
                        if next_route_table_update.is_none() {
                            next_route_table_update =
                                Some(Instant::now() + Duration::from_millis(100));
                        }
                    }
                    Action::UpdateRoutes => {
                        next_route_table_update = None;
                        Weak::upgrade(&router)
                            .ok_or_else(|| format_err!("Router shut down"))?
                            .update_routes(node_table.build_routes())
                            .await?;
                    }
                }
            }
        },
        "Failed planning routes",
    ));
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
            node_table
                .update_link(
                    (*from).into(),
                    (*to).into(),
                    (*link_id).into(),
                    LinkDescription { round_trip_time: Duration::from_millis(*rtt) },
                )
                .unwrap();
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
}
