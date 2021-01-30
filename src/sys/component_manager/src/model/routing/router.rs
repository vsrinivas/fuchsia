// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilitySource, ComponentCapability, InternalCapability},
        model::{
            component::{ComponentInstance, ExtendedInstance},
            error::ModelError,
            routing::error::RoutingError,
        },
    },
    cm_rust::{
        CapabilityDecl, CapabilityDeclCommon, CapabilityName, ExposeDecl, ExposeDeclCommon,
        ExposeSource, ExposeTarget, FromEnum, OfferDecl, OfferDeclCommon, OfferSource, OfferTarget,
        RegistrationDeclCommon, RegistrationSource, SourceName, UseDecl, UseDeclCommon, UseSource,
    },
    moniker::{AbsoluteMoniker, ChildMoniker, PartialMoniker},
    std::{marker::PhantomData, sync::Arc},
};

/// Routes a capability to its source based on a particular routing strategy.
///
/// Callers invoke builder-like methods to construct the routing strategy.
///
/// # Example
/// ```
/// let router = Router::new()
///     .use(use_decl, target)
///     .offer::<OfferProtocolDecl>()
///     .expose::<ExposeProtocolDecl>();
/// ```
pub struct Router<Use = (), Registration = (), Offer = (), Expose = ()> {
    use_: Use,
    registration: Registration,
    offer: Offer,
    expose: Expose,
}

impl Router {
    /// Creates a new `Router` that must be configured with a routing strategy by
    /// calling the various builder-like methods.
    pub fn new() -> Self {
        Router { use_: (), registration: (), offer: (), expose: () }
    }

    /// Configure the `Router` to start routing from an `Expose` declaration to its source.
    pub fn start_expose<E>(
        self,
        expose_decl: E,
        target: Arc<ComponentInstance>,
    ) -> Router<(), (), (), StartFromExpose<E>> {
        Router {
            use_: self.use_,
            registration: self.registration,
            offer: self.offer,
            expose: StartFromExpose {
                expose_decl,
                target,
                expose: Expose { _phantom_data: PhantomData },
            },
        }
    }

    /// Configure the `Router` to start routing from a `Use` declaration.
    pub fn use_<U>(
        self,
        use_decl: U,
        target: Arc<ComponentInstance>,
    ) -> Router<Use<U>, (), (), ()> {
        Router {
            use_: Use { use_decl, target },
            registration: self.registration,
            offer: self.offer,
            expose: self.expose,
        }
    }

    /// Configure the `Router` to start routing from an environment `Registration`
    /// declaration.
    pub fn registration<R>(
        self,
        registration_decl: R,
        target: Arc<ComponentInstance>,
    ) -> Router<(), Registration<R>, (), ()> {
        Router {
            use_: self.use_,
            registration: Registration { registration_decl, target },
            offer: self.offer,
            expose: self.expose,
        }
    }
}

impl<U> Router<Use<U>, (), (), ()> {
    /// Configure the `Router` to route from a `Use` declaration to a matching `Offer`
    /// declaration.
    pub fn offer<O>(self) -> Router<Use<U>, (), Offer<O>, ()> {
        Router {
            use_: self.use_,
            registration: self.registration,
            offer: Offer { _phantom_data: PhantomData },
            expose: self.expose,
        }
    }
}

impl<R> Router<(), Registration<R>, (), ()> {
    /// Configure the `Router` to route from an environment `Registration` declaration to a
    /// matching `Offer` declaration.
    pub fn offer<O>(self) -> Router<(), Registration<R>, Offer<O>, ()> {
        Router {
            use_: self.use_,
            registration: self.registration,
            offer: Offer { _phantom_data: PhantomData },
            expose: self.expose,
        }
    }
}

impl<U, R, O> Router<U, R, Offer<O>, ()> {
    /// Configure the `Router` to route from an `Offer` declaration to a matching `Expose`
    /// declaration.
    pub fn expose<E>(self) -> Router<U, R, Offer<O>, Expose<E>> {
        Router {
            use_: self.use_,
            registration: self.registration,
            offer: self.offer,
            expose: Expose { _phantom_data: PhantomData },
        }
    }
}

// Implement the Use -> Offer routing strategy. In this strategy, there is no Expose.
impl<U, O> Router<Use<U>, (), Offer<O>, ()>
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
    pub async fn route<S, V>(
        self,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySource, ModelError>
    where
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        let start_child_moniker = self.use_.target.child_moniker().cloned();
        let (use_, component_instance) = match self.use_.route(&sources, visitor).await? {
            DeclOrSource::Source(source) => return Ok(source),
            DeclOrSource::Decl(use_, component_instance) => (use_, component_instance),
        };
        match self
            .offer
            .route(use_, component_instance, start_child_moniker.unwrap(), &sources, visitor)
            .await?
        {
            DeclOrSource::Source(source) => Ok(source),
            DeclOrSource::Decl(_, _) => Err(ModelError::unsupported("failed to find source")),
        }
    }
}

