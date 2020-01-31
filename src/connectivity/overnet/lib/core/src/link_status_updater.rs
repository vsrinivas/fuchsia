// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::{log_errors, Observable},
    labels::{NodeId, NodeLinkId},
    link::LinkStatus,
    runtime::spawn,
};
use anyhow::{bail, format_err};
use futures::{prelude::*, select, stream::SelectAll};
use std::{collections::HashMap, pin::Pin, rc::Rc, time::Duration};

pub type StatusChangeStream = Pin<Box<dyn Stream<Item = (NodeLinkId, NodeId, Option<Duration>)>>>;
pub type LinkStatePublisher = futures::channel::mpsc::Sender<StatusChangeStream>;
type LinkStateReceiver = futures::channel::mpsc::Receiver<StatusChangeStream>;

pub fn spawn_link_status_updater(
    observable: Rc<Observable<Vec<LinkStatus>>>,
    mut receiver: LinkStateReceiver,
) {
    let mut link_status = HashMap::new();
    spawn(log_errors(
        async move {
            let mut select_all = SelectAll::new();
            loop {
                enum Action {
                    Publish(Option<(NodeLinkId, NodeId, Option<Duration>)>),
                    Register(Option<StatusChangeStream>),
                };
                let action = select! {
                    x = select_all.next().fuse() => Action::Publish(x),
                    x = receiver.next().fuse() => Action::Register(x),
                };
                match action {
                    Action::Publish(Some((node_link_id, node_id, duration))) => {
                        if let Some(duration) = duration {
                            link_status.insert(node_link_id, (node_id, duration));
                        } else {
                            link_status.remove(&node_link_id);
                        }
                        // TODO: batch these every few hundred milliseconds
                        observable.push(
                            link_status
                                .iter()
                                .map(|(node_link_id, (node_id, round_trip_time))| LinkStatus {
                                    to: *node_id,
                                    local_id: *node_link_id,
                                    round_trip_time: *round_trip_time,
                                })
                                .collect(),
                        );
                    }
                    Action::Publish(None) => {
                        // Wait for new links
                        select_all.push(
                            receiver
                                .next()
                                .await
                                .ok_or_else(|| format_err!("Registration channel is gone"))?,
                        );
                    }
                    Action::Register(Some(s)) => {
                        select_all.push(s);
                    }
                    Action::Register(None) => bail!("Registration channel is gone"),
                }
            }
        },
        "Failed propagating link status",
    ));
}
