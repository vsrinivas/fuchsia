// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        events::{
            error::EventsError,
            event::EventMode,
            registry::{
                EventRegistry, EventSubscription, ExecutionMode, SubscriptionOptions,
                SubscriptionType,
            },
            serve::serve_event_stream,
        },
        hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, ComponentDecl, UseDecl, UseEventStreamDecl},
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::lock::Mutex,
    log::warn,
    moniker::{AbsoluteMoniker, ExtendedMoniker},
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
    },
};

pub struct EventStreamAttachment {
    /// The target path in which to connect to the event stream in a component's
    /// outgoing directory.
    target_path: String,
    /// The server end of a component's event stream.
    server_end: ServerEnd<fsys::EventStreamMarker>,
}

/// Creates EventStreams on component resolution according to statically declared
/// event_streams, and passes them along to components on start.
pub struct EventStreamProvider {
    /// A shared reference to the event registry used to subscribe and dispatch events.
    registry: Weak<EventRegistry>,

    /// A mapping from a component instance's AbsoluteMoniker, to the set of
    /// event streams and their corresponding paths in the component instance's out directory.
    streams: Arc<Mutex<HashMap<ExtendedMoniker, Vec<EventStreamAttachment>>>>,

    /// The mode in which component manager is running.
    execution_mode: ExecutionMode,
}

impl EventStreamProvider {
    pub fn new(registry: Weak<EventRegistry>, execution_mode: ExecutionMode) -> Self {
        Self { registry, streams: Arc::new(Mutex::new(HashMap::new())), execution_mode }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "EventStreamProvider",
            vec![EventType::Destroyed, EventType::Resolved],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Returns the server end of the event stream at the provided `target_path` associated with
    /// the component with the provided `target_moniker`. This method returns None if such a stream
    /// does not exist or the channel has already been taken.
    pub async fn take_static_event_stream(
        &self,
        target_moniker: &ExtendedMoniker,
        target_path: String,
    ) -> Option<ServerEnd<fsys::EventStreamMarker>> {
        let mut streams = self.streams.lock().await;
        if let Some(event_streams) = streams.get_mut(&target_moniker) {
            if let Some(pos) = event_streams
                .iter()
                .position(|event_stream| event_stream.target_path == target_path)
            {
                let event_stream = event_streams.remove(pos);
                return Some(event_stream.server_end);
            }
        }
        return None;
    }

    /// Creates a static EventStream listening for the specified `events` for a given |target_moniker|
    /// component and with the provided `target_path`.
    pub async fn create_static_event_stream(
        self: &Arc<Self>,
        target_moniker: &ExtendedMoniker,
        target_path: String,
        events: Vec<EventSubscription>,
    ) -> Result<fasync::Task<()>, ModelError> {
        let registry = self.registry.upgrade().ok_or(EventsError::RegistryNotFound)?;
        let subscription_type = match target_moniker {
            ExtendedMoniker::ComponentManager => SubscriptionType::AboveRoot,
            ExtendedMoniker::ComponentInstance(abs_moniker) => {
                SubscriptionType::Component(abs_moniker.clone())
            }
        };
        let options = SubscriptionOptions::new(subscription_type, self.execution_mode.clone());
        let event_stream = registry.subscribe(&options, events).await?;
        let mut streams = self.streams.lock().await;
        let event_streams = streams.entry(target_moniker.clone()).or_insert(vec![]);
        let (client_end, server_end) = create_endpoints::<fsys::EventStreamMarker>().unwrap();
        event_streams.push(EventStreamAttachment { target_path, server_end });
        Ok(fasync::Task::spawn(async move {
            if let Err(e) = serve_event_stream(event_stream, client_end).await {
                warn!("{}", e);
            }
        }))
    }

    async fn on_component_destroyed(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut streams = self.streams.lock().await;
        streams.remove(&ExtendedMoniker::ComponentInstance(target_moniker.clone()));
        Ok(())
    }

    async fn on_component_resolved(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        decl: &ComponentDecl,
    ) -> Result<(), ModelError> {
        for use_decl in &decl.uses {
            match use_decl {
                UseDecl::EventStream(UseEventStreamDecl { target_path, events }) => {
                    self.create_static_event_stream(
                        &ExtendedMoniker::ComponentInstance(target_moniker.clone()),
                        target_path.to_string(),
                        events
                            .iter()
                            .map(|subscription| EventSubscription {
                                event_name: CapabilityName::from(subscription.event_name.clone()),
                                mode: match subscription.mode {
                                    cm_rust::EventMode::Sync => EventMode::Sync,
                                    _ => EventMode::Async,
                                },
                            })
                            .collect(),
                    )
                    .await?
                    .detach();
                }
                _ => {}
            }
        }
        Ok(())
    }
}

#[async_trait]
impl Hook for EventStreamProvider {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::Destroyed) => {
                self.on_component_destroyed(&event.target_moniker).await?;
            }
            Ok(EventPayload::Resolved { decl, .. }) => {
                self.on_component_resolved(&event.target_moniker, decl).await?;
            }
            _ => {}
        }
        Ok(())
    }
}