// Implement the Expose routing strategy. In this strategy, routing starts from an Expose
// declaration.
impl<E> Router<(), (), (), StartFromExpose<E>>
where
    E: ExposeDeclCommon + ErrorNotFoundInChild + FromEnum<ExposeDecl> + Into<ExposeDecl> + Clone,
{
    /// Routes a capability from its `Expose` declaration to its source by following `Expose`
    /// declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Expose` declaration in the routing path, as well
    /// as the final `Capability` declaration if `sources` permits.
    pub async fn route<S, V>(
        self,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySource, ModelError>
    where
        S: Sources,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        self.expose.route(&sources, visitor).await
    }
}

// Implement the Use -> Offer -> Expose routing strategy. This is the full, common routing
// strategy.
impl<U, O, E> Router<Use<U>, (), Offer<O>, Expose<E>>
where
    U: UseDeclCommon + ErrorNotFoundFromParent + Into<UseDecl>,
    O: OfferDeclCommon
        + ErrorNotFoundFromParent
        + ErrorNotFoundInChild
        + FromEnum<OfferDecl>
        + Into<OfferDecl>
        + Clone,
    E: ExposeDeclCommon + ErrorNotFoundInChild + FromEnum<ExposeDecl> + Into<ExposeDecl> + Clone,
{
    /// Routes a capability from its `Use` declaration to its source by following `Offer` and
    /// `Expose` declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` and `Expose` declaration in the routing path, as well
    /// as the final `Capability` declaration if `sources` permits.
    pub async fn route<S, V>(
        self,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySource, ModelError>
    where
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        let start_child_moniker = self.use_.target.child_moniker().cloned();
        let (use_, component_instance) = match self.use_.route(&sources, visitor).await? {
            DeclOrSource::Source(source) => return Ok(source),
            DeclOrSource::Decl(use_, component_instance) => (use_, component_instance),
        };
        let (offer, component_instance) = match self
            .offer
            .route(use_, component_instance, start_child_moniker.unwrap(), &sources, visitor)
            .await?
        {
            DeclOrSource::Source(source) => return Ok(source),
            DeclOrSource::Decl(offer, component_instance) => (offer, component_instance),
        };
        self.expose.route(offer, component_instance, &sources, visitor).await
    }
}

// Implement the Registration -> Offer -> Expose routing strategy. This is the strategy used
// to route capabilities registered in environments.
impl<R, O, E> Router<(), Registration<R>, Offer<O>, Expose<E>>
where
    R: RegistrationDeclCommon + ErrorNotFoundFromParent + ErrorNotFoundInChild,
    O: OfferDeclCommon
        + ErrorNotFoundFromParent
        + ErrorNotFoundInChild
        + FromEnum<OfferDecl>
        + Into<OfferDecl>
        + Clone,
    E: ExposeDeclCommon + ErrorNotFoundInChild + FromEnum<ExposeDecl> + Into<ExposeDecl> + Clone,
{
    /// Routes a capability from its environment `Registration` declaration to its source by
    /// following `Offer` and `Expose` declarations.
    /// `sources` defines what are the valid sources of the capability.
    /// See [`AllowedSourcesBuilder`].
    /// `visitor` is invoked for each `Offer` and `Expose` declaration in the routing path, as
    /// well as the final `Capability` declaration if `sources` permits.
    pub async fn route<S, V>(
        self,
        sources: S,
        visitor: &mut V,
    ) -> Result<CapabilitySource, ModelError>
    where
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        let start_child_moniker = self.registration.target.child_moniker().cloned();
        let (registration, component_instance) =
            match self.registration.route(&sources, visitor).await? {
                DeclOrSource::Source(source) => return Ok(source),
                DeclOrSource::Decl(registration, component_instance) => {
                    (registration, component_instance)
                }
            };
        match registration.source() {
            RegistrationSource::Parent => {
                // The capability is being registered from the parent, so the offer chain must be
                // ascended.
                let (offer, component_instance) = match self
                    .offer
                    .route(
                        registration,
                        component_instance,
                        start_child_moniker.unwrap(),
                        &sources,
                        visitor,
                    )
                    .await?
                {
                    DeclOrSource::Source(source) => return Ok(source),
                    DeclOrSource::Decl(offer, component_instance) => (offer, component_instance),
                };
                self.expose.route(offer, component_instance, &sources, visitor).await
            }
            RegistrationSource::Child(_) => {
                // The capability is being registered from a child, so the offer routing can be
                // skipped.
                self.expose.route(registration, component_instance, &sources, visitor).await
            }
            _ => unreachable!("handled by registration routing"),
        }
    }
}

