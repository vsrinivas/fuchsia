// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_source::{CapabilitySourceInterface, CollectionCapabilityProvider},
        component_instance::{ComponentInstanceInterface, WeakComponentInstanceInterface},
        error::RoutingError,
        router::{
            find_matching_expose, CapabilityVisitor, ErrorNotFoundFromParent, ErrorNotFoundInChild,
            Expose, ExposeVisitor, Offer, OfferVisitor, RoutingStrategy, Sources,
        },
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, ExposeDecl, ExposeDeclCommon, OfferDecl, OfferDeclCommon},
    derivative::Derivative,
    from_enum::FromEnum,
    moniker::{AbsoluteMonikerBase, ChildMonikerBase, PartialChildMoniker},
    std::sync::Arc,
};

/// Routes an offer declaration to its sources within a collection.
#[derive(Derivative)]
#[derivative(Clone(bound = "O: Clone, S: Clone, V: Clone"))]
pub(super) struct RouteOfferFromCollection<C: ComponentInstanceInterface, B, O, E, S, V> {
    pub router: RoutingStrategy<B, Offer<O>, Expose<E>>,
    pub collection_component: WeakComponentInstanceInterface<C>,
    pub collection_name: String,
    pub offer_decl: O,
    pub sources: S,
    pub visitor: V,
}

#[async_trait]
impl<C, B, O, E, S, V> CollectionCapabilityProvider<C>
    for RouteOfferFromCollection<C, B, O, E, S, V>
where
    C: ComponentInstanceInterface + 'static,
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
        list_instances_impl::<C, E>(
            &self.collection_component,
            &self.collection_name,
            self.offer_decl.source_name(),
        )
        .await
    }

    async fn route_instance(
        &self,
        instance: &str,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError> {
        let target = self.collection_component.upgrade()?;
        let (child_moniker, child_component): (PartialChildMoniker, Arc<C>) = {
            target
                .live_children_in_collection(&self.collection_name)
                .await?
                .into_iter()
                .find_map(move |(m, c)| if m.name() == instance { Some((m, c)) } else { None })
                .ok_or_else(|| RoutingError::OfferFromChildInstanceNotFound {
                    child_moniker: PartialChildMoniker::new(
                        instance.to_string(),
                        Some(self.collection_name.clone()),
                    ),
                    moniker: target.abs_moniker().to_partial(),
                    capability_id: self.offer_decl.source_name().clone().into(),
                })?
        };

        let expose_decl: E = {
            let child_decl = child_component.decl().await?;
            find_matching_expose(self.offer_decl.source_name(), &child_decl).cloned().ok_or_else(
                || {
                    O::error_not_found_in_child(
                        target.abs_moniker().to_partial(),
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

    fn clone_boxed(&self) -> Box<dyn CollectionCapabilityProvider<C>> {
        Box::new(self.clone())
    }
}

/// Routes an expose declaration to its sources within a collection.
#[derive(Derivative)]
#[derivative(Clone(bound = "E: Clone, S: Clone, V: Clone"))]
pub(super) struct RouteExposeFromCollection<C: ComponentInstanceInterface, B, O, E, S, V> {
    pub router: RoutingStrategy<B, O, Expose<E>>,
    pub collection_component: WeakComponentInstanceInterface<C>,
    pub collection_name: String,
    pub expose_decl: E,
    pub sources: S,
    pub visitor: V,
}

#[async_trait]
impl<C, B, O, E, S, V> CollectionCapabilityProvider<C>
    for RouteExposeFromCollection<C, B, O, E, S, V>
where
    C: ComponentInstanceInterface + 'static,
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
        list_instances_impl::<C, E>(
            &self.collection_component,
            &self.collection_name,
            self.expose_decl.source_name(),
        )
        .await
    }

    async fn route_instance(
        &self,
        instance: &str,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError> {
        let target = self.collection_component.upgrade()?;
        let (child_moniker, child_component): (PartialChildMoniker, Arc<C>) = {
            target
                .live_children_in_collection(&self.collection_name)
                .await?
                .into_iter()
                .find_map(move |(m, c)| if m.name() == instance { Some((m, c)) } else { None })
                .ok_or_else(|| RoutingError::ExposeFromChildInstanceNotFound {
                    child_moniker: PartialChildMoniker::new(
                        instance.to_string(),
                        Some(self.collection_name.clone()),
                    ),
                    moniker: target.abs_moniker().to_partial(),
                    capability_id: self.expose_decl.source_name().clone().into(),
                })?
        };

        let expose_decl: E = {
            let child_decl = child_component.decl().await?;
            find_matching_expose(self.expose_decl.source_name(), &child_decl).cloned().ok_or_else(
                || {
                    E::error_not_found_in_child(
                        target.abs_moniker().to_partial(),
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

    fn clone_boxed(&self) -> Box<dyn CollectionCapabilityProvider<C>> {
        Box::new(self.clone())
    }
}

/// Returns a list of instance names, where the names are derived from components in the
/// collection `collection_name` that expose the capability `E` with source name `capability_name`.
async fn list_instances_impl<C, E>(
    component: &WeakComponentInstanceInterface<C>,
    collection_name: &str,
    capability_name: &CapabilityName,
) -> Result<Vec<String>, RoutingError>
where
    C: ComponentInstanceInterface,
    E: ExposeDeclCommon + FromEnum<ExposeDecl>,
{
    let mut instances = Vec::new();
    let component = component.upgrade()?;
    let components: Vec<(PartialChildMoniker, Arc<C>)> =
        component.live_children_in_collection(collection_name).await?;
    for (partial_moniker, component) in components {
        match component.decl().await {
            Ok(decl) => {
                if find_matching_expose::<E>(capability_name, &decl).is_some() {
                    instances.push(partial_moniker.name().to_string())
                }
            }
            // Ignore errors. One misbehaving component should not affect the entire collection.
            Err(_) => {}
        }
    }
    Ok(instances)
}
