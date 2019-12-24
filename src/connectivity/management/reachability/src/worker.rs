// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::eventloop::Event,
    anyhow::Error,
    fidl_fuchsia_net_stack as stack, fidl_fuchsia_netstack as netstack, fuchsia_async as fasync,
    futures::{channel::mpsc, StreamExt, TryFutureExt},
};

/// `EventWorker` waits for events from netstack and stack and sends them on the indicated
/// `event_chan`.
pub struct EventWorker;

impl EventWorker {
    pub fn spawn(
        self,
        streams: (stack::StackEventStream, netstack::NetstackEventStream),
        event_chan: mpsc::UnboundedSender<Event>,
    ) {
        fasync::spawn_local(
            async move {
                let mut select_stream = futures::stream::select(
                    streams.0.map(|e| e.map(Event::StackEvent)),
                    streams.1.map(|e| e.map(Event::NetstackEvent)),
                );

                while let Some(e) = select_stream.next().await {
                    debug!("Sending event: {:?}", e);
                    match e {
                        Ok(e) => event_chan.unbounded_send(e)?,
                        Err(e) => error!("Fidl event error: {}", e),
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|err: Error| error!("Sending event error {:?}", err)),
        );
    }
}

/// `TimerWorker` waits for timer events and sends them on the indicated
/// `event_chan`.
pub struct TimerWorker;

impl TimerWorker {
    pub fn spawn(
        self,
        mut timer: fasync::Interval,
        event_chan: mpsc::UnboundedSender<Event>,
        id: u64,
    ) {
        debug!("spawn periodic timer");
        fasync::spawn_local(async move {
            while let Some(()) = (timer.next()).await {
                event_chan.unbounded_send(Event::TimerEvent(id)).unwrap_or_else(
                    |err: mpsc::TrySendError<Event>| error!("Sending event error {:?}", err),
                );
            }
        });
    }
}