/// A trait for extracting the source of a capability.
pub trait Sources {
    /// The supported capability declaration type for namespace and component sources.
    /// Can be `()` if namespace and component sources are not supported.
    type CapabilityDecl;

    /// Return the [`InternalCapability`] representing this framework capability source, or
    /// [`ModelError::Unsupported`] if unsupported.
    fn framework_source(&self, name: CapabilityName) -> Result<InternalCapability, ModelError>;

    /// Return the [`InternalCapability`] representing this built-in capability source, or
    /// [`ModelError::Unsupported`] if unsupported.
    fn builtin_source(&self, name: CapabilityName) -> Result<InternalCapability, ModelError>;

    /// Checks whether capability sources are supported, returning [`ModelError::Unsupported`]
    /// if they are not.
    fn capability_source(&self) -> Result<(), ModelError>;

    /// Checks whether namespace capability sources are supported.
    fn is_namespace_supported(&self) -> bool;

    /// Looks for a namespace capability in the list of capability sources.
    /// If found, the declaration is visited by `visitor` and the declaration is wrapped
    /// in a [`ComponentCapability`].
    /// Returns [`ModelError::Unsupported`] if namespace capabilities are unsupported.
    fn find_namespace_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<Option<ComponentCapability>, ModelError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>;

    /// Looks for a component capability in the list of capability sources.
    /// If found, the declaration is visited by `visitor` and the declaration is wrapped
    /// in a [`ComponentCapability`].
    /// Returns [`ModelError::Unsupported`] if component capabilities are unsupported.
    fn find_component_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<ComponentCapability, ModelError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>;
}

/// Defines which capability source types are supported.
pub struct AllowedSourcesBuilder<CapabilityDecl = ()> {
    framework: Option<fn(CapabilityName) -> InternalCapability>,
    builtin: Option<fn(CapabilityName) -> InternalCapability>,
    capability: bool,
    namespace: bool,
    component: bool,
    _decl: PhantomData<CapabilityDecl>,
}

impl<C> AllowedSourcesBuilder<C> {
    /// Creates a new [`AllowedSourcesBuilder`] that does not allow any capability source types.
    pub fn new() -> Self {
        Self {
            framework: None,
            builtin: None,
            capability: false,
            namespace: false,
            component: false,
            _decl: PhantomData,
        }
    }

    /// Allows framework capability source types (`from: "framework"` in `CML`).
    pub fn framework(self, builder: fn(CapabilityName) -> InternalCapability) -> Self {
        Self { framework: Some(builder), ..self }
    }

    /// Allows built-in capability source types (`from: "parent"` in `CML` where the parent component_instance is
    /// component_manager).
    pub fn builtin(self, builder: fn(CapabilityName) -> InternalCapability) -> Self {
        Self { builtin: Some(builder), ..self }
    }

    /// Allows capability source types that originate from other capabilities (`from: "#storage"` in
    /// `CML`).
    pub fn capability(self) -> Self {
        Self { capability: true, ..self }
    }
}

impl<C> AllowedSourcesBuilder<Allow<C>>
where
    C: CapabilityDeclCommon,
{
    /// Allows namespace capability source types, which are capabilities that are installed in
    /// component_manager's incoming namespace.
    pub fn namespace(self) -> Self {
        Self { namespace: true, ..self }
    }

    /// Allows component capability source types (`from: "self"` in `CML`).
    pub fn component(self) -> Self {
        Self { component: true, ..self }
    }
}

/// Marker that differentiates an [`AllowedSourcesBuilder`] that allows namespace or component
/// source types from one that does not.
pub struct Allow<C>(PhantomData<C>);

// Implementation of `Sources` that does not allow namespace or component source types, which means
// the `CapabilityDecl` type is not needed.
impl Sources for AllowedSourcesBuilder<()> {
    type CapabilityDecl = ();

    fn framework_source(&self, name: CapabilityName) -> Result<InternalCapability, ModelError> {
        self.framework
            .as_ref()
            .map(|b| b(name))
            .ok_or_else(|| ModelError::unsupported("routing from framework"))
    }

    fn builtin_source(&self, name: CapabilityName) -> Result<InternalCapability, ModelError> {
        self.builtin
            .as_ref()
            .map(|b| b(name))
            .ok_or_else(|| ModelError::unsupported("routing from built-in capability"))
    }

