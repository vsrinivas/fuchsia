// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::{Observer, PollMutex},
    labels::{NodeId, NodeLinkId},
    link::LinkStatus,
    router::Router,
};
use anyhow::{bail, format_err, Error};
use fuchsia_async::Timer;
use futures::{future::poll_fn, lock::Mutex, prelude::*, ready};
use std::{
    collections::{BTreeMap, BinaryHeap},
    sync::{Arc, Weak},
    task::{Context, Poll, Waker},
    time::Duration,
};

/// Assumed forwarding time through a node.
/// This is a temporary hack to alleviate some bad route selection.
const FORWARDING_TIME: Duration = Duration::from_millis(100);

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
    version: u64,
    wake_on_version_change: Option<Waker>,
}

impl NodeTable {
    /// Create a new node table rooted at `root_node`
    pub fn new(root_node: NodeId) -> NodeTable {
        NodeTable { root_node, nodes: BTreeMap::new(), version: 0, wake_on_version_change: None }
    }

    fn poll_new_version(&mut self, ctx: &mut Context<'_>, last_version: &mut u64) -> Poll<()> {
        if *last_version == self.version {
            self.wake_on_version_change = Some(ctx.waker().clone());
            Poll::Pending
        } else {
            *last_version = self.version;
            Poll::Ready(())
        }
    }

    fn get_or_create_node_mut(&mut self, node_id: NodeId) -> &mut Node {
        self.nodes.entry(node_id).or_insert_with(|| Node { links: BTreeMap::new() })
    }

    /// Update a single link on a node.
    fn update_link(
        &mut self,
        from: NodeId,
        to: NodeId,
        link_id: NodeLinkId,
        desc: LinkDescription,
    ) -> Result<(), Error> {
        log::trace!(
            "{:?} update_link: from:{:?} to:{:?} link_id:{:?} desc:{:?}",
            self.root_node,
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
        Ok(())
    }

    fn update_links(&mut self, from: NodeId, links: Vec<LinkStatus>) -> Result<(), Error> {
        self.get_or_create_node_mut(from).links.clear();
        for LinkStatus { to, local_id, round_trip_time } in links.into_iter() {
            self.update_link(from, to, local_id, LinkDescription { round_trip_time })?;
        }
        self.version += 1;
        self.wake_on_version_change.take().map(|w| w.wake());
        Ok(())
    }

    /// Build a routing table for our node based on current link data
    fn build_routes(&self) -> impl Iterator<Item = (NodeId, NodeLinkId)> {
        let mut todo = BinaryHeap::new();

        log::trace!("{:?} BUILD ROUTES: {:?}", self.root_node, self.nodes);

        let mut progress = BTreeMap::<NodeId, NodeProgress>::new();
        for (link_id, link) in self.nodes.get(&self.root_node).unwrap().links.iter() {
            if link.to == self.root_node {
                continue;
            }
            todo.push(link.to);
            let new_progress = NodeProgress {
                round_trip_time: link.desc.round_trip_time + 2 * FORWARDING_TIME,
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
                    round_trip_time: progress_from.round_trip_time
                        + link.desc.round_trip_time
                        + 2 * FORWARDING_TIME,
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
pub(crate) struct RemoteRoutingUpdate {
    pub(crate) from_node_id: NodeId,
    pub(crate) status: Vec<LinkStatus>,
}

pub(crate) type RemoteRoutingUpdateSender = futures::channel::mpsc::Sender<RemoteRoutingUpdate>;
pub(crate) type RemoteRoutingUpdateReceiver = futures::channel::mpsc::Receiver<RemoteRoutingUpdate>;

pub(crate) fn routing_update_channel() -> (RemoteRoutingUpdateSender, RemoteRoutingUpdateReceiver) {
    futures::channel::mpsc::channel(1)
}

pub(crate) async fn run_route_planner(
    router: &Weak<Router>,
    mut remote_updates: RemoteRoutingUpdateReceiver,
    mut local_updates: Observer<Vec<LinkStatus>>,
) -> Result<(), Error> {
    let get_router = move || Weak::upgrade(router).ok_or_else(|| format_err!("router gone"));
    let node_table = Arc::new(Mutex::new(NodeTable::new(get_router()?.node_id())));
    let remote_node_table = node_table.clone();
    let local_node_table = node_table.clone();
    let update_node_table = node_table;
    let _: ((), (), ()) = futures::future::try_join3(
        async move {
            while let Some(RemoteRoutingUpdate { from_node_id, status }) =
                remote_updates.next().await
            {
                let mut node_table = remote_node_table.lock().await;
                if from_node_id == node_table.root_node {
                    log::warn!("Attempt to update own node id links as remote");
                    continue;
                }
                if let Err(e) = node_table.update_links(from_node_id, status) {
                    log::warn!("Update remote links from {:?} failed: {:?}", from_node_id, e);
                    continue;
                }
            }
            Ok::<_, Error>(())
        },
        async move {
            while let Some(status) = local_updates.next().await {
                let mut node_table = local_node_table.lock().await;
                let root_node = node_table.root_node;
                if let Err(e) = node_table.update_links(root_node, status) {
                    log::warn!("Update local links failed: {:?}", e);
                    continue;
                }
            }
            Ok(())
        },
        async move {
            let mut pm = PollMutex::new(&*update_node_table);
            let mut current_version = 0;
            let mut poll_version = move |ctx: &mut Context<'_>| {
                let mut node_table = ready!(pm.poll(ctx));
                ready!(node_table.poll_new_version(ctx, &mut current_version));
                Poll::Ready(node_table)
            };
            loop {
                let node_table = poll_fn(&mut poll_version).await;
                get_router()?.update_routes(node_table.build_routes(), "new_routes").await?;
                drop(node_table);
                Timer::new(Duration::from_millis(100)).await;
            }
        },
    )
    .await?;
    Ok(())
}

#[cfg(test)]
mod test {

    use super::*;
    use arbitrary::{Arbitrary, Unstructured};
    use rand::Rng;
    use std::collections::HashMap;
    use std::time::Instant;

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
        crate::test_util::init();
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

    #[derive(Arbitrary, Debug, Clone, Copy)]
    struct DoesntFormLoops {
        a_to_b: u64,
        b_to_a: u64,
        a_to_c: u64,
        c_to_a: u64,
    }

    fn verify_no_loops(config: DoesntFormLoops) {
        // With node configuration:
        // B(2) - A(1) - C(3)
        // Verify that routes from A to B do not point at C
        // and that routes from A to C do not point at B

        println!("{:?}", config);

        let built: HashMap<NodeId, NodeLinkId> = construct_node_table_from_links(&[
            (1, 2, 100, config.a_to_b),
            (2, 1, 200, config.b_to_a),
            (1, 3, 300, config.a_to_c),
            (3, 1, 400, config.c_to_a),
        ])
        .build_routes()
        .collect();

        assert_eq!(built.get(&2.into()), Some(&100.into()));
        assert_eq!(built.get(&3.into()), Some(&300.into()));
    }

    #[test]
    fn no_loops() {
        crate::test_util::init();
        let start = Instant::now();
        while Instant::now() - start < Duration::from_secs(1) {
            let mut random_junk = [0u8; 64];
            rand::thread_rng().fill(&mut random_junk);
            verify_no_loops(Arbitrary::arbitrary(&mut Unstructured::new(&random_junk)).unwrap());
        }
    }
}
