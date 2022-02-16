// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        model::{
            component::{ComponentInstance, WeakComponentInstance},
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
            routing::service::CollectionServiceDirectoryProvider,
        },
    },
    async_trait::async_trait,
    cm_moniker::InstancedExtendedMoniker,
    cm_rust::CapabilityTypeName,
    moniker::AbsoluteMoniker,
    routing::capability_source::{AggregateCapability, AggregateCapabilityProvider},
    std::sync::{Arc, Weak},
};

/// Creates `CapabilityProvider`s for capabilities routed from collections.
#[derive(Clone)]
pub struct CollectionCapabilityHost {
    model: Weak<Model>,
}

impl CollectionCapabilityHost {
    pub fn new(model: Weak<Model>) -> Self {
        Self { model }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "CollectionCapabilityHost",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_collection_capability_routed_async(
        self: Arc<Self>,
        source: WeakComponentInstance,
        target_moniker: AbsoluteMoniker,
        aggregate_capability_provider: Box<dyn AggregateCapabilityProvider<ComponentInstance>>,
        capability: &AggregateCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        // If some other capability has already been installed, then there's nothing to do here.
        if capability_provider.is_none() && capability.type_name() == CapabilityTypeName::Service {
            let model = self.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
            let target = WeakComponentInstance::new(&model.look_up(&target_moniker).await?);

            Ok(Some(Box::new(
                CollectionServiceDirectoryProvider::create(
                    target,
                    &source.upgrade()?,
                    aggregate_capability_provider,
                )
                .await?,
            )))
        } else {
            Ok(capability_provider)
        }
    }
}

#[async_trait]
impl Hook for CollectionCapabilityHost {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source:
                CapabilitySource::Collection {
                    component,
                    capability,
                    capability_provider: aggregate_capability_provider,
                    ..
                },
            capability_provider,
        }) = &event.result
        {
            let target_moniker = match &event.target_moniker {
                InstancedExtendedMoniker::ComponentManager => {
                    Err(ModelError::UnexpectedComponentManagerMoniker)
                }
                InstancedExtendedMoniker::ComponentInstance(moniker) => Ok(moniker),
            }?;
            let mut capability_provider = capability_provider.lock().await;
            *capability_provider = self
                .on_collection_capability_routed_async(
                    component.clone(),
                    target_moniker.to_absolute_moniker(),
                    aggregate_capability_provider.clone_boxed(),
                    &capability,
                    capability_provider.take(),
                )
                .await?;
        }
        Ok(())
    }
}
