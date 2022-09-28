// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_source::{AggregateCapabilityProvider, CapabilitySourceInterface},
        component_instance::{
            ComponentInstanceInterface, ResolvedInstanceInterface, WeakComponentInstanceInterface,
        },
        error::RoutingError,
        router::{
            find_matching_expose, CapabilityVisitor, ErrorNotFoundInChild, Expose, ExposeVisitor,
            Offer, OfferVisitor, RoutingStrategy, Sources,
        },
        DebugRouteMapper,
    },
    async_trait::async_trait,
    cm_rust::{
        CapabilityName, ExposeDecl, ExposeDeclCommon, ExposeServiceDecl, OfferServiceDecl,
        ServiceDecl,
    },
    derivative::Derivative,
    from_enum::FromEnum,
    moniker::{ChildMoniker, ChildMonikerBase},
    std::sync::Arc,
};

/// Provides capabilities exposed by children in a collection.
///
/// Given a collection and the name of a capability, this provider returns a list of children
/// within the collection that expose the capability, and routes to a particular child's exposed
/// capability with that name.
///
/// This is used during collection routing to aggregate service instances across
/// all children within the collection.
#[derive(Derivative)]
#[derivative(Clone(bound = "E: Clone, S: Clone, V: Clone, M: Clone"))]
pub(super) struct CollectionCapabilityProvider<C: ComponentInstanceInterface, U, O, E, S, V, M> {
    pub router: RoutingStrategy<U, O, Expose<E>>,

    /// Component that contains the collection.
    pub collection_component: WeakComponentInstanceInterface<C>,

    /// Name of the collection within `collection_component`.
    pub collection_name: String,

    /// Name of the capability as exposed by children in the collection.
    pub capability_name: CapabilityName,

    pub sources: S,
    pub visitor: V,
    pub mapper: M,
}

#[async_trait]
impl<C, U, O, E, S, V, M> AggregateCapabilityProvider<C>
    for CollectionCapabilityProvider<C, U, O, E, S, V, M>
