// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilitySource, InternalCapability},
        model::{
            component::{ComponentInstance, InstanceState},
            error::ModelError,
            events::{
                dispatcher::{EventDispatcher, EventDispatcherScope},
                error::EventsError,
                filter::EventFilter,
                mode_set::EventModeSet,
                stream::EventStream,
                synthesizer::{EventSynthesisProvider, EventSynthesizer},
            },
            hooks::{
                Event as ComponentEvent, EventPayload, EventType, HasEventType, Hook,
                HooksRegistration,
            },
            model::Model,
            routing::{RouteRequest, RouteSource},
        },
    },
    ::routing::route_capability,
    async_trait::async_trait,
    cm_rust::{CapabilityName, EventMode, UseDecl, UseEventDecl},
    fuchsia_trace as trace,
    futures::lock::Mutex,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ExtendedMoniker},
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
    },
};

// TODO(https://fxbug.dev/61861): remove alias once the routing lib has a stable API.
pub type EventSubscription = ::routing::event::EventSubscription;

#[derive(Debug)]
pub struct RoutedEvent {
    pub source_name: CapabilityName,
    pub mode: EventMode,
    pub scopes: Vec<EventDispatcherScope>,
}

#[derive(Debug)]
pub struct RequestedEventState {
    pub mode: EventMode,
    pub scopes: Vec<EventDispatcherScope>,
}

impl RequestedEventState {
    pub fn new(mode: EventMode) -> Self {
        Self { mode, scopes: Vec::new() }
    }
}

#[derive(Debug)]
pub struct RouteEventsResult {
    /// Maps from source name to a mode and set of scope monikers.
    mapping: HashMap<CapabilityName, RequestedEventState>,
}

impl RouteEventsResult {
    fn new() -> Self {
        Self { mapping: HashMap::new() }
    }

    fn insert(
        &mut self,
        source_name: CapabilityName,
        mode: EventMode,
        scope: EventDispatcherScope,
    ) {
        let event_state = self.mapping.entry(source_name).or_insert(RequestedEventState::new(mode));
        if !event_state.scopes.contains(&scope) {
            event_state.scopes.push(scope);
        }
    }

    pub fn len(&self) -> usize {
        self.mapping.len()
    }

    pub fn contains_event(&self, event_name: &CapabilityName) -> bool {
        self.mapping.contains_key(event_name)
    }

    pub fn to_vec(self) -> Vec<RoutedEvent> {
        self.mapping
            .into_iter()
            .map(|(source_name, state)| RoutedEvent {
                source_name,
                mode: state.mode,
                scopes: state.scopes,
            })
            .collect()
    }
}

#[derive(Clone)]
pub struct SubscriptionOptions {
    /// Determines how event routing is done.
    pub subscription_type: SubscriptionType,
    /// Specifies the mode ComponentManager was started in.
    pub execution_mode: ExecutionMode,
}

impl SubscriptionOptions {
    pub fn new(subscription_type: SubscriptionType, execution_mode: ExecutionMode) -> Self {
        Self { subscription_type, execution_mode }
    }
}

impl Default for SubscriptionOptions {
    fn default() -> SubscriptionOptions {
        SubscriptionOptions {
            subscription_type: SubscriptionType::Component(AbsoluteMoniker::root()),
            execution_mode: ExecutionMode::Production,
        }
    }
}

#[derive(Clone, PartialEq, Eq)]
pub enum SubscriptionType {
    /// Indicates that a client above the root is subscribing to events (e.g. a test).
    /// Event routing will be bypassed and all events can be subscribed.
    AboveRoot,
    /// Indicates that a component is subscribing to events and the target is
    /// the provided AbsoluteMoniker.
    Component(AbsoluteMoniker),
}

#[derive(Clone)]
pub enum ExecutionMode {
    /// Indicates that the component manager is running in Debug mode. This
    /// enables some additional events such as CapabilityRouted.
    Debug,
    /// Indicates that the component manager is running in Production mode.
    Production,
}

impl ExecutionMode {
    pub fn is_debug(&self) -> bool {
        match self {
            ExecutionMode::Debug => true,
            ExecutionMode::Production => false,
        }
    }
}