    fn capability_source(&self) -> Result<(), ModelError> {
        if self.capability {
            Ok(())
        } else {
            Err(ModelError::unsupported("routing from other capability"))
        }
    }

    fn is_namespace_supported(&self) -> bool {
        false
    }

    fn find_namespace_source<V>(
        &self,
        _: &CapabilityName,
        _: &[CapabilityDecl],
        _: &mut V,
    ) -> Result<Option<ComponentCapability>, ModelError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
    {
        Err(ModelError::unsupported("routing from namespace"))
    }

    fn find_component_source<V>(
        &self,
        _: &CapabilityName,
        _: &[CapabilityDecl],
        _: &mut V,
    ) -> Result<ComponentCapability, ModelError>
    where
        V: CapabilityVisitor<CapabilityDecl = Self::CapabilityDecl>,
    {
        Err(ModelError::unsupported("routing from component"))
    }
}

/// Implementation of `Sources` that allows namespace and component source types, which means
/// the `CapabilityDecl` must satisfy some specific type constraints.
impl<C> Sources for AllowedSourcesBuilder<Allow<C>>
where
    C: CapabilityDeclCommon + FromEnum<CapabilityDecl> + Into<ComponentCapability> + Clone,
{
    type CapabilityDecl = C;

    fn framework_source(&self, name: CapabilityName) -> Result<InternalCapability, ModelError> {
        self.framework
            .as_ref()
            .map(|b| b(name))
            .ok_or_else(|| ModelError::unsupported("routing from framework"))
    }

    fn builtin_source(&self, name: CapabilityName) -> Result<InternalCapability, ModelError> {
        self.builtin
            .as_ref()
            .map(|b| b(name))
            .ok_or_else(|| ModelError::unsupported("routing from built-in capability"))
    }

    fn capability_source(&self) -> Result<(), ModelError> {
        if self.capability {
            Ok(())
        } else {
            Err(ModelError::unsupported("routing from other capability"))
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
    ) -> Result<Option<ComponentCapability>, ModelError>
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
            Err(ModelError::unsupported("routing from namespace"))
        }
    }

    fn find_component_source<V>(
        &self,
        name: &CapabilityName,
        capabilities: &[CapabilityDecl],
        visitor: &mut V,
    ) -> Result<ComponentCapability, ModelError>
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
            Err(ModelError::unsupported("routing from component"))
        }
    }
}

/// The result of one phase of routing. Either the source of the capability was found,
/// or the next phase of routing must begin.
enum DeclOrSource<D> {
    Decl(D, Arc<ComponentInstance>),
    Source(CapabilitySource),
}

/// The `Use` phase of routing.
pub struct Use<U> {
    use_decl: U,
    target: Arc<ComponentInstance>,
}

impl<U> Use<U>
where
    U: UseDeclCommon + Into<UseDecl>,
{
    /// Routes the capability starting from `self.use_decl` to either a valid source (as defined by
    /// `sources`) or the declaration that ends this phase of routing.
    async fn route<S, V>(self, sources: &S, visitor: &mut V) -> Result<DeclOrSource<U>, ModelError>
    where
        S: Sources,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        match self.use_decl.source() {
            UseSource::Framework => Ok(DeclOrSource::Source(CapabilitySource::Framework {
                capability: sources.framework_source(self.use_decl.source_name().clone())?,
                scope_moniker: self.target.abs_moniker.clone(),
            })),
            UseSource::Capability(_) => {
                sources.capability_source()?;
                Ok(DeclOrSource::Source(CapabilitySource::Capability {
                    component: self.target.as_weak(),
                    source_capability: ComponentCapability::Use(self.use_decl.into()),
                }))
            }
            UseSource::Parent => match self.target.try_get_parent()? {
                ExtendedInstance::AboveRoot(cm_component_instance) => {
                    if sources.is_namespace_supported() {
                        if let Some(capability) = sources.find_namespace_source(
                            self.use_decl.source_name(),
                            &cm_component_instance.namespace_capabilities,
                            visitor,
                        )? {
                            return Ok(DeclOrSource::Source(CapabilitySource::Namespace {
                                capability,
                            }));
                        }
                    }
                    Ok(DeclOrSource::Source(CapabilitySource::Builtin {
                        capability: sources.builtin_source(self.use_decl.source_name().clone())?,
                    }))
                }
                ExtendedInstance::Component(component_instance) => {
                    Ok(DeclOrSource::Decl(self.use_decl, component_instance))
                }
            },
            UseSource::Debug => {
                // This is not supported today. It might be worthwhile to support this if
                // more than just protocol has a debug capability.
                return Err(ModelError::unsupported("debug capability"));
            }
        }
    }
}

