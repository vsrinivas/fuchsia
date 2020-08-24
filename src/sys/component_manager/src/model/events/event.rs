// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{hooks::Event as ComponentEvent, moniker::AbsoluteMoniker},
    fuchsia_trace as trace,
    futures::channel::oneshot,
};

#[derive(PartialEq, Clone)]
pub enum SyncMode {
    Sync,
    Async,
}

/// Created for a particular component event.
/// Contains the Event that occurred along with a means to resume/unblock the component manager.
#[must_use = "invoke resume() otherwise component manager will be halted indefinitely!"]
#[derive(Debug)]
pub struct Event {
    /// The event itself.
    pub event: ComponentEvent,

    /// The scope where this event comes from. This can be seen as a superset of the
    /// `event.target_moniker` itself given that the events might have been offered from an
    /// ancestor realm.
    pub scope_moniker: AbsoluteMoniker,

    /// This Sender is used to unblock the component manager if available.
    /// If a Sender is unspecified then that indicates that this event is asynchronous and
    /// non-blocking.
    pub responder: Option<oneshot::Sender<()>>,
}

impl Event {
    pub fn sync_mode(&self) -> SyncMode {
        if self.responder.is_none() {
            SyncMode::Async
        } else {
            SyncMode::Sync
        }
    }

    pub fn resume(self) {
        trace::duration!("component_manager", "events:resume");
        trace::flow_step!("component_manager", "event", self.event.id);
        if let Some(responder) = self.responder {
            // If this returns an error, there isn't much we can do, presumably
            // the caller hoping to resume the component manager has other
            // means to understand component manager didn't resume.
            let _ = responder.send(());
        }
    }
}
