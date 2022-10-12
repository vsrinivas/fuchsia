// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        events::{event::Event, registry::ComponentEventRoute},
        hooks::{
            Event as ComponentEvent, EventError, EventErrorPayload, EventPayload, TransferEvent,
        },
    },
    ::routing::event::EventFilter,
    anyhow::Error,
    cm_rust::{DictionaryValue, EventMode},
    futures::{
        channel::{mpsc, oneshot},
        lock::Mutex,
        sink::SinkExt,
    },
    maplit::hashmap,
    moniker::{AbsoluteMonikerBase, ExtendedMoniker},
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
    // The moniker of the component subscribing to events.
    subscriber: ExtendedMoniker,

    /// Determines whether component manager waits for a response from the
    /// event receiver.
    mode: EventMode,

    /// Specifies the realms that this EventDispatcher can dispatch events from and under what
    /// conditions.
    scopes: Vec<EventDispatcherScope>,

    /// An `mpsc::Sender` used to dispatch an event. Note that this
    /// `mpsc::Sender` is wrapped in an Mutex<..> to allow it to be passed along
    /// to other tasks for dispatch. The Event is a lifecycle event that occurred,
    /// and the Option<Vec<ComponentEventRoute>> is the path that the event
    /// took (if applicable) to reach the destination. This route
    /// is used for dynamic permission checks (to filter events a component shouldn't have
    /// access to), and to rebase the moniker of the event.
    tx: Mutex<mpsc::UnboundedSender<(Event, Option<Vec<ComponentEventRoute>>)>>,

    /// Route information used externally for evaluating scopes
    pub route: Vec<ComponentEventRoute>,
}

impl EventDispatcher {
    pub fn new(
        subscriber: ExtendedMoniker,
        mode: EventMode,
        scopes: Vec<EventDispatcherScope>,
        tx: mpsc::UnboundedSender<(Event, Option<Vec<ComponentEventRoute>>)>,
    ) -> Self {
        Self::new_with_route(subscriber, mode, scopes, tx, vec![])
    }

    pub fn new_with_route(
        subscriber: ExtendedMoniker,
        mode: EventMode,
        scopes: Vec<EventDispatcherScope>,
        tx: mpsc::UnboundedSender<(Event, Option<Vec<ComponentEventRoute>>)>,
        route: Vec<ComponentEventRoute>,
    ) -> Self {
        // TODO(fxbug.dev/48360): flatten scope_monikers. There might be monikers that are
        // contained within another moniker in the list.
        Self { subscriber, mode, scopes, tx: Mutex::new(tx), route }
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

        let (maybe_responder_tx, maybe_responder_rx) = match self.mode {
            EventMode::Async => (None, None),
            EventMode::Sync => {
                let (responder_tx, responder_rx) = oneshot::channel();
                (Some(responder_tx), Some(responder_rx))
            }
        };

        {
            let mut tx = self.tx.lock().await;
            tx.send((
                Event {
                    event: event.transfer().await,
                    scope_moniker,
                    responder: maybe_responder_tx,
                },
                None,
            ))
            .await?;
        }
        Ok(maybe_responder_rx)
    }

    fn find_scope(&self, event: &ComponentEvent) -> Option<&EventDispatcherScope> {
        // TODO(fxbug.dev/48360): once flattening of monikers is done, we would expect to have a single
        // moniker here. For now taking the first one and ignoring the rest.
        // Ensure that the event is coming from a realm within the scope of this dispatcher and
        // matching the path filter if one exists.
        self.scopes.iter().filter(|scope| scope.contains(&self.subscriber, &event)).next()
    }
}

/// A scope for dispatching and filters on that scope.
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct EventDispatcherScope {
    /// The moniker of the realm
    pub moniker: ExtendedMoniker,

    /// Filters for an event in that realm.
    pub filter: EventFilter,
}

