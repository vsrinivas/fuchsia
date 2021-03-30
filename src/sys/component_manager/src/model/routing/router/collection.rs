// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        model::{
            component::{ComponentInstance, ComponentManagerInstance, WeakComponentInstance},
            routing::{
                error::RoutingError,
                router::{
                    find_matching_expose, CapabilityVisitor, ErrorNotFoundFromParent,
                    ErrorNotFoundInChild, Expose, ExposeVisitor, Offer, OfferVisitor,
                    RoutingStrategy, Sources,
                },
            },
        },
    },
    ::routing::capability_source::CollectionCapabilityProvider,
    async_trait::async_trait,
    cm_rust::{CapabilityName, ExposeDecl, ExposeDeclCommon, OfferDecl, OfferDeclCommon},
    from_enum::FromEnum,
    moniker::PartialMoniker,
    std::sync::Arc,
};

/// Routes an offer declaration to its sources within a collection.
pub(super) struct RouteOfferFromCollection<B, O, E, S, V> {
    pub router: RoutingStrategy<B, Offer<O>, Expose<E>>,
    pub collection_component: WeakComponentInstance,
    pub collection_name: String,
    pub offer_decl: O,
    pub sources: S,
    pub visitor: V,
}

#[async_trait]
impl<B, O, E, S, V> CollectionCapabilityProvider<ComponentInstance, ComponentManagerInstance>
    for RouteOfferFromCollection<B, O, E, S, V>
where
    B: Send + Sync + 'static,
    O: OfferDeclCommon
        + ErrorNotFoundFromParent
        + ErrorNotFoundInChild
        + FromEnum<OfferDecl>
        + Into<OfferDecl>
        + Clone
        + 'static,
    E: ExposeDeclCommon
        + ErrorNotFoundInChild
        + FromEnum<ExposeDecl>
        + Into<ExposeDecl>
        + Clone
        + 'static,
    S: Sources + 'static,
    V: OfferVisitor<OfferDecl = O>,
    V: ExposeVisitor<ExposeDecl = E>,
    V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    V: Clone + Send + Sync + 'static,
{
    async fn list_instances(&self) -> Result<Vec<String>, RoutingError> {
        list_instances_impl::<E>(
            &self.collection_component,
            &self.collection_name,
            self.offer_decl.source_name(),
        )
        .await
    }

    async fn route_instance(&self, instance: &str) -> Result<CapabilitySource, RoutingError> {
        let target = self.collection_component.upgrade()?;
        let (child_moniker, child_component): (PartialMoniker, Arc<ComponentInstance>) = {
            let state = target
                .lock_resolved_state()
                .await
                .map_err(|err| RoutingError::resolve_failed(&target.abs_moniker, err))?;
            let x = state
                .live_children_in_collection(&self.collection_name)
                .find_map(
                    |(m, c)| if m.name() == instance { Some((m.clone(), c.clone())) } else { None },
                )
                .ok_or_else(|| RoutingError::OfferFromChildInstanceNotFound {
                    child_moniker: PartialMoniker::new(
                        instance.to_string(),
                        Some(self.collection_name.clone()),
                    ),
                    moniker: target.abs_moniker.clone(),
                    capability_id: self.offer_decl.source_name().clone().into(),
                })?;
            x
        };

        let expose_decl: E = {
            let state = child_component
                .lock_resolved_state()
                .await
                .map_err(|err| RoutingError::resolve_failed(&child_component.abs_moniker, err))?;
            find_matching_expose(self.offer_decl.source_name(), state.decl()).cloned().ok_or_else(
                || {
                    O::error_not_found_in_child(
                        target.abs_moniker.clone(),
                        child_moniker,
                        self.offer_decl.source_name().clone(),
                    )
                },
            )?
        };
        self.router
            .route_from_expose(
                expose_decl,
                child_component,
                self.sources.clone(),
                &mut self.visitor.clone(),
            )
            .await
    }

    fn clone_boxed(
        &self,
    ) -> Box<dyn CollectionCapabilityProvider<ComponentInstance, ComponentManagerInstance>> {
        Box::new(self.clone())
    }
}

// NOTE: Manual implementation of Clone is required, as the derived implementation
// puts a Clone requirement on all generic params, which are used in PhantomData
// and don't need to be Clone.
impl<B, O, E, S, V> Clone for RouteOfferFromCollection<B, O, E, S, V>
where
    O: Clone,
    S: Clone,
    V: Clone,
{
    fn clone(&self) -> Self {
        Self {
            router: self.router.clone(),
            collection_component: self.collection_component.clone(),
            collection_name: self.collection_name.clone(),
            offer_decl: self.offer_decl.clone(),
            sources: self.sources.clone(),
            visitor: self.visitor.clone(),
        }
    }
}