/// The environment `Registration` phase of routing.
pub struct Registration<R> {
    registration_decl: R,
    target: Arc<ComponentInstance>,
}

impl<R> Registration<R>
where
    R: RegistrationDeclCommon,
{
    /// Routes the capability starting from `self.registration_decl` to either a valid source
    /// (as defined by `sources`) or the declaration that ends this phase of routing.
    async fn route<S, V>(self, sources: &S, visitor: &mut V) -> Result<DeclOrSource<R>, ModelError>
    where
        S: Sources,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        match self.registration_decl.source() {
            RegistrationSource::Self_ => {
                let component_instance_state = self.target.lock_resolved_state().await?;
                Ok(DeclOrSource::Source(CapabilitySource::Component {
                    capability: sources.find_component_source(
                        self.registration_decl.source_name(),
                        &component_instance_state.decl().capabilities,
                        visitor,
                    )?,
                    component: self.target.as_weak(),
                }))
            }
            RegistrationSource::Parent => match self.target.try_get_parent()? {
                ExtendedInstance::AboveRoot(cm_component_instance) => {
                    if sources.is_namespace_supported() {
                        if let Some(capability) = sources.find_namespace_source(
                            self.registration_decl.source_name(),
                            &cm_component_instance.namespace_capabilities,
                            visitor,
                        )? {
                            return Ok(DeclOrSource::Source(CapabilitySource::Namespace {
                                capability,
                            }));
                        }
                    }
                    Ok(DeclOrSource::Source(CapabilitySource::Builtin {
                        capability: sources
                            .builtin_source(self.registration_decl.source_name().clone())?,
                    }))
                }
                ExtendedInstance::Component(component_instance) => {
                    Ok(DeclOrSource::Decl(self.registration_decl, component_instance))
                }
            },
            RegistrationSource::Child(child) => {
                let component_instance_state = self.target.lock_resolved_state().await?;
                let partial = PartialMoniker::new(child.clone(), None);
                let child_component_instance = component_instance_state
                    .get_live_child(&partial)
                    .ok_or_else(|| RoutingError::EnvironmentFromChildInstanceNotFound {
                        child_moniker: partial,
                        moniker: self.target.abs_moniker.clone(),
                        capability_name: self.registration_decl.source_name().clone(),
                        capability_type: R::TYPE,
                    })?;
                Ok(DeclOrSource::Decl(self.registration_decl, child_component_instance))
            }
        }
    }
}

/// The `Offer` phase of routing.
pub struct Offer<O> {
    _phantom_data: PhantomData<O>,
}

impl<O> Offer<O>
where
    O: OfferDeclCommon + ErrorNotFoundFromParent + FromEnum<OfferDecl> + Into<OfferDecl> + Clone,
{
    /// Routes the capability starting from `start_decl` to either a valid source (as defined by
    /// `sources`) or the declaration that ends this phase of routing.
    async fn route<S, V, U>(
        self,
        start_decl: U,
        component_instance: Arc<ComponentInstance>,
        last_child_moniker: ChildMoniker,
        sources: &S,
        visitor: &mut V,
    ) -> Result<DeclOrSource<O>, ModelError>
    where
        S: Sources,
        V: OfferVisitor<OfferDecl = O>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        U: SourceName + ErrorNotFoundFromParent,
    {
        let (component_instance, offer) = ascend_offer_chain(
            component_instance,
            last_child_moniker,
            AscendStartDecl::Start(start_decl),
            visitor,
        )
        .await?;
        let component_instance = match component_instance {
            ExtendedInstance::AboveRoot(cm_component_instance) => {
                if sources.is_namespace_supported() {
                    if let Some(capability) = sources.find_namespace_source(
                        offer.source_name(),
                        &cm_component_instance.namespace_capabilities,
                        visitor,
                    )? {
                        return Ok(DeclOrSource::Source(CapabilitySource::Namespace {
                            capability,
                        }));
                    }
                }
                return Ok(DeclOrSource::Source(CapabilitySource::Builtin {
                    capability: sources.builtin_source(offer.source_name().clone())?,
                }));
            }
            ExtendedInstance::Component(component_instance) => component_instance,
        };
        match offer.source() {
            OfferSource::Framework => Ok(DeclOrSource::Source(CapabilitySource::Framework {
                capability: sources.framework_source(offer.source_name().clone())?,
                scope_moniker: component_instance.abs_moniker.clone(),
            })),
            OfferSource::Self_ => {
                let component_instance_state = component_instance.lock_resolved_state().await?;
                Ok(DeclOrSource::Source(CapabilitySource::Component {
                    capability: sources.find_component_source(
                        offer.source_name(),
                        &component_instance_state.decl().capabilities,
                        visitor,
                    )?,
                    component: component_instance.as_weak(),
                }))
            }
            OfferSource::Capability(_) => {
                sources.capability_source()?;
                Ok(DeclOrSource::Source(CapabilitySource::Capability {
                    source_capability: ComponentCapability::Offer(offer.into()),
                    component: component_instance.as_weak(),
                }))
            }
            OfferSource::Child(child) => {
                let component_instance_state = component_instance.lock_resolved_state().await?;
                let partial = PartialMoniker::new(child.clone(), None);
                let child_component_instance = component_instance_state
                    .get_live_child(&partial)
                    .ok_or_else(|| RoutingError::OfferFromChildInstanceNotFound {
                        child_moniker: partial,
                        moniker: component_instance.abs_moniker.clone(),
                        capability_id: offer.source_name().clone().into(),
                    })?;
                Ok(DeclOrSource::Decl(offer, child_component_instance))
            }
            OfferSource::Parent => unreachable!("handled in ascend_offer_chain"),
        }
    }
}