impl EventDispatcherScope {
    pub fn new(moniker: ExtendedMoniker) -> Self {
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

    /// Given the subscriber, indicates whether or not the event is contained
    /// in this scope.
    pub fn contains(&self, subscriber: &ExtendedMoniker, event: &ComponentEvent) -> bool {
        let in_scope = match &event.result {
            Ok(EventPayload::CapabilityRequested { source_moniker, .. })
            | Err(EventError {
                event_error_payload: EventErrorPayload::CapabilityRequested { source_moniker, .. },
                ..
            }) => match &subscriber {
                ExtendedMoniker::ComponentManager => true,
                ExtendedMoniker::ComponentInstance(target) => *source_moniker == *target,
            },
            // CapabilityRouted events are never dispatched
            Ok(EventPayload::CapabilityRouted { .. })
            | Err(EventError {
                event_error_payload: EventErrorPayload::CapabilityRouted { .. },
                ..
            }) => false,
            _ => {
                let contained_in_realm = self.moniker.contains_in_realm(&event.target_moniker);
                let is_component_instance = matches!(
                    &event.target_moniker,
                    ExtendedMoniker::ComponentInstance(instance) if instance.is_root()
                );
                contained_in_realm || is_component_instance
            }
        };

        if !in_scope {
            return false;
        }

        // TODO(fsamuel): Creating hashmaps on every lookup is not ideal, but in practice this
        // likely doesn't happen too often.
        let filterable_fields = match &event.result {
            Ok(EventPayload::CapabilityRequested { name, .. }) => Some(hashmap! {
                "name".to_string() => DictionaryValue::Str(name.into())
            }),
            Ok(EventPayload::DirectoryReady { name, .. }) => Some(hashmap! {
                "name".to_string() => DictionaryValue::Str(name.into())
            }),
            Err(EventError {
                event_error_payload: EventErrorPayload::CapabilityRequested { name, .. },
                ..
            }) => Some(hashmap! {
                "name".to_string() => DictionaryValue::Str(name.into())
            }),
            Err(EventError {
                event_error_payload: EventErrorPayload::DirectoryReady { name, .. },
                ..
            }) => Some(hashmap! {
                "name".to_string() => DictionaryValue::Str(name.into())
            }),
            _ => None,
        };
        self.filter.has_fields(&filterable_fields)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::capability::CapabilitySource,
        ::routing::capability_source::InternalCapability,
        assert_matches::assert_matches,
        fuchsia_zircon as zx,
        futures::StreamExt,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
        std::{
            convert::TryInto,
            sync::{Arc, Weak},
        },
    };

    struct EventDispatcherFactory {
        /// The receiving end of a channel of Events.
        rx: mpsc::UnboundedReceiver<(Event, Option<Vec<ComponentEventRoute>>)>,

        /// The sending end of a channel of Events.
        tx: mpsc::UnboundedSender<(Event, Option<Vec<ComponentEventRoute>>)>,
    }

    impl EventDispatcherFactory {
        fn new() -> Self {
            let (tx, rx) = mpsc::unbounded();
            Self { rx, tx }
        }

        /// Receives the next event from the sender.
        pub async fn next_event(&mut self) -> Option<ComponentEvent> {
            self.rx.next().await.map(|(e, _)| e.event)
        }

        fn create_dispatcher(
            &self,
            subscriber: ExtendedMoniker,
            mode: EventMode,
        ) -> Arc<EventDispatcher> {
            let scopes =
                vec![EventDispatcherScope::new(AbsoluteMoniker::root().into()).for_debug()];
            Arc::new(EventDispatcher::new(subscriber, mode, scopes, self.tx.clone()))
        }
    }

    async fn dispatch_capability_requested_event(
        dispatcher: &EventDispatcher,
        source_moniker: &AbsoluteMoniker,
    ) -> Option<oneshot::Receiver<()>> {
        let (_, capability_server_end) = zx::Channel::create().unwrap();
        let capability_server_end = Arc::new(Mutex::new(Some(capability_server_end)));
        let event = ComponentEvent::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root/a/b/c",
            Ok(EventPayload::CapabilityRequested {
                source_moniker: source_moniker.clone(),
                name: "foo".to_string(),
                capability: capability_server_end,
            }),
        );
        dispatcher.dispatch(&event).await.ok().flatten()
    }

