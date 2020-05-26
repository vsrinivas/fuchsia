// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::{CompositeStreamExt, Observable},
    labels::{NodeId, NodeLinkId},
    link::LinkStatus,
};
use anyhow::Error;
use futures::prelude::*;
use std::{collections::HashMap, pin::Pin, sync::Arc, time::Duration};

pub type StatusChangeStream =
    Pin<Box<dyn Send + Stream<Item = (NodeLinkId, NodeId, Option<Duration>)>>>;
pub type LinkStatePublisher = futures::channel::mpsc::Sender<StatusChangeStream>;
pub type LinkStateReceiver = futures::channel::mpsc::Receiver<StatusChangeStream>;

pub async fn run_link_status_updater(
    observable: Arc<Observable<Vec<LinkStatus>>>,
    receiver: LinkStateReceiver,
) -> Result<(), Error> {
    let mut link_status = HashMap::new();
    let mut receiver = receiver.composite();
    while let Some((node_link_id, node_id, duration)) = receiver.next().await {
        log::trace!("link_status_updater: Publish {:?} {:?} {:?}", node_link_id, node_id, duration);
        if let Some(duration) = duration {
            link_status.insert(node_link_id, (node_id, duration));
        } else {
            link_status.remove(&node_link_id);
        }
        // TODO: batch these every few hundred milliseconds
        observable
            .push(
                link_status
                    .iter()
                    .map(|(node_link_id, (node_id, round_trip_time))| LinkStatus {
                        to: *node_id,
                        local_id: *node_link_id,
                        round_trip_time: *round_trip_time,
                    })
                    .collect(),
            )
            .await;
    }
    Ok(())
}
