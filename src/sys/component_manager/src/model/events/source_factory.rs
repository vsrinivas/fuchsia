// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        model::{
            error::ModelError,
            events::{
                registry::EventRegistry,
                source::{EventSource, EventSourceV2},
                stream_provider::EventStreamProvider,
            },
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
        },
    },
    ::routing::capability_source::InternalCapability,
    async_trait::async_trait,
    cm_rust::CapabilityName,
    lazy_static::lazy_static,
    moniker::{AbsoluteMoniker, ExtendedMoniker},
    std::sync::{Arc, Weak},
};

lazy_static! {
    pub static ref EVENT_SOURCE_SERVICE_NAME: CapabilityName = "fuchsia.sys2.EventSource".into();
}

/// Allows to create `EventSource`s and tracks all the created ones.
pub struct EventSourceFactory {
    model: Weak<Model>,

    /// The event registry. It subscribes to all events happening in the system and
    /// routes them to subscribers.
    // TODO(fxbug.dev/48512): instead of using a global registry integrate more with the hooks model.
    event_registry: Weak<EventRegistry>,

    // The static event stream provider.
    event_stream_provider: Weak<EventStreamProvider>,
}

impl EventSourceFactory {
    pub fn new(
        model: Weak<Model>,
        event_registry: Weak<EventRegistry>,
        event_stream_provider: Weak<EventStreamProvider>,
    ) -> Self {
        Self { model, event_registry, event_stream_provider }
    }

    /// Creates the subscription to the required events.
    /// `DirectoryReady` used to track events and associate them with the component that needs them
    /// as well as the scoped that will be allowed. Also the EventSource protocol capability.
    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![
            // This hook provides the EventSource capability to components in the tree
            HooksRegistration::new(
                "EventSourceFactory",
                vec![EventType::CapabilityRouted],
                Arc::downgrade(self) as Weak<dyn Hook>,
            ),
        ]
    }

    /// Creates an event source for an above-root subscriber.
    pub async fn create_for_above_root(&self) -> Result<EventSource, ModelError> {
        EventSource::new_for_above_root(
            self.model.clone(),
            self.event_registry.clone(),
            self.event_stream_provider.clone(),
        )
        .await
    }

    /// Creates a `EventSource` for the given `subscriber`.
    pub async fn create(&self, subscriber: AbsoluteMoniker) -> Result<EventSource, ModelError> {
        EventSource::new(
            ExtendedMoniker::ComponentInstance(subscriber),
            self.model.clone(),
            self.event_registry.clone(),
            self.event_stream_provider.clone(),
        )
        .await
    }

    /// Creates a `EventSource` for the given `subscriber`.
    pub async fn create_v2(
        &self,
        subscriber: AbsoluteMoniker,
        name: CapabilityName,
    ) -> Result<EventSourceV2, ModelError> {
        EventSourceV2::new(self.create(subscriber).await?, name).await
    }

    pub async fn create_v2_for_above_root(&self) -> Result<EventSourceV2, ModelError> {
        EventSourceV2::new(self.create_for_above_root().await?, CapabilityName::from("")).await
    }

    /// Returns an EventSource. An EventSource holds an InstancedAbsoluteMoniker that
    /// corresponds to the component in which it will receive events.
    async fn on_capability_routed_async(
        self: Arc<Self>,
        capability_decl: &InternalCapability,
        target_moniker: AbsoluteMoniker,
        capability: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        if capability_decl.matches_protocol(&EVENT_SOURCE_SERVICE_NAME) {
            let event_source = self.create(target_moniker).await?;
            Ok(Some(Box::new(event_source) as Box<dyn CapabilityProvider>))
        } else {
            match capability_decl {
                InternalCapability::EventStream(name) => {
                    let event_source = self.create_v2(target_moniker, name.clone()).await?;
                    return Ok(Some(Box::new(event_source)));
                }
                _ => {}
            }
            Ok(capability)
        }
    }
}

#[async_trait]
impl Hook for EventSourceFactory {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        let target_moniker = event
            .target_moniker
            .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
        match &event.result {
            Ok(EventPayload::CapabilityRouted {
                source: CapabilitySource::Builtin { capability, .. },
                capability_provider,
            }) => {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_capability_routed_async(
                        &capability,
                        target_moniker.clone(),
                        capability_provider.take(),
                    )
                    .await?;
            }
            Ok(EventPayload::CapabilityRouted {
                source: CapabilitySource::Framework { capability, .. },
                capability_provider,
            }) => {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_capability_routed_async(
                        &capability,
                        target_moniker.clone(),
                        capability_provider.take(),
                    )
                    .await?;
            }
            _ => {}
        }
        Ok(())
    }
}
