// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        events::{
            error::EventsError,
            registry::{EventRegistry, EventSubscription},
            serve::serve_event_stream,
            stream::EventStream,
        },
        hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, ComponentDecl, EventMode, UseDecl, UseEventStreamDeprecatedDecl},
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::lock::Mutex,
    moniker::{AbsoluteMoniker, ExtendedMoniker},
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
    },
};

pub struct EventStreamAttachment {
    /// The name of this event stream.
    name: String,
    /// The server end of a component's event stream.
    server_end: Option<ServerEnd<fsys::EventStreamMarker>>,
    /// The task serving the event stream and using the client_end
    /// associated with the above server_end
    _task: fasync::Task<()>,
}

/// V2 variant of EventStreamAttachment
/// contains the event stream and its name.
pub struct EventStreamAttachmentV2 {
    /// The name of this event stream.
    name: String,
    /// The server end of a component's event stream.
    server_end: Option<EventStream>,
}

/// An absolute path to a directory within a specified component.
#[derive(Eq, Hash, PartialEq, Clone)]
struct AbsolutePath {
    /// The path where the event stream will be installed
    /// in target_moniker.
    path: String,
    /// The absolute path to the component that this path refers to.
    target_moniker: ExtendedMoniker,
}

/// Mutable event stream state, guarded by a mutex in the
/// EventStreamProvider which allows for mutation.
struct StreamState {
    /// A mapping from a component instance's ExtendedMoniker, to the set of
    /// event streams and their corresponding paths in the component instance's out directory.
    streams: HashMap<ExtendedMoniker, Vec<EventStreamAttachment>>,

    /// A mapping from a component instance's InstancedAbsoluteMoniker, to the set of
    /// event streams and their corresponding paths in the component instance's out directory.
    streams_v2: HashMap<ExtendedMoniker, Vec<EventStreamAttachmentV2>>,

    /// Looks up subscriptions per component over a component's lifetime.
    /// This is used solely for removing subscriptions from the subscriptions HashMap when
    /// a component is purged.
    subscription_component_lookup: HashMap<ExtendedMoniker, HashMap<AbsolutePath, Vec<String>>>,
}

/// Creates EventStreams on component resolution according to statically declared
/// event_streams, and passes them along to components on start.
pub struct EventStreamProvider {
    /// A shared reference to the event registry used to subscribe and dispatch events.
    registry: Weak<EventRegistry>,

    state: Arc<Mutex<StreamState>>,
}

