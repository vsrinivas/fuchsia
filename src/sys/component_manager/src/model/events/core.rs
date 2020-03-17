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
                serve::serve_event_source_sync,
            },
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
        },
    },
    anyhow::format_err,
    async_trait::async_trait,
    cm_rust::{CapabilityName, CapabilityPath},
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
    pub static ref EVENT_SOURCE_SYNC_SERVICE: CapabilityPath =
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
}

impl EventSourceFactory {
    /// Creates a new event source factory.
    pub fn new() -> Self {
        Self {
            event_source_registry: Mutex::new(HashMap::new()),
            event_registry: Arc::new(EventRegistry::new()),
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
                // TODO(fxb/48359): track destroyed to clean up any stored data.
                vec![EventType::CapabilityRouted],
                Arc::downgrade(self) as Weak<dyn Hook>,
            ),
        ]);
        hooks
    }

    /// Creates a `EventSource` for the given `target_moniker`.
    pub async fn create(&self, target_moniker: AbsoluteMoniker) -> EventSource {
        EventSource::new(target_moniker, &self.event_registry).await
    }

    /// Returns an EventSource. An EventSource holds an AbsoluteMoniker that
    /// corresponds to the realm in which it will receive events.
    async fn on_scoped_framework_capability_routed_async(
        self: Arc<Self>,
        capability_decl: &FrameworkCapability,
        target_moniker: AbsoluteMoniker,
        scope_moniker: AbsoluteMoniker,
        capability: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        match (capability, capability_decl) {
            (None, FrameworkCapability::Protocol(source_path))
                if *source_path == *EVENT_SOURCE_SYNC_SERVICE =>
            {
                let event_source_registry = self.event_source_registry.lock().await;
                if let Some(system) = event_source_registry.get(&target_moniker) {
                    return Ok(Some(Box::new(system.clone()) as Box<dyn CapabilityProvider>));
                } else {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "Unable to find EventSource in registry for {}",
                        scope_moniker
                    )));
                }
            }
            (None, FrameworkCapability::Event(source_name)) => {
                let mut event_source_registry = self.event_source_registry.lock().await;
                let key = target_moniker;
                if event_source_registry.get(&key).is_none() {
                    let event_source = self.create(key.clone()).await;
                    event_source_registry.insert(key.clone(), event_source);
                }
                let event_source = event_source_registry.get_mut(&key).unwrap();
                event_source.allow_event(source_name.clone(), scope_moniker);
                Ok(None)
            }
            (c, _) => return Ok(c),
        }
    }

    #[cfg(test)]
    pub async fn create_for_test(&self, target_moniker: AbsoluteMoniker) -> EventSource {
        let mut event_source = self.create(target_moniker).await;
        event_source.allow_all_events(AbsoluteMoniker::root());
        event_source
    }

    pub async fn is_event_allowed(
        &self,
        target_moniker: &AbsoluteMoniker,
        event_name: &CapabilityName,
        scope_moniker: &AbsoluteMoniker,
    ) -> bool {
        let registry = self.event_source_registry.lock().await;
        registry
            .get(target_moniker)
            .map(|event_source| event_source.is_event_allowed(event_name, scope_moniker))
            .unwrap_or(false)
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
            _ => {}
        }
        Ok(())
    }
}

/// A system responsible for implementing basic events functionality on a scoped realm.
#[derive(Clone)]
pub struct EventSource {
    /// The moniker that identifies the component that requested this event source.
    target_moniker: AbsoluteMoniker,

    /// A shared reference to the event registry used to subscribe and dispatche events.
    registry: Weak<EventRegistry>,

    /// Used for `EventSourceSync.StartComponentTree`.
    // TODO(fxb/48245): this shouldn't be done for any EventSource. Only for tests.
    resolve_instance_event_stream: Arc<Mutex<Option<EventStream>>>,

    /// The set of events this EventSource can subscribe to and their scopes.
    allowed_events: HashMap<CapabilityName, HashSet<AbsoluteMoniker>>,
}

#[derive(Debug, Error, PartialEq)]
pub enum EventsError {
    #[error("Events not allowed for subscription {:?}", names)]
    NotAvailable { names: Vec<String> },