where
    C: ComponentInstanceInterface + 'static,
    U: Send + Sync + 'static,
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
    M: DebugRouteMapper + Send + Sync + Clone + 'static,
{
    /// Returns a list of instances of capabilities in this provider.
    ///
    /// Instances correspond to the names of children in the collection that expose the capability
    /// with the name `capability_name`.
    ///
    /// In the case of service capabilities, they are *not* instances inside that service, but
    /// rather service capabilities with the same name that are exposed by different children.
    async fn list_instances(&self) -> Result<Vec<String>, RoutingError> {
        let mut instances = Vec::new();
        let component = self.collection_component.upgrade()?;
        let components: Vec<(ChildMoniker, Arc<C>)> =
            component.lock_resolved_state().await?.children_in_collection(&self.collection_name);
        for (moniker, child_component) in components {
            let child_exposes = child_component.lock_resolved_state().await.map(|c| c.exposes());
            match child_exposes {
                Ok(child_exposes) => {
                    if find_matching_expose::<E>(&self.capability_name, &child_exposes).is_some() {
                        instances.push(moniker.name().to_string())
                    }
                }
                // Ignore errors. One misbehaving component should not affect the entire collection.
                Err(_) => {}
            }
        }
        Ok(instances)
    }

    /// Returns a `CapabilitySourceInterface` to a capability exposed by a child.
    ///
    /// `instance` is the name of the child that exposes the capability, as returned by
    /// `list_instances`.
    async fn route_instance(
        &self,
        instance: &str,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError> {
        let collection_component = self.collection_component.upgrade()?;
        let (child_moniker, child_component): (ChildMoniker, Arc<C>) = {
            collection_component
                .lock_resolved_state()
                .await?
                .children_in_collection(&self.collection_name)
                .into_iter()
                .find(|child| child.0.name() == instance)
                .ok_or_else(|| RoutingError::OfferFromChildInstanceNotFound {
                    child_moniker: ChildMoniker::new(instance, Some(&self.collection_name)),
                    moniker: collection_component.abs_moniker().clone(),
                    capability_id: self.capability_name.clone().into(),
                })?
        };

        let expose_decl: E = {
            let child_exposes = child_component.lock_resolved_state().await?.exposes();
            find_matching_expose(&self.capability_name, &child_exposes).cloned().ok_or_else(
                || {
                    E::error_not_found_in_child(
                        collection_component.abs_moniker().clone(),
                        child_moniker,
                        self.capability_name.clone(),
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
                &mut self.mapper.clone(),
            )
            .await
    }

    fn clone_boxed(&self) -> Box<dyn AggregateCapabilityProvider<C>> {
        Box::new(self.clone())
    }
}

#[derive(Derivative)]
#[derivative(Clone(bound = "S: Clone, M: Clone, V: Clone"))]
pub(super) struct AggregateServiceProvider<C: ComponentInstanceInterface, U, S, M, V> {
    router: RoutingStrategy<U, Offer<OfferServiceDecl>, Expose<ExposeServiceDecl>>,

    /// Component that offered the aggregate service
    component: WeakComponentInstanceInterface<C>,

    /// List of offer decl to follow for routing each service provider used in the overall aggregation
    offer_decls: Vec<OfferServiceDecl>,

    sources: S,
    visitor: V,
    mapper: M,
}

impl<C, U, S, M, V> AggregateServiceProvider<C, U, S, M, V>
where
    C: ComponentInstanceInterface + 'static,
    U: Send + Sync + 'static,
    S: Sources<CapabilityDecl = ServiceDecl> + 'static,
    V: OfferVisitor<OfferDecl = OfferServiceDecl>
        + ExposeVisitor<ExposeDecl = ExposeServiceDecl>
        + CapabilityVisitor<CapabilityDecl = ServiceDecl>,
    V: Send + Sync + Clone + 'static,
    M: DebugRouteMapper + Send + Sync + Clone + 'static,
{
    pub(super) fn new(
        offer_service_decls: Vec<OfferServiceDecl>,
        component: WeakComponentInstanceInterface<C>,
        router: RoutingStrategy<U, Offer<OfferServiceDecl>, Expose<ExposeServiceDecl>>,
        sources: S,
        visitor: V,
        mapper: M,
    ) -> Self {
        let single_instance_decls: Vec<OfferServiceDecl> = offer_service_decls
            .iter()
            .map(|o| {
                o.source_instance_filter.iter().flatten().map(move |instance_name| {
                    // Create a service decl for each filtered service instance so that when the aggregate
                    // capability routes an instance each entry can ignore the component name.
                    let mut single_instance_decl = o.clone();
                    single_instance_decl.source_instance_filter = Some(vec![instance_name.clone()]);
                    single_instance_decl
                })
            })
            .flatten()
            .collect();
        Self { router, offer_decls: single_instance_decls, sources, visitor, component, mapper }
    }
}

#[async_trait]
impl<C, U, S, M, V> AggregateCapabilityProvider<C> for AggregateServiceProvider<C, U, S, M, V>
where
    C: ComponentInstanceInterface + 'static,
    U: Send + Sync + 'static,
    S: Sources<CapabilityDecl = ServiceDecl> + 'static,
    V: OfferVisitor<OfferDecl = OfferServiceDecl>
        + ExposeVisitor<ExposeDecl = ExposeServiceDecl>
        + CapabilityVisitor<CapabilityDecl = ServiceDecl>,
    V: Send + Sync + Clone + 'static,
    M: DebugRouteMapper + Send + Sync + Clone + 'static,
{
    async fn list_instances(&self) -> Result<Vec<String>, RoutingError> {
        // Returns the offer source string as the instance name. e.g child instance name, collection name, "parent", "framework", etc.
        Ok(self
            .offer_decls
            .iter()
            .flat_map(|o| o.clone().source_instance_filter.unwrap_or(Vec::new()))
            .collect())
    }

    async fn route_instance(
        &self,
        instance: &str,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError> {
        for offer_decl in &self.offer_decls {
            if offer_decl
                .source_instance_filter
                .as_ref()
                .map(|allowed_instances| allowed_instances.contains(&instance.to_string()))
                .unwrap_or(false)
            {
                return self
                    .router
                    .route_from_offer(
                        offer_decl.clone(),
                        self.component.upgrade().map_err(|e| {
                            RoutingError::unsupported_route_source(format!(
                                "error upgrading aggregation point component {}",
                                e
                            ))
                        })?,
                        self.sources.clone(),
                        &mut self.visitor.clone(),
                        &mut self.mapper.clone(),
                    )
                    .await;
            }
        }
        Err(RoutingError::unsupported_route_source(format!("instance '{}' not found", instance)))
    }

    fn clone_boxed(&self) -> Box<dyn AggregateCapabilityProvider<C>> {
        Box::new(self.clone())
    }
}
