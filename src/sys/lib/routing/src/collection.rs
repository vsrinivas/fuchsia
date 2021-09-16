// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_source::{CapabilitySourceInterface, CollectionCapabilityProvider},
        component_instance::{ComponentInstanceInterface, WeakComponentInstanceInterface},
        error::RoutingError,
        router::{
            find_matching_expose, CapabilityVisitor, ErrorNotFoundInChild, Expose, ExposeVisitor,
            RoutingStrategy, Sources,
        },
        DebugRouteMapper,
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, ExposeDecl, ExposeDeclCommon, SourceName},
    derivative::Derivative,
    from_enum::FromEnum,
    moniker::{AbsoluteMonikerBase, ChildMonikerBase, PartialChildMoniker},
    std::sync::Arc,
};

/// Provides service capabilities exposed by children in a collection.
///
/// Given a collection and an expose declaration that describes the the service,
/// this provider returns a list of children within the collection that expose
/// the service, and routes to a particular child's exposed service.
///
/// This is used during collection routing to aggregate service instances across
/// all children within the collection.
#[derive(Derivative)]
#[derivative(Clone(bound = "E: Clone, D: Clone, S: Clone, V: Clone, M: Clone"))]
pub(super) struct CollectionServiceProvider<C: ComponentInstanceInterface, U, O, E, D, S, V, M> {
    pub router: RoutingStrategy<U, O, Expose<E>>,

    /// Component that contains the collection.
    pub collection_component: WeakComponentInstanceInterface<C>,

    /// Name of the collection within `collection_component`.
    pub collection_name: String,

    /// Declaration of the service at the routing target.
    ///
    /// This declaration identifies the service to be routed by name.
    pub target_decl: D,

    pub sources: S,
    pub visitor: V,
    pub mapper: M,
}

#[async_trait]
impl<C, U, O, E, D, S, V, M> CollectionCapabilityProvider<C>
    for CollectionServiceProvider<C, U, O, E, D, S, V, M>
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
    D: SourceName + Clone + Send + Sync + 'static,
    S: Sources + 'static,
    V: ExposeVisitor<ExposeDecl = E>,
    V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    V: Clone + Send + Sync + 'static,
    M: DebugRouteMapper + Send + Sync + Clone + 'static,
{
    /// Returns a list of instances of service capabilities in this provider.
    ///
    /// Instances correspond to the names of children in the collection that expose
    /// the service described by `target_decl`. They are *not* instances inside
    /// that service, but rather separate capabilities of the same type exposed by
    /// different children.
    async fn list_instances(&self) -> Result<Vec<String>, RoutingError> {
        list_instances_impl::<C, E>(
            &self.collection_component,
            &self.collection_name,
            self.target_decl.source_name(),
        )
        .await
    }

    /// Returns a `CapabilitySourceInterface` to a service capability exposed by a child.
    ///
    /// `instance` is the name of the child that exposes the service, as returned by
    /// `list_instances`.
    async fn route_instance(
        &self,
        instance: &str,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError> {
        let collection_component = self.collection_component.upgrade()?;
        let (child_moniker, child_component): (PartialChildMoniker, Arc<C>) = {
            collection_component
                .live_children_in_collection(&self.collection_name)
                .await?
                .into_iter()
                .find_map(move |(m, c)| if m.name() == instance { Some((m, c)) } else { None })
                .ok_or_else(|| RoutingError::OfferFromChildInstanceNotFound {
                    child_moniker: PartialChildMoniker::new(
                        instance.to_string(),
                        Some(self.collection_name.clone()),
                    ),
                    moniker: collection_component.abs_moniker().to_partial(),
                    capability_id: self.target_decl.source_name().clone().into(),
                })?
        };

        let expose_decl: E = {
            let child_decl = child_component.decl().await?;
            find_matching_expose(self.target_decl.source_name(), &child_decl).cloned().ok_or_else(
                || {
                    E::error_not_found_in_child(
                        collection_component.abs_moniker().to_partial(),
                        child_moniker,
                        self.target_decl.source_name().clone(),
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