    #[error("Registry not found")]
    RegistryNotFound,
}

impl EventSource {
    /// Creates a new `EventSource` that will be used by the component identified with the given
    /// `target_moniker`.
    pub async fn new(target_moniker: AbsoluteMoniker, registry: &Arc<EventRegistry>) -> Self {
        // TODO(fxb/48245): this shouldn't be done for any EventSource. Only for tests.
        let resolve_instance_event_stream = Arc::new(Mutex::new(Some(
            registry.subscribe(vec![(EventType::Resolved, hashset!(target_moniker.clone()))]).await,
        )));
        Self {
            target_moniker,
            registry: Arc::downgrade(&registry),
            resolve_instance_event_stream,
            allowed_events: HashMap::new(),
        }
    }

    /// Drops the `Resolved` event stream, thereby permitting components within the
    /// realm to be started.
    pub async fn start_component_tree(&mut self) {
        let mut resolve_instance_event_stream = self.resolve_instance_event_stream.lock().await;
        *resolve_instance_event_stream = None;
    }

    /// Subscribes to events provided in the `events` vector if they have been previously allowed
    /// (`allow_event`) on this event source under the scopes they were allowed.
    pub async fn subscribe(&self, events: Vec<EventType>) -> Result<EventStream, EventsError> {
        // The client might request to subscribe to events that it's not allowed to see. Events
        // that are allowed should have been defined in its manifest and either offered to it or
        // requested from the current realm.
        let (allowed, not_allowed) = events.into_iter().fold(
            (Vec::new(), Vec::new()),
            |(mut allowed, mut not_allowed), event| {
                if let Some(scopes) = self.allowed_events.get(&event.to_string().into()) {
                    allowed.push((event, scopes.clone()));
                } else {
                    not_allowed.push(event.to_string());
                }
                (allowed, not_allowed)
            },
        );
        if !not_allowed.is_empty() {
            return Err(EventsError::NotAvailable { names: not_allowed });
        }
        // Create an event stream for the given events
        if let Some(registry) = self.registry.upgrade() {
            return Ok(registry.subscribe(allowed).await);
        }
        Err(EventsError::RegistryNotFound)
    }

    /// Serves a `EventSourceSync` FIDL protocol.
    pub fn serve_async(self, stream: fevents::EventSourceSyncRequestStream) {
        fasync::spawn(async move {
            serve_event_source_sync(self, stream).await;
        });
    }

    /// Allows all events for this event source with the given scope for all of them.
    pub fn allow_all_events(&mut self, scope_moniker: AbsoluteMoniker) {
        for event in EventType::values() {
            self.allow_event(event.to_string().into(), scope_moniker.clone());
        }
    }

    /// Allows an event under the given scope.
    pub fn allow_event(&mut self, source_name: CapabilityName, scope_moniker: AbsoluteMoniker) {
        self.allowed_events.entry(source_name).or_insert(HashSet::new()).insert(scope_moniker);
    }

    /// Returns a reference to the event registry.
    #[cfg(test)]
    pub fn registry(&self) -> Weak<EventRegistry> {
        self.registry.clone()
    }

    pub fn is_event_allowed(
        &self,
        event_name: &CapabilityName,
        scope_moniker: &AbsoluteMoniker,
    ) -> bool {
        self.allowed_events
            .get(event_name)
            .map(|scopes| scopes.contains(scope_moniker))
            .unwrap_or(false)
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
        self.serve_async(stream);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn subscribe_fails_when_not_allowed() {
        let event_source_factory = Arc::new(EventSourceFactory::new());
        let mut event_source = event_source_factory.create(AbsoluteMoniker::root()).await;
        event_source.allow_event(EventType::Started.to_string().into(), AbsoluteMoniker::root());
        let events = vec![EventType::Destroyed, EventType::Stopped];
        let result = event_source.subscribe(events.clone()).await;
        assert!(result.is_err());
        assert_eq!(
            result.err(),
            Some(EventsError::NotAvailable {
                names: vec!["destroyed".to_string(), "stopped".to_string()]
            })
        );
    }
}
