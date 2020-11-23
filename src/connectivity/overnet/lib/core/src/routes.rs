// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::future_help::{Observable, Observer};
use crate::{NodeId, NodeLinkId};
use anyhow::Error;
use fidl_fuchsia_overnet_protocol::RouteMetrics;
use fuchsia_async::Task;
use futures::prelude::*;
use std::collections::BTreeMap;
use std::convert::TryInto;
use std::sync::Arc;
use std::time::Duration;

#[derive(Clone, Debug)]
pub(crate) struct ForwardingTable {
    table: Arc<BTreeMap<NodeId, Metrics>>,
}

impl ForwardingTable {
    pub(crate) fn empty() -> ForwardingTable {
        ForwardingTable { table: Arc::new(BTreeMap::new()) }
    }

    pub(crate) fn iter(&self) -> impl Iterator<Item = (NodeId, &Metrics)> {
        self.table.iter().map(|(n, m)| (*n, m))
    }

    pub(crate) fn route_for(&self, peer: NodeId) -> Option<NodeLinkId> {
        self.table.get(&peer).map(|peer| peer.node_link_id)
    }

    pub(crate) fn filter_out_via(self, node_id: NodeId) -> ForwardingTable {
        ForwardingTable {
            table: Arc::new(
                self.table
                    .iter()
                    .filter(|(&destination, metrics)| {
                        destination != node_id && !metrics.is_via(node_id)
                    })
                    .map(|(destination, metrics)| (*destination, metrics.clone()))
                    .collect(),
            ),
        }
    }

    pub(crate) fn is_significantly_different_to(&self, other: &Self) -> bool {
        if !self.table.keys().eq(other.table.keys()) {
            return true;
        }

        for (a, b) in self.table.values().zip(other.table.values()) {
            if a.is_significantly_different_to(b) {
                return true;
            }
        }

        return false;
    }
}

#[derive(Clone, Debug)]
struct ReceivedMetrics {
    round_trip_time: Option<Duration>,
    intermediate_hops: Vec<NodeId>,
}

impl From<RouteMetrics> for ReceivedMetrics {
    fn from(m: RouteMetrics) -> Self {
        Self {
            round_trip_time: m.round_trip_time_us.map(Duration::from_micros),
            intermediate_hops: m
                .intermediate_hops
                .map(|hops| hops.into_iter().map(Into::into).collect())
                .unwrap_or_else(Vec::new),
        }
    }
}

fn score_rtt(rtt: Option<Duration>) -> impl PartialOrd {
    rtt.map(|d| -d.as_secs_f32())
}

#[derive(Clone, Debug)]
pub(crate) struct LinkMetrics {
    pub round_trip_time: Option<Duration>,
    pub node_link_id: NodeLinkId,
}

impl LinkMetrics {
    pub(crate) fn score(&self) -> impl PartialOrd {
        score_rtt(self.round_trip_time)
    }
}

#[derive(Clone, Debug)]
pub(crate) struct Metrics {
    round_trip_time: Option<Duration>,
    intermediate_hops: Vec<NodeId>,
    node_link_id: NodeLinkId,
}

impl From<&Metrics> for RouteMetrics {
    fn from(metrics: &Metrics) -> Self {
        Self {
            round_trip_time_us: metrics
                .round_trip_time
                .and_then(|rtt| rtt.as_micros().try_into().ok()),
            intermediate_hops: Some(metrics.intermediate_hops.iter().map(Into::into).collect()),
            ..Self::empty()
        }
    }
}

impl From<&LinkMetrics> for Metrics {
    fn from(metrics: &LinkMetrics) -> Self {
        Self {
            round_trip_time: metrics.round_trip_time,
            intermediate_hops: Vec::new(),
            node_link_id: metrics.node_link_id,
        }
    }
}

impl Metrics {
    fn join(own_node_id: NodeId, received: ReceivedMetrics, link: &LinkMetrics) -> Self {
        let mut intermediate_hops = received.intermediate_hops;
        intermediate_hops.push(own_node_id);
        Metrics {
            round_trip_time: received.round_trip_time.zip(link.round_trip_time).map(|(a, b)| a + b),
            node_link_id: link.node_link_id,
            intermediate_hops,
        }
    }

