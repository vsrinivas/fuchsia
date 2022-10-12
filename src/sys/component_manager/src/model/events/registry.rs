// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        model::{
            component::{ComponentInstance, InstanceState},
            error::ModelError,
            events::{
                dispatcher::{EventDispatcher, EventDispatcherScope},
                error::EventsError,
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
    ::routing::{
        capability_source::InternalCapability,
        component_instance::{ComponentInstanceInterface, ResolvedInstanceInterface},
        event::EventFilter,
        route_capability, route_event_stream_capability,
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, EventMode, UseDecl, UseEventDecl, UseEventStreamDecl},
    futures::lock::Mutex,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase, ExtendedMoniker},
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
    pub route: Vec<ComponentEventRoute>,
}

#[derive(Debug)]
pub struct RequestedEventState {
    pub mode: EventMode,
    pub scopes: Vec<EventDispatcherScope>,
    pub route: Vec<ComponentEventRoute>,
}

impl RequestedEventState {
    pub fn new(mode: EventMode, route: Vec<ComponentEventRoute>) -> Self {
        Self { mode, scopes: Vec::new(), route }
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
        route: Vec<ComponentEventRoute>,
    ) {
        let event_state =
            self.mapping.entry(source_name).or_insert(RequestedEventState::new(mode, route));
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
                route: state.route,
            })
            .collect()
    }
}

fn event_name_remap(value: &str) -> &str {
    value.trim_end_matches("_v2")
}

/// Subscribes to events from multiple tasks and sends events to all of them.
pub struct EventRegistry {
    model: Weak<Model>,
    dispatcher_map: Arc<Mutex<HashMap<CapabilityName, Vec<Weak<EventDispatcher>>>>>,
    event_synthesizer: EventSynthesizer,
}

/// Contains routing information about an event.
/// This is used to downscope the moniker for the event
/// and filter events to only allowed components.
#[derive(Debug, Clone)]
pub struct ComponentEventRoute {
    /// Component child name string. We don't yet
    /// know a stronger type during routing, and the type of
    /// the object could change during runtime in the case of dynamic
    /// collections. Examples of things this could be:
    /// * <component_manager> -- refers to component manager if AboveRoot
    /// filtering is performed.
    /// * The name of a component relative to its parent
    /// * The name of a collection relative to its parent
    pub component: String,
    /// A list of scopes that this route applies to
    pub scope: Option<Vec<String>>,
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
    pub async fn subscribe_v2(
        &self,
        subscriber: &ExtendedMoniker,
        subscriptions: Vec<EventSubscription>,
    ) -> Result<EventStream, ModelError> {
        // Register event capabilities if any. It identifies the sources of these events (might be
        // the parent or this component itself). It constructs an "allow-list tree" of events and
        // component instances.
        let mut event_names = HashMap::new();
        for subscription in subscriptions {
            if event_names.insert(subscription.event_name.clone(), subscription.mode).is_some() {
                return Err(EventsError::duplicate_event(subscription.event_name).into());
            }
        }

        let events = match &subscriber {
            ExtendedMoniker::ComponentManager => event_names
                .iter()
                .map(|(source_name, mode)| RoutedEvent {
                    source_name: source_name.clone(),
                    mode: mode.clone(),
                    scopes: vec![
                        EventDispatcherScope::new(AbsoluteMoniker::root().into()).for_debug()
                    ],
                    route: vec![],
                })
                .collect(),
            ExtendedMoniker::ComponentInstance(target_moniker) => {
                let route_result = self.route_events_v2(&target_moniker, &event_names).await?;
                // Each target name that we routed, will have an associated scope. The number of
                // scopes must be equal to the number of target names.
                let total_scopes: usize =
                    route_result.mapping.values().map(|state| state.scopes.len()).sum();
                if total_scopes != event_names.len() {
                    let names = event_names
                        .keys()
                        .filter(|event_name| !route_result.contains_event(&event_name))
                        .cloned()
                        .collect();
                    return Err(EventsError::not_available(names).into());
                }
                route_result.to_vec()
            }
        };

        self.subscribe_with_routed_events(&subscriber, events).await
    }