/// Subscribes to events from multiple tasks and sends events to all of them.
pub struct EventRegistry {
    model: Weak<Model>,
    dispatcher_map: Arc<Mutex<HashMap<CapabilityName, Vec<Weak<EventDispatcher>>>>>,
    event_synthesizer: EventSynthesizer,
}

impl EventRegistry {
    pub fn new(model: Weak<Model>) -> Self {
        let event_synthesizer = EventSynthesizer::new(model.clone());
        Self { model, dispatcher_map: Arc::new(Mutex::new(HashMap::new())), event_synthesizer }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![
            // This hook must be registered with all events.
            // However, a task will only receive events to which it subscribed.
            HooksRegistration::new(
                "EventRegistry",
                EventType::values(),
                Arc::downgrade(self) as Weak<dyn Hook>,
            ),
        ]
    }

    /// Register a provider for an synthesized event.
    pub fn register_synthesis_provider(
        &mut self,
        event: EventType,
        provider: Arc<dyn EventSynthesisProvider>,
    ) {
        self.event_synthesizer.register_provider(event, provider);
    }

    /// Subscribes to events of a provided set of EventTypes.
    pub async fn subscribe(
        &self,
        options: &SubscriptionOptions,
        subscriptions: Vec<EventSubscription>,
    ) -> Result<EventStream, ModelError> {
        // Register event capabilities if any. It identifies the sources of these events (might be
        // the parent or this component itself). It consturcts an "allow-list tree" of events and
        // component instances.
        let mut event_names = HashMap::new();
        for subscription in subscriptions {
            if event_names
                .insert(subscription.event_name.clone(), subscription.mode.clone())
                .is_some()
            {
                return Err(EventsError::duplicate_event(subscription.event_name).into());
            }
        }
        let events = match &options.subscription_type {
            SubscriptionType::AboveRoot => event_names
                .iter()
                .map(|(source_name, mode)| RoutedEvent {
                    source_name: source_name.clone(),
                    mode: mode.clone(),
                    scopes: vec![
                        EventDispatcherScope::new(AbsoluteMoniker::root().into()).for_debug()
                    ],
                })
                .collect(),
            SubscriptionType::Component(target_moniker) => {
                let route_result = self.route_events(&target_moniker, &event_names).await?;
                if route_result.len() != event_names.len() {
                    let names = event_names
                        .keys()
                        .into_iter()
                        .filter(|event_name| !route_result.contains_event(&event_name))
                        .cloned()
                        .collect();
                    return Err(EventsError::not_available(names).into());
                }
                route_result.to_vec()
            }
        };

        self.subscribe_with_routed_events(&options, events).await
    }

    pub async fn subscribe_with_routed_events(
        &self,
        options: &SubscriptionOptions,
        events: Vec<RoutedEvent>,
    ) -> Result<EventStream, ModelError> {
        // TODO(fxbug.dev/48510): get rid of this channel and use FIDL directly.
        let mut event_stream = EventStream::new();

        let mut dispatcher_map = self.dispatcher_map.lock().await;
        for event in &events {
            if EventType::synthesized_only()
                .iter()
                .all(|e| e.to_string() != event.source_name.str())
            {
                let dispatchers = dispatcher_map.entry(event.source_name.clone()).or_insert(vec![]);
                let dispatcher = event_stream.create_dispatcher(
                    options.clone(),
                    event.mode.clone(),
                    event.scopes.clone(),
                );
                dispatchers.push(dispatcher);
            }
        }

        let events = events.into_iter().map(|event| (event.source_name, event.scopes)).collect();
        self.event_synthesizer.spawn_synthesis(event_stream.sender(), events);

        Ok(event_stream)
    }

