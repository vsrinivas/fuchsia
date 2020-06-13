// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        events::{
            event::{Event, SyncMode},
            filter::EventFilter,
            registry::{SubscriptionOptions, SubscriptionType},
        },
        hooks::{
            Event as ComponentEvent, EventError, EventErrorPayload, EventPayload, HasEventType,
            TransferEvent,
        },
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
    /// The options used to generate this EventDispatcher.
    options: SubscriptionOptions,

    /// Specifies the realms that this EventDispatcher can dispatch events from and under what
    /// conditions.
    scopes: Vec<ScopeMetadata>,

    /// An `mpsc::Sender` used to dispatch an event. Note that this
    /// `mpsc::Sender` is wrapped in an Mutex<..> to allow it to be passed along
    /// to other tasks for dispatch.
    tx: Mutex<mpsc::UnboundedSender<Event>>,
}

impl EventDispatcher {
    pub fn new(
        options: SubscriptionOptions,
        scopes: Vec<ScopeMetadata>,
        tx: mpsc::UnboundedSender<Event>,
    ) -> Self {
        // TODO(fxb/48360): flatten scope_monikers. There might be monikers that are
        // contained within another moniker in the list.
        Self { options, scopes, tx: Mutex::new(tx) }
    }

    /// Sends the event to an event stream, if fired in the scope of `scope_moniker`. Returns
    /// a responder which can be blocked on.
    pub async fn dispatch(
        &self,
        event: &ComponentEvent,
    ) -> Result<Option<oneshot::Receiver<()>>, Error> {
        let maybe_scope = self.find_scope(&event);
        if maybe_scope.is_none() {
            return Ok(None);
        }

        let scope_moniker = maybe_scope.unwrap().moniker.clone();

        trace::duration!("component_manager", "events:send");
        let event_type = format!("{:?}", event.event_type());
        let target_moniker = event.target_moniker.to_string();
        trace::flow_begin!(
            "component_manager",
            "event",
            event.id,
            "event_type" => event_type.as_str(),
            "target_moniker" => target_moniker.as_str()
        );

        let (maybe_responder_tx, maybe_responder_rx) = match self.options.sync_mode {
            SyncMode::Async => (None, None),
            SyncMode::Sync => {
                let (responder_tx, responder_rx) = oneshot::channel();
                (Some(responder_tx), Some(responder_rx))
            }
        };

        {
            let mut tx = self.tx.lock().await;
            tx.send(Event {
                event: event.transfer().await,
                scope_moniker,
                responder: maybe_responder_tx,
            })
            .await?;
        }
        Ok(maybe_responder_rx)
    }

    /// Indicates whether this event can be dispatched to the realm specified by `target`.
    fn can_send_to_target(&self, event: &ComponentEvent) -> bool {
        match event.result {
            Ok(EventPayload::CapabilityRequested { .. })
            | Err(EventError {
                event_error_payload: EventErrorPayload::CapabilityRequested { .. },
                ..
            }) => match &self.options.subscription_type {
                SubscriptionType::Debug => true,
                SubscriptionType::Normal(target) => event.target_moniker == *target,
            },
            _ => true,
        }
    }

