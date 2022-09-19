// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::routes::LinkMetrics;
use crate::{
    future_help::{Observable, Observer},
    labels::{NodeId, NodeLinkId},
    router::routes::ClientType,
};
use anyhow::Error;
use futures::prelude::*;
use std::{
    collections::{BTreeMap, HashMap},
    time::Duration,
};

pub(crate) type LinkStatePublisher =
    futures::channel::mpsc::Sender<(NodeLinkId, NodeId, ClientType, Observer<Option<Duration>>)>;
pub(crate) type LinkStateReceiver =
    futures::channel::mpsc::Receiver<(NodeLinkId, NodeId, ClientType, Observer<Option<Duration>>)>;

type LinkStatusMap = HashMap<NodeLinkId, (NodeId, Option<Duration>, ClientType)>;

/// The link status updater handles changes to the status of links. "Status" here means whether the
/// link is up or down, as well as the current ping time of the link as used for routing.
///
/// The [`LinkStateReceiver`] is a channel that will give us an [`Observer`] for each new link as it
/// is connected. The `Observer` contains an `Option<Duration>` where the duration given is the RTT
/// of that link. The option becomes `None` if the link becomes disconnected.
///
/// The link state is updated into the `observable` argument, which contains a `BTreeMap`
/// associating node IDs to information about the link to that node.
pub(crate) async fn run_link_status_updater(
    observable: Observable<BTreeMap<NodeId, LinkMetrics>>,
    receiver: LinkStateReceiver,
) -> Result<(), Error> {
    let link_status = Observable::new(LinkStatusMap::new());
    let link_status_observer = link_status.new_observer();
    futures::future::join(collate(receiver, link_status), reduce(link_status_observer, observable))
        .await;
    Ok(())
}

/// Continually condenses updates about the status of active links into an
/// `Observable<LinkStatusMap>`.
async fn collate(receiver: LinkStateReceiver, link_status: Observable<LinkStatusMap>) {
    let link_status = &link_status;
    receiver
        .for_each_concurrent(
            None,
            |(node_link_id, node_id, client_type, mut ping_time_observer)| async move {
                while let Some(duration) = ping_time_observer.next().await {
                    link_status
                        .edit(|link_status| {
                            link_status.insert(node_link_id, (node_id, duration, client_type));
                        })
                        .await;
                }
                link_status
                    .edit(|link_status| {
                        link_status.remove(&node_link_id);
                    })
                    .await;
            },
        )
        .await
}

/// Continually condenses a map of the status of all links into a map giving the status of the best
/// available link by which to reach each node.
async fn reduce(
    mut link_status_observer: Observer<LinkStatusMap>,
    observable: Observable<BTreeMap<NodeId, LinkMetrics>>,
) {
    while let Some(link_status) = link_status_observer.next().await {
        let mut new_status: BTreeMap<NodeId, LinkMetrics> = Default::default();
        for (&node_link_id, &(node_id, round_trip_time, client_type)) in link_status.iter() {
            let metrics = LinkMetrics { node_link_id, round_trip_time, client_type };
            new_status
                .entry(node_id)
                .and_modify(|link_metrics| {
                    if metrics.score() > link_metrics.score() {
                        *link_metrics = metrics.clone();
                    }
                })
                .or_insert(metrics);
        }
        observable.push(new_status).await;
    }
}
