// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The special-purpose event loop used by the reachability monitor.
//!
//! This event loop receives events from netstack. Thsose events are used by the reachability
//! monitor to infer the connectivity state.

use crate::worker::EventWorker;
use failure::{bail, Error};
use futures::channel::mpsc;
use futures::prelude::*;
use reachability_core::Monitor;

/// The events that can trigger an action in the event loop.
#[derive(Debug)]
pub enum Event {
    /// An event coming from fuchsia.net.stack.
    StackEvent(fidl_fuchsia_net_stack::StackEvent),
    /// An event coming from fuchsia.netstack.
    NetstackEvent(fidl_fuchsia_netstack::NetstackEvent),
}

/// The event loop.
pub struct EventLoop {
    event_recv: mpsc::UnboundedReceiver<Event>,
    monitor: Monitor,
}

impl EventLoop {
    /// `new` returns an `EventLoop` instance.
    pub fn new() -> Self {
        let (event_send, event_recv) = futures::channel::mpsc::unbounded::<Event>();

        let mut monitor = Monitor::new();
        let streams = monitor.take_event_streams();
        let event_worker = EventWorker;
        event_worker.spawn(streams, event_send.clone());
        EventLoop { event_recv, monitor }
    }

    /// `run` starts the event loop.
    pub async fn run(mut self) -> Result<(), Error> {
        info!("collecting initial state.");
        self.monitor.populate_state().await?;

        info!("starting event loop");
        loop {
            match self.event_recv.next().await {
                Some(Event::StackEvent(event)) => self.handle_stack_event(event).await,
                Some(Event::NetstackEvent(event)) => self.handle_netstack_event(event).await,
                None => bail!("Stream of events ended unexpectedly"),
            }
        }
    }

    async fn handle_stack_event(&mut self, event: fidl_fuchsia_net_stack::StackEvent) {
        info!("stack event received {:#?}", event);
        self.monitor
            .stack_event(event)
            .await
            .unwrap_or_else(|err| error!("error updating state: {:?}", err));
    }

    async fn handle_netstack_event(&mut self, event: fidl_fuchsia_netstack::NetstackEvent) {
        info!("netstack event received {:#?}", event);
        self.monitor
            .netstack_event(event)
            .await
            .unwrap_or_else(|err| error!("error updating state: {:?}", err));
    }
}