    // TODO(fxbug.dev/48510): get rid of this
    /// Sends the event to all dispatchers and waits to be unblocked by all
    async fn dispatch(&self, event: &ComponentEvent) -> Result<(), ModelError> {
        // Copy the senders so we don't hold onto the sender map lock
        // If we didn't do this, it is possible to deadlock while holding onto this lock.
        // For example,
        // Task A : call dispatch(event1) -> lock on sender map -> send -> wait for responders
        // Task B : call dispatch(event2) -> lock on sender map
        // If task B was required to respond to event1, then this is a deadlock.
        // Neither task can make progress.
        let dispatchers = {
            let mut dispatcher_map = self.dispatcher_map.lock().await;
            if let Some(dispatchers) = dispatcher_map.get_mut(&event.event_type().into()) {
                let mut strong_dispatchers = vec![];
                dispatchers.retain(|dispatcher| {
                    if let Some(dispatcher) = dispatcher.upgrade() {
                        strong_dispatchers.push(dispatcher);
                        true
                    } else {
                        false
                    }
                });
                strong_dispatchers
            } else {
                // There were no senders for this event. Do nothing.
                return Ok(());
            }
        };

        let mut responder_channels = vec![];
        for dispatcher in dispatchers {
            let result = dispatcher.dispatch(event).await;
            match result {
                Ok(Some(responder_channel)) => {
                    // A future can be canceled if the EventStream was dropped after
                    // a send. We don't crash the system when this happens. It is
                    // perfectly valid for a EventStream to be dropped. That simply
                    // means that the EventStream is no longer interested in future
                    // events. So we force each future to return a success. This
                    // ensures that all the futures can be driven to completion.
                    let responder_channel = async move {
                        trace::duration!("component_manager", "events:wait_for_resume");
                        let _ = responder_channel.await;
                        trace::flow_end!("component_manager", "event", event.id);
                    };
                    responder_channels.push(responder_channel);
                }
                // There's nothing to do if event is outside the scope of the given
                // `EventDispatcher`.
                Ok(None) => (),
                Err(_) => {
                    // A send can fail if the EventStream was dropped. We don't
                    // crash the system when this happens. It is perfectly
                    // valid for a EventStream to be dropped. That simply means
                    // that the EventStream is no longer interested in future
                    // events.
                }
            }
        }

        // Wait until all tasks have used the responder to unblock.
        {
            trace::duration!("component_manager", "events:wait_for_all_resume");
            futures::future::join_all(responder_channels).await;
        }

        Ok(())
    }

    pub async fn route_events(
        &self,
        target_moniker: &AbsoluteMoniker,
        events: &HashMap<CapabilityName, EventMode>,
    ) -> Result<RouteEventsResult, ModelError> {
        let model = self.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
        let component = model.look_up(&target_moniker.to_partial()).await?;
        let decl = {
            let state = component.lock_state().await;
            match *state {
                InstanceState::New | InstanceState::Discovered => {
                    panic!("route_events: not resolved");
                }
                InstanceState::Resolved(ref s) => s.decl().clone(),
                InstanceState::Purged => {
                    return Err(ModelError::instance_not_found(target_moniker.to_partial()));
                }
            }
        };

        let mut result = RouteEventsResult::new();
        for use_decl in decl.uses {
            match use_decl {
                UseDecl::Event(event_decl) => {
                    if let Some(mode) = events.get(&event_decl.target_name) {
                        let (source_name, scope_moniker) =
                            Self::route_event(event_decl.clone(), &component).await?;
                        let scope = EventDispatcherScope::new(scope_moniker)
                            .with_filter(EventFilter::new(event_decl.filter))
                            .with_mode_set(EventModeSet::new(event_decl.mode));
                        if scope.mode_set.supports_mode(mode) {
                            result.insert(source_name, mode.clone(), scope);
                        }
                    }
                }
                _ => {}
            }
        }

        Ok(result)
    }

    /// Routes an event and returns its source name and scope on success.
    async fn route_event(
        event_decl: UseEventDecl,
        component: &Arc<ComponentInstance>,
    ) -> Result<(CapabilityName, ExtendedMoniker), ModelError> {
        let route_source = route_capability(RouteRequest::UseEvent(event_decl), component).await?;
        match route_source {
            RouteSource::Event(CapabilitySource::Framework {
                capability: InternalCapability::Event(source_name),
                component,
            }) => Ok((source_name, component.moniker.into())),
            RouteSource::Event(CapabilitySource::Builtin {
                capability: InternalCapability::Event(source_name),
                ..
            }) if source_name == "directory_ready" => {
                Ok((source_name, ExtendedMoniker::ComponentManager))
            }
            _ => unreachable!(),
        }
    }

    #[cfg(test)]
    async fn dispatchers_per_event_type(&self, event_type: EventType) -> usize {
        let dispatcher_map = self.dispatcher_map.lock().await;
        dispatcher_map
            .get(&event_type.into())
            .map(|dispatchers| dispatchers.len())
            .unwrap_or_default()
    }
}

