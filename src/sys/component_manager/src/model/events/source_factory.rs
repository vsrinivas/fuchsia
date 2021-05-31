// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability},
        model::{
            error::ModelError,
            events::{
                registry::{EventRegistry, ExecutionMode, SubscriptionOptions, SubscriptionType},
                source::EventSource,
                stream_provider::EventStreamProvider,
            },
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
        },
    },
    async_trait::async_trait,
    cm_rust::CapabilityName,
    lazy_static::lazy_static,
    moniker::AbsoluteMoniker,
    std::sync::{Arc, Weak},
};

lazy_static! {
    pub static ref EVENT_SOURCE_SERVICE_NAME: CapabilityName = "fuchsia.sys2.EventSource".into();
}

/// Allows to create `EventSource`s and tracks all the created ones.
pub struct EventSourceFactory {
    /// The component model, needed to route events.
    model: Weak<Model>,

    /// The event registry. It subscribes to all events happening in the system and
    /// routes them to subscribers.
    // TODO(fxbug.dev/48512): instead of using a global registry integrate more with the hooks model.
    event_registry: Weak<EventRegistry>,

    // The static event stream provider.
    event_stream_provider: Weak<EventStreamProvider>,

    execution_mode: ExecutionMode,
}

impl EventSourceFactory {
    pub fn new(
        model: Weak<Model>,
        event_registry: Weak<EventRegistry>,
        event_stream_provider: Weak<EventStreamProvider>,
        execution_mode: ExecutionMode,
    ) -> Self {
        Self { model, event_registry, event_stream_provider, execution_mode }
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

    /// Creates a debug event source.
    pub async fn create_for_debug(&self) -> Result<EventSource, ModelError> {
        EventSource::new_for_debug(
            self.model.clone(),
            self.event_registry.clone(),
            self.event_stream_provider.clone(),
        )
        .await
    }

    /// Creates a `EventSource` for the given `target_moniker`.
    pub async fn create(&self, target_moniker: AbsoluteMoniker) -> Result<EventSource, ModelError> {
        EventSource::new(
            self.model.clone(),
            SubscriptionOptions::new(
                SubscriptionType::Component(target_moniker),
                self.execution_mode.clone(),
            ),
            self.event_registry.clone(),
            self.event_stream_provider.clone(),
        )
        .await
    }

    /// Returns an EventSource. An EventSource holds an AbsoluteMoniker that
    /// corresponds to the component in which it will receive events.
    async fn on_capability_routed_async(
        self: Arc<Self>,
        capability_decl: &InternalCapability,
        target_moniker: AbsoluteMoniker,
        capability: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        if capability_decl.matches_protocol(&EVENT_SOURCE_SERVICE_NAME) {
            let event_source = self.create(target_moniker.clone()).await?;
            Ok(Some(Box::new(event_source.clone()) as Box<dyn CapabilityProvider>))
        } else {
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
            _ => {}
        }
        Ok(())
    }
}
