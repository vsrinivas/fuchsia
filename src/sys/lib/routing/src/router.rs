// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_source::{CapabilitySourceInterface, ComponentCapability, InternalCapability},
        collection::{RouteExposeFromCollection, RouteOfferFromCollection},
        component_instance::{
            ComponentInstanceInterface, ExtendedInstanceInterface, TopInstanceInterface,
        },
        error::RoutingError,
    },
    cm_rust::{
        CapabilityDecl, CapabilityDeclCommon, CapabilityName, ComponentDecl, ExposeDecl,
        ExposeDeclCommon, ExposeSource, ExposeTarget, OfferDecl, OfferDeclCommon, OfferSource,
        OfferTarget, RegistrationDeclCommon, RegistrationSource, UseDecl, UseDeclCommon, UseSource,
    },
    derivative::Derivative,
    from_enum::FromEnum,
    moniker::{AbsoluteMoniker, ChildMoniker, ChildMonikerBase, PartialChildMoniker},
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
    U: UseDeclCommon + ErrorNotFoundFromParent + Into<UseDecl>,
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
    pub async fn route<C, S, V>(
        self,
        use_decl: U,
        target: Arc<C>,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
    {
        let decl = target.decl().await?;
        Ok(CapabilitySourceInterface::<C>::Component {
            capability: sources.find_component_source(
                use_decl.source_name(),
                &decl.capabilities,
                visitor,
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
    U: UseDeclCommon + ErrorNotFoundFromParent + Into<UseDecl>,
    O: OfferDeclCommon + ErrorNotFoundFromParent + FromEnum<OfferDecl> + Into<OfferDecl> + Clone,
{
    /// Routes a capability from its `Use` declaration to its source by following `Offer`
    /// declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` declaration in the routing path, as well as the final
    /// `Capability` declaration if `sources` permits.
    pub async fn route<C, S, V>(
        self,
        use_decl: U,
        use_target: Arc<C>,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        match Use::route(use_decl, use_target, &sources, visitor).await? {
            UseResult::Source(source) => return Ok(source),
            UseResult::FromParent(offer, component) => {
                self.route_from_offer(offer, component, sources, visitor).await
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
    pub async fn route_from_offer<C, S, V>(
        self,
        offer_decl: O,
        offer_target: Arc<C>,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        match Offer::route(offer_decl, offer_target, &sources, visitor).await? {
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
        }
    }
}

// Implement the Use -> Offer -> Expose routing strategy. This is the full, common routing
// strategy.
impl<U, O, E> RoutingStrategy<Use<U>, Offer<O>, Expose<E>>
where
    U: UseDeclCommon + ErrorNotFoundFromParent + ErrorNotFoundInChild + Into<UseDecl> + 'static,
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
    /// Routes a capability from its `Use` declaration to its source by following `Offer` and
    /// `Expose` declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` and `Expose` declaration in the routing path, as well
    /// as the final `Capability` declaration if `sources` permits.
    pub async fn route<C, S, V>(
        self,
        use_decl: U,
        use_target: Arc<C>,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
    {
        match Use::route(use_decl, use_target.clone(), &sources, visitor).await? {
            UseResult::Source(source) => return Ok(source),
            UseResult::FromParent(offer, component) => {
                self.route_from_offer(offer, component, sources, visitor).await
            }
            UseResult::FromChild(use_decl, child_component) => {
                let child_decl = child_component.decl().await?;
                let expose_decl = find_matching_expose(use_decl.source_name(), &child_decl)
                    .cloned()
                    .ok_or_else(|| {
                        let child_moniker = child_component
                            .child_moniker()
                            .expect("ChildMoniker should exist")
                            .to_partial();
                        <U as ErrorNotFoundInChild>::error_not_found_in_child(
                            use_target.abs_moniker().clone(),
                            child_moniker,
                            use_decl.source_name().clone(),
                        )
                    })?;
                self.route_from_expose(expose_decl, child_component, sources, visitor).await
            }
        }
    }
}

// Implement the Registration -> Offer -> Expose routing strategy. This is the strategy used
// to route capabilities registered in environments.
impl<R, O, E> RoutingStrategy<Registration<R>, Offer<O>, Expose<E>>
where
    R: RegistrationDeclCommon + ErrorNotFoundFromParent + ErrorNotFoundInChild + 'static,
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
    /// Routes a capability from its environment `Registration` declaration to its source by
    /// following `Offer` and `Expose` declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` and `Expose` declaration in the routing path, as
    /// well as the final `Capability` declaration if `sources` permits.
    pub async fn route<C, S, V>(
        self,
        registration_decl: R,
        registration_target: Arc<C>,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
    {
        match Registration::route(registration_decl, registration_target, &sources, visitor).await?
        {
            RegistrationResult::Source(source) => return Ok(source),
            RegistrationResult::FromParent(offer, component) => {
                self.route_from_offer(offer, component, sources, visitor).await
            }
            RegistrationResult::FromChild(expose, component) => {
                self.route_from_expose(expose, component, sources, visitor).await
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
    /// Routes a capability from its `Offer` declaration to its source by following `Offer` and
    /// `Expose` declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` and `Expose` declaration in the routing path, as well
    /// as the final `Capability` declaration if `sources` permits.
    pub async fn route_from_offer<C, S, V>(
        self,
        offer_decl: O,
        offer_target: Arc<C>,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
    {
        match Offer::route(offer_decl, offer_target, &sources, visitor).await? {
            OfferResult::Source(source) => return Ok(source),
            OfferResult::OfferFromChild(offer, component) => {
                let (expose, component) = change_directions(offer, component).await?;
                self.route_from_expose(expose, component, sources, visitor).await
            }
            OfferResult::OfferFromCollection(offer_decl, collection_component, collection_name) => {
                Ok(CapabilitySourceInterface::<C>::Collection {
                    collection_name: collection_name.clone(),
                    source_name: offer_decl.source_name().clone(),
                    capability_provider: Box::new(RouteOfferFromCollection {
                        router: self,
                        collection_name,
                        collection_component: collection_component.as_weak(),
                        offer_decl,
                        sources: sources.clone(),
                        visitor: visitor.clone(),
                    }),
                    component: collection_component.as_weak(),
                })
            }
        }
    }
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
    /// Routes a capability from its `Expose` declaration to its source by following `Expose`
    /// declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Expose` declaration in the routing path, as well
    /// as the final `Capability` declaration if `sources` permits.
    pub async fn route_from_expose<C, S, V>(
        self,
        expose_decl: E,
        expose_target: Arc<C>,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>
    where
        C: ComponentInstanceInterface + 'static,
        S: Sources + 'static,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        V: Clone + Send + Sync + 'static,
    {
        match Expose::route(expose_decl, expose_target, &sources, visitor).await? {
            ExposeResult::Source(source) => Ok(source),
            ExposeResult::ExposeFromCollection(
                expose_decl,
                collection_component,
                collection_name,
            ) => Ok(CapabilitySourceInterface::<C>::Collection {
                collection_name: collection_name.clone(),
                source_name: expose_decl.source_name().clone(),
                capability_provider: Box::new(RouteExposeFromCollection {
                    router: self,
                    collection_name,
                    collection_component: collection_component.as_weak(),
                    expose_decl,
                    sources: sources.clone(),
                    visitor: visitor.clone(),
                }),
                component: collection_component.as_weak(),
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
    fn framework_source(&self, name: CapabilityName) -> Result<InternalCapability, RoutingError>;

    /// Checks whether capability sources are supported, returning [`RoutingError::UnsupportedRouteSource`]
    /// if they are not.
    fn capability_source(&self) -> Result<(), RoutingError>;

    /// Checks whether collection sources are supported, returning [`ModelError::Unsupported`]
    /// if they are not.
    fn collection_source(&self) -> Result<(), RoutingError>;

    /// Checks whether namespace capability sources are supported.
    fn is_namespace_supported(&self) -> bool;

    /// Looks for a namespace capability in the list of capability sources.
    /// If found, the declaration is visited by `visitor` and the declaration is wrapped
    /// in a [`ComponentCapability`].
    /// Returns [`RoutingError::UnsupportedRouteSource`] if namespace capabilities are unsupported.
    fn find_namespace_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<Option<ComponentCapability>, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>;

    /// Looks for a built-in capability in the list of capability sources.
    /// If found, the capability's name is wrapped in an [`InternalCapability`].
    /// Returns [`RoutingError::UnsupportedRouteSource`] if built-in capabilities are unsupported.
    fn find_builtin_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<Option<InternalCapability>, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>;

    /// Looks for a component capability in the list of capability sources.
    /// If found, the declaration is visited by `visitor` and the declaration is wrapped
    /// in a [`ComponentCapability`].
    /// Returns [`RoutingError::UnsupportedRouteSource`] if component capabilities are unsupported.
    fn find_component_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<ComponentCapability, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>;
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
        + Clone,
{
    type CapabilityDecl = C;

    fn framework_source(&self, name: CapabilityName) -> Result<InternalCapability, RoutingError> {
        self.framework
            .as_ref()
            .map(|b| b(name))
            .ok_or_else(|| RoutingError::unsupported_route_source("framework"))
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

    fn find_namespace_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<Option<ComponentCapability>, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
    {
        if self.namespace {
            if let Some(decl) = capabilities
                .iter()
                .flat_map(FromEnum::from_enum)
                .find(|decl: &&C| decl.name() == name)
                .cloned()
            {
                visitor.visit(&decl)?;
                Ok(Some(decl.into()))
            } else {
                Ok(None)
            }
        } else {
            Err(RoutingError::unsupported_route_source("namespace"))
        }
    }

    fn find_builtin_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<Option<InternalCapability>, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
    {
        if self.builtin {
            if let Some(decl) = capabilities
                .iter()
                .flat_map(FromEnum::from_enum)
                .find(|decl: &&C| decl.name() == name)
                .cloned()
            {
                visitor.visit(&decl)?;
                Ok(Some(decl.into()))
            } else {
                Ok(None)
            }
        } else {
            Err(RoutingError::unsupported_route_source("built-in"))
        }
    }

    fn find_component_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<ComponentCapability, RoutingError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
    {
        if self.component {
            let decl = capabilities
                .iter()
                .flat_map(FromEnum::from_enum)
                .find(|decl: &&C| decl.name() == name)
                .cloned()
                .expect("CapabilityDecl missing, FIDL validation should catch this");
            visitor.visit(&decl)?;
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
    FromParent(O, Arc<C>),
    /// The Use led to a child Expose declaration.
    /// Note: Instead of FromChild carrying an ExposeDecl of the matching child, it carries a
    /// UseDecl. This is because some RoutingStrategy<> don't support Expose, but are still
    /// required to enumerate over UseResult<>.
    FromChild(U, Arc<C>),
}

impl<U> Use<U>
where
    U: UseDeclCommon + ErrorNotFoundFromParent + Into<UseDecl>,
{
    /// Routes the capability starting from the `use_` declaration at `target` to either a valid
    /// source (as defined by `sources`) or the Offer declaration that ends this phase of routing.
    async fn route<C, S, V, O>(
        use_: U,
        target: Arc<C>,
        sources: &S,
        visitor: &mut V,
    ) -> Result<UseResult<C, O, U>, RoutingError>
    where
        C: ComponentInstanceInterface,
        S: Sources,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        O: OfferDeclCommon + FromEnum<OfferDecl> + ErrorNotFoundFromParent + Clone,
    {
        match use_.source() {
            UseSource::Framework => {
                Ok(UseResult::Source(CapabilitySourceInterface::<C>::Framework {
                    capability: sources.framework_source(use_.source_name().clone())?,
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
                    let parent_offer: O = {
                        let parent_decl = parent_component.decl().await?;
                        let child_moniker =
                            target.child_moniker().cloned().expect("ChildMoniker should exist");
                        find_matching_offer(use_.source_name(), &child_moniker, &parent_decl)
                            .cloned()
                            .ok_or_else(|| {
                                <U as ErrorNotFoundFromParent>::error_not_found_from_parent(
                                    target.abs_moniker().clone(),
                                    use_.source_name().clone(),
                                )
                            })?
                    };
                    Ok(UseResult::FromParent(parent_offer, parent_component))
                }
            },
            UseSource::Child(name) => {
                let moniker = target.abs_moniker();
                let child_component = {
                    let partial = PartialChildMoniker::new(name.clone(), None);
                    target.get_live_child(&partial).await?.ok_or_else(|| {
                        RoutingError::use_from_child_not_found(
                            moniker,
                            use_.source_name().clone(),
                            name.clone(),
                        )
                    })?
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
    FromParent(O, Arc<C>),
    /// The Registration led to a child Expose declaration.
    FromChild(E, Arc<C>),
}

impl<R> Registration<R>
where
    R: RegistrationDeclCommon + ErrorNotFoundFromParent + ErrorNotFoundInChild,
{
    /// Routes the capability starting from the `registration` declaration at `target` to either a
    /// valid source (as defined by `sources`) or the Offer or Expose declaration that ends this
    /// phase of routing.
    async fn route<C, S, V, O, E>(
        registration: R,
        target: Arc<C>,
        sources: &S,
        visitor: &mut V,
    ) -> Result<RegistrationResult<C, O, E>, RoutingError>
    where
        C: ComponentInstanceInterface,
        S: Sources,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        O: OfferDeclCommon + FromEnum<OfferDecl> + Clone,
        E: ExposeDeclCommon + FromEnum<ExposeDecl> + Clone,
    {
        match registration.source() {
            RegistrationSource::Self_ => {
                let decl = target.decl().await?;
                Ok(RegistrationResult::Source(CapabilitySourceInterface::<C>::Component {
                    capability: sources.find_component_source(
                        registration.source_name(),
                        &decl.capabilities,
                        visitor,
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
                    )? {
                        return Ok(RegistrationResult::Source(
                            CapabilitySourceInterface::<C>::Builtin {
                                capability,
                                top_instance: Arc::downgrade(&top_instance),
                            },
                        ));
                    }
                    Err(RoutingError::use_from_component_manager_not_found(
                        registration.source_name().to_string(),
                    ))
                }
                ExtendedInstanceInterface::<C>::Component(parent_component) => {
                    let parent_offer: O = {
                        let parent_decl = parent_component.decl().await?;
                        let child_moniker =
                            target.child_moniker().cloned().expect("ChildMoniker should exist");
                        find_matching_offer(
                            registration.source_name(),
                            &child_moniker,
                            &parent_decl,
                        )
                        .cloned()
                        .ok_or_else(|| {
                            <R as ErrorNotFoundFromParent>::error_not_found_from_parent(
                                target.abs_moniker().clone(),
                                registration.source_name().clone(),
                            )
                        })?
                    };
                    Ok(RegistrationResult::FromParent(parent_offer, parent_component))
                }
            },
            RegistrationSource::Child(child) => {
                let child_component = {
                    let partial = PartialChildMoniker::new(child.clone(), None);
                    target.get_live_child(&partial).await?.ok_or_else(|| {
                        RoutingError::EnvironmentFromChildInstanceNotFound {
                            child_moniker: partial,
                            moniker: target.abs_moniker().clone(),
                            capability_name: registration.source_name().clone(),
                            capability_type: R::TYPE,
                        }
                    })?
                };

                let child_expose: E = {
                    let child_decl = child_component.decl().await?;
                    find_matching_expose(registration.source_name(), &child_decl)
                        .cloned()
                        .ok_or_else(|| {
                            let child_moniker = child_component
                                .child_moniker()
                                .expect("ChildMoniker should exist")
                                .to_partial();
                            <R as ErrorNotFoundInChild>::error_not_found_in_child(
                                target.abs_moniker().clone(),
                                child_moniker,
                                registration.source_name().clone(),
                            )
                        })?
                };
                Ok(RegistrationResult::FromChild(child_expose, child_component))
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
}

impl<O> Offer<O>
where
    O: OfferDeclCommon + ErrorNotFoundFromParent + FromEnum<OfferDecl> + Into<OfferDecl> + Clone,
{
    /// Routes the capability starting from the `offer` declaration at `target` to either a valid
    /// source (as defined by `sources`) or the declaration that ends this phase of routing.
    async fn route<C, S, V>(
        mut offer: O,
        mut target: Arc<C>,
        sources: &S,
        visitor: &mut V,
    ) -> Result<OfferResult<C, O>, RoutingError>
    where
        C: ComponentInstanceInterface,
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        loop {
            OfferVisitor::visit(visitor, &offer)?;

            match offer.source() {
                OfferSource::Self_ => {
                    let decl = target.decl().await?;
                    return Ok(OfferResult::Source(CapabilitySourceInterface::<C>::Component {
                        capability: sources.find_component_source(
                            offer.source_name(),
                            &decl.capabilities,
                            visitor,
                        )?,
                        component: target.as_weak(),
                    }));
                }
                OfferSource::Framework => {
                    return Ok(OfferResult::Source(CapabilitySourceInterface::<C>::Framework {
                        capability: sources.framework_source(offer.source_name().clone())?,
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
                    let child_moniker =
                        target.child_moniker().cloned().expect("ChildMoniker should exist");
                    let parent_offer = {
                        let parent_decl = parent_component.decl().await?;
                        find_matching_offer(offer.source_name(), &child_moniker, &parent_decl)
                            .cloned()
                            .ok_or_else(|| {
                                <O as ErrorNotFoundFromParent>::error_not_found_from_parent(
                                    target.abs_moniker().clone(),
                                    offer.source_name().clone(),
                                )
                            })?
                    };
                    offer = parent_offer;
                    target = parent_component;
                }
                OfferSource::Child(_) => {
                    return Ok(OfferResult::OfferFromChild(offer, target));
                }
                OfferSource::Collection(name) => {
                    sources.collection_source()?;
                    {
                        let decl = target.decl().await?;
                        decl.collections.iter().find(|c| &c.name == name).ok_or_else(|| {
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
                let partial = PartialChildMoniker::new(child.clone(), None);
                component.get_live_child(&partial).await?.ok_or_else(|| {
                    RoutingError::OfferFromChildInstanceNotFound {
                        child_moniker: partial,
                        moniker: component.abs_moniker().clone(),
                        capability_id: offer.source_name().clone().into(),
                    }
                })?
            };
            let expose = {
                let child_decl = child_component.decl().await?;
                find_matching_expose(offer.source_name(), &child_decl).cloned().ok_or_else(
                    || {
                        let child_moniker = child_component
                            .child_moniker()
                            .expect("ChildMoniker should exist")
                            .to_partial();
                        <O as ErrorNotFoundInChild>::error_not_found_in_child(
                            component.abs_moniker().clone(),
                            child_moniker,
                            offer.source_name().clone(),
                        )
                    },
                )?
            };
            Ok((expose, child_component))
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
    async fn route<C, S, V>(
        mut expose: E,
        mut target: Arc<C>,
        sources: &S,
        visitor: &mut V,
    ) -> Result<ExposeResult<C, E>, RoutingError>
    where
        C: ComponentInstanceInterface,
        S: Sources,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        loop {
            ExposeVisitor::visit(visitor, &expose)?;

            match expose.source() {
                ExposeSource::Self_ => {
                    let decl = target.decl().await?;
                    return Ok(ExposeResult::Source(CapabilitySourceInterface::<C>::Component {
                        capability: sources.find_component_source(
                            expose.source_name(),
                            &decl.capabilities,
                            visitor,
                        )?,
                        component: target.as_weak(),
                    }));
                }
                ExposeSource::Framework => {
                    return Ok(ExposeResult::Source(CapabilitySourceInterface::<C>::Framework {
                        capability: sources.framework_source(expose.source_name().clone())?,
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
                        let child_moniker = PartialChildMoniker::new(child.clone(), None);
                        target.get_live_child(&child_moniker).await?.ok_or_else(|| {
                            RoutingError::ExposeFromChildInstanceNotFound {
                                child_moniker,
                                moniker: target.abs_moniker().clone(),
                                capability_id: expose.source_name().clone().into(),
                            }
                        })?
                    };
                    let child_expose = {
                        let child_decl = child_component.decl().await?;
                        find_matching_expose(expose.source_name(), &child_decl)
                            .cloned()
                            .ok_or_else(|| {
                                let child_moniker = child_component
                                    .child_moniker()
                                    .expect("ChildMoniker should exist")
                                    .to_partial();
                                <E as ErrorNotFoundInChild>::error_not_found_in_child(
                                    target.abs_moniker().clone(),
                                    child_moniker,
                                    expose.source_name().clone(),
                                )
                            })?
                    };
                    expose = child_expose;
                    target = child_component;
                }
                ExposeSource::Collection(name) => {
                    sources.collection_source()?;
                    {
                        let decl = target.decl().await?;
                        decl.collections.iter().find(|c| &c.name == name).ok_or_else(|| {
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

fn target_matches_moniker(parent_target: &OfferTarget, child_moniker: &ChildMoniker) -> bool {
    match (parent_target, child_moniker.collection()) {
        (OfferTarget::Child(target_child_name), None) => target_child_name == child_moniker.name(),
        (OfferTarget::Collection(target_collection_name), Some(collection)) => {
            target_collection_name == collection
        }
        _ => false,
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
    decl: &'a ComponentDecl,
) -> Option<&'a O>
where
    O: OfferDeclCommon + FromEnum<OfferDecl>,
{
    decl.offers.iter().flat_map(FromEnum::<OfferDecl>::from_enum).find(|offer: &&O| {
        *offer.target_name() == *source_name
            && target_matches_moniker(offer.target(), &child_moniker)
    })
}

pub fn find_matching_expose<'a, E>(
    source_name: &CapabilityName,
    decl: &'a ComponentDecl,
) -> Option<&'a E>
where
    E: ExposeDeclCommon + FromEnum<ExposeDecl>,
{
    decl.exposes.iter().flat_map(FromEnum::<ExposeDecl>::from_enum).find(|expose: &&E| {
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
        child_moniker: PartialChildMoniker,
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
        struct $name;

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