/// The `Expose` phase of routing.
pub struct Expose<E> {
    _phantom_data: PhantomData<E>,
}

impl<E> Expose<E>
where
    E: ExposeDeclCommon + ErrorNotFoundInChild + FromEnum<ExposeDecl> + Into<ExposeDecl> + Clone,
{
    /// Routes the capability starting from `offer_decl` to a valid source (as defined by
    /// `sources`).
    async fn route<S, V, O>(
        self,
        offer_decl: O,
        component_instance: Arc<ComponentInstance>,
        sources: &S,
        visitor: &mut V,
    ) -> Result<CapabilitySource, ModelError>
    where
        S: Sources,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
        O: SourceName + ErrorNotFoundInChild,
    {
        let (component_instance, expose) =
            descend_expose_chain(component_instance, DescendStartDecl::Start(offer_decl), visitor)
                .await?;
        match expose.source() {
            ExposeSource::Self_ => {
                let component_instance_state = component_instance.lock_resolved_state().await?;
                Ok(CapabilitySource::Component {
                    capability: sources.find_component_source(
                        expose.source_name(),
                        &component_instance_state.decl().capabilities,
                        visitor,
                    )?,
                    component: component_instance.as_weak(),
                })
            }
            ExposeSource::Framework => Ok(CapabilitySource::Framework {
                capability: sources.framework_source(expose.source_name().clone())?,
                scope_moniker: component_instance.abs_moniker.clone(),
            }),
            ExposeSource::Capability(_) => {
                sources.capability_source()?;
                Ok(CapabilitySource::Capability {
                    source_capability: ComponentCapability::Expose(expose.into()),
                    component: component_instance.as_weak(),
                })
            }
            ExposeSource::Child(_) => unreachable!("handled by descend_expose_chain"),
        }
    }
}

/// The special `StartFromExpose` phase of routing.
pub struct StartFromExpose<E> {
    expose_decl: E,
    target: Arc<ComponentInstance>,
    expose: Expose<E>,
}