/// Routes an expose declaration to its sources within a collection.
pub(super) struct RouteExposeFromCollection<B, O, E, S, V> {
    pub router: RoutingStrategy<B, O, Expose<E>>,
    pub collection_component: WeakComponentInstance,
    pub collection_name: String,
    pub expose_decl: E,
    pub sources: S,
    pub visitor: V,
}

#[async_trait]
impl<B, O, E, S, V> CollectionCapabilityProvider<ComponentInstance, ComponentManagerInstance>
    for RouteExposeFromCollection<B, O, E, S, V>
where
    B: Send + Sync + 'static,
    O: Send + Sync + 'static,
    E: ExposeDeclCommon
        + ErrorNotFoundInChild
        + FromEnum<ExposeDecl>
        + Into<ExposeDecl>
        + Clone
        + 'static,
    S: Sources + 'static,
    V: ExposeVisitor<ExposeDecl = E>,
    V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    V: Clone + Send + Sync + 'static,
{
    async fn list_instances(&self) -> Result<Vec<String>, RoutingError> {
        list_instances_impl::<E>(
            &self.collection_component,
            &self.collection_name,
            self.expose_decl.source_name(),
        )
        .await
    }

    async fn route_instance(&self, instance: &str) -> Result<CapabilitySource, RoutingError> {
        let target = self.collection_component.upgrade()?;
        let (child_moniker, child_component): (PartialMoniker, Arc<ComponentInstance>) = {
            let state = target
                .lock_resolved_state()
                .await
                .map_err(|err| RoutingError::resolve_failed(&target.abs_moniker, err))?;
            let x = state
                .live_children_in_collection(&self.collection_name)
                .find_map(
                    |(m, c)| if m.name() == instance { Some((m.clone(), c.clone())) } else { None },
                )
                .ok_or_else(|| RoutingError::ExposeFromChildInstanceNotFound {
                    child_moniker: PartialMoniker::new(
                        instance.to_string(),
                        Some(self.collection_name.clone()),
                    ),
                    moniker: target.abs_moniker.clone(),
                    capability_id: self.expose_decl.source_name().clone().into(),
                })?;
            x
        };

        let expose_decl: E = {
            let state = child_component
                .lock_resolved_state()
                .await
                .map_err(|err| RoutingError::resolve_failed(&child_component.abs_moniker, err))?;
            find_matching_expose(self.expose_decl.source_name(), state.decl()).cloned().ok_or_else(
                || {
                    E::error_not_found_in_child(
                        target.abs_moniker.clone(),
                        child_moniker,
                        self.expose_decl.source_name().clone(),
                    )
                },
            )?
        };
        self.router
            .route_from_expose(
                expose_decl,
                child_component,
                self.sources.clone(),
                &mut self.visitor.clone(),
            )
            .await
    }

    fn clone_boxed(
        &self,
    ) -> Box<dyn CollectionCapabilityProvider<ComponentInstance, ComponentManagerInstance>> {
        Box::new(self.clone())
    }
}

// NOTE: Manual implementation of Clone is required, as the derived implementation
// puts a Clone requirement on all generic params, which are used in PhantomData
// and don't need to be Clone.
impl<B, O, E, S, V> Clone for RouteExposeFromCollection<B, O, E, S, V>
where
    E: Clone,
    S: Clone,
    V: Clone,
{
    fn clone(&self) -> Self {
        Self {
            router: self.router.clone(),
            collection_component: self.collection_component.clone(),
            collection_name: self.collection_name.clone(),
            expose_decl: self.expose_decl.clone(),
            sources: self.sources.clone(),
            visitor: self.visitor.clone(),
        }
    }
}

/// Returns a list of instance names, where the names are derived from components in the
/// collection `collection_name` that expose the capability `E` with source name `capability_name`.
async fn list_instances_impl<E>(
    component: &WeakComponentInstance,
    collection_name: &str,
    capability_name: &CapabilityName,
) -> Result<Vec<String>, RoutingError>
where
    E: ExposeDeclCommon + FromEnum<ExposeDecl>,
{
    let mut instances = Vec::new();
    let component = component.upgrade()?;
    let components: Vec<(PartialMoniker, Arc<ComponentInstance>)> = {
        let state = component
            .lock_resolved_state()
            .await
            .map_err(|err| RoutingError::resolve_failed(&component.abs_moniker, err))?;
        state
            .live_children_in_collection(collection_name)
            .map(|(p, c)| (p.clone(), c.clone()))
            .collect()
    };
    for (partial_moniker, component) in components {
        match component.lock_resolved_state().await {
            Ok(state) => {
                if find_matching_expose::<E>(capability_name, state.decl()).is_some() {
                    instances.push(partial_moniker.name().to_string())
                }
            }
            // Ignore errors. One misbehaving component should not affect the entire collection.
            Err(_) => {}
        }
    }
    Ok(instances)
}
