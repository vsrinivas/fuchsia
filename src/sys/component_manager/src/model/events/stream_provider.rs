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
        rights::{Rights, WRITE_RIGHTS},
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, ComponentDecl, UseDecl, UseEventStreamDecl},
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::lock::Mutex,
    moniker::AbsoluteMoniker,
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
    streams: Arc<Mutex<HashMap<AbsoluteMoniker, Vec<EventStreamAttachment>>>>,

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
            vec![EventType::Destroyed, EventType::Resolved, EventType::Started],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn create_static_event_stream(
        &self,
        registry: &Arc<EventRegistry>,
        target_moniker: &AbsoluteMoniker,
        target_path: String,
        events: Vec<CapabilityName>,
    ) -> Result<(), ModelError> {
        let options = SubscriptionOptions::new(
            SubscriptionType::Component(target_moniker.clone()),
            self.execution_mode.clone(),
        );
        let event_stream = registry
            .subscribe(
                &options,
                events
                    .into_iter()
                    .map(|event| EventSubscription::new(event, EventMode::Async))
                    .collect(),
            )
            .await?;
        let mut streams = self.streams.lock().await;
        let event_streams = streams.entry(target_moniker.clone()).or_insert(vec![]);
        let (client_end, server_end) = create_endpoints::<fsys::EventStreamMarker>().unwrap();
        event_streams
            .push(EventStreamAttachment { target_path: target_path.to_string(), server_end });
        fasync::Task::spawn(async move {
            serve_event_stream(event_stream, client_end).await.unwrap();
        })
        .detach();
        Ok(())
    }

    async fn on_component_destroyed(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut streams = self.streams.lock().await;
        streams.remove(&target_moniker);
        Ok(())
    }

    async fn on_component_resolved(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        decl: &ComponentDecl,
    ) -> Result<(), ModelError> {
        let registry = self.registry.upgrade().ok_or(EventsError::RegistryNotFound)?;
        for use_decl in &decl.uses {
            match use_decl {
                UseDecl::EventStream(UseEventStreamDecl { target_path, events }) => {
                    let events = events.iter().map(|e| CapabilityName(e.to_string())).collect();
                    self.create_static_event_stream(
                        &registry,
                        target_moniker,
                        target_path.to_string(),
                        events,
                    )
                    .await?;
                }
                _ => {}
            }
        }
        Ok(())
    }

    async fn on_component_started(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        outgoing_dir: &DirectoryProxy,
    ) -> Result<(), ModelError> {
        let mut streams = self.streams.lock().await;
        if let Some(event_streams) = streams.remove(&target_moniker) {
            for event_stream in event_streams {
                let canonicalized_path = io_util::canonicalize_path(&event_stream.target_path);
                outgoing_dir
                    .open(
                        Rights::from(*WRITE_RIGHTS).into_legacy(),
                        fio::MODE_TYPE_SERVICE,
                        &canonicalized_path,
                        ServerEnd::new(event_stream.server_end.into_channel()),
                    )
                    .map_err(|_| {
                        ModelError::open_directory_error(
                            target_moniker.clone(),
                            canonicalized_path.clone(),
                        )
                    })?;
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
            Ok(EventPayload::Resolved { decl }) => {
                self.on_component_resolved(&event.target_moniker, decl).await?;
            }
            Ok(EventPayload::Started { runtime, .. }) => {
                if let Some(outgoing_dir) = &runtime.outgoing_dir {
                    self.on_component_started(&event.target_moniker, outgoing_dir).await?;
                }
            }
            _ => {}
        }
        Ok(())
    }
}
