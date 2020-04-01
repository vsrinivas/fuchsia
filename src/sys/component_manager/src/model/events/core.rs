// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        model::{
            error::ModelError,
            events::{
                registry::{EventRegistry, EventStream, SyncMode},
                serve::serve_event_source_sync,
            },
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
            moniker::AbsoluteMoniker,
            realm::Realm,
            routing,
        },
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, CapabilityPath, ComponentDecl, UseDecl, UseEventDecl},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_test_events as fevents, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::Mutex,
    lazy_static::lazy_static,
    log::*,
    maplit::hashset,
    std::{
        collections::{HashMap, HashSet},
        convert::TryInto,
        path::PathBuf,
        sync::{Arc, Weak},
    },
    thiserror::Error,
};

lazy_static! {
    pub static ref EVENT_SOURCE_SERVICE_PATH: CapabilityPath =
        "/svc/fuchsia.test.events.EventSource".try_into().unwrap();
    pub static ref EVENT_SOURCE_SYNC_SERVICE_PATH: CapabilityPath =
        "/svc/fuchsia.test.events.EventSourceSync".try_into().unwrap();
}

/// Allows to create `EventSource`s and tracks all the created ones.
pub struct EventSourceFactory {
    /// Tracks the event source used by each component ideantified with the given `moniker`.
    event_source_registry: Mutex<HashMap<AbsoluteMoniker, EventSource>>,

    /// The event registry. It subscribes to all events happening in the system and
    /// routes them to subscribers.
    // TODO(fxb/48512): instead of using a global registry integrate more with the hooks model.
    event_registry: Arc<EventRegistry>,

    /// The component model, needed to route events.
    model: Weak<Model>,
}

impl EventSourceFactory {
    /// Creates a new event source factory.
    pub fn new(model: Weak<Model>) -> Self {
        Self {
            event_source_registry: Mutex::new(HashMap::new()),
            event_registry: Arc::new(EventRegistry::new()),
            model,
        }
    }

    /// Creates the subscription to the required events.
    /// `CapabilityReady` used to track events and associate them with the component that needs them
    /// as well as the scoped that will be allowed. Also the EventSource protocol capability.
    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        let mut hooks = self.event_registry.hooks();
        hooks.append(&mut vec![
            // This hook provides the EventSource capability to components in the tree
            HooksRegistration::new(
                "EventSourceFactory",
                vec![EventType::CapabilityRouted, EventType::Destroyed, EventType::Resolved],
                Arc::downgrade(self) as Weak<dyn Hook>,
            ),
        ]);
        hooks
    }

    /// Creates a debug event source.
    pub async fn create_for_debug(&self) -> Result<EventSource, ModelError> {
        EventSource::new_for_debug(
            self.model.clone(),
            AbsoluteMoniker::root(),
            &self.event_registry,
        )
        .await
    }

    /// Creates a `EventSource` for the given `target_moniker`.
    pub async fn create(
        &self,
        target_moniker: AbsoluteMoniker,
        sync_mode: SyncMode,
    ) -> Result<EventSource, ModelError> {
        EventSource::new(self.model.clone(), target_moniker, &self.event_registry, sync_mode).await
    }

    /// Returns an EventSource. An EventSource holds an AbsoluteMoniker that
    /// corresponds to the realm in which it will receive events.
    async fn on_scoped_framework_capability_routed_async(
        self: Arc<Self>,
        capability_decl: &FrameworkCapability,
        target_moniker: AbsoluteMoniker,
        _scope_moniker: AbsoluteMoniker,
        capability: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match (capability, capability_decl) {
            (None, FrameworkCapability::Protocol(source_path))
                if *source_path == *EVENT_SOURCE_SERVICE_PATH
                    || *source_path == *EVENT_SOURCE_SYNC_SERVICE_PATH =>
            {
                let event_source_registry = self.event_source_registry.lock().await;
                if let Some(event_source) = event_source_registry.get(&target_moniker) {
                    Ok(Some(Box::new(event_source.clone()) as Box<dyn CapabilityProvider>))
                } else {
                    return Err(ModelError::capability_discovery_error(format!(
                        "Unable to find EventSource in registry for {}",
                        target_moniker
                    )));
                }
            }
            (c, _) => return Ok(c),
        }
    }

    async fn on_destroyed_async(self: &Arc<Self>, target_moniker: &AbsoluteMoniker) {
        let mut event_source_registry = self.event_source_registry.lock().await;
        event_source_registry.remove(&target_moniker);
    }

    async fn on_resolved_async(
        self: &Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        decl: &ComponentDecl,
    ) -> Result<(), ModelError> {
        let sync_mode = if decl.uses_protocol_from_framework(&EVENT_SOURCE_SERVICE_PATH) {
            SyncMode::Async
        } else if decl.uses_protocol_from_framework(&EVENT_SOURCE_SYNC_SERVICE_PATH) {
            SyncMode::Sync
        } else {
            return Ok(());
        };
        let key = target_moniker.clone();
        let mut event_source_registry = self.event_source_registry.lock().await;
        // It is currently assumed that a component instance's declaration
        // is resolved only once. Someday, this may no longer be true if individual
        // components can be updated.
        assert!(!event_source_registry.contains_key(&key));
        // An EventSource is created on resolution in order to ensure that discovery
        // and resolution of children is not missed.
        let event_source = self.create(key.clone(), sync_mode).await?;
        event_source_registry.insert(key, event_source);
        Ok(())
    }

    #[cfg(test)]
    async fn has_event_source(&self, abs_moniker: &AbsoluteMoniker) -> bool {
        let event_source_registry = self.event_source_registry.lock().await;
        event_source_registry.contains_key(abs_moniker)
    }
}

