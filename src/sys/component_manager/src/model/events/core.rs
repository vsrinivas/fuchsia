// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        model::{
            error::ModelError,
            events::{
                registry::{EventRegistry, EventStream},
                serve::serve_event_source,
            },
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
        },
    },
    anyhow::format_err,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_breakpoints as fbreak, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{future::BoxFuture, lock::Mutex},
    lazy_static::lazy_static,
    std::{
        collections::HashMap,
        convert::TryInto,
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    pub static ref EVENT_SOURCE_SERVICE: cm_rust::CapabilityPath =
        "/svc/fuchsia.test.breakpoints.BreakpointSystem".try_into().unwrap();
}

pub struct EventSourceFactory {
    inner: Arc<EventSourceFactoryInner>,
}

impl EventSourceFactory {
    pub fn new() -> Self {
        Self { inner: EventSourceFactoryInner::new() }
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![
            // This hook must be registered with all events.
            // However, a task will only receive events to which it subscribed.
            HooksRegistration::new(
                "EventRegistry",
                vec![
                    EventType::AddDynamicChild,
                    EventType::BeforeStartInstance,
                    EventType::PostDestroyInstance,
                    EventType::PreDestroyInstance,
                    EventType::ResolveInstance,
                    EventType::RouteCapability,
                    EventType::StopInstance,
                ],
                Arc::downgrade(&self.inner.event_registry) as Weak<dyn Hook>,
            ),
            // This hook provides the EventSource capability to components in the tree
            HooksRegistration::new(
                "EventSourceFactory",
                vec![EventType::ResolveInstance, EventType::RouteCapability],
                Arc::downgrade(&self.inner) as Weak<dyn Hook>,
            ),
        ]
    }

    pub async fn create(&self, scope_moniker: Option<AbsoluteMoniker>) -> EventSource {
        self.inner.create(scope_moniker).await
    }
}

struct EventSourceFactoryInner {
    event_source_registry: Mutex<HashMap<Option<AbsoluteMoniker>, EventSource>>,
    event_registry: Arc<EventRegistry>,
}

impl EventSourceFactoryInner {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            event_source_registry: Mutex::new(HashMap::new()),
            event_registry: Arc::new(EventRegistry::new()),
        })
    }

    /// Creates a `EventSource` that only receives events within the realm
    /// specified by `scope_moniker`.
    pub async fn create(&self, scope_moniker: Option<AbsoluteMoniker>) -> EventSource {
        EventSource::new(scope_moniker, self.event_registry.clone()).await
    }

    /// Returns an EventSource. An EventSource holds an AbsoluteMoniker that
    /// corresponds to the realm in which it will receive events.
    async fn on_route_scoped_framework_capability_async(
        self: Arc<Self>,
        capability_decl: &FrameworkCapability,
        scope_moniker: AbsoluteMoniker,
        capability: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match (capability, capability_decl) {
            (None, FrameworkCapability::Protocol(source_path))
                if *source_path == *EVENT_SOURCE_SERVICE =>
            {
                let key = Some(scope_moniker.clone());
                let event_source_registry = self.event_source_registry.lock().await;
                if let Some(system) = event_source_registry.get(&key) {
                    return Ok(Some(Box::new(system.clone()) as Box<dyn CapabilityProvider>));
                } else {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "Unable to find EventSource in registry for {}",
                        scope_moniker
                    )));
                }
            }
            (c, _) => return Ok(c),
        };
    }
}

impl Hook for EventSourceFactoryInner {
    fn on(self: Arc<Self>, event: &Event) -> BoxFuture<'_, Result<(), ModelError>> {
        Box::pin(async move {
            match &event.payload {
                EventPayload::RouteCapability {
                    source:
                        CapabilitySource::Framework { capability, scope_moniker: Some(scope_moniker) },
                    capability_provider,
                } => {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_scoped_framework_capability_async(
                            &capability,
                            scope_moniker.clone(),
                            capability_provider.take(),
                        )
                        .await?;
                }
                EventPayload::ResolveInstance { decl } => {
                    if decl.uses_protocol_from_framework(&EVENT_SOURCE_SERVICE) {
                        let key = Some(event.target_moniker.clone());
                        let mut event_source_registry = self.event_source_registry.lock().await;
                        // It is currently assumed that a component instance's declaration
                        // is resolved only once. Someday, this may no longer be true if individual
                        // components can be updated.
                        assert!(!event_source_registry.contains_key(&key));
                        let event_source = self.create(key.clone()).await;
                        event_source_registry.insert(key, event_source);
                    }
                }
                _ => {}
            }
            Ok(())
        })
    }
}

/// A system responsible for implementing basic events functionality on a scoped realm.
#[derive(Clone)]
pub struct EventSource {
    scope_moniker: Option<AbsoluteMoniker>,
    registry: Arc<EventRegistry>,
    resolve_instance_receiver: Arc<Mutex<Option<EventStream>>>,
}

impl EventSource {
    pub async fn new(scope_moniker: Option<AbsoluteMoniker>, registry: Arc<EventRegistry>) -> Self {
        let resolve_instance_receiver = Arc::new(Mutex::new(Some(
            registry.subscribe(scope_moniker.clone(), vec![EventType::ResolveInstance]).await,
        )));
        Self { scope_moniker, registry, resolve_instance_receiver }
    }

    /// Drops the `ResolveInstance` receiver, thereby permitting components within the
    /// realm to be started.
    pub async fn start_component_tree(&mut self) {
        let mut resolve_instance_receiver = self.resolve_instance_receiver.lock().await;
        *resolve_instance_receiver = None;
    }

    /// Subscribes to events corresponding to the `events` vector. The events are scoped
    /// to this `EventSource`'s realm. In other words, this EventSoure  will only
    /// receive events that have a target moniker within the realm of it's `scope_moniker`.
    pub async fn subscribe(&self, events: Vec<EventType>) -> EventStream {
        // Create a receiver for the given events
        self.registry.subscribe(self.scope_moniker.clone(), events.clone()).await
    }

    /// Serves a `EventSource` FIDL protocol.
    pub fn serve_async(self, stream: fbreak::BreakpointSystemRequestStream) {
        fasync::spawn(async move {
            serve_event_source(self, stream).await;
        });
    }

    // Returns an AbsoluteMoniker corresponding to the scope of this EventSource.
    pub fn scope(&self) -> Option<AbsoluteMoniker> {
        self.scope_moniker.clone()
    }
}

#[async_trait]
impl CapabilityProvider for EventSource {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let stream = ServerEnd::<fbreak::BreakpointSystemMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        self.serve_async(stream);
        Ok(())
    }
}
