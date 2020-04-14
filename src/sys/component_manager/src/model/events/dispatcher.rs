// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        events::{
            event::{Event, SyncMode},
            filter::EventFilter,
        },
        hooks::{Event as ComponentEvent, EventPayload},
        moniker::AbsoluteMoniker,
    },
    anyhow::Error,
    cm_rust::DictionaryValue,
    fuchsia_trace as trace,
    futures::{
        channel::{mpsc, oneshot},
        lock::Mutex,
        sink::SinkExt,
    },
    maplit::hashmap,
};

/// EventDispatcher and EventStream are two ends of a channel.
///
/// EventDispatcher represents the sending end of the channel.
///
/// An EventDispatcher receives events of a particular event type,
/// and dispatches though events out to the EventStream if they fall within
/// one of the scopes associated with the dispatcher.
///
/// EventDispatchers are owned by EventStreams. If an EventStream is dropped,
/// all corresponding EventDispatchers are dropped.
///
/// An EventStream is owned by the client - usually a test harness or a
/// EventSource. It receives a Event from an EventDispatcher and propagates it
/// to the client.
pub struct EventDispatcher {
    /// Whether or not this EventDispatcher dispatches events asynchronously.
    sync_mode: SyncMode,

    /// Specifies the realms that this EventDispatcher can dispatch events from and under what
    /// conditions.
    scopes: Vec<ScopeMetadata>,

    /// An `mpsc::Sender` used to dispatch an event. Note that this
    /// `mpsc::Sender` is wrapped in an Mutex<..> to allow it to be passed along
    /// to other tasks for dispatch.
    tx: Mutex<mpsc::Sender<Event>>,
}

impl EventDispatcher {
    pub fn new(sync_mode: SyncMode, scopes: Vec<ScopeMetadata>, tx: mpsc::Sender<Event>) -> Self {
        // TODO(fxb/48360): flatten scope_monikers. There might be monikers that are
        // contained within another moniker in the list.
        Self { sync_mode, scopes, tx: Mutex::new(tx) }
    }

    /// Sends the event to an event stream, if fired in the scope of `scope_moniker`. Returns
    /// a responder which can be blocked on.
    pub async fn dispatch(
        &self,
        event: ComponentEvent,
    ) -> Result<Option<oneshot::Receiver<()>>, Error> {
        let maybe_scope = self.find_scope(&event);
        if maybe_scope.is_none() {
            return Ok(None);
        }

        let scope_moniker = maybe_scope.unwrap().moniker.clone();

        trace::duration!("component_manager", "events:send");
        let event_type = format!("{:?}", event.payload.type_());
        let target_moniker = event.target_moniker.to_string();
        trace::flow_begin!(
            "component_manager",
            "event",
            event.id,
            "event_type" => event_type.as_str(),
            "target_moniker" => target_moniker.as_str()
        );
        let (maybe_responder_tx, maybe_responder_rx) = if self.sync_mode == SyncMode::Async {
            (None, None)
        } else {
            let (responder_tx, responder_rx) = oneshot::channel();
            (Some(responder_tx), Some(responder_rx))
        };
        {
            let mut tx = self.tx.lock().await;
            tx.send(Event { event, scope_moniker, responder: maybe_responder_tx }).await?;
        }
        Ok(maybe_responder_rx)
    }

    fn find_scope(&self, event: &ComponentEvent) -> Option<&ScopeMetadata> {
        let filterable_fields = match &event.payload {
            EventPayload::CapabilityReady { path, .. } => Some(hashmap! {
                "path".to_string() => DictionaryValue::Str(path.into())
            }),
            _ => None,
        };

        // TODO(fxb/48360): once flattening of monikers is done, we would expect to have a single
        // moniker here. For now taking the first one and ignoring the rest.
        // Ensure that the event is coming from a realm within the scope of this dispatcher and
        // matching the path filter if one exists.
        self.scopes
            .iter()
            .filter(|scope| {
                let filter_matches = scope.filter.has_fields(&filterable_fields);
                filter_matches && scope.moniker.contains_in_realm(&event.target_moniker)
            })
            .next()
    }
}

/// A scope for dispatching and filters on that scope.
#[derive(Debug, Eq, PartialEq)]
pub struct ScopeMetadata {
    /// The moniker of the realm
    pub moniker: AbsoluteMoniker,

    /// Filters for an event in that realm.
    pub filter: EventFilter,
}

impl ScopeMetadata {
    pub fn new(moniker: AbsoluteMoniker) -> Self {
        Self { moniker, filter: EventFilter::new(None) }
    }

    pub fn with_filter(mut self, filter: EventFilter) -> Self {
        self.filter = filter;
        self
    }

    /// For the top-level EventStreams and event strems used in unit tests in the c_m codebase we
    /// don't take filters into account.
    pub fn for_debug(mut self) -> Self {
        self.filter = EventFilter::debug();
        self
    }
}