    async fn dispatch_capability_routed_event(
        dispatcher: &EventDispatcher,
    ) -> Option<oneshot::Receiver<()>> {
        let empty_capability_provider = Arc::new(Mutex::new(None));
        let event = ComponentEvent::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root/a/b/c",
            Ok(EventPayload::CapabilityRouted {
                source: CapabilitySource::Builtin {
                    capability: InternalCapability::Protocol(
                        "fuchsia.sys2.MyAwesomeProtocol".try_into().unwrap(),
                    ),
                    top_instance: Weak::new(),
                },
                capability_provider: empty_capability_provider,
            }),
        );
        dispatcher.dispatch(&event).await.ok().flatten()
    }

    // This test verifies that the CapabilityRequested event can only be sent to a source
    // that matches its source moniker.
    #[fuchsia::test]
    async fn can_send_capability_requested_to_source() {
        // Verify we can dispatch to a debug source.
        // Sync events get a responder if the message was dispatched.
        let mut factory = EventDispatcherFactory::new();
        let dispatcher =
            factory.create_dispatcher(ExtendedMoniker::ComponentManager, EventMode::Async);
        let source_moniker = vec!["root:0", "a:0", "b:0", "c:0"].into();
        assert!(dispatch_capability_requested_event(&dispatcher, &source_moniker).await.is_none());
        assert_matches!(
            factory.next_event().await,
            Some(ComponentEvent { result: Ok(EventPayload::CapabilityRequested { .. }), .. })
        );

        // Verify that we cannot dispatch the CapabilityRequested event to the root component.
        let subscriber = ExtendedMoniker::ComponentInstance(vec!["root:0"].into());
        let dispatcher = factory.create_dispatcher(subscriber, EventMode::Sync);
        assert!(dispatch_capability_requested_event(&dispatcher, &source_moniker).await.is_none());

        // Verify that we cannot dispatch the CapabilityRequested event to the root:0/a:0 component.
        let subscriber = ExtendedMoniker::ComponentInstance(vec!["root:0", "a:0"].into());
        let dispatcher = factory.create_dispatcher(subscriber, EventMode::Sync);
        assert!(dispatch_capability_requested_event(&dispatcher, &source_moniker).await.is_none());

        // Verify that we cannot dispatch the CapabilityRequested event to the root:0/a:0/b:0 component.
        let subscriber = ExtendedMoniker::ComponentInstance(vec!["root:0", "a:0", "b:0"].into());
        let dispatcher = factory.create_dispatcher(subscriber, EventMode::Sync);
        assert!(dispatch_capability_requested_event(&dispatcher, &source_moniker).await.is_none());

        // Verify that we CAN dispatch the CapabilityRequested event to the root:0/a:0/b:0/c:0 component.
        let subscriber =
            ExtendedMoniker::ComponentInstance(vec!["root:0", "a:0", "b:0", "c:0"].into());
        let dispatcher = factory.create_dispatcher(subscriber, EventMode::Sync);
        assert!(dispatch_capability_requested_event(&dispatcher, &source_moniker).await.is_some());
        assert_matches!(
            factory.next_event().await,
            Some(ComponentEvent { result: Ok(EventPayload::CapabilityRequested { .. }), .. })
        );

        // Verify that we cannot dispatch the CapabilityRequested event to the root:0/a:0/b:0/c:0/d:0 component.
        let subscriber =
            ExtendedMoniker::ComponentInstance(vec!["root:0", "a:0", "b:0", "c:0", "d:0"].into());
        let dispatcher = factory.create_dispatcher(subscriber, EventMode::Sync);
        assert!(dispatch_capability_requested_event(&dispatcher, &source_moniker).await.is_none());
    }

    // This test verifies that the CapabilityRouted event can never be sent.
    #[fuchsia::test]
    async fn cannot_send_capability_routed() {
        let factory = EventDispatcherFactory::new();

        // Verify that we cannot dispatch the CapabilityRouted event for above-root subscribers.
        let dispatcher =
            factory.create_dispatcher(ExtendedMoniker::ComponentManager, EventMode::Sync);
        // This indicates that the event was not dispatched.
        assert!(dispatch_capability_routed_event(&dispatcher).await.is_none());

        // Verify that we cannot dispatch the CapabilityRouted event for in-tree subscribers.
        let subscriber = ExtendedMoniker::ComponentInstance(vec!["root:0"].into());
        let dispatcher = factory.create_dispatcher(subscriber, EventMode::Sync);
        // This indicates that the event was not dispatched.
        assert!(dispatch_capability_routed_event(&dispatcher).await.is_none());
    }
}