impl<E> StartFromExpose<E>
where
    E: ExposeDeclCommon + ErrorNotFoundInChild + FromEnum<ExposeDecl> + Into<ExposeDecl> + Clone,
{
    /// Routes the capability starting from `self.expose_decl` to a valid source
    /// (as defined by `sources`).
    async fn route<S, V>(self, sources: &S, visitor: &mut V) -> Result<CapabilitySource, ModelError>
    where
        S: Sources,
        V: ExposeVisitor<ExposeDecl = E>,
        V: CapabilityVisitor<CapabilityDecl = S::CapabilityDecl>,
    {
        ExposeVisitor::visit(visitor, &self.expose_decl)?;
        match self.expose_decl.source() {
            ExposeSource::Self_ => {
                let component_instance_state = self.target.lock_resolved_state().await?;
                Ok(CapabilitySource::Component {
                    capability: sources.find_component_source(
                        self.expose_decl.source_name(),
                        &component_instance_state.decl().capabilities,
                        visitor,
                    )?,
                    component: self.target.as_weak(),
                })
            }
            ExposeSource::Framework => Ok(CapabilitySource::Framework {
                capability: sources.framework_source(self.expose_decl.source_name().clone())?,
                scope_moniker: self.target.abs_moniker.clone(),
            }),
            ExposeSource::Capability(_) => {
                sources.capability_source()?;
                Ok(CapabilitySource::Capability {
                    source_capability: ComponentCapability::Expose(self.expose_decl.into()),
                    component: self.target.as_weak(),
                })
            }
            ExposeSource::Child(child) => {
                let child_component_instance = {
                    let component_instance_state = self.target.lock_resolved_state().await?;
                    let partial = PartialMoniker::new(child.clone(), None);
                    component_instance_state.get_live_child(&partial).ok_or_else(|| {
                        RoutingError::ExposeFromChildInstanceNotFound {
                            child_moniker: partial,
                            moniker: self.target.abs_moniker.clone(),
                            capability_id: self.expose_decl.source_name().clone().into(),
                        }
                    })?
                };
                self.expose
                    .route(self.expose_decl, child_component_instance, sources, visitor)
                    .await
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
    fn visit(&mut self, offer: &Self::OfferDecl) -> Result<(), ModelError>;
}

/// Visitor pattern trait for visiting a variant of [`ExposeDecl`] specific to a capability type.
pub trait ExposeVisitor {
    /// The concrete declaration type.
    type ExposeDecl: ExposeDeclCommon;

    /// Visit a variant of [`ExposeDecl`] specific to the capability.
    /// Returning an `Err` cancels visitation.
    fn visit(&mut self, expose: &Self::ExposeDecl) -> Result<(), ModelError>;
}

/// Visitor pattern trait for visiting a variant of [`CapabilityDecl`] specific to a capability
/// type.
pub trait CapabilityVisitor {
    /// The concrete declaration type. Can be `()` if the capability type does not support
    /// namespace or component source types.
    type CapabilityDecl;

    /// Visit a variant of [`CapabilityDecl`] specific to the capability.
    /// Returning an `Err` cancels visitation.
    fn visit(&mut self, _capability_decl: &Self::CapabilityDecl) -> Result<(), ModelError> {
        Ok(())
    }
}

enum AscendStartDecl<S, O> {
    Start(S),
    Offer(O),
}

impl<S: SourceName, O: SourceName> AscendStartDecl<S, O> {
    fn source_name(&self) -> &CapabilityName {
        match self {
            AscendStartDecl::Start(s) => s.source_name(),
            AscendStartDecl::Offer(o) => o.source_name(),
        }
    }
}

/// Ascend the component topology at `component_instance` by following `Offer` declarations starting from `decl`.
/// Each `Offer` declaration is visited by `visitor`. Traversal stops when the component manager's
/// component_instance is reached, a source is found, or an `Offer`-from-child declaration is found.
async fn ascend_offer_chain<S, V>(
    mut component_instance: Arc<ComponentInstance>,
    mut child_moniker: ChildMoniker,
    mut decl: AscendStartDecl<S, V::OfferDecl>,
    visitor: &mut V,
) -> Result<(ExtendedInstance, V::OfferDecl), ModelError>
where
    S: SourceName + ErrorNotFoundFromParent,
    V: OfferVisitor,
    V::OfferDecl: ErrorNotFoundFromParent + FromEnum<OfferDecl> + Clone,
{
    loop {
        let capability_name = decl.source_name();
        let component_instance_state = component_instance.lock_resolved_state().await?;
        let offer = component_instance_state
            .decl()
            .offers
            .iter()
            .flat_map(<V::OfferDecl as FromEnum<OfferDecl>>::from_enum)
            .find(|offer| {
                *offer.target_name() == *capability_name
                    && target_matches_moniker(offer.target(), &child_moniker)
            })
            .ok_or_else(|| {
                let abs_moniker = component_instance.abs_moniker.child(child_moniker.clone());
                match &decl {
                    AscendStartDecl::Start(_) => {
                        S::error_not_found_from_parent(abs_moniker, capability_name.clone())
                    }
                    AscendStartDecl::Offer(_) => V::OfferDecl::error_not_found_from_parent(
                        abs_moniker,
                        capability_name.clone(),
                    ),
                }
            })?;
        visitor.visit(offer)?;
        match offer.source() {
            OfferSource::Parent => {
                decl = AscendStartDecl::Offer(offer.clone());
                let parent_component_instance = match component_instance.try_get_parent()? {
                    ExtendedInstance::Component(component_instance) => component_instance,
                    ExtendedInstance::AboveRoot(component_instance) => {
                        return Ok((ExtendedInstance::AboveRoot(component_instance), offer.clone()))
                    }
                };
                drop(component_instance_state);
                child_moniker =
                    component_instance.child_moniker().cloned().expect("ChildMoniker should exist");
                component_instance = parent_component_instance;
            }
            _ => {
                let offer = offer.clone();
                drop(component_instance_state);
                return Ok((ExtendedInstance::Component(component_instance), offer));
            }
        }
    }
}

enum DescendStartDecl<S, E> {
    Start(S),
    Expose(E),
}

impl<S, E> DescendStartDecl<S, E>
where
    S: SourceName,
    E: SourceName,
{
    fn source_name(&self) -> &CapabilityName {
        match self {
            DescendStartDecl::Start(s) => s.source_name(),
            DescendStartDecl::Expose(e) => e.source_name(),
        }
    }
}

/// Descend the component topology at `component_instance` by following `Expose` declarations starting from
/// `decl`. Each `Expose` declaration is visited by `visitor`. Traversal stops when a source is
/// found.
async fn descend_expose_chain<S, V>(
    mut component_instance: Arc<ComponentInstance>,
    mut decl: DescendStartDecl<S, V::ExposeDecl>,
    visitor: &mut V,
) -> Result<(Arc<ComponentInstance>, V::ExposeDecl), ModelError>
where
    S: SourceName + ErrorNotFoundInChild,
    V: ExposeVisitor,
    V::ExposeDecl: ErrorNotFoundInChild + FromEnum<ExposeDecl> + Clone,
{
    loop {
        let capability_name = decl.source_name();
        let component_instance_state = component_instance.lock_resolved_state().await?;
        let expose = component_instance_state
            .decl()
            .exposes
            .iter()
            .flat_map(<V::ExposeDecl as FromEnum<ExposeDecl>>::from_enum)
            .find(|expose| {
                *expose.target_name() == *capability_name
                    && *expose.target() == ExposeTarget::Parent
            })
            .ok_or_else(|| {
                let child_moniker = component_instance
                    .child_moniker()
                    .expect("ChildMoniker should exist")
                    .to_partial();
                let moniker = component_instance.abs_moniker.parent().expect("Must have parent");
                match &decl {
                    DescendStartDecl::Start(_) => {
                        S::error_not_found_in_child(moniker, child_moniker, capability_name.clone())
                    }
                    DescendStartDecl::Expose(_) => V::ExposeDecl::error_not_found_in_child(
                        moniker,
                        child_moniker,
                        capability_name.clone(),
                    ),
                }
            })?;
        visitor.visit(expose)?;
        match expose.source() {
            ExposeSource::Child(child_name) => {
                let capability_name = expose.source_name().clone();
                decl = DescendStartDecl::Expose(expose.clone());
                let child_moniker = PartialMoniker::new(child_name.clone(), None);
                let child_component_instance = component_instance_state
                    .get_live_child(&child_moniker)
                    .ok_or_else(|| RoutingError::ExposeFromChildInstanceNotFound {
                        child_moniker,
                        moniker: component_instance.abs_moniker.clone(),
                        capability_id: capability_name.into(),
                    })?;
                drop(component_instance_state);
                component_instance = child_component_instance;
            }
            _ => {
                let expose = expose.clone();
                drop(component_instance_state);
                return Ok((component_instance, expose));
            }
        }
    }
}

/// Implemented by declaration types to emit a proper error when a matching offer is not found in the parent.
pub trait ErrorNotFoundFromParent {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError;
}

/// Implemented by declaration types to emit a proper error when a matching expose is not found in the child.
pub trait ErrorNotFoundInChild {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError;
}

/// Creates a unit struct that implements a visitor for each declared type.
macro_rules! make_noop_visitor {
    ($name:ident, {
        $(OfferDecl => $offer_decl:ty,)*
        $(ExposeDecl => $expose_decl:ty,)*
        $(CapabilityDecl => $cap_decl:ty,)*
    }) => {
        struct $name;

        $(
            impl crate::model::routing::router::OfferVisitor for $name {
                type OfferDecl = $offer_decl;

                fn visit(&mut self, _decl: &Self::OfferDecl) -> Result<(), crate::model::error::ModelError> {
                    Ok(())
                }
            }
        )*

        $(
            impl crate::model::routing::router::ExposeVisitor for $name {
                type ExposeDecl = $expose_decl;

                fn visit(&mut self, _decl: &Self::ExposeDecl) -> Result<(), crate::model::error::ModelError> {
                    Ok(())
                }
            }
        )*

        $(
            impl crate::model::routing::router::CapabilityVisitor for $name {
                type CapabilityDecl = $cap_decl;

                fn visit(&mut self, _decl: &Self::CapabilityDecl) -> Result<(), crate::model::error::ModelError> {
                    Ok(())
                }
            }
        )*
    };
}