impl EventStreamProvider {
    pub fn new(registry: Weak<EventRegistry>) -> Self {
        Self {
            registry,
            state: Arc::new(Mutex::new(StreamState {
                streams: HashMap::new(),
                streams_v2: HashMap::new(),
                subscription_component_lookup: HashMap::new(),
            })),
        }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "EventStreamProvider",
            vec![EventType::Destroyed, EventType::Resolved],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    pub async fn take_events(
        self: &Arc<Self>,
        target_moniker: ExtendedMoniker,
        path: String,
    ) -> Option<Vec<String>> {
        let state = self.state.lock().await;
        if let Some(subscriptions) = state.subscription_component_lookup.get(&target_moniker) {
            if let Some(subscriptions_by_path) =
                subscriptions.get(&AbsolutePath { path, target_moniker })
            {
                return Some(subscriptions_by_path.clone());
            }
        }
        None
    }

    /// Creates a static event stream for any static capabilities (such as capability_requested)
    /// Static capabilities must be instantiated before component initialization to prevent race
    /// conditions.
    pub async fn create_v2_static_event_stream(
        self: &Arc<Self>,
        subscriber: &ExtendedMoniker,
        stream_name: String,
        subscription: EventSubscription,
        path: String,
    ) -> Result<(), ModelError> {
        let registry = self.registry.upgrade().ok_or(EventsError::RegistryNotFound)?;
        let event_stream = registry.subscribe_v2(&subscriber, vec![subscription]).await?;
        let absolute_path = AbsolutePath { target_moniker: subscriber.clone(), path };
        let mut state = self.state.lock().await;
        if !state.subscription_component_lookup.contains_key(&subscriber) {
            state.subscription_component_lookup.insert(subscriber.clone(), HashMap::new());
        }
        if let Some(subscriptions) = state.subscription_component_lookup.get_mut(&subscriber) {
            if !subscriptions.contains_key(&absolute_path) {
                subscriptions.insert(absolute_path.clone(), vec![]);
            }
            let path_list = subscriptions.get_mut(&absolute_path).unwrap();
            path_list.push(stream_name.clone());
            let event_streams = state.streams_v2.entry(subscriber.clone()).or_insert(vec![]);
            event_streams.push(EventStreamAttachmentV2 {
                name: stream_name,
                server_end: Some(event_stream),
            });
        }
        Ok(())
    }

    /// Returns the server end of the event stream with provided `name` associated with
    /// the component with the provided `target_moniker`. This method returns None if such a stream
    /// does not exist or the channel has already been taken.
    pub async fn take_static_event_stream(
        &self,
        target_moniker: &ExtendedMoniker,
        stream_name: String,
    ) -> Option<ServerEnd<fsys::EventStreamMarker>> {
        let mut state = self.state.lock().await;
        if let Some(event_streams) = state.streams.get_mut(&target_moniker) {
            if let Some(attachment) =
                event_streams.iter_mut().find(|event_stream| event_stream.name == stream_name)
            {
                return attachment.server_end.take();
            }
        }
        return None;
    }

    /// Returns the server end of the event stream with provided `name` associated with
    /// the component with the provided `target_moniker`. This method returns None if such a stream
    /// does not exist or the channel has already been taken.
    pub async fn take_v2_static_event_stream(
        &self,
        target_moniker: &ExtendedMoniker,
        stream_name: String,
    ) -> Option<EventStream> {
        let mut state = self.state.lock().await;
        if let Some(event_streams) = state.streams_v2.get_mut(&target_moniker) {
            if let Some(attachment) =
                event_streams.iter_mut().find(|event_stream| event_stream.name == stream_name)
            {
                return attachment.server_end.take();
            }
        }
        return None;
    }

    /// Creates a static EventStream listening for the specified `events` for a given |target_moniker|
    /// component and with the provided `target_path`.
    pub async fn create_static_event_stream(
        self: &Arc<Self>,
        subscriber: &ExtendedMoniker,
        stream_name: String,
        subscriptions: Vec<EventSubscription>,
    ) -> Result<(), ModelError> {
        let registry = self.registry.upgrade().ok_or(EventsError::RegistryNotFound)?;
        let event_stream = registry.subscribe(&subscriber, subscriptions).await?;
        let mut state = self.state.lock().await;
        let event_streams = state.streams.entry(subscriber.clone()).or_insert(vec![]);
        let (client_end, server_end) = create_endpoints::<fsys::EventStreamMarker>().unwrap();
        let task = fasync::Task::spawn(async move {
            serve_event_stream(event_stream, client_end).await;
        });
        event_streams.push(EventStreamAttachment {
            name: stream_name,
            server_end: Some(server_end),
            _task: task,
        });
        Ok(())
    }

    async fn on_component_destroyed(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut state = self.state.lock().await;
        // Remove all event streams associated with the `target_moniker` component.
        state.streams.remove(&ExtendedMoniker::ComponentInstance(target_moniker.clone()));
        state.streams_v2.remove(&ExtendedMoniker::ComponentInstance(target_moniker.clone()));
        state
            .subscription_component_lookup
            .remove(&ExtendedMoniker::ComponentInstance(target_moniker.clone()));
        Ok(())
    }

    async fn try_route_v2_events(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        decl: &ComponentDecl,
    ) -> Result<bool, ModelError> {
        let mut routed_v2 = false;
        for use_decl in &decl.uses {
            match use_decl {
                UseDecl::EventStream(decl) => {
                    self.create_v2_static_event_stream(
                        &ExtendedMoniker::ComponentInstance(target_moniker.clone()),
                        decl.source_name.to_string(),
                        EventSubscription {
                            event_name: decl.source_name.clone(),
                            mode: EventMode::Async,
                        },
                        decl.target_path.to_string(),
                    )
                    .await?;
                    routed_v2 = true;
                }
                _ => {}
            }
        }
        Ok(routed_v2)
    }

    async fn on_component_resolved(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        decl: &ComponentDecl,
    ) -> Result<(), ModelError> {
        // NOTE: We can't have deprecated and new events
        // active in the same component.
        // To prevent this from happening we use conditional routing.
        // First, new events are routed, and if a single new event is routed
        // successfully we don't attempt to route legacy capabilities.
        // This logic will be removed once everything is switched to
        // v2 events.
        // TODO(https://fxbug.dev/81980): Remove once fully migrated.
        // Note that a component can still use v1 and v2 events, and can get the v1
        // events as long as all the v2 events fail to route.
        let (routed_v2, err) = match self.try_route_v2_events(target_moniker, decl).await {
            Ok(routed) => (routed, None),
            Err(error) => (false, Some(error)),
        };
        let mut has_legacy_stream = false;
        if !routed_v2 {
            for use_decl in &decl.uses {
                match use_decl {
                    UseDecl::EventStreamDeprecated(UseEventStreamDeprecatedDecl {
                        name,
                        subscriptions,
                        ..
                    }) => {
                        self.create_static_event_stream(
                            &ExtendedMoniker::ComponentInstance(target_moniker.clone()),
                            name.to_string(),
                            subscriptions
                                .iter()
                                .map(|subscription| {
                                    EventSubscription::new(
                                        CapabilityName::from(subscription.event_name.clone()),
                                        EventMode::Async,
                                    )
                                })
                                .collect(),
                        )
                        .await?;
                        has_legacy_stream = true;
                    }
                    _ => {}
                }
            }
        }
        if let Some(error) = err {
            if !has_legacy_stream {
                return Err(error);
            }
        }
        Ok(())
    }
}

#[async_trait]
impl Hook for EventStreamProvider {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        let target_moniker = event
            .target_moniker
            .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
        match &event.result {
            Ok(EventPayload::Destroyed) => {
                self.on_component_destroyed(target_moniker).await?;
            }
            Ok(EventPayload::Resolved { decl, .. }) => {
                self.on_component_resolved(target_moniker, decl).await?;
            }
            _ => {}
        }
        Ok(())
    }
}