    /// Subscribes to events of a provided set of EventTypes.
    pub async fn subscribe(
        &self,
        subscriber: &ExtendedMoniker,
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
        let events = match &subscriber {
            ExtendedMoniker::ComponentManager => event_names
                .iter()
                .map(|(source_name, mode)| RoutedEvent {
                    source_name: source_name.clone(),
                    mode: mode.clone(),
                    scopes: vec![
                        EventDispatcherScope::new(AbsoluteMoniker::root().into()).for_debug()
                    ],
                    route: vec![],
                })
                .collect(),
            ExtendedMoniker::ComponentInstance(target_moniker) => {
                let route_result = self.route_events(target_moniker, &event_names).await?;
                // Each target name that we routed, will have an associated scope. The number of
                // scopes must be equal to the number of target names.
                let total_scopes: usize =
                    route_result.mapping.values().map(|state| state.scopes.len()).sum();
                if total_scopes != event_names.len() {
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

        self.subscribe_with_routed_events(&subscriber, events).await
    }

    pub async fn subscribe_with_routed_events(
        &self,
        subscriber: &ExtendedMoniker,
        mut events: Vec<RoutedEvent>,
    ) -> Result<EventStream, ModelError> {
        // TODO(fxbug.dev/48510): get rid of this channel and use FIDL directly.
        let mut event_stream = EventStream::new();

        let mut dispatcher_map = self.dispatcher_map.lock().await;
        for event in &mut events {
            event.source_name = CapabilityName::from(event_name_remap(event.source_name.str()));
            if EventType::synthesized_only()
                .iter()
                .all(|e| e.to_string() != event.source_name.str())
            {
                // NOTE: This is for ALL events other than Running.
                let dispatchers = dispatcher_map.entry(event.source_name.clone()).or_insert(vec![]);
                let dispatcher = event_stream.create_dispatcher(
                    subscriber.clone(),
                    event.mode.clone(),
                    event.scopes.clone(),
                    event.route.clone(),
                );
                dispatchers.push(dispatcher);
            }
        }
        // In the case of v2 events, this function will be called
        // once per event. We need to preserve the routing information from
        // that event in order to determine which events should and shouldn't
        // be routed to the listener.
        if events.len() == 1 {
            event_stream.route = events[0].route.clone();
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

        for dispatcher in &dispatchers {
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
                        responder_channel.await.unwrap_or(()); // Ignore cancellation.
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
            futures::future::join_all(responder_channels).await;
        }

        Ok(())
    }

    /// Routes a list of events to a specified moniker.
    /// Returns the event_stream capabilities.
    pub async fn route_events_v2(
        &self,
        target_moniker: &AbsoluteMoniker,
        events: &HashMap<CapabilityName, EventMode>,
    ) -> Result<RouteEventsResult, ModelError> {
        let model = self.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
        let component = model.look_up(&target_moniker).await?;
        let decl = {
            let state = component.lock_state().await;
            match *state {
                InstanceState::New | InstanceState::Unresolved => {
                    // This should never happen. By this point,
                    // we've validated that the instance state should
                    // be resolved because we're routing events to it.
                    // If not, this is an internal error from which we can't recover,
                    // and indicates a bug in component manager.
                    unreachable!("route_events_v2: not resolved");
                }
                InstanceState::Resolved(ref s) => s.decl().clone(),
                InstanceState::Destroyed => {
                    return Err(ModelError::instance_not_found(target_moniker.clone()));
                }
            }
        };

        let mut result = RouteEventsResult::new();
        for use_decl in decl.uses {
            match use_decl {
                UseDecl::EventStream(event_decl) => {
                    if let Some(mode) = events.get(&event_decl.source_name) {
                        let (source_name, scope_moniker, route) =
                            Self::route_single_event_v2(event_decl.clone(), &component).await?;
                        let mut scope = EventDispatcherScope::new(scope_moniker);
                        if let Some(filter) = event_decl.filter {
                            scope = scope.with_filter(EventFilter::new(Some(filter)));
                        }
                        result.insert(source_name, mode.clone(), scope, route);
                    }
                }
                _ => {}
            }
        }

        Ok(result)
    }

    pub async fn route_events(
        &self,
        target_moniker: &AbsoluteMoniker,
        events: &HashMap<CapabilityName, EventMode>,
    ) -> Result<RouteEventsResult, ModelError> {
        let model = self.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
        let component = model.look_up(&target_moniker).await?;
        let decl = {
            let state = component.lock_state().await;
            match *state {
                InstanceState::New | InstanceState::Unresolved => {
                    unreachable!("route_events: not resolved");
                }
                InstanceState::Resolved(ref s) => s.decl().clone(),
                InstanceState::Destroyed => {
                    return Err(ModelError::instance_not_found(target_moniker.clone()));
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
                            .with_filter(EventFilter::new(event_decl.filter));
                        result.insert(source_name, mode.clone(), scope, vec![]);
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
        let (route_source, _route) =
            route_capability(RouteRequest::UseEvent(event_decl), component).await?;
        match route_source {
            RouteSource::Event(CapabilitySource::Framework {
                capability: InternalCapability::Event(source_name),
                component,
            }) => Ok((source_name, component.abs_moniker.into())),
            RouteSource::Event(CapabilitySource::Builtin {
                capability: InternalCapability::Event(source_name),
                ..
            }) if source_name == "directory_ready" => {
                Ok((source_name, ExtendedMoniker::ComponentManager))
            }
            _ => unreachable!(),
        }
    }

    /// Routes an event and returns its source name and scope on success.
    async fn route_single_event_v2(
        event_decl: UseEventStreamDecl,
        component: &Arc<ComponentInstance>,
    ) -> Result<(CapabilityName, ExtendedMoniker, Vec<ComponentEventRoute>), ModelError> {
        let mut components = vec![];
        let mut route = vec![];
        let (route_source, _route) =
            route_event_stream_capability(event_decl.clone(), component, &mut components).await?;
        let mut search_name: CapabilityName = event_decl.source_name;
        // Handle scope in "use" clause
        if let Some(moniker) = component.child_moniker() {
            route.push(ComponentEventRoute {
                component: moniker.to_string(),
                scope: event_decl.scope.map_or(None, |scopes| {
                    Some(
                        scopes
                            .iter()
                            .map(|s| match s {
                                cm_rust::EventScope::Child(child) => child.name.to_string(),
                                cm_rust::EventScope::Collection(collection) => {
                                    collection.to_string()
                                }
                            })
                            .collect(),
                    )
                }),
            });
        }
        for component in components {
            let locked_state = component.lock_resolved_state().await?;
            let offers = locked_state.offers();
            let exposes = locked_state.exposes();
            let mut component_route = ComponentEventRoute {
                component: if let Some(moniker) = component.child_moniker() {
                    moniker.name().to_string()
                } else {
                    "<root>".to_string()
                },
                scope: None,
            };

            for offer in offers {
                match offer {
                    cm_rust::OfferDecl::EventStream(stream) => {
                        // Found match, continue up tree.
                        if stream.target_name == search_name {
                            search_name = stream.source_name;
                            if let Some(scopes) = stream.scope {
                                component_route.scope = Some(
                                    scopes
                                        .iter()
                                        .map(|s| match s {
                                            cm_rust::EventScope::Child(child) => {
                                                child.name.to_string()
                                            }
                                            cm_rust::EventScope::Collection(collection) => {
                                                collection.to_string()
                                            }
                                        })
                                        .collect(),
                                );
                            }
                        }
                    }
                    _ => {}
                }
            }

            for expose in exposes {
                // Found match, continue up tree.
                match expose {
                    cm_rust::ExposeDecl::EventStream(stream) => {
                        if stream.target_name == search_name {
                            search_name = stream.source_name;
                        }
                    }
                    _ => {}
                }
            }
            route.push(component_route);
        }
        match route_source {
            RouteSource::EventStream(CapabilitySource::Framework {
                capability: InternalCapability::EventStream(source_name),
                component,
            }) => Ok((source_name, component.abs_moniker.into(), route)),
            RouteSource::EventStream(CapabilitySource::Builtin {
                capability: InternalCapability::EventStream(source_name),
                ..
            }) => Ok((source_name, ExtendedMoniker::ComponentManager, route)),
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
        crate::model::{
            hooks::{Event as ComponentEvent, EventError, EventErrorPayload, EventPayload},
            testing::test_helpers::{TestModelResult, *},
        },
        ::routing::error::ComponentInstanceError,
        assert_matches::assert_matches,
        cm_rust::{
            Availability, ChildDecl, ComponentDecl, DependencyType, DictionaryValue, OfferDecl,
            OfferEventDecl, OfferSource, OfferTarget, UseDecl, UseEventDecl, UseSource,
        },
        fidl_fuchsia_component_decl as fdecl, fuchsia_zircon as zx,
        futures::StreamExt,
        maplit::hashmap,
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
            Ok(EventPayload::Discovered { instance_id: 0 }),
        );
        registry.dispatch(&event).await
    }

    async fn dispatch_error_event(registry: &EventRegistry) -> Result<(), ModelError> {
        let root = AbsoluteMoniker::root();
        let event = ComponentEvent::new_for_test(
            root.clone(),
            "fuchsia-pkg://root",
            Err(EventError::new(
                &ModelError::instance_not_found(root.clone()),
                EventErrorPayload::Resolved,
            )),
        );
        registry.dispatch(&event).await
    }

    #[fuchsia::test]
    async fn drop_dispatcher_when_event_stream_dropped() {
        let TestModelResult { model, .. } = TestEnvironmentBuilder::new().build().await;
        let event_registry = EventRegistry::new(Arc::downgrade(&model));

        assert_eq!(0, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        let mut event_stream_a = event_registry
            .subscribe(
                &ExtendedMoniker::ComponentManager,
                vec![EventSubscription::new(EventType::Discovered.into(), EventMode::Async)],
            )
            .await
            .expect("subscribe succeeds");

        assert_eq!(1, event_registry.dispatchers_per_event_type(EventType::Discovered).await);

        let mut event_stream_b = event_registry
            .subscribe(
                &ExtendedMoniker::ComponentManager,
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
                &ExtendedMoniker::ComponentManager,
                vec![EventSubscription::new(EventType::Resolved.into(), EventMode::Async)],
            )
            .await
            .expect("subscribed to event stream");

        assert_eq!(1, event_registry.dispatchers_per_event_type(EventType::Resolved).await);

        dispatch_error_event(&event_registry).await.unwrap();

        let event = event_stream.next().await.map(|(event, _)| event.event).unwrap();

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

        let mut event_stream_a = event_registry
            .subscribe(
                &ExtendedMoniker::ComponentManager,
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
                &ExtendedMoniker::ComponentManager,
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

        let event_a = event_stream_a.next().await.map(|(event, _)| event.event).unwrap();

        // Verify that we received a valid CapabilityRequested event.
        assert_matches!(event_a.result, Ok(EventPayload::CapabilityRequested { .. }));

        let event_b = event_stream_b.next().await.map(|(event, _)| event.event).unwrap();

        // Verify that we received the event error.
        assert_matches!(
            event_b.result,
            Err(EventError {
                event_error_payload: EventErrorPayload::CapabilityRequested { .. },
                ..
            })
        );
    }

    #[fuchsia::test]
    async fn subscribe_to_same_event_different_scope() {
        let TestModelResult { model, .. } = TestEnvironmentBuilder::new()
            .set_components(vec![
                (
                    "root",
                    ComponentDecl {
                        children: vec![ChildDecl {
                            name: "foo".to_string(),
                            url: "test:///foo".to_string(),
                            startup: fdecl::StartupMode::Lazy,
                            on_terminate: None,
                            environment: None,
                        }],
                        offers: vec![
                            OfferDecl::Event(OfferEventDecl {
                                source: OfferSource::Framework,
                                source_name: "capability_requested".into(),
                                target: OfferTarget::static_child("foo".to_string()),
                                target_name: "foo_requested".into(),
                                filter: Some(hashmap! {
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.foo.Foo".to_string())
                                }),
                                availability: Availability::Required,
                            }),
                            OfferDecl::Event(OfferEventDecl {
                                source: OfferSource::Framework,
                                source_name: "capability_requested".into(),
                                target: OfferTarget::static_child("foo".to_string()),
                                target_name: "bar_requested".into(),
                                filter: Some(hashmap! {
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.bar.Bar".to_string())
                                }),
                                availability: Availability::Required,
                            }),
                        ],
                        ..Default::default()
                    },
                ),
                (
                    "foo",
                    ComponentDecl {
                        uses: vec![
                            UseDecl::Event(UseEventDecl {
                                source: UseSource::Parent,
                                source_name: "foo_requested".into(),
                                target_name: "foo_requested".into(),
                                filter: Some(hashmap! {
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.foo.Foo".to_string())
                                }),
                                dependency_type: DependencyType::Strong,
                                availability: Availability::Required,
                            }),
                            UseDecl::Event(UseEventDecl {
                                source: UseSource::Framework,
                                source_name: "bar_requested".into(),
                                target_name: "bar_requested".into(),
                                filter: Some(hashmap! {
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.bar.Bar".to_string())
                                }),
                                dependency_type: DependencyType::Strong,
                                availability: Availability::Required,
                            }),
                        ],
                        ..Default::default()
                    },
                ),
            ])
            .build()
            .await;
        let event_registry = EventRegistry::new(Arc::downgrade(&model));

        assert_eq!(
            0,
            event_registry.dispatchers_per_event_type(EventType::CapabilityRequested).await
        );

        let subscriber =
            ExtendedMoniker::ComponentInstance(AbsoluteMoniker::parse_str("/foo").unwrap());

        let _event_stream = event_registry
            .subscribe(
                &subscriber,
                vec![
                    EventSubscription::new("bar_requested".into(), EventMode::Async),
                    EventSubscription::new("foo_requested".into(), EventMode::Async),
                ],
            )
            .await
            .expect("can subscribe");

        assert_eq!(
            1,
            event_registry.dispatchers_per_event_type(EventType::CapabilityRequested).await
        );
    }
}
