// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        availability::AvailabilityServiceVisitor,
        capability_source::{
            AggregateCapability, CapabilitySourceInterface, ComponentCapability, InternalCapability,
        },
        collection::{AggregateServiceProvider, CollectionCapabilityProvider},
        component_instance::{
            ComponentInstanceInterface, ExtendedInstanceInterface, ResolvedInstanceInterface,
            TopInstanceInterface,
        },
        error::RoutingError,
        DebugRouteMapper, RegistrationDecl,
    },
    cm_rust::{
        name_mappings_to_map, Availability, CapabilityDecl, CapabilityDeclCommon, CapabilityName,
        ExposeDecl, ExposeDeclCommon, ExposeServiceDecl, ExposeSource, ExposeTarget, OfferDecl,
        OfferDeclCommon, OfferServiceDecl, OfferSource, OfferTarget, RegistrationDeclCommon,
        RegistrationSource, ServiceDecl, UseDecl, UseDeclCommon, UseServiceDecl, UseSource,
    },
    derivative::Derivative,
    from_enum::FromEnum,
    moniker::{AbsoluteMoniker, ChildMoniker, ChildMonikerBase},
    std::collections::{HashMap, HashSet},
    std::{marker::PhantomData, sync::Arc},
};

/// Routes a capability to its source based on a particular routing strategy.
///
/// Callers invoke builder-like methods to construct the routing strategy.
///
/// # Example
/// ```
/// let router = RoutingStrategy::new()
///     .use_::<UseProtocolDecl>()
///     .offer::<OfferProtocolDecl>()
///     .expose::<ExposeProtocolDecl>();
/// ```
#[derive(Derivative)]
#[derivative(Clone(bound = ""), Copy(bound = ""))]
pub struct RoutingStrategy<Start = (), Offer = (), Expose = ()>(
    PhantomData<(Start, Offer, Expose)>,
);

impl RoutingStrategy {
    /// Creates a new `Router` that must be configured with a routing strategy by
    /// calling the various builder-like methods.
    pub const fn new() -> Self {
        RoutingStrategy(PhantomData)
    }

    /// Configure the `Router` to start routing from a `Use` declaration.
    pub const fn use_<U>(self) -> RoutingStrategy<Use<U>, (), ()> {
        RoutingStrategy(PhantomData)
    }

    /// Configure the `Router` to start routing from an environment `Registration`
    /// declaration.
    pub const fn registration<R>(self) -> RoutingStrategy<Registration<R>, (), ()> {
        RoutingStrategy(PhantomData)
    }
}

// Implement the Use routing strategy. In this strategy, there is neither no
// Expose nor Offer. This strategy allows components to route capabilities to
// themselves.
impl<U> RoutingStrategy<Use<U>, (), ()>
where
    U: UseDeclCommon + ErrorNotFoundFromParent + Into<UseDecl> + Clone,
{
    /// Configure the `Router` to route from a `Use` declaration to a matching `Offer`
    /// declaration.
    pub fn offer<O>(self) -> RoutingStrategy<Use<U>, Offer<O>, ()> {
        RoutingStrategy(PhantomData)
    }

    /// Routes a capability from its `Use` declaration to its source by
    /// capabilities declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Capability` declaration if `sources`
    /// permits.
    pub async fn route<C, S, V, M>(
        self,
        use_decl: U,
        target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        mapper.add_use(target.abs_moniker().clone(), use_decl.clone().into());
        let target_capabilities = target.lock_resolved_state().await?.capabilities();
        Ok(CapabilitySourceInterface::<C>::Component {
            capability: sources.find_component_source(
                use_decl.source_name(),
                target.abs_moniker(),
                &target_capabilities,
                visitor,
                mapper,
            )?,
            component: target.as_weak(),
        })
    }
}

impl<R> RoutingStrategy<Registration<R>, (), ()> {
    /// Configure the `Router` to route from an environment `Registration` declaration to a
    /// matching `Offer` declaration.
    pub const fn offer<O>(self) -> RoutingStrategy<Registration<R>, Offer<O>, ()> {
        RoutingStrategy(PhantomData)
    }
}

impl<S, O> RoutingStrategy<S, Offer<O>, ()> {
    /// Configure the `Router` to route from an `Offer` declaration to a matching `Expose`
    /// declaration.
    pub const fn expose<E>(self) -> RoutingStrategy<S, Offer<O>, Expose<E>> {
        RoutingStrategy(PhantomData)
    }
}

