// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        events::{
            dispatcher::{EventDispatcher, ScopeMetadata},
            event::{Event, SyncMode},
        },
        hooks::{EventType, HasEventType},
        moniker::AbsoluteMoniker,
    },
    fuchsia_trace as trace,
    futures::{channel::mpsc, StreamExt},
    std::sync::{Arc, Weak},
};

pub struct EventStream {
    /// The receiving end of a channel of Events.
    rx: mpsc::Receiver<Event>,
    /// The sending end of a channel of Events.
    tx: mpsc::Sender<Event>,
    /// A vector of EventDispatchers to this EventStream.
    /// EventStream assumes ownership of the dispatchers. They are
    /// destroyed when this EventStream is destroyed.
    dispatchers: Vec<Arc<EventDispatcher>>,
}

impl EventStream {
    pub fn new() -> Self {
        let (tx, rx) = mpsc::channel(2);
        Self { rx, tx, dispatchers: vec![] }
    }

    pub fn create_dispatcher(
        &mut self,
        sync_mode: SyncMode,
        scopes: Vec<ScopeMetadata>,
    ) -> Weak<EventDispatcher> {
        let dispatcher = Arc::new(EventDispatcher::new(sync_mode.clone(), scopes, self.tx.clone()));
        self.dispatchers.push(dispatcher.clone());
        Arc::downgrade(&dispatcher)
    }

    /// Receives the next event from the sender.
    pub async fn next(&mut self) -> Option<Event> {
        trace::duration!("component_manager", "events:next");
        self.rx.next().await
    }

    /// Waits for an event with a particular EventType against a component with a
    /// particular moniker. Ignores all other events.
    pub async fn wait_until(
        &mut self,
        expected_event_type: EventType,
        expected_moniker: AbsoluteMoniker,
    ) -> Option<Event> {
        while let Some(event) = self.next().await {
            let actual_event_type = event.event.event_type();
            if expected_moniker == event.event.target_moniker
                && expected_event_type == actual_event_type
            {
                return Some(event);
            }
            event.resume();
        }
        None
    }
}
