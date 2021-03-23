// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::routes::LinkMetrics;
use crate::{
    future_help::{Observable, Observer},
    labels::{NodeId, NodeLinkId},
};
use anyhow::Error;
use futures::prelude::*;
use std::{
    collections::{BTreeMap, HashMap},
    time::Duration,
};

pub(crate) type LinkStatePublisher =
    futures::channel::mpsc::Sender<(NodeLinkId, NodeId, Observer<Option<Duration>>)>;
pub(crate) type LinkStateReceiver =
    futures::channel::mpsc::Receiver<(NodeLinkId, NodeId, Observer<Option<Duration>>)>;

type LinkStatusMap = HashMap<NodeLinkId, (NodeId, Option<Duration>)>;

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

async fn collate(receiver: LinkStateReceiver, link_status: Observable<LinkStatusMap>) {
    let link_status = &link_status;
    receiver
        .for_each_concurrent(None, |(node_link_id, node_id, mut ping_time_observer)| async move {
            while let Some(duration) = ping_time_observer.next().await {
                link_status
                    .edit(|link_status| {
                        link_status.insert(node_link_id, (node_id, duration));
                    })
                    .await;
            }
            link_status
                .edit(|link_status| {
                    link_status.remove(&node_link_id);
                })
                .await;
        })
        .await
}

async fn reduce(
    mut link_status_observer: Observer<LinkStatusMap>,
    observable: Observable<BTreeMap<NodeId, LinkMetrics>>,
) {
    while let Some(link_status) = link_status_observer.next().await {
        let mut new_status: BTreeMap<NodeId, LinkMetrics> = Default::default();
        for (&node_link_id, &(node_id, round_trip_time)) in link_status.iter() {
            let metrics = LinkMetrics { node_link_id, round_trip_time };
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