#[async_trait]
impl Hook for EventRegistry {
    async fn on(self: Arc<Self>, event: &ComponentEvent) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::CapabilityRouted { source, .. }) => {
                // Only dispatch the CapabilityRouted event for capabilities
                // that can be in a component's namespace.
                // TODO(fxbug.dev/54251): In the future, if we wish to be able to mock or
                // interpose runners, we can introduce, a new, separate event
                // type.
                if source.can_be_in_namespace() {
                    return self.dispatch(event).await;
                }
                return Ok(());
            }
            _ => self.dispatch(event).await,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            capability::ComponentCapability,
            model::{
                component::ComponentInstance,
                environment::Environment,
                events::event::Event,
                hooks::{Event as ComponentEvent, EventError, EventErrorPayload, EventPayload},
                testing::test_helpers::{TestModelResult, *},
            },
        },
        ::routing::{
            component_instance::ComponentInstanceInterface, error::ComponentInstanceError,
        },
        cm_rust::ProtocolDecl,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        matches::assert_matches,
        moniker::AbsoluteMoniker,
    };

    async fn dispatch_capability_requested_event(
        registry: &EventRegistry,
    ) -> Result<(), ModelError> {
        let (_, capability_server_end) = zx::Channel::create().unwrap();
        let capability_server_end = Arc::new(Mutex::new(Some(capability_server_end)));
        let event = ComponentEvent::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRequested {
                source_moniker: AbsoluteMoniker::root(),
                name: "foo".to_string(),
                capability: capability_server_end,
            }),
        );
        registry.dispatch(&event).await
    }

    async fn dispatch_fake_event(registry: &EventRegistry) -> Result<(), ModelError> {
        let event = ComponentEvent::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::Discovered),
        );
        registry.dispatch(&event).await
    }

    async fn dispatch_error_event(registry: &EventRegistry) -> Result<(), ModelError> {
        let root = AbsoluteMoniker::root();
        let event = ComponentEvent::new_for_test(
            root.clone(),
            "fuchsia-pkg://root",
            Err(EventError::new(
                &ModelError::instance_not_found(root.to_partial()),
                EventErrorPayload::Resolved,
            )),
        );
        registry.dispatch(&event).await
    }

    #[fuchsia::test]
    async fn capability_routed_dispatch() -> Result<(), ModelError> {
        let TestModelResult { model, .. } = TestEnvironmentBuilder::new().build().await;
        let registry = EventRegistry::new(Arc::downgrade(&model));
        let mut event_stream = registry
            .subscribe(
                &SubscriptionOptions::new(SubscriptionType::AboveRoot, ExecutionMode::Debug),
                vec![EventSubscription::new(EventType::CapabilityRouted.into(), EventMode::Sync)],
            )
            .await
            .expect("subscribe succeeds");
        assert_eq!(1, registry.dispatchers_per_event_type(EventType::CapabilityRouted).await);

        let component = ComponentInstance::new_root(
            Environment::empty(),
            Weak::new(),
            Weak::new(),
            "test:///root".to_string(),
        );
        let capability = ComponentCapability::Protocol(ProtocolDecl {
            name: "foo".into(),
            source_path: "/svc/foo".parse().unwrap(),
        });
        let source = CapabilitySource::Component {
            capability: capability.clone(),
            component: component.as_weak(),
        };
        let capability_provider = Arc::new(Mutex::new(None));
        let event = ComponentEvent::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRouted { source: source.clone(), capability_provider }),
        );
        fasync::Task::spawn(async move {
            registry.dispatch(&event).await.expect("failed dispatch");
        })
        .detach();

        let event = event_stream.next().await.expect("null event");
        assert_matches!(event, Event {
            event: ComponentEvent {
                result: Ok(EventPayload::CapabilityRouted {
                    source: CapabilitySource::Component {
                        capability, ..
                    },
                    ..
                }),
                ..
            },
            scope_moniker,
            ..
        } if capability == capability && scope_moniker == AbsoluteMoniker::root().into());

        Ok(())
    }

    #[fuchsia::test]
    async fn drop_dispatcher_when_event_stream_dropped() {
        let TestModelResult { model, .. } = TestEnvironmentBuilder::new().build().await;
        let event_registry = EventRegistry::new(Arc::downgrade(&model));

        assert_eq!(0, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        let mut event_stream_a = event_registry
            .subscribe(
                &SubscriptionOptions::new(SubscriptionType::AboveRoot, ExecutionMode::Production),
                vec![EventSubscription::new(EventType::Discovered.into(), EventMode::Async)],
            )
            .await
            .expect("subscribe succeeds");

        assert_eq!(1, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        let mut event_stream_b = event_registry
            .subscribe(
                &SubscriptionOptions::new(SubscriptionType::AboveRoot, ExecutionMode::Production),
                vec![EventSubscription::new(EventType::Discovered.into(), EventMode::Async)],
            )
            .await
            .expect("subscribe succeeds");

        assert_eq!(2, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        dispatch_fake_event(&event_registry).await.unwrap();

        // Verify that both EventStreams receive the event.
        assert!(event_stream_a.next().await.is_some());
        assert!(event_stream_b.next().await.is_some());
        assert_eq!(2, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        drop(event_stream_a);

        // EventRegistry won't drop EventDispatchers until an event is dispatched.
        assert_eq!(2, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        dispatch_fake_event(&event_registry).await.unwrap();

        assert!(event_stream_b.next().await.is_some());
        assert_eq!(1, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        drop(event_stream_b);

        dispatch_fake_event(&event_registry).await.unwrap();
        assert_eq!(0, event_registry.dispatchers_per_event_type(EventType::Discovered).await);
    }

    #[fuchsia::test]
    async fn event_error_dispatch() {
        let TestModelResult { model, .. } = TestEnvironmentBuilder::new().build().await;
        let event_registry = EventRegistry::new(Arc::downgrade(&model));

        assert_eq!(0, event_registry.dispatchers_per_event_type(EventType::Resolved).await);

        let mut event_stream = event_registry
            .subscribe(
                &SubscriptionOptions::new(SubscriptionType::AboveRoot, ExecutionMode::Production),
                vec![EventSubscription::new(EventType::Resolved.into(), EventMode::Async)],
            )
            .await
            .expect("subscribed to event stream");

        assert_eq!(1, event_registry.dispatchers_per_event_type(EventType::Resolved).await);

        dispatch_error_event(&event_registry).await.unwrap();

        let event = event_stream.next().await.map(|e| e.event).unwrap();

        // Verify that we received the event error.
        assert_matches!(
            event.result,
            Err(EventError {
                source: ModelError::ComponentInstanceError {
                    err: ComponentInstanceError::InstanceNotFound { .. }
                },
                event_error_payload: EventErrorPayload::Resolved,
            })
        );
    }

    #[fuchsia::test]
    async fn capability_requested_over_two_event_streams() {
        let TestModelResult { model, .. } = TestEnvironmentBuilder::new().build().await;
        let event_registry = EventRegistry::new(Arc::downgrade(&model));

        assert_eq!(
            0,
            event_registry.dispatchers_per_event_type(EventType::CapabilityRequested).await
        );

        let options =
            SubscriptionOptions::new(SubscriptionType::AboveRoot, ExecutionMode::Production);

        let mut event_stream_a = event_registry
            .subscribe(
                &options,
                vec![EventSubscription::new(
                    EventType::CapabilityRequested.into(),
                    EventMode::Async,
                )],
            )
            .await
            .expect("subscribe succeeds");

        assert_eq!(
            1,
            event_registry.dispatchers_per_event_type(EventType::CapabilityRequested).await
        );

        let mut event_stream_b = event_registry
            .subscribe(
                &options,
                vec![EventSubscription::new(
                    EventType::CapabilityRequested.into(),
                    EventMode::Async,
                )],
            )
            .await
            .expect("subscribe succeeds");

        assert_eq!(
            2,
            event_registry.dispatchers_per_event_type(EventType::CapabilityRequested).await
        );

        dispatch_capability_requested_event(&event_registry).await.unwrap();

        let event_a = event_stream_a.next().await.map(|e| e.event).unwrap();

        // Verify that we received a valid CapabilityRequested event.
        assert_matches!(event_a.result, Ok(EventPayload::CapabilityRequested { .. }));

        let event_b = event_stream_b.next().await.map(|e| e.event).unwrap();

        // Verify that we received the event error.
        assert_matches!(
            event_b.result,
            Err(EventError {
                event_error_payload: EventErrorPayload::CapabilityRequested { .. },
                ..
            })
        );
    }
}