    fn find_scope(&self, event: &ComponentEvent) -> Option<&ScopeMetadata> {
        let filterable_fields = match &event.result {
            Ok(EventPayload::CapabilityRequested { path, .. }) => Some(hashmap! {
                "path".to_string() => DictionaryValue::Str(path.into())
            }),
            Ok(EventPayload::CapabilityReady { path, .. }) => Some(hashmap! {
                "path".to_string() => DictionaryValue::Str(path.into())
            }),
            Err(EventError {
                event_error_payload: EventErrorPayload::CapabilityRequested { path, .. },
                ..
            }) => Some(hashmap! {
                "path".to_string() => DictionaryValue::Str(path.into())
            }),
            Err(EventError {
                event_error_payload: EventErrorPayload::CapabilityReady { path, .. },
                ..
            }) => Some(hashmap! {
                "path".to_string() => DictionaryValue::Str(path.into())
            }),
            _ => None,
        };

        if !self.can_send_to_target(&event) {
            return None;
        }

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
#[derive(Debug, Clone, Eq, PartialEq)]
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

#[cfg(test)]
mod tests {
    use {
        super::*, fuchsia_zircon as zx, futures::StreamExt, matches::assert_matches, std::sync::Arc,
    };

    struct EventDispatcherFactory {
        /// The receiving end of a channel of Events.
        rx: mpsc::UnboundedReceiver<Event>,

        /// The sending end of a channel of Events.
        tx: mpsc::UnboundedSender<Event>,
    }

    impl EventDispatcherFactory {
        fn new() -> Self {
            let (tx, rx) = mpsc::unbounded();
            Self { rx, tx }
        }

        /// Receives the next event from the sender.
        pub async fn next_event(&mut self) -> Option<ComponentEvent> {
            self.rx.next().await.map(|e| e.event)
        }

        fn create_dispatcher(&self, options: SubscriptionOptions) -> Arc<EventDispatcher> {
            let scopes = vec![ScopeMetadata::new(AbsoluteMoniker::root()).for_debug()];
            Arc::new(EventDispatcher::new(options, scopes, self.tx.clone()))
        }
    }

    async fn dispatch_capability_requested_event(
        dispatcher: &EventDispatcher,
        target_moniker: &AbsoluteMoniker,
    ) -> Option<oneshot::Receiver<()>> {
        let (_, capability_server_end) = zx::Channel::create().unwrap();
        let capability_server_end = Arc::new(Mutex::new(Some(capability_server_end)));
        let event = ComponentEvent::new_for_test(
            target_moniker.clone(),
            "fuchsia-pkg://root/a/b/c",
            Ok(EventPayload::CapabilityRequested {
                path: "/svc/foo".to_string(),
                capability: capability_server_end,
            }),
        );
        dispatcher.dispatch(&event).await.ok().flatten()
    }

    // This test verifies that the CapabilityRequested event can only be sent to a target
    // that matches its target moniker.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn can_send_capability_requested_to_target() {
        // Verify we can dispatch to a debug target.
        // Sync events get a responder if the message was dispatched.
        let options = SubscriptionOptions::new(SubscriptionType::Debug, SyncMode::Async);
        let mut factory = EventDispatcherFactory::new();
        let dispatcher = factory.create_dispatcher(options);
        let target_moniker = vec!["root:0", "a:0", "b:0", "c:0"].into();
        assert!(dispatch_capability_requested_event(&dispatcher, &target_moniker).await.is_none());
        assert_matches!(
            factory.next_event().await,
            Some(ComponentEvent { result: Ok(EventPayload::CapabilityRequested { .. }), .. })
        );

        // Verify that we cannot dispatch the CapabilityRequested event to the root component.
        let options = SubscriptionOptions::new(
            SubscriptionType::Normal(vec!["root:0"].into()),
            SyncMode::Sync,
        );
        let dispatcher = factory.create_dispatcher(options);
        assert!(dispatch_capability_requested_event(&dispatcher, &target_moniker).await.is_none());

        // Verify that we cannot dispatch the CapabilityRequested event to the root:0/a:0 component.
        let options = SubscriptionOptions::new(
            SubscriptionType::Normal(vec!["root:0", "a:0"].into()),
            SyncMode::Sync,
        );
        let dispatcher = factory.create_dispatcher(options);
        assert!(dispatch_capability_requested_event(&dispatcher, &target_moniker).await.is_none());

        // Verify that we cannot dispatch the CapabilityRequested event to the root:0/a:0/b:0 component.
        let options = SubscriptionOptions::new(
            SubscriptionType::Normal(vec!["root:0", "a:0", "b:0"].into()),
            SyncMode::Sync,
        );
        let dispatcher = factory.create_dispatcher(options);
        assert!(dispatch_capability_requested_event(&dispatcher, &target_moniker).await.is_none());

        // Verify that we CAN dispatch the CapabilityRequested event to the root:0/a:0/b:0/c:0 component.
        let options = SubscriptionOptions::new(
            SubscriptionType::Normal(vec!["root:0", "a:0", "b:0", "c:0"].into()),
            SyncMode::Sync,
        );
        let dispatcher = factory.create_dispatcher(options);
        assert!(dispatch_capability_requested_event(&dispatcher, &target_moniker).await.is_some());
        assert_matches!(
            factory.next_event().await,
            Some(ComponentEvent { result: Ok(EventPayload::CapabilityRequested { .. }), .. })
        );

        // Verify that we cannot dispatch the CapabilityRequested event to the root:0/a:0/b:0/c:0/d:0 component.
        let options = SubscriptionOptions::new(
            SubscriptionType::Normal(vec!["root:0", "a:0", "b:0", "c:0", "d:0"].into()),
            SyncMode::Sync,
        );
        let dispatcher = factory.create_dispatcher(options);
        assert!(dispatch_capability_requested_event(&dispatcher, &target_moniker).await.is_none());
    }
}
