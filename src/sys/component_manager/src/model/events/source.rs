// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        model::{
            error::ModelError,
            events::{
                dispatcher::ScopeMetadata,
                error::EventsError,
                event::SyncMode,
                filter::EventFilter,
                registry::{EventRegistry, RoutedEvent},
                serve::serve_event_source_sync,
                stream::EventStream,
            },
            hooks::EventType,
            model::Model,
            moniker::AbsoluteMoniker,
            realm::Realm,
            routing,
        },
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, UseDecl, UseEventDecl},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::{
        collections::HashMap,
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

/// A system responsible for implementing basic events functionality on a scoped realm.
#[derive(Clone)]
pub struct EventSource {
    /// The component model, needed to route events.
    model: Weak<Model>,

    /// The moniker identifying the realm that requested this event source
    target_moniker: AbsoluteMoniker,

    /// A shared reference to the event registry used to subscribe and dispatche events.
    registry: Weak<EventRegistry>,

    /// Used for `BlockingEventSource.StartComponentTree`.
    // TODO(fxb/48245): this shouldn't be done for any EventSource. Only for tests.
    resolve_instance_event_stream: Arc<Mutex<Option<EventStream>>>,

    /// Whether or not this is a debug instance.
    debug: bool,

    /// Whether or not this EventSource dispatches events asynchronously.
    sync_mode: SyncMode,
}

struct RouteEventsResult {
    /// Maps from source name to a set of scope monikers
    mapping: HashMap<CapabilityName, Vec<ScopeMetadata>>,
}

impl RouteEventsResult {
    fn new() -> Self {
        Self { mapping: HashMap::new() }
    }

    fn insert(&mut self, source_name: CapabilityName, scope: ScopeMetadata) {
        let values = self.mapping.entry(source_name).or_insert(Vec::new());
        if !values.contains(&scope) {
            values.push(scope);
        }
    }

    fn len(&self) -> usize {
        self.mapping.len()
    }

    fn contains_event(&self, event_name: &CapabilityName) -> bool {
        self.mapping.contains_key(event_name)
    }

    fn to_vec(self) -> Vec<RoutedEvent> {
        self.mapping
            .into_iter()
            .map(|(source_name, scopes)| RoutedEvent { source_name, scopes })
            .collect()
    }
}

impl EventSource {
    /// Creates a new `EventSource` that will be used by the component identified with the given
    /// `target_moniker`.
    pub async fn new(
        model: Weak<Model>,
        target_moniker: AbsoluteMoniker,
        registry: &Arc<EventRegistry>,
        sync_mode: SyncMode,
    ) -> Result<Self, ModelError> {
        // TODO(fxb/48245): this shouldn't be done for any EventSource. Only for tests.
        let resolve_instance_event_stream = Arc::new(Mutex::new(if sync_mode == SyncMode::Async {
            None
        } else {
            Some(
                registry
                    .subscribe(
                        &sync_mode,
                        vec![RoutedEvent {
                            source_name: EventType::Resolved.into(),
                            scopes: vec![ScopeMetadata::new(target_moniker.clone())],
                        }],
                    )
                    .await?,
            )
        }));
        Ok(Self {
            registry: Arc::downgrade(&registry),
            model,
            target_moniker,
            resolve_instance_event_stream,
            debug: false,
            sync_mode,
        })
    }

    pub async fn new_for_debug(
        model: Weak<Model>,
        target_moniker: AbsoluteMoniker,
        registry: &Arc<EventRegistry>,
        sync_mode: SyncMode,
    ) -> Result<Self, ModelError> {
        let mut event_source = Self::new(model, target_moniker, registry, sync_mode).await?;
        event_source.debug = true;
        Ok(event_source)
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
        // Register event capabilities if any. It identifies the sources of these events (might be the
        // containing realm or this realm itself). It consturcts an "allow-list tree" of events and
        // realms.
        let events = if self.debug {
            events
                .into_iter()
                .map(|event| RoutedEvent {
                    source_name: event.clone(),
                    scopes: vec![ScopeMetadata::new(AbsoluteMoniker::root()).for_debug()],
                })
                .collect()
        } else {
            let route_result = self.route_events(&events).await?;
            if route_result.len() != events.len() {
                let names = events
                    .into_iter()
                    .filter(|event| !route_result.contains_event(&event))
                    .collect();
                return Err(EventsError::not_available(names).into());
            }
            route_result.to_vec()
        };

        // Create an event stream for the given events
        if let Some(registry) = self.registry.upgrade() {
            return registry.subscribe(&self.sync_mode, events).await;
        }
        Err(EventsError::RegistryNotFound.into())
    }

    /// Serves a `EventSource` FIDL protocol.
    pub fn serve(self, stream: fsys::BlockingEventSourceRequestStream) {
        fasync::spawn(async move {
            serve_event_source_sync(self, stream).await;
        });
    }

    async fn route_events(
        &self,
        events: &Vec<CapabilityName>,
    ) -> Result<RouteEventsResult, ModelError> {
        let model = self.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
        let realm = model.look_up_realm(&self.target_moniker).await?;
        let decl = {
            let state = realm.lock_state().await;
            state.as_ref().expect("route_events: not registered").decl().clone()
        };

        let mut result = RouteEventsResult::new();
        for use_decl in decl.uses {
            match &use_decl {
                UseDecl::Event(event_decl) => {
                    if !events.contains(&event_decl.target_name) {
                        continue;
                    }
                    let (source_name, scope_moniker) = self.route_event(event_decl, &realm).await?;
                    let scope = ScopeMetadata::new(scope_moniker)
                        .with_filter(EventFilter::new(event_decl.filter.clone()));
                    result.insert(source_name, scope);
                }
                _ => {}
            }
        }

        Ok(result)
    }

    /// Routes an event and returns its source name and scope on success.
    async fn route_event(
        &self,
        event_decl: &UseEventDecl,
        realm: &Arc<Realm>,
    ) -> Result<(CapabilityName, AbsoluteMoniker), ModelError> {
        routing::route_use_event_capability(&UseDecl::Event(event_decl.clone()), &realm).await.map(
            |source| match source {
                CapabilitySource::Framework {
                    capability: FrameworkCapability::Event(source_name),
                    scope_moniker: Some(scope_moniker),
                } => (source_name, scope_moniker),
                _ => unreachable!(),
            },
        )
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
        let stream = ServerEnd::<fsys::BlockingEventSourceMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        self.serve(stream);
        Ok(())
    }
}