#[async_trait]
impl Hook for EventSourceFactory {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.payload {
            EventPayload::CapabilityRouted {
                source:
                    CapabilitySource::Framework { capability, scope_moniker: Some(scope_moniker) },
                capability_provider,
            } => {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_scoped_framework_capability_routed_async(
                        &capability,
                        event.target_moniker.clone(),
                        scope_moniker.clone(),
                        capability_provider.take(),
                    )
                    .await?;
            }
            EventPayload::Destroyed => {
                self.on_destroyed_async(&event.target_moniker).await;
            }
            EventPayload::Resolved { decl } => {
                self.on_resolved_async(&event.target_moniker, decl).await?;
            }
            _ => {}
        }
        Ok(())
    }
}

/// A system responsible for implementing basic events functionality on a scoped realm.
#[derive(Clone)]
pub struct EventSource {
    /// The component model, needed to route events.
    model: Weak<Model>,

    /// The moniker identifying the realm that requested this event source
    target_moniker: AbsoluteMoniker,

    /// A shared reference to the event registry used to subscribe and dispatche events.
    registry: Weak<EventRegistry>,

    /// Used for `EventSourceSync.StartComponentTree`.
    // TODO(fxb/48245): this shouldn't be done for any EventSource. Only for tests.
    resolve_instance_event_stream: Arc<Mutex<Option<EventStream>>>,

    /// Whether or not this is a debug instance.
    debug: bool,

    /// Whether or not this EventSource dispatches events asynchronously.
    sync_mode: SyncMode,
}

#[derive(Debug, Error)]
pub enum EventsError {
    #[error("Registry not found")]
    RegistryNotFound,

    #[error("Events not allowed for subscription {:?}", names)]
    NotAvailable { names: Vec<CapabilityName> },

    // TODO(fxb/48720): use dedicated RoutingError type.
    #[error("Routing failed")]
    RoutingFailed(#[source] ModelError),
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
                        vec![(EventType::Resolved, hashset!(target_moniker.clone()))],
                    )
                    .await,
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

