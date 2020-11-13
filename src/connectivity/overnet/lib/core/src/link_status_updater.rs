// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    future_help::{Observable, Observer, PollMutex},
    labels::{NodeId, NodeLinkId},
    routes::LinkMetrics,
};
use anyhow::Error;
use fuchsia_async::{Task, Timer};
use futures::{future::poll_fn, lock::Mutex, prelude::*, ready};
use std::{
    collections::{BTreeMap, HashMap},
    sync::Arc,
    task::{Poll, Waker},
    time::Duration,
};

pub(crate) type LinkStatePublisher =
    futures::channel::mpsc::Sender<(NodeLinkId, NodeId, Observer<Option<Duration>>)>;
pub(crate) type LinkStateReceiver =
    futures::channel::mpsc::Receiver<(NodeLinkId, NodeId, Observer<Option<Duration>>)>;

pub(crate) async fn run_link_status_updater(
    my_node_id: NodeId,
    observable: Observable<BTreeMap<NodeId, LinkMetrics>>,
    mut receiver: LinkStateReceiver,
) -> Result<(), Error> {
    struct RecvState {
        incoming: Vec<(NodeLinkId, NodeId, Observer<Option<Duration>>)>,
        waker: Option<Waker>,
    }
    let recv_state = Arc::new(Mutex::new(RecvState { incoming: Vec::new(), waker: None }));
    let publisher_recv_state = recv_state.clone();
    let _publisher = Task::spawn(async move {
        let mut poll_mutex = PollMutex::new(&publisher_recv_state);
        let mut link_status = HashMap::<NodeLinkId, (NodeId, Duration)>::new();
        loop {
            poll_fn(|ctx| {
                let mut updated = false;
                let mut recv_state = ready!(poll_mutex.poll(ctx));
                let mut i = 0;
                while i != recv_state.incoming.len() {
                    let (node_link_id, node_id, stream) = &mut recv_state.incoming[i];
                    loop {
                        match stream.poll_next_unpin(ctx) {
                            Poll::Pending => {
                                i += 1;
                                break;
                            }
                            Poll::Ready(Some(Some(duration))) => {
                                updated = true;
                                link_status.insert(*node_link_id, (*node_id, duration));
                            }
                            Poll::Ready(Some(None)) => (),
                            Poll::Ready(None) => {
                                updated = true;
                                link_status.remove(node_link_id);
                                recv_state.incoming.remove(i);
                                break;
                            }
                        }
                    }
                }
                if updated {
                    Poll::Ready(())
                } else {
                    recv_state.waker = Some(ctx.waker().clone());
                    Poll::Pending
                }
            })
            .await;
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
            log::trace!("[{:?}] new status is {:?}", my_node_id, new_status);
            observable.push(new_status).await;
            Timer::new(Duration::from_millis(300)).await;
        }
    });
    while let Some(incoming) = receiver.next().await {
        let mut recv_state = recv_state.lock().await;
        recv_state.incoming.push(incoming);
        recv_state.waker.take().map(|w| w.wake());
    }
    Ok(())
}
