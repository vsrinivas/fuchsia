// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilityProvider,
        channel,
        model::{
            error::ModelError,
            events::{
                error::EventsError,
                event::SyncMode,
                registry::{EventRegistry, ExecutionMode, SubscriptionOptions, SubscriptionType},
                serve::serve_event_source_sync,
                stream::EventStream,
            },
            hooks::EventType,
            model::Model,
        },
    },
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

/// A system responsible for implementing basic events functionality on a scoped realm.
#[derive(Clone)]
pub struct EventSource {
    /// The component model, needed to route events.
    model: Weak<Model>,

    /// A shared reference to the event registry used to subscribe and dispatch events.
    registry: Weak<EventRegistry>,

    /// Used for `BlockingEventSource.StartComponentTree`.
    // TODO(fxbug.dev/48245): this shouldn't be done for any EventSource. Only for tests.
    resolve_instance_event_stream: Arc<Mutex<Option<EventStream>>>,

    /// The options used to subscribe to events.
    options: SubscriptionOptions,
}

impl EventSource {
    /// Creates a new `EventSource` that will be used by the component identified with the given
    /// `target_moniker`.
    pub async fn new(
        model: Weak<Model>,
        options: SubscriptionOptions,
        registry: Weak<EventRegistry>,
    ) -> Result<Self, ModelError> {
        // TODO(fxbug.dev/48245): this shouldn't be done for any EventSource. Only for tests.
        let resolve_instance_event_stream = Arc::new(Mutex::new(match options.sync_mode {
            SyncMode::Async => None,
            SyncMode::Sync => {
                let registry = registry.upgrade().ok_or(EventsError::RegistryNotFound)?;
                Some(registry.subscribe(&options, vec![EventType::Resolved.into()]).await?)
            }
        }));

        Ok(Self { registry, model, options, resolve_instance_event_stream })
    }

    pub async fn new_for_debug(
        model: Weak<Model>,
        registry: Weak<EventRegistry>,
        sync_mode: SyncMode,
    ) -> Result<Self, ModelError> {
        Self::new(
            model,
            SubscriptionOptions::new(SubscriptionType::AboveRoot, sync_mode, ExecutionMode::Debug),
            registry,
        )
        .await
    }

    /// Drops the `Resolved` event stream, thereby permitting components within the
    /// realm to be started.
    pub async fn start_component_tree(&mut self) {
        let mut resolve_instance_event_stream = self.resolve_instance_event_stream.lock().await;
        *resolve_instance_event_stream = None;
    }

    /// Subscribes to events provided in the `events` vector.
    ///
    /// The client might request to subscribe to events that it's not allowed to see. Events
    /// that are allowed should have been defined in its manifest and either offered to it or
    /// requested from the current realm.
    pub async fn subscribe(
        &mut self,
        events: Vec<CapabilityName>,
    ) -> Result<EventStream, ModelError> {
        let registry = self.registry.upgrade().ok_or(EventsError::RegistryNotFound)?;

        // Create an event stream for the given events
        registry.subscribe(&self.options, events).await
    }

    /// Serves a `EventSource` FIDL protocol.
    pub fn serve(self, stream: fsys::BlockingEventSourceRequestStream) {
        fasync::Task::spawn(async move {
            serve_event_source_sync(self, stream).await;
        })
        .detach();
    }
}

#[async_trait]
impl CapabilityProvider for EventSource {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let stream = ServerEnd::<fsys::BlockingEventSourceMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        self.serve(stream);
        Ok(())
    }
}