    async fn new_for_debug(
        model: Weak<Model>,
        target_moniker: AbsoluteMoniker,
        registry: &Arc<EventRegistry>,
    ) -> Result<Self, ModelError> {
        let mut event_source = Self::new(model, target_moniker, registry, SyncMode::Sync).await?;
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
    // TODO(fxb/48721): this should take a vector of strings not event types. Lots of cleanup can
    // be done in this function once that is done.
    pub async fn subscribe(&mut self, events: Vec<EventType>) -> Result<EventStream, EventsError> {
        // Register event capabilities if any. It identifies the sources of these events (might be the
        // containing realm or this realm itself). It consturcts an "allow-list tree" of events and
        // realms.
        let events = if self.debug {
            events.into_iter().map(|event| (event, hashset!(AbsoluteMoniker::root()))).collect()
        } else {
            let routed_events =
                self.route_events(&events).await.map_err(|e| EventsError::RoutingFailed(e))?;
            if routed_events.len() != events.len() {
                let names = events
                    .into_iter()
                    .filter(|event| !routed_events.contains_key(&event))
                    .map(|event| event.to_string().into())
                    .collect();
                return Err(EventsError::NotAvailable { names });
            }
            routed_events.into_iter().collect()
        };

        // Create an event stream for the given events
        if let Some(registry) = self.registry.upgrade() {
            // TODO(fxb/48721): we should also pass the target name
            return Ok(registry.subscribe(&self.sync_mode, events).await);
        }
        Err(EventsError::RegistryNotFound)
    }

    /// Serves a `EventSource` FIDL protocol.
    pub fn serve(self, stream: fevents::EventSourceSyncRequestStream) {
        fasync::spawn(async move {
            serve_event_source_sync(self, stream).await;
        });
    }

    async fn route_events(
        &self,
        events: &Vec<EventType>,
    ) -> Result<HashMap<EventType, HashSet<AbsoluteMoniker>>, ModelError> {
        let model = self.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
        let realm = model.look_up_realm(&self.target_moniker).await?;
        let decl = {
            let state = realm.lock_state().await;
            state.as_ref().expect("route_events: not registered").decl().clone()
        };

        let mut result = HashMap::new();
        let events = events
            .into_iter()
            .map(|event| event.to_string().into())
            .collect::<Vec<CapabilityName>>();
        for use_decl in decl.uses {
            match &use_decl {
                UseDecl::Event(event_decl) => {
                    if !events.contains(&event_decl.target_name) {
                        continue;
                    }
                    let (source_name, scope_moniker) = self.route_event(event_decl, &realm).await?;
                    result
                        .entry(source_name.to_string().try_into().map_err(|e| {
                            ModelError::capability_discovery_error(format!("Unknown event: {}", e))
                        })?)
                        .or_insert(HashSet::new())
                        .insert(scope_moniker);
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
        let stream = ServerEnd::<fevents::EventSourceSyncMarker>::new(server_end)
            .into_stream()
            .expect("could not convert channel into stream");
        self.serve(stream);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            hooks::Hooks, model::ModelParams, resolver::ResolverRegistry,
            testing::test_helpers::ComponentDeclBuilder,
        },
        cm_rust::{UseProtocolDecl, UseSource},
    };

    async fn dispatch_resolved_event(
        hooks: &Hooks,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let decl = ComponentDeclBuilder::new()
            .use_(UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Framework,
                source_path: (*EVENT_SOURCE_SYNC_SERVICE_PATH).clone(),
                target_path: (*EVENT_SOURCE_SYNC_SERVICE_PATH).clone(),
            }))
            .build();
        let event =
            Event::new(target_moniker.clone(), EventPayload::Resolved { decl: decl.clone() });
        hooks.dispatch(&event).await
    }

    async fn dispatch_destroyed_event(
        hooks: &Hooks,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let event = Event::new(target_moniker.clone(), EventPayload::Destroyed);
        hooks.dispatch(&event).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn drop_event_source_when_component_destroyed() {
        let model = {
            let registry = ResolverRegistry::new();
            Arc::new(Model::new(ModelParams {
                root_component_url: "test:///root".to_string(),
                root_resolver_registry: registry,
            }))
        };
        let event_source_factory = Arc::new(EventSourceFactory::new(Arc::downgrade(&model)));

        let hooks = Hooks::new(None);
        hooks.install(event_source_factory.hooks()).await;

        let root = AbsoluteMoniker::root();

        // Verify that there is no EventSource for the root until we dispatch the Resolved event.
        assert!(!event_source_factory.has_event_source(&root).await);
        dispatch_resolved_event(&hooks, &root).await.unwrap();
        assert!(event_source_factory.has_event_source(&root).await);
        // Verify that destroying the component destroys the EventSource.
        dispatch_destroyed_event(&hooks, &root).await.unwrap();
        assert!(!event_source_factory.has_event_source(&root).await);
    }
}