    pub(crate) fn is_via(&self, node_id: NodeId) -> bool {
        self.intermediate_hops.iter().find(|&&n| n == node_id).is_some()
    }

    pub(crate) fn score(&self) -> impl PartialOrd {
        score_rtt(self.round_trip_time)
    }

    fn is_significantly_different_to(&self, other: &Self) -> bool {
        if self.node_link_id != other.node_link_id {
            return true;
        }

        if self.intermediate_hops != other.intermediate_hops {
            return true;
        }

        if let Some(a) = self.round_trip_time {
            if let Some(b) = other.round_trip_time {
                let big_rtt = std::cmp::max(a, b);
                let small_rtt = std::cmp::min(a, b);
                let rtt_diff = big_rtt - small_rtt;
                if rtt_diff > std::cmp::max(Duration::from_millis(5), big_rtt / 5) {
                    return true;
                }
            } else {
                return true;
            }
        } else if other.round_trip_time.is_some() {
            return true;
        }

        false
    }
}

#[derive(Clone, Debug)]
struct Route {
    destination: NodeId,
    via: NodeId,
    received_metrics: ReceivedMetrics,
}

#[derive(Default, Debug, Clone)]
struct DB {
    routes: Vec<Route>,
    links: BTreeMap<NodeId, LinkMetrics>,
}

pub(crate) struct Routes {
    db: Observable<DB>,
    forwarding_table: Observable<ForwardingTable>,
}

impl Routes {
    pub(crate) fn new() -> Routes {
        let db = Observable::new(DB::default());
        let forwarding_table =
            Observable::new(ForwardingTable { table: Arc::new(BTreeMap::new()) });
        Routes { db, forwarding_table }
    }

    pub(crate) async fn update(
        &self,
        via: NodeId,
        routes: impl Iterator<Item = (NodeId, RouteMetrics)>,
    ) {
        self.db
            .edit(|db| {
                db.routes.retain(|r| r.via != via);
                db.routes.extend(routes.map(|(destination, route_metrics)| Route {
                    destination,
                    via,
                    received_metrics: route_metrics.into(),
                }));
            })
            .await;
    }

    pub(crate) fn new_forwarding_table_observer(&self) -> Observer<ForwardingTable> {
        self.forwarding_table.new_observer()
    }

    pub(crate) async fn run_planner(
        self: &Arc<Self>,
        own_node_id: NodeId,
        link_state: Observer<BTreeMap<NodeId, LinkMetrics>>,
    ) -> Result<(), Error> {
        let _merger = Task::spawn(self.clone().merge_links(link_state));

        let mut db = self.db.new_observer();
        let mut last_emitted = ForwardingTable::empty();
        while let Some(db) = db.next().await {
            log::trace!("[{:?}] Update with new route database {:#?}", own_node_id, db);
            let mut wip: BTreeMap<NodeId, Metrics> = db
                .links
                .iter()
                .map(|(destination, link_metrics)| (*destination, link_metrics.into()))
                .collect();
            for Route { destination, via, received_metrics } in db.routes.into_iter() {
                if let Some(link_metrics) = db.links.get(&via) {
                    let metrics = Metrics::join(own_node_id, received_metrics, link_metrics);
                    wip.entry(destination)
                        .and_modify(|existing| {
                            if metrics.score() > existing.score() {
                                *existing = metrics.clone();
                            }
                        })
                        .or_insert(metrics);
                }
            }
            let table = ForwardingTable { table: Arc::new(wip) };
            if last_emitted.is_significantly_different_to(&table) {
                log::trace!("[{:?}] New forwarding table: {:#?}", own_node_id, table);
                self.forwarding_table.push(table.clone()).await;
                last_emitted = table;
            }
        }
        Ok(())
    }

    async fn merge_links(self: Arc<Self>, mut link_state: Observer<BTreeMap<NodeId, LinkMetrics>>) {
        while let Some(links) = link_state.next().await {
            self.db
                .edit(|db| {
                    db.links = links;
                })
                .await;
            //Timer::new(Duration::from_millis(600)).await;
        }
    }
}
