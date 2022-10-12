// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilityProvider,
        model::{
            error::ModelError,
            events::{
                error::EventsError,
                registry::{EventRegistry, EventSubscription},
                serve::{serve_event_source_sync, serve_event_stream_v2},
                stream::EventStream,
                stream_provider::EventStreamProvider,
            },
        },
    },
    async_trait::async_trait,
    cm_rust::CapabilityName,
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    futures::{SinkExt, StreamExt},
    moniker::ExtendedMoniker,
    std::{path::PathBuf, sync::Weak},
};

// Event source v2 (supporting event streams)
#[derive(Clone)]
pub struct EventSourceV2 {
    pub v1: EventSource,
    pub name: CapabilityName,
}

impl EventSourceV2 {
    pub async fn new(v1: EventSource, name: CapabilityName) -> Result<Self, ModelError> {
        Ok(Self { v1, name })
    }

    /// Subscribes to events provided in the `events` vector.
    ///
    /// The client might request to subscribe to events that it's not allowed to see. Events
    /// that are allowed should have been defined in its manifest and either offered to it or
    /// requested from the current realm.
    pub async fn subscribe(
        &mut self,
        requests: Vec<EventSubscription>,
    ) -> Result<EventStream, ModelError> {
        let registry = self.v1.registry.upgrade().ok_or(EventsError::RegistryNotFound)?;
        let mut static_streams = vec![];
        if let Some(stream_provider) = self.v1.stream_provider.upgrade() {
            for request in requests {
                if let Some(res) = stream_provider
                    .take_v2_static_event_stream(
                        &self.v1.subscriber,
                        request.event_name.to_string(),
                    )
                    .await
                {
                    static_streams.push(res);
                } else {
                    // Subscribe to events in the registry, discarding prior events
                    // from before this subscribe call if this is the second
                    // time opening the event stream.
                    if request.event_name.to_string() == "capability_requested" {
                        // Don't support creating a new capability_requested stream.
                        return Err(ModelError::unsupported(
                            "capability_requested cannot be taken twice.",
                        ));
                    }
                    let stream = registry.subscribe_v2(&self.v1.subscriber, vec![request]).await?;
                    static_streams.push(stream);
                }
            }
        }
        // Create an event stream for the given events
        let mut stream = registry.subscribe_v2(&self.v1.subscriber, vec![]).await?;
        for mut request in static_streams {
            let mut tx = stream.sender();
            stream.tasks.push(fuchsia_async::Task::spawn(async move {
                while let Some((event, _)) = request.next().await {
                    if let Err(_) = tx.send((event, Some(request.route.clone()))).await {
                        break;
                    }
                }
            }));
        }
        Ok(stream)
    }

    /// Subscribes to all applicable events in a single use statement.
    /// This method may be called once per path, and will return None
    /// if the event stream has already been consumed.
    async fn subscribe_all(
        &mut self,
        target_moniker: ExtendedMoniker,
        path: String,
    ) -> Result<Option<EventStream>, ModelError> {
        if let Some(stream_provider) = self.v1.stream_provider.upgrade() {
            if let Some(event_names) = stream_provider.take_events(target_moniker, path).await {
                let subscriptions = event_names
                    .into_iter()
                    .map(|name| EventSubscription {
                        event_name: CapabilityName::from(name),
                        mode: cm_rust::EventMode::Async,
                    })
                    .collect();
                return Ok(Some(self.subscribe(subscriptions).await?));
            }
        }
        Ok(None)
    }
}

/// A system responsible for implementing basic events functionality on a scoped realm.
#[derive(Clone)]
pub struct EventSource {
    /// A shared reference to the event registry used to subscribe and dispatch events.
    registry: Weak<EventRegistry>,

    /// The static EventStreamProvider tracks all static event streams. It can be used to take the
    /// server end of the static event streams.
    stream_provider: Weak<EventStreamProvider>,

    /// The moniker of the component subscribing to events.
    subscriber: ExtendedMoniker,
}

impl EventSource {
    pub async fn new(
        subscriber: ExtendedMoniker,
        registry: Weak<EventRegistry>,
        stream_provider: Weak<EventStreamProvider>,
    ) -> Result<Self, ModelError> {
        Ok(Self { registry, stream_provider, subscriber })
    }

    pub async fn new_for_above_root(
        registry: Weak<EventRegistry>,
        stream_provider: Weak<EventStreamProvider>,
    ) -> Result<Self, ModelError> {
        Self::new(ExtendedMoniker::ComponentManager, registry, stream_provider).await
    }

    /// Subscribes to events provided in the `requests` vector.
    ///
    /// The client might request to subscribe to events that it's not allowed to see. Events
    /// that are allowed should have been defined in its manifest and either offered to it or
    /// requested from the current realm.
    pub async fn subscribe(
        &mut self,
        requests: Vec<EventSubscription>,
    ) -> Result<EventStream, ModelError> {
        let registry = self.registry.upgrade().ok_or(EventsError::RegistryNotFound)?;
        // Create an event stream for the given events
        registry.subscribe(&self.subscriber, requests).await
    }

    pub async fn take_static_event_stream(
        &self,
        target_path: String,
    ) -> Option<ServerEnd<fsys::EventStreamMarker>> {
        if let Some(stream_provider) = self.stream_provider.upgrade() {
            return stream_provider.take_static_event_stream(&self.subscriber, target_path).await;
        }
        return None;
    }

    /// Serves a `EventSource` FIDL protocol.
    pub async fn serve(self, stream: fsys::EventSourceRequestStream) {
        serve_event_source_sync(self, stream).await;
    }
}

#[async_trait]
impl CapabilityProvider for EventSourceV2 {
    async fn open(
        mut self: Box<Self>,
        task_scope: TaskScope,
        _flags: fio::OpenFlags,
        _open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let stream = ServerEnd::<fsys::EventStream2Marker>::new(server_end);
        task_scope
            .add_task(async move {
                let moniker = self.v1.subscriber.clone();
                if let Ok(Some(event_stream)) = self
                    .subscribe_all(moniker, relative_path.into_os_string().into_string().unwrap())
                    .await
                {
                    serve_event_stream_v2(event_stream, stream).await;
                }
            })
            .await;
        Ok(())
    }
}

#[async_trait]
impl CapabilityProvider for EventSource {
    async fn open(
        self: Box<Self>,
        task_scope: TaskScope,
        _flags: fio::OpenFlags,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let stream = ServerEnd::<fsys::EventSourceMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        task_scope
            .add_task(async move {
                serve_event_source_sync(*self, stream).await;
            })
            .await;
        Ok(())
    }
}