// Implement the Use -> Offer routing strategy. In this strategy, there is no Expose.
impl<U, O> RoutingStrategy<Use<U>, Offer<O>, ()>
where
    U: UseDeclCommon + ErrorNotFoundFromParent + Into<UseDecl> + Clone,
    O: OfferDeclCommon + ErrorNotFoundFromParent + FromEnum<OfferDecl> + Into<OfferDecl> + Clone,
{
    pub async fn route<C, S, V, M>(
        self,
        use_decl: U,
        use_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        M: DebugRouteMapper + 'static,
    {
        let mut route = vec![];
        self.route_extended_strategy(use_decl, use_target, sources, visitor, mapper, &mut route)
            .await
    }

    /// Routes a capability from its `Use` declaration to its source by following `Offer`
    /// declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` declaration in the routing path, as well as the final
    /// `Capability` declaration if `sources` permits.
    pub async fn route_extended_strategy<C, S, V, M>(
        self,
        use_decl: U,
        use_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
        route: &mut Vec<Arc<C>>,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        M: DebugRouteMapper + 'static,
    {
        let availability = use_decl.availability().clone();
        match Use::route(use_decl, use_target, &sources, visitor, mapper).await? {
            UseResult::Source(source) => return Ok(source),
            UseResult::FromParent(offers, component) => {
                if offers.len() == 1 {
                    let offer_opt: Option<&O> = offers.first();
                    self.route_from_offer(
                        offer_opt.unwrap().clone(),
                        component,
                        sources,
                        visitor,
                        mapper,
                        route,
                    )
                    .await
                } else {
                    create_aggregate_source(availability, offers, component, mapper.clone())
                }
            }
            UseResult::FromChild(_use_decl, _component) => {
                unreachable!("found use from child but capability cannot be exposed")
            }
        }
    }

    /// Routes a capability from its `Offer` declaration to its source by following `Offer`
    /// declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` declaration in the routing path, as well as the final
    /// `Capability` declaration if `sources` permits.
    pub async fn route_from_offer<C, S, V, M>(
        self,
        offer_decl: O,
        offer_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
        route: &mut Vec<Arc<C>>,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        M: DebugRouteMapper,
    {
        match Offer::route(offer_decl, offer_target, &sources, visitor, mapper, route).await? {
            OfferResult::Source(source) => Ok(source),
            OfferResult::OfferFromChild(_, _) => {
                // This condition should not happen since cm_fidl_validator ensures
                // that this kind of declaration cannot exist.
                unreachable!("found offer from child but capability cannot be exposed")
            }
            OfferResult::OfferFromCollection(_, _, _) => {
                // This condition should not happen since cm_fidl_validator ensures
                // that this kind of declaration cannot exist.
                unreachable!("found offer from collection but capability cannot be exposed")
            }
            OfferResult::OfferFromAggregate(_, _) => {
                // This condition should not happen since cm_fidl_validator ensures
                // that this kind of declaration cannot exist.
                unreachable!("found offer from aggregate but capability cannot be exposed")
            }
        }
    }
}

// Implement the Use -> Offer -> Expose routing strategy. This is the full, common routing
// strategy.
impl<U, O, E> RoutingStrategy<Use<U>, Offer<O>, Expose<E>>
where
    U: UseDeclCommon
        + ErrorNotFoundFromParent
        + ErrorNotFoundInChild
        + Into<UseDecl>
        + Clone
        + 'static,
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
{
    pub async fn route<C, S, V, M>(
        self,
        use_decl: U,
        use_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        let mut route = vec![];
        self.route_extended_strategy(use_decl, use_target, sources, visitor, mapper, &mut route)
            .await
    }

    /// Routes a capability from its `Use` declaration to its source by following `Offer` and
    /// `Expose` declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` and `Expose` declaration in the routing path, as well
    /// as the final `Capability` declaration if `sources` permits.
    pub async fn route_extended_strategy<C, S, V, M>(
        self,
        use_decl: U,
        use_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
        route: &mut Vec<Arc<C>>,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        let availability = use_decl.availability().clone();
        match Use::route(use_decl, use_target.clone(), &sources, visitor, mapper).await? {
            UseResult::Source(source) => return Ok(source),
            UseResult::FromParent(offers, component) => {
                if offers.len() == 1 {
                    let offer_opt: Option<&O> = offers.first();
                    self.route_from_offer_extended(
                        offer_opt.unwrap().clone(),
                        component,
                        sources,
                        visitor,
                        mapper,
                        route,
                    )
                    .await
                } else {
                    create_aggregate_source(availability, offers, component, mapper.clone())
                }
            }
            UseResult::FromChild(use_decl, child_component) => {
                let child_exposes = child_component.lock_resolved_state().await?.exposes();
                let expose_decl = find_matching_expose(use_decl.source_name(), &child_exposes)
                    .cloned()
                    .ok_or_else(|| {
                        let child_moniker =
                            child_component.child_moniker().expect("ChildMoniker should exist");
                        <U as ErrorNotFoundInChild>::error_not_found_in_child(
                            use_target.abs_moniker().clone(),
                            child_moniker.clone(),
                            use_decl.source_name().clone(),
                        )
                    })?;
                self.route_from_expose_extended(
                    expose_decl,
                    child_component,
                    sources,
                    visitor,
                    mapper,
                    route,
                )
                .await
            }
        }
    }
}

// Implement the Registration -> Offer -> Expose routing strategy. This is the strategy used
// to route capabilities registered in environments.
impl<R, O, E> RoutingStrategy<Registration<R>, Offer<O>, Expose<E>>
where
    R: RegistrationDeclCommon
        + ErrorNotFoundFromParent
        + ErrorNotFoundInChild
        + Into<RegistrationDecl>
        + Clone
        + 'static,
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
{
    pub async fn route<C, S, V, M>(
        self,
        registration_decl: R,
        registration_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        let mut route = vec![];
        self.route_extended_strategy(
            registration_decl,
            registration_target,
            sources,
            visitor,
            mapper,
            &mut route,
        )
        .await
    }

    /// Routes a capability from its environment `Registration` declaration to its source by
    /// following `Offer` and `Expose` declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` and `Expose` declaration in the routing path, as
    /// well as the final `Capability` declaration if `sources` permits.
    pub async fn route_extended_strategy<C, S, V, M>(
        self,
        registration_decl: R,
        registration_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
        route: &mut Vec<Arc<C>>,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        match Registration::route(registration_decl, registration_target, &sources, visitor, mapper)
            .await?
        {
            RegistrationResult::Source(source) => return Ok(source),
            RegistrationResult::FromParent(offers, component) => {
                if offers.len() == 1 {
                    let offer_opt: Option<&O> = offers.first();
                    self.route_from_offer_extended(
                        offer_opt.unwrap().clone(),
                        component,
                        sources,
                        visitor,
                        mapper,
                        route,
                    )
                    .await
                } else {
                    // capabilities used in a registgration are always required.
                    create_aggregate_source(
                        Availability::Required,
                        offers,
                        component,
                        mapper.clone(),
                    )
                }
            }
            RegistrationResult::FromChild(expose, component) => {
                self.route_from_expose_extended(expose, component, sources, visitor, mapper, route)
                    .await
            }
        }
    }
}

// Common offer routing shared between Registration and Use routing.
impl<B, O, E> RoutingStrategy<B, Offer<O>, Expose<E>>
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
{
    pub async fn route_from_offer<C, S, V, M>(
        self,
        offer_decl: O,
        offer_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        let mut route = vec![];
        self.route_from_offer_extended(
            offer_decl,
            offer_target,
            sources,
            visitor,
            mapper,
            &mut route,
        )
        .await
    }

    /// Routes a capability from its `Offer` declaration to its source by following `Offer` and
    /// `Expose` declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` and `Expose` declaration in the routing path, as well
    /// as the final `Capability` declaration if `sources` permits.
    pub async fn route_from_offer_extended<C, S, V, M>(
        self,
        offer_decl: O,
        offer_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
        route: &mut Vec<Arc<C>>,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        match Offer::route(offer_decl, offer_target, &sources, visitor, mapper, route).await? {
            OfferResult::Source(source) => return Ok(source),
            OfferResult::OfferFromChild(offer, component) => {
                let offer_decl: OfferDecl = offer.clone().into();

                let (expose, component) = change_directions(offer, component).await?;

                let capability_source = self
                    .route_from_expose_extended(expose, component, sources, visitor, mapper, route)
                    .await?;
                if let OfferDecl::Service(offer_service_decl) = offer_decl {
                    if offer_service_decl.source_instance_filter.is_some()
                        || offer_service_decl.renamed_instances.is_some()
                    {
                        // TODO(https://fxbug.dev/97147) support collection sources as well.
                        if let CapabilitySourceInterface::Component { capability, component } =
                            capability_source
                        {
                            return Ok(CapabilitySourceInterface::<C>::FilteredService {
                                capability: capability,
                                component: component,
                                source_instance_filter: offer_service_decl
                                    .source_instance_filter
                                    .unwrap_or(vec![]),
                                instance_name_source_to_target: offer_service_decl
                                    .renamed_instances
                                    .map_or(HashMap::new(), name_mappings_to_map),
                            });
                        }
                    }
                }
                Ok(capability_source)
            }
            OfferResult::OfferFromCollection(offer_decl, collection_component, collection_name) => {
                Ok(CapabilitySourceInterface::<C>::Collection {
                    capability: AggregateCapability::Service(offer_decl.source_name().clone()),
                    component: collection_component.as_weak(),
                    capability_provider: Box::new(CollectionCapabilityProvider {
                        router: self,
                        collection_name: collection_name.clone(),
                        collection_component: collection_component.as_weak(),
                        capability_name: offer_decl.source_name().clone(),
                        sources: sources.clone(),
                        visitor: visitor.clone(),
                        mapper: mapper.clone(),
                    }),
                    collection_name,
                })
            }
            OfferResult::OfferFromAggregate(offer_decls, aggregation_component) => {
                // TODO(102532): get optional to work with aggregate services
                create_aggregate_source(
                    Availability::Required,
                    offer_decls,
                    aggregation_component,
                    mapper.clone(),
                )
            }
        }
    }
}

fn create_aggregate_source<C, O, M>(
    starting_availability: Availability,
    offer_decls: Vec<O>,
    aggregation_component: Arc<C>,
    mapper: M,
) -> Result<CapabilitySourceInterface<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
    O: OfferDeclCommon + FromEnum<OfferDecl> + Into<OfferDecl> + Clone,
    M: DebugRouteMapper + 'static,
{
    // Check that all of the service offers contain non-conflicting filter instances.
    {
        let mut seen_instances: HashSet<String> = HashSet::new();
        for o in offer_decls.iter() {
            if let OfferDecl::Service(offer_service_decl) = o.clone().into() {
                match offer_service_decl.source_instance_filter {
                    None => {
                        return Err(RoutingError::unsupported_route_source(
                            "Aggregate offers must be of service capabilities with source_instance_filter set",
                        ));
                    }
                    Some(allowed_instances) => {
                        for instance in allowed_instances.iter() {
                            if !seen_instances.insert(instance.clone()) {
                                return Err(RoutingError::unsupported_route_source(format!(
                                    "Instance {} found in multiple offers of the same service.",
                                    instance
                                )));
                            }
                        }
                    }
                }
            } else {
                return Err(RoutingError::unsupported_route_source(
                    "Aggregate source must consist of only service capabilities",
                ));
            }
        }
    }
    let (source_name, offer_service_decls) = offer_decls.iter().fold(
        (CapabilityName("".to_string()), Vec::<OfferServiceDecl>::new()),
        |(_, mut decls), o| {
            if let OfferDecl::Service(offer_service_decl) = o.clone().into() {
                decls.push(offer_service_decl);
            }
            (o.source_name().clone(), decls)
        },
    );
    // TODO(fxbug.dev/71881) Make the Collection CapabilitySourceInterface type generic
    // for other types of aggregations.
    Ok(CapabilitySourceInterface::<C>::Aggregate {
        capability: AggregateCapability::Service(source_name),
        component: aggregation_component.as_weak(),
        capability_provider: Box::new(AggregateServiceProvider::new(
            offer_service_decls,
            aggregation_component.as_weak(),
            RoutingStrategy::new()
                .use_::<UseServiceDecl>()
                .offer::<OfferServiceDecl>()
                .expose::<ExposeServiceDecl>(),
            AllowedSourcesBuilder::<ServiceDecl>::new().component().collection(),
            AvailabilityServiceVisitor(starting_availability.into()),
            mapper,
        )),
    })
}

// Common expose routing shared between Registration and Use routing.
impl<B, O, E> RoutingStrategy<B, O, Expose<E>>
where
    B: Send + Sync + 'static,
    O: Send + Sync + 'static,
    E: ExposeDeclCommon
        + ErrorNotFoundInChild
        + FromEnum<ExposeDecl>
        + Into<ExposeDecl>
        + Clone
        + 'static,
{
    pub async fn route_from_expose<C, S, V, M>(
        self,
        expose_decl: E,
        expose_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        let mut route = vec![];
        self.route_from_expose_extended(
            expose_decl,
            expose_target,
            sources,
            visitor,
            mapper,
            &mut route,
        )
        .await
    }

    /// Routes a capability from its `Expose` declaration to its source by following `Expose`
    /// declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Expose` declaration in the routing path, as well
    /// as the final `Capability` declaration if `sources` permits.
    pub async fn route_from_expose_extended<C, S, V, M>(
        self,
        expose_decl: E,
        expose_target: Arc<C>,
        sources: S,
        visitor: &mut V,
        mapper: &mut M,
        route: &mut Vec<Arc<C>>,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
        M: DebugRouteMapper + 'static,
    {
        match Expose::route(expose_decl, expose_target, &sources, visitor, mapper, route).await? {
            ExposeResult::Source(source) => Ok(source),
            ExposeResult::ExposeFromCollection(
                expose_decl,
                collection_component,
                collection_name,
            ) => Ok(CapabilitySourceInterface::<C>::Collection {
                capability: AggregateCapability::Service(expose_decl.source_name().clone()),
                component: collection_component.as_weak(),
                capability_provider: Box::new(CollectionCapabilityProvider {
                    router: self,
                    collection_name: collection_name.clone(),
                    collection_component: collection_component.as_weak(),
                    capability_name: expose_decl.source_name().clone(),
                    sources: sources.clone(),
                    visitor: visitor.clone(),
                    mapper: mapper.clone(),
                }),
                collection_name: collection_name.clone(),
            }),
        }
    }
}

/// A trait for extracting the source of a capability.
pub trait Sources: Clone + Send + Sync {
    /// The supported capability declaration type for namespace, component and built-in sources.
    type CapabilityDecl;

    /// Return the [`InternalCapability`] representing this framework capability source, or
    /// [`RoutingError::UnsupportedRouteSource`] if unsupported.
    fn framework_source<M>(
        &self,
        name: CapabilityName,
        mapper: &mut M,
    ) -> Result<InternalCapability, RoutingError>
    where
        M: DebugRouteMapper;

    /// Checks whether capability sources are supported, returning [`RoutingError::UnsupportedRouteSource`]
    /// if they are not.
    // TODO(fxb/61861): Add route mapping for capability sources.
    fn capability_source(&self) -> Result<(), RoutingError>;

    /// Checks whether collection sources are supported, returning [`ModelError::Unsupported`]
    /// if they are not.
    // TODO(fxb/61861): Consider adding route mapping for collection sources, although we won't need this
    // for the route static analyzer.
    fn collection_source(&self) -> Result<(), RoutingError>;

    /// Checks whether namespace capability sources are supported.
    fn is_namespace_supported(&self) -> bool;

    /// Looks for a namespace capability in the list of capability sources.
    /// If found, the declaration is visited by `visitor` and the declaration is wrapped
    /// in a [`ComponentCapability`].
    /// Returns [`RoutingError::UnsupportedRouteSource`] if namespace capabilities are unsupported.
    fn find_namespace_source<V, M>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<Option<ComponentCapability>, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
        M: DebugRouteMapper;

    /// Looks for a built-in capability in the list of capability sources.
    /// If found, the capability's name is wrapped in an [`InternalCapability`].
    /// Returns [`RoutingError::UnsupportedRouteSource`] if built-in capabilities are unsupported.
    fn find_builtin_source<V, M>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<Option<InternalCapability>, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
        M: DebugRouteMapper;

    /// Looks for a component capability in the list of capability sources for the component instance
    /// with moniker `abs_moniker`.
    /// If found, the declaration is visited by `visitor` and the declaration is wrapped
    /// in a [`ComponentCapability`].
    /// Returns [`RoutingError::UnsupportedRouteSource`] if component capabilities are unsupported.
    fn find_component_source<V, M>(
        &self,
        name: &CapabilityName,
        abs_moniker: &AbsoluteMoniker,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<ComponentCapability, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
        M: DebugRouteMapper;
}

/// Defines which capability source types are supported.
#[derive(Derivative)]
#[derivative(Clone(bound = ""))]
pub struct AllowedSourcesBuilder<C>
where
    C: CapabilityDeclCommon
        + FromEnum<CapabilityDecl>
        + Into<ComponentCapability>
        + Into<InternalCapability>
        + Clone,
{
    framework: Option<fn(CapabilityName) -> InternalCapability>,
    builtin: bool,
    capability: bool,
    collection: bool,
    namespace: bool,
    component: bool,
    _decl: PhantomData<C>,
}

impl<
        C: CapabilityDeclCommon
            + FromEnum<CapabilityDecl>
            + Into<ComponentCapability>
            + Into<InternalCapability>
            + Clone,
    > AllowedSourcesBuilder<C>
{
    /// Creates a new [`AllowedSourcesBuilder`] that does not allow any capability source types.
    pub fn new() -> Self {
        Self {
            framework: None,
            builtin: false,
            capability: false,
            collection: false,
            namespace: false,
            component: false,
            _decl: PhantomData,
        }
    }

    /// Allows framework capability source types (`from: "framework"` in `CML`).
    pub fn framework(self, builder: fn(CapabilityName) -> InternalCapability) -> Self {
        Self { framework: Some(builder), ..self }
    }

    /// Allows capability source types that originate from other capabilities (`from: "#storage"` in
    /// `CML`).
    pub fn capability(self) -> Self {
        Self { capability: true, ..self }
    }

    /// Allows capability sources to originate from a collection.
    pub fn collection(self) -> Self {
        Self { collection: true, ..self }
    }

    /// Allows namespace capability source types, which are capabilities that are installed in
    /// component_manager's incoming namespace.
    pub fn namespace(self) -> Self {
        Self { namespace: true, ..self }
    }

    /// Allows component capability source types (`from: "self"` in `CML`).
    pub fn component(self) -> Self {
        Self { component: true, ..self }
    }

    /// Allows built-in capability source types (`from: "parent"` in `CML` where the parent component_instance is
    /// component_manager).
    pub fn builtin(self) -> Self {
        Self { builtin: true, ..self }
    }
}

// Implementation of `Sources` that allows namespace, component, and/or built-in source
// types.
impl<C> Sources for AllowedSourcesBuilder<C>
where
    C: CapabilityDeclCommon
        + FromEnum<CapabilityDecl>
        + Into<ComponentCapability>
        + Into<InternalCapability>
        + Into<CapabilityDecl>
        + Clone,
{
    type CapabilityDecl = C;

    fn framework_source<M>(
        &self,
        name: CapabilityName,
        mapper: &mut M,
    ) -> Result<InternalCapability, RoutingError>
    where
        M: DebugRouteMapper,
    {
        let source = self
            .framework
            .as_ref()
            .map(|b| b(name.clone()))
            .ok_or_else(|| RoutingError::unsupported_route_source("framework"));
        mapper.add_framework_capability(name);
        source
    }

    fn capability_source(&self) -> Result<(), RoutingError> {
        if self.capability {
            Ok(())
        } else {
            Err(RoutingError::unsupported_route_source("capability"))
        }
    }

    fn collection_source(&self) -> Result<(), RoutingError> {
        if self.collection {
            Ok(())
        } else {
            Err(RoutingError::unsupported_route_source("collection"))
        }
    }

    fn is_namespace_supported(&self) -> bool {
        self.namespace
    }

    fn find_namespace_source<V, M>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<Option<ComponentCapability>, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
        M: DebugRouteMapper,
    {
        if self.namespace {
            if let Some(decl) = capabilities
                .iter()
                .flat_map(FromEnum::from_enum)
                .find(|decl: &&C| decl.name() == name)
                .cloned()
            {
                visitor.visit(&decl)?;
                mapper.add_namespace_capability(decl.clone().into());
                Ok(Some(decl.into()))
            } else {
                Ok(None)
            }
        } else {
            Err(RoutingError::unsupported_route_source("namespace"))
        }
    }

    fn find_builtin_source<V, M>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<Option<InternalCapability>, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
        M: DebugRouteMapper,
    {
        if self.builtin {
            if let Some(decl) = capabilities
                .iter()
                .flat_map(FromEnum::from_enum)
                .find(|decl: &&C| decl.name() == name)
                .cloned()
            {
                visitor.visit(&decl)?;
                mapper.add_builtin_capability(decl.clone().into());
                Ok(Some(decl.into()))
            } else {
                Ok(None)
            }
        } else {
            Err(RoutingError::unsupported_route_source("built-in"))
        }
    }

    fn find_component_source<V, M>(
        &self,
        name: &CapabilityName,
        abs_moniker: &AbsoluteMoniker,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<ComponentCapability, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
        M: DebugRouteMapper,
    {
        if self.component {
            let decl = capabilities
                .iter()
                .flat_map(FromEnum::from_enum)
                .find(|decl: &&C| decl.name() == name)
                .cloned()
                .expect("CapabilityDecl missing, FIDL validation should catch this");
            visitor.visit(&decl)?;
            mapper.add_component_capability(abs_moniker.clone(), decl.clone().into());
            Ok(decl.into())
        } else {
            Err(RoutingError::unsupported_route_source("component"))
        }
    }
}

/// The `Use` phase of routing.
pub struct Use<U>(PhantomData<U>);

/// The result of routing a Use declaration to the next phase.
enum UseResult<C: ComponentInstanceInterface, O, U> {
    /// The source of the Use was found (Framework, AboveRoot, etc.)
    Source(CapabilitySourceInterface<C>),
    /// The Use led to a parent Offer declaration.
    FromParent(Vec<O>, Arc<C>),
    /// The Use led to a child Expose declaration.
    /// Note: Instead of FromChild carrying an ExposeDecl of the matching child, it carries a
    /// UseDecl. This is because some RoutingStrategy<> don't support Expose, but are still
    /// required to enumerate over UseResult<>.
    FromChild(U, Arc<C>),
}

impl<U> Use<U>
where
    U: UseDeclCommon + ErrorNotFoundFromParent + Into<UseDecl> + Clone,
{
    /// Routes the capability starting from the `use_` declaration at `target` to either a valid
    /// source (as defined by `sources`) or the Offer declaration that ends this phase of routing.
    async fn route<C, S, V, O, M>(
        use_: U,
        target: Arc<C>,
        sources: &S,
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<UseResult<C, O, U>, RoutingError>
    where
        C: ComponentInstanceInterface,
        S: Sources,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        O: OfferDeclCommon + FromEnum<OfferDecl> + ErrorNotFoundFromParent + Clone,
        M: DebugRouteMapper,
    {
        mapper.add_use(target.abs_moniker().clone(), use_.clone().into());
        match use_.source() {
            UseSource::Framework => {
                Ok(UseResult::Source(CapabilitySourceInterface::<C>::Framework {
                    capability: sources.framework_source(use_.source_name().clone(), mapper)?,
                    component: target.as_weak(),
                }))
            }
            UseSource::Capability(_) => {
                sources.capability_source()?;
                Ok(UseResult::Source(CapabilitySourceInterface::<C>::Capability {
                    component: target.as_weak(),
                    source_capability: ComponentCapability::Use(use_.into()),
                }))
            }
            UseSource::Parent => match target.try_get_parent()? {
                ExtendedInstanceInterface::<C>::AboveRoot(top_instance) => {
                    if sources.is_namespace_supported() {
                        if let Some(capability) = sources.find_namespace_source(
                            use_.source_name(),
                            top_instance.namespace_capabilities(),
                            visitor,
                            mapper,
                        )? {
                            return Ok(UseResult::Source(
                                CapabilitySourceInterface::<C>::Namespace {
                                    capability,
                                    top_instance: Arc::downgrade(&top_instance),
                                },
                            ));
                        }
                    }
                    if let Some(capability) = sources.find_builtin_source(
                        use_.source_name(),
                        top_instance.builtin_capabilities(),
                        visitor,
                        mapper,
                    )? {
                        return Ok(UseResult::Source(CapabilitySourceInterface::<C>::Builtin {
                            capability,
                            top_instance: Arc::downgrade(&top_instance),
                        }));
                    }
                    Err(RoutingError::use_from_component_manager_not_found(
                        use_.source_name().to_string(),
                    ))
                }
                ExtendedInstanceInterface::<C>::Component(parent_component) => {
                    let parent_offers: Vec<O> = {
                        let parent_offers = parent_component.lock_resolved_state().await?.offers();
                        let child_moniker =
                            target.child_moniker().expect("ChildMoniker should exist");
                        let found_offers = find_matching_offers(
                            use_.source_name(),
                            &child_moniker,
                            &parent_offers,
                        );
                        if found_offers.is_empty() {
                            return Err(
                                <U as ErrorNotFoundFromParent>::error_not_found_from_parent(
                                    target.abs_moniker().clone(),
                                    use_.source_name().clone(),
                                ),
                            );
                        } else {
                            found_offers
                        }
                    };
                    Ok(UseResult::FromParent(parent_offers, parent_component))
                }
            },
            UseSource::Child(name) => {
                let moniker = target.abs_moniker();
                let child_component = {
                    let child_moniker = ChildMoniker::try_new(name, None)?;
                    target.lock_resolved_state().await?.get_child(&child_moniker).ok_or_else(
                        || {
                            RoutingError::use_from_child_instance_not_found(
                                &child_moniker,
                                moniker,
                                name.clone(),
                            )
                        },
                    )?
                };

                Ok(UseResult::FromChild(use_, child_component))
            }
            UseSource::Debug => {
                // This is not supported today. It might be worthwhile to support this if
                // more than just protocol has a debug capability.
                return Err(RoutingError::unsupported_route_source("debug capability"));
            }
            UseSource::Self_ => {
                return Err(RoutingError::unsupported_route_source("self"));
            }
        }
    }
}

/// The environment `Registration` phase of routing.
pub struct Registration<R>(PhantomData<R>);

/// The result of routing a Registration declaration to the next phase.
enum RegistrationResult<C: ComponentInstanceInterface, O, E> {
    /// The source of the Registration was found (Framework, AboveRoot, etc.).
    Source(CapabilitySourceInterface<C>),
    /// The Registration led to a parent Offer declaration.
    FromParent(Vec<O>, Arc<C>),
    /// The Registration led to a child Expose declaration.
    FromChild(E, Arc<C>),
}

impl<R> Registration<R>
where
    R: RegistrationDeclCommon
        + ErrorNotFoundFromParent
        + ErrorNotFoundInChild
        + Into<RegistrationDecl>
        + Clone,
{
    /// Routes the capability starting from the `registration` declaration at `target` to either a
    /// valid source (as defined by `sources`) or the Offer or Expose declaration that ends this
    /// phase of routing.
    async fn route<C, S, V, O, E, M>(
        registration: R,
        target: Arc<C>,
        sources: &S,
        visitor: &mut V,
        mapper: &mut M,
    ) -> Result<RegistrationResult<C, O, E>, RoutingError>
    where
        C: ComponentInstanceInterface,
        S: Sources,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        O: OfferDeclCommon + FromEnum<OfferDecl> + Clone,
        E: ExposeDeclCommon + FromEnum<ExposeDecl> + Clone,
        M: DebugRouteMapper,
    {
        mapper.add_registration(target.abs_moniker().clone(), registration.clone().into());
        match registration.source() {
            RegistrationSource::Self_ => {
                let target_capabilities = target.lock_resolved_state().await?.capabilities();
                Ok(RegistrationResult::Source(CapabilitySourceInterface::<C>::Component {
                    capability: sources.find_component_source(
                        registration.source_name(),
                        target.abs_moniker(),
                        &target_capabilities,
                        visitor,
                        mapper,
                    )?,
                    component: target.as_weak(),
                }))
            }
            RegistrationSource::Parent => match target.try_get_parent()? {
                ExtendedInstanceInterface::<C>::AboveRoot(top_instance) => {
                    if sources.is_namespace_supported() {
                        if let Some(capability) = sources.find_namespace_source(
                            registration.source_name(),
                            top_instance.namespace_capabilities(),
                            visitor,
                            mapper,
                        )? {
                            return Ok(RegistrationResult::Source(
                                CapabilitySourceInterface::<C>::Namespace {
                                    capability,
                                    top_instance: Arc::downgrade(&top_instance),
                                },
                            ));
                        }
                    }
                    if let Some(capability) = sources.find_builtin_source(
                        registration.source_name(),
                        top_instance.builtin_capabilities(),
                        visitor,
                        mapper,
                    )? {
                        return Ok(RegistrationResult::Source(
                            CapabilitySourceInterface::<C>::Builtin {
                                capability,
                                top_instance: Arc::downgrade(&top_instance),
                            },
                        ));
                    }
                    Err(RoutingError::register_from_component_manager_not_found(
                        registration.source_name().to_string(),
                    ))
                }
                ExtendedInstanceInterface::<C>::Component(parent_component) => {
                    let parent_offers: Vec<O> = {
                        let parent_offers = parent_component.lock_resolved_state().await?.offers();
                        let child_moniker =
                            target.child_moniker().expect("ChildMoniker should exist");
                        let found_offers = find_matching_offers(
                            registration.source_name(),
                            &child_moniker,
                            &parent_offers,
                        );
                        if found_offers.is_empty() {
                            return Err(
                                <R as ErrorNotFoundFromParent>::error_not_found_from_parent(
                                    target.abs_moniker().clone(),
                                    registration.source_name().clone(),
                                ),
                            );
                        } else {
                            found_offers
                        }
                    };
                    Ok(RegistrationResult::FromParent(parent_offers, parent_component))
                }
            },
            RegistrationSource::Child(child) => {
                let child_component = {
                    let child_moniker = ChildMoniker::try_new(child, None)?;
                    target.lock_resolved_state().await?.get_child(&child_moniker).ok_or_else(
                        || RoutingError::EnvironmentFromChildInstanceNotFound {
                            child_moniker,
                            moniker: target.abs_moniker().clone(),
                            capability_name: registration.source_name().clone(),
                            capability_type: R::TYPE.to_string(),
                        },
                    )?
                };

                let child_expose: E = {
                    let child_exposes = child_component.lock_resolved_state().await?.exposes();
                    find_matching_expose(registration.source_name(), &child_exposes)
                        .cloned()
                        .ok_or_else(|| {
                            let child_moniker =
                                child_component.child_moniker().expect("ChildMoniker should exist");
                            <R as ErrorNotFoundInChild>::error_not_found_in_child(
                                target.abs_moniker().clone(),
                                child_moniker.clone(),
                                registration.source_name().clone(),
                            )
                        })?
                };
                Ok(RegistrationResult::FromChild(child_expose, child_component.clone()))
            }
        }
    }
}

/// The `Offer` phase of routing.
pub struct Offer<O>(PhantomData<O>);

/// The result of routing an Offer declaration to the next phase.
enum OfferResult<C: ComponentInstanceInterface, O> {
    /// The source of the Offer was found (Framework, AboveRoot, Component, etc.).
    Source(CapabilitySourceInterface<C>),
    /// The Offer led to an Offer-from-child declaration.
    /// Not all capabilities can be exposed, so let the caller decide how to handle this.
    OfferFromChild(O, Arc<C>),
    /// Offer from collection.
    OfferFromCollection(O, Arc<C>, String),
    /// Offer from multiple sources.
    OfferFromAggregate(Vec<O>, Arc<C>),
}

impl<O> Offer<O>
where
    O: OfferDeclCommon + ErrorNotFoundFromParent + FromEnum<OfferDecl> + Into<OfferDecl> + Clone,
{
    /// Routes the capability starting from the `offer` declaration at `target` to either a valid
    /// source (as defined by `sources`) or the declaration that ends this phase of routing.
    async fn route<C, S, V, M>(
        mut offer: O,
        mut target: Arc<C>,
        sources: &S,
        visitor: &mut V,
        mapper: &mut M,
        route: &mut Vec<Arc<C>>,
    ) -> Result<OfferResult<C, O>, RoutingError>
    where
        C: ComponentInstanceInterface,
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        M: DebugRouteMapper,
    {
        loop {
            mapper.add_offer(target.abs_moniker().clone(), offer.clone().into());
            OfferVisitor::visit(visitor, &offer)?;
            route.push(target.clone());
            match offer.source() {
                OfferSource::Void => {
                    panic!("an error should have been emitted by the availability walker before we reach this point");
                }
                OfferSource::Self_ => {
                    let target_capabilities = target.lock_resolved_state().await?.capabilities();
                    let component_capability = sources.find_component_source(
                        offer.source_name(),
                        target.abs_moniker(),
                        &target_capabilities,
                        visitor,
                        mapper,
                    )?;
                    // if offerdecl is for a filtered service return the associated filterd source.
                    return match offer.into() {
                        OfferDecl::Service(offer_service_decl) => {
                            if offer_service_decl.source_instance_filter.is_some()
                                || offer_service_decl.renamed_instances.is_some()
                            {
                                Ok(OfferResult::Source(
                                    CapabilitySourceInterface::<C>::FilteredService {
                                        capability: component_capability,
                                        component: target.as_weak(),
                                        source_instance_filter: offer_service_decl
                                            .source_instance_filter
                                            .unwrap_or(vec![]),
                                        instance_name_source_to_target: offer_service_decl
                                            .renamed_instances
                                            .map_or(HashMap::new(), name_mappings_to_map),
                                    },
                                ))
                            } else {
                                Ok(OfferResult::Source(CapabilitySourceInterface::<C>::Component {
                                    capability: component_capability,
                                    component: target.as_weak(),
                                }))
                            }
                        }
                        _ => Ok(OfferResult::Source(CapabilitySourceInterface::<C>::Component {
                            capability: component_capability,
                            component: target.as_weak(),
                        })),
                    };
                }
                OfferSource::Framework => {
                    return Ok(OfferResult::Source(CapabilitySourceInterface::<C>::Framework {
                        capability: sources
                            .framework_source(offer.source_name().clone(), mapper)?,
                        component: target.as_weak(),
                    }))
                }
                OfferSource::Capability(_) => {
                    sources.capability_source()?;
                    return Ok(OfferResult::Source(CapabilitySourceInterface::<C>::Capability {
                        source_capability: ComponentCapability::Offer(offer.into()),
                        component: target.as_weak(),
                    }));
                }
                OfferSource::Parent => {
                    let parent_component = match target.try_get_parent()? {
                        ExtendedInstanceInterface::<C>::AboveRoot(top_instance) => {
                            if sources.is_namespace_supported() {
                                if let Some(capability) = sources.find_namespace_source(
                                    offer.source_name(),
                                    top_instance.namespace_capabilities(),
                                    visitor,
                                    mapper,
                                )? {
                                    return Ok(OfferResult::Source(
                                        CapabilitySourceInterface::<C>::Namespace {
                                            capability,
                                            top_instance: Arc::downgrade(&top_instance),
                                        },
                                    ));
                                }
                            }
                            if let Some(capability) = sources.find_builtin_source(
                                offer.source_name(),
                                top_instance.builtin_capabilities(),
                                visitor,
                                mapper,
                            )? {
                                return Ok(OfferResult::Source(
                                    CapabilitySourceInterface::<C>::Builtin {
                                        capability,
                                        top_instance: Arc::downgrade(&top_instance),
                                    },
                                ));
                            }
                            return Err(RoutingError::offer_from_component_manager_not_found(
                                offer.source_name().to_string(),
                            ));
                        }
                        ExtendedInstanceInterface::<C>::Component(component) => component,
                    };
                    let child_moniker = target.child_moniker().expect("ChildMoniker should exist");
                    let parent_offers: Vec<O> = {
                        let parent_offers = parent_component.lock_resolved_state().await?.offers();
                        let found_offers = find_matching_offers(
                            offer.source_name(),
                            &child_moniker,
                            &parent_offers,
                        );
                        if found_offers.is_empty() {
                            return Err(
                                <O as ErrorNotFoundFromParent>::error_not_found_from_parent(
                                    target.abs_moniker().clone(),
                                    offer.source_name().clone(),
                                ),
                            );
                        } else {
                            found_offers
                        }
                    };
                    if parent_offers.len() == 1 {
                        offer = parent_offers.first().unwrap().clone();
                        target = parent_component;
                    } else {
                        return Ok(OfferResult::OfferFromAggregate(parent_offers, target));
                    }
                }
                OfferSource::Child(_) => {
                    return Ok(OfferResult::OfferFromChild(offer, target));
                }
                OfferSource::Collection(name) => {
                    sources.collection_source()?;
                    {
                        let target_collections = target.lock_resolved_state().await?.collections();
                        target_collections.iter().find(|c| &c.name == name).ok_or_else(|| {
                            RoutingError::OfferFromCollectionNotFound {
                                collection: name.clone(),
                                moniker: target.abs_moniker().clone(),
                                capability: offer.source_name().clone(),
                            }
                        })?;
                    }
                    let name = name.clone();
                    return Ok(OfferResult::OfferFromCollection(offer, target, name));
                }
            }
        }
    }
}

/// Finds the matching Expose declaration for an Offer-from-child, changing the
/// direction in which the Component Tree is being navigated (from up to down).
async fn change_directions<C, O, E>(
    offer: O,
    component: Arc<C>,
) -> Result<(E, Arc<C>), RoutingError>
where
    C: ComponentInstanceInterface,
    O: OfferDeclCommon + ErrorNotFoundInChild,
    E: ExposeDeclCommon + FromEnum<ExposeDecl> + Clone,
{
    match offer.source() {
        OfferSource::Child(child) => {
            let child_component = {
                let child_moniker = ChildMoniker::try_new(&child.name, child.collection.as_ref())?;
                component.lock_resolved_state().await?.get_child(&child_moniker).ok_or_else(
                    || RoutingError::OfferFromChildInstanceNotFound {
                        child_moniker,
                        moniker: component.abs_moniker().clone(),
                        capability_id: offer.source_name().clone().into(),
                    },
                )?
            };
            let expose = {
                let child_exposes = child_component.lock_resolved_state().await?.exposes();
                find_matching_expose(offer.source_name(), &child_exposes).cloned().ok_or_else(
                    || {
                        let child_moniker =
                            child_component.child_moniker().expect("ChildMoniker should exist");
                        <O as ErrorNotFoundInChild>::error_not_found_in_child(
                            component.abs_moniker().clone(),
                            child_moniker.clone(),
                            offer.source_name().clone(),
                        )
                    },
                )?
            };
            Ok((expose, child_component.clone()))
        }
        _ => panic!("change_direction called with offer that does not change direction"),
    }
}

/// The `Expose` phase of routing.
pub struct Expose<E>(PhantomData<E>);

/// The result of routing an Expose declaration to the next phase.
enum ExposeResult<C: ComponentInstanceInterface, E> {
    /// The source of the Expose was found (Framework, Component, etc.).
    Source(CapabilitySourceInterface<C>),
    /// The source of the Expose comes from a collection.
    ExposeFromCollection(E, Arc<C>, String),
}

impl<E> Expose<E>
where
    E: ExposeDeclCommon + ErrorNotFoundInChild + FromEnum<ExposeDecl> + Into<ExposeDecl> + Clone,
{
    /// Routes the capability starting from the `expose` declaration at `target` to a valid source
    /// (as defined by `sources`).
    async fn route<C, S, V, M>(
        mut expose: E,
        mut target: Arc<C>,
        sources: &S,
        visitor: &mut V,
        mapper: &mut M,
        route: &mut Vec<Arc<C>>,
    ) -> Result<ExposeResult<C, E>, RoutingError>
    where
        C: ComponentInstanceInterface,
        S: Sources,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        M: DebugRouteMapper,
    {
        loop {
            mapper.add_expose(target.abs_moniker().clone(), expose.clone().into());
            ExposeVisitor::visit(visitor, &expose)?;
            route.push(target.clone());
            match expose.source() {
                ExposeSource::Self_ => {
                    let target_capabilities = target.lock_resolved_state().await?.capabilities();
                    return Ok(ExposeResult::Source(CapabilitySourceInterface::<C>::Component {
                        capability: sources.find_component_source(
                            expose.source_name(),
                            target.abs_moniker(),
                            &target_capabilities,
                            visitor,
                            mapper,
                        )?,
                        component: target.as_weak(),
                    }));
                }
                ExposeSource::Framework => {
                    return Ok(ExposeResult::Source(CapabilitySourceInterface::<C>::Framework {
                        capability: sources
                            .framework_source(expose.source_name().clone(), mapper)?,
                        component: target.as_weak(),
                    }))
                }
                ExposeSource::Capability(_) => {
                    sources.capability_source()?;
                    return Ok(ExposeResult::Source(CapabilitySourceInterface::<C>::Capability {
                        source_capability: ComponentCapability::Expose(expose.into()),
                        component: target.as_weak(),
                    }));
                }
                ExposeSource::Child(child) => {
                    let child_component = {
                        let child_moniker = ChildMoniker::try_new(child, None)?;
                        target.lock_resolved_state().await?.get_child(&child_moniker).ok_or_else(
                            || RoutingError::ExposeFromChildInstanceNotFound {
                                child_moniker,
                                moniker: target.abs_moniker().clone(),
                                capability_id: expose.source_name().clone().into(),
                            },
                        )?
                    };
                    let child_expose = {
                        let child_exposes = child_component.lock_resolved_state().await?.exposes();
                        find_matching_expose(expose.source_name(), &child_exposes)
                            .cloned()
                            .ok_or_else(|| {
                                let child_moniker = child_component
                                    .child_moniker()
                                    .expect("ChildMoniker should exist");
                                <E as ErrorNotFoundInChild>::error_not_found_in_child(
                                    target.abs_moniker().clone(),
                                    child_moniker.clone(),
                                    expose.source_name().clone(),
                                )
                            })?
                    };
                    expose = child_expose;
                    target = child_component.clone();
                }
                ExposeSource::Collection(name) => {
                    sources.collection_source()?;
                    {
                        let target_collections = target.lock_resolved_state().await?.collections();
                        target_collections.iter().find(|c| &c.name == name).ok_or_else(|| {
                            RoutingError::ExposeFromCollectionNotFound {
                                collection: name.clone(),
                                moniker: target.abs_moniker().clone(),
                                capability: expose.source_name().clone(),
                            }
                        })?;
                    }
                    let name = name.clone();
                    return Ok(ExposeResult::ExposeFromCollection(expose, target, name));
                }
            }
        }
    }
}

fn target_matches_moniker(target: &OfferTarget, child_moniker: &ChildMoniker) -> bool {
    match target {
        OfferTarget::Child(target_ref) => {
            target_ref.name == child_moniker.name()
                && target_ref.collection.as_ref().map(|c| c.as_str()) == child_moniker.collection()
        }
        OfferTarget::Collection(target_collection) => {
            Some(target_collection.as_str()) == child_moniker.collection()
        }
    }
}

/// Visitor pattern trait for visiting a variant of [`OfferDecl`] specific to a capability type.
pub trait OfferVisitor {
    /// The concrete declaration type.
    type OfferDecl: OfferDeclCommon;

    /// Visit a variant of [`OfferDecl`] specific to the capability.
    /// Returning an `Err` cancels visitation.
    fn visit(&mut self, offer: &Self::OfferDecl) -> Result<(), RoutingError>;
}

/// Visitor pattern trait for visiting a variant of [`ExposeDecl`] specific to a capability type.
pub trait ExposeVisitor {
    /// The concrete declaration type.
    type ExposeDecl: ExposeDeclCommon;

    /// Visit a variant of [`ExposeDecl`] specific to the capability.
    /// Returning an `Err` cancels visitation.
    fn visit(&mut self, expose: &Self::ExposeDecl) -> Result<(), RoutingError>;
}

/// Visitor pattern trait for visiting a variant of [`CapabilityDecl`] specific to a capability
/// type.
pub trait CapabilityVisitor {
    /// The concrete declaration type. Can be `()` if the capability type does not support
    /// namespace, component, or built-in source types.
    type CapabilityDecl;

    /// Visit a variant of [`CapabilityDecl`] specific to the capability.
    /// Returning an `Err` cancels visitation.
    fn visit(&mut self, _capability_decl: &Self::CapabilityDecl) -> Result<(), RoutingError> {
        Ok(())
    }
}

pub fn find_matching_offer<'a, O>(
    source_name: &CapabilityName,
    child_moniker: &ChildMoniker,
    offers: &'a Vec<OfferDecl>,
) -> Option<&'a O>
where
    O: OfferDeclCommon + FromEnum<OfferDecl>,
{
    offers.iter().flat_map(FromEnum::<OfferDecl>::from_enum).find(|offer: &&O| {
        *offer.target_name() == *source_name
            && target_matches_moniker(offer.target(), &child_moniker)
    })
}

pub fn find_matching_offers<'a, O>(
    source_name: &CapabilityName,
    child_moniker: &ChildMoniker,
    offers: &'a Vec<OfferDecl>,
) -> Vec<O>
where
    O: OfferDeclCommon + FromEnum<OfferDecl> + Clone,
{
    offers
        .iter()
        .flat_map(FromEnum::<OfferDecl>::from_enum)
        .filter(|offer: &&O| {
            *offer.target_name() == *source_name
                && target_matches_moniker(offer.target(), &child_moniker)
        })
        .cloned()
        .collect()
}

pub fn find_matching_expose<'a, E>(
    source_name: &CapabilityName,
    exposes: &'a Vec<ExposeDecl>,
) -> Option<&'a E>
where
    E: ExposeDeclCommon + FromEnum<ExposeDecl>,
{
    exposes.iter().flat_map(FromEnum::<ExposeDecl>::from_enum).find(|expose: &&E| {
        *expose.target_name() == *source_name && *expose.target() == ExposeTarget::Parent
    })
}

/// Implemented by declaration types to emit a proper error when a matching offer is not found in the parent.
pub trait ErrorNotFoundFromParent {
    fn error_not_found_from_parent(
        decl_site_moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError;
}

/// Implemented by declaration types to emit a proper error when a matching expose is not found in the child.
pub trait ErrorNotFoundInChild {
    fn error_not_found_in_child(
        decl_site_moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError;
}

/// Creates a unit struct that implements a visitor for each declared type.
#[macro_export]
macro_rules! make_noop_visitor {
    ($name:ident, {
        $(OfferDecl => $offer_decl:ty,)*
        $(ExposeDecl => $expose_decl:ty,)*
        $(CapabilityDecl => $cap_decl:ty,)*
    }) => {
        #[derive(Clone)]
        pub struct $name;

        $(
            impl $crate::router::OfferVisitor for $name {
                type OfferDecl = $offer_decl;

                fn visit(&mut self, _decl: &Self::OfferDecl) -> Result<(), $crate::error::RoutingError> {
                    Ok(())
                }
            }
        )*

        $(
            impl $crate::router::ExposeVisitor for $name {
                type ExposeDecl = $expose_decl;

                fn visit(&mut self, _decl: &Self::ExposeDecl) -> Result<(), $crate::error::RoutingError> {
                    Ok(())
                }
            }
        )*

        $(
            impl $crate::router::CapabilityVisitor for $name {
                type CapabilityDecl = $cap_decl;

                fn visit(
                    &mut self,
                    _decl: &Self::CapabilityDecl
                ) -> Result<(), $crate::error::RoutingError> {
                    Ok(())
                }
            }
        )*
    };
}
