// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Construct component realms by listing the components and the routes between them

use {
    crate::{error::*, mock, Moniker, Realm},
    anyhow, cm_rust,
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
    futures::future::BoxFuture,
    maplit::hashmap,
    std::{collections::HashMap, convert::TryInto},
};

/// A capability that is routed through the custom realms
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum Capability {
    Protocol(String),
    // Name, Path, rights
    Directory(String, String, fio2::Operations),
    Event(Event, cm_rust::EventMode),
    // Name, path
    Storage(String, String),
}

impl Capability {
    pub fn protocol(name: impl Into<String>) -> Self {
        Self::Protocol(name.into())
    }

    pub fn directory(
        name: impl Into<String>,
        path: impl Into<String>,
        rights: fio2::Operations,
    ) -> Self {
        Self::Directory(name.into(), path.into(), rights)
    }

    pub fn event(event: Event, mode: cm_rust::EventMode) -> Self {
        Self::Event(event, mode)
    }

    pub fn storage(name: impl Into<String>, path: impl Into<String>) -> Self {
        Self::Storage(name.into(), path.into())
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum Event {
    Started,
    Stopped,
    Running,
    // Filter.name
    CapabilityRequested(String),
    // Filter.name
    DirectoryReady(String),
}

impl Event {
    pub fn started() -> Self {
        Self::Started
    }

    pub fn stopped() -> Self {
        Self::Stopped
    }

    pub fn running() -> Self {
        Self::Running
    }
    pub fn capability_requested(filter_name: impl Into<String>) -> Self {
        Self::CapabilityRequested(filter_name.into())
    }

    pub fn directory_ready(filter_name: impl Into<String>) -> Self {
        Self::DirectoryReady(filter_name.into())
    }
}

impl Event {
    fn name(&self) -> &'static str {
        match self {
            Event::Started => "started",
            Event::Stopped => "stopped",
            Event::Running => "running",
            Event::CapabilityRequested(_) => "capability_requested",
            Event::DirectoryReady(_) => "directory_ready",
        }
    }

    /// Returns the Event Filter that some events (like DirectoryReady and CapabilityRequested)
    /// have.
    fn filter(&self) -> Option<HashMap<String, cm_rust::DictionaryValue>> {
        match self {
            Event::CapabilityRequested(name) | Event::DirectoryReady(name) => Some(
                hashmap!("name".to_string() => cm_rust::DictionaryValue::Str(name.to_string())),
            ),
            _ => None,
        }
    }
}

/// The source or destination of a capability route.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum RouteEndpoint {
    /// One end of this capability route is a component in our custom realms. The value of this
    /// should be a moniker that was used in a prior [`RealmBuilder::add_component`] call.
    Component(String),

    /// One end of this capability route is above the root component in the generated realms
    AboveRoot,
}

impl RouteEndpoint {
    pub fn component(path: impl Into<String>) -> Self {
        Self::Component(path.into())
    }

    pub fn above_root() -> Self {
        Self::AboveRoot
    }

    fn unwrap_component_moniker(&self) -> Moniker {
        match self {
            RouteEndpoint::Component(m) => m.clone().into(),
            _ => panic!("capability source is not a component"),
        }
    }
}

/// A capability route from one source component to one or more target components.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CapabilityRoute {
    pub capability: Capability,
    pub source: RouteEndpoint,
    pub targets: Vec<RouteEndpoint>,
}

/// The source for a component
#[derive(Clone)]
pub enum ComponentSource {
    /// A component URL, such as `fuchsia-pkg://fuchsia.com/package-name#meta/manifest.cm`
    Url(String),

    /// An in-process component mock
    Mock(mock::Mock),
}

impl ComponentSource {
    pub fn url(url: impl Into<String>) -> Self {
        Self::Url(url.into())
    }

    pub fn mock<M>(mock_fn: M) -> Self
    where
        M: Fn(mock::MockHandles) -> BoxFuture<'static, Result<(), anyhow::Error>>
            + Sync
            + Send
            + 'static,
    {
        Self::Mock(mock::Mock::new(mock_fn))
    }
}

#[derive(Clone)]
struct Component {
    source: ComponentSource,
    eager: bool,
}

/// `RealmBuilder` takes as input a set of component definitions and routes between them and
/// produces a `Realm` which can be run in a [component
/// collection](https://fuchsia.dev/fuchsia-src/concepts/components/v2/realms#collections).
///
/// The source for a developer-component may be either a URL or a local component mock. See
/// [`Mock`] for more information on component mocks.
///
/// For an example of using a `RealmBuilder`, imagine following structure:
///
/// ```
///   a
///  / \
/// b   c
///     |
///     d
/// ```
///
/// Where `d` is a URL component and `b` is a mock component, `d` accesses the `fuchsia.foobar`
/// protocol from `b`, and the `artifacts` directory is exposed from `d` up through `a`. This
/// structure can be built with the following:
///
/// ```
/// let mut builder = RealmBuilder::new().await?;
/// builder.add_component("c/d", ComponentSource::url("fuchsia-pkg://fuchsia.com/d#meta/d.cm"))?
///        .add_component("b", ComponentSource::mock(move |h: MockHandles| {
///            Box::pin(implementation_for_b(h))
///        }))?
///        .add_route(CapabilityRoute {
///            capability: Capability::protocol("fuchsia.foobar"),
///            source: RouteEndpoint::component("b"),
///            targets: vec![RouteEndpoint::component("c/d")],
///        })?
///        .add_route(CapabilityRoute {
///            capability: Capability::Directory(
///                "artifacts",
///                "/path-for-artifacts",
///                fio2::RW_STAR_DIR
///            ),
///            source: RouteEndpoint::component("c/d"),
///            targets: vec![RouteEndpoint::AboveRoot],
///        })?;
/// let realm = builder.build().await?;
/// ```
///
/// Note that the root component in our imagined structure is actually unnamed when working with
/// the [`Realm`] and `RealmBuilder`. The name is generated when the component is created in a
/// collection.
///
/// Due to the approach taken here of generating the non-executable components, only leaf nodes in
/// the generated component tree may be developer-provided. This means, for example, that a mock
/// component may not be a parent of another component, offering its capabilities to the child. The
/// realms should instead have the mock component as a sibling, with the mock's generated
/// non-executable parent offering the mock's capabilities to the child.
pub struct RealmBuilder {
    realm: Realm,
}

impl RealmBuilder {
    pub async fn new() -> Result<Self, Error> {
        Ok(Self { realm: Realm::new().await? })
    }

    pub fn build_on(realm: Realm) -> Self {
        Self { realm }
    }

    /// Adds a new component to the realm. The `moniker` field should be one or more component
    /// child names separated by `/`s.
    ///
    /// Any missing parent components will be automatically filled in with empty component
    /// declarations, so when adding components to the builder parents should be added before
    /// children. As an example, to add components "a" and "a/b", the calls must be made in this
    /// order:
    ///
    /// ```
    /// let mut builder = RealmBuilder::new().await?;
    /// builder.add_component("a", ComponentSource::Mock(...))?
    ///        .add_component("a/b", ComponentSource::Mock(...))?
    /// ```
    ///
    /// If the `add_component` calls were reversed the second one would cause a
    /// `ComponentAlreadyExists` error, because an `a` component would be generated as part of the
    /// call to add `a/b`.
    pub async fn add_component<M>(
        &mut self,
        moniker: M,
        source: ComponentSource,
    ) -> Result<&mut Self, Error>
    where
        M: Into<Moniker>,
    {
        let moniker = moniker.into();
        if self.realm.contains(&moniker) {
            // TODO: differentiate errors between "this already exists" and "this is a leaf node"
            return Err(BuilderError::ComponentAlreadyExists(moniker).into());
        }

        if moniker != Moniker::root() && !self.realm.contains(&Moniker::root()) {
            self.realm.add_component(Moniker::root(), cm_rust::ComponentDecl::default())?;
        }
        let ancestry = moniker.ancestry();
        // The ancestry is sorted from child to parent, but we need to add components to
        // the tree in the other direction.
        for ancestor in ancestry.into_iter().rev() {
            if !self.realm.contains(&ancestor) {
                self.realm.add_component(ancestor, cm_rust::ComponentDecl::default())?;
            }
        }

        match source {
            ComponentSource::Url(url) => {
                if moniker.is_root() {
                    return Err(BuilderError::RootComponentCantHaveUrl.into());
                }
                let parent_moniker = moniker.parent().unwrap();
                let parent_decl = self.realm.get_decl_mut(&parent_moniker)?;
                parent_decl.children.push(cm_rust::ChildDecl {
                    name: moniker.child_name().unwrap().clone(),
                    url: url.to_string(),
                    startup: fsys::StartupMode::Lazy,
                    environment: None,
                });
            }
            ComponentSource::Mock(mock) => {
                self.realm.add_mocked_component(moniker, mock).await?;
            }
        }
        Ok(self)
    }

    /// Identical to add_component, but the child (and any ancestors it has) will be marked as
    /// eager from the root component.
    pub async fn add_eager_component<M>(
        &mut self,
        moniker: M,
        source: ComponentSource,
    ) -> Result<&mut Self, Error>
    where
        M: Into<Moniker>,
    {
        let moniker = moniker.into();
        self.add_component(moniker.clone(), source).await?;
        self.realm.mark_as_eager(&moniker)?;
        for ancestor in moniker.ancestry() {
            self.realm.mark_as_eager(&ancestor)?;
        }
        Ok(self)
    }

    /// Adds a protocol capability route between the `source` endpoint and
    /// the provided `targets`.
    pub fn add_protocol_route<S: DiscoverableService>(
        &mut self,
        source: RouteEndpoint,
        targets: Vec<RouteEndpoint>,
    ) -> Result<&mut Self, Error> {
        self.add_route(CapabilityRoute {
            capability: Capability::protocol(S::SERVICE_NAME),
            source,
            targets,
        })
    }

    /// Adds a capability route between two points in the realm. Does nothing if the route
    /// already exists.
    pub fn add_route(&mut self, route: CapabilityRoute) -> Result<&mut Self, Error> {
        if let RouteEndpoint::Component(moniker) = &route.source {
            let moniker = moniker.clone().into();
            if !self.realm.contains(&moniker) {
                match moniker.parent().and_then(|p| Some(self.realm.get_decl_mut(&p))) {
                    Some(Ok(decl))
                        if decl.children.iter().any(|c| Some(&c.name) == moniker.child_name()) =>
                    {
                        ()
                    }
                    _ => return Err(BuilderError::MissingRouteSource(moniker).into()),
                }
            }
        }
        if route.targets.is_empty() {
            return Err(BuilderError::EmptyRouteTargets.into());
        }
        for target in &route.targets {
            if &route.source == target {
                return Err(BuilderError::RouteSourceAndTargetMatch(route.clone()).into());
            }
            if let RouteEndpoint::Component(moniker) = target {
                let moniker = moniker.clone().into();
                if !self.realm.contains(&moniker) {
                    return Err(BuilderError::MissingRouteTarget(moniker).into());
                }
            }
        }

        for target in &route.targets {
            if *target == RouteEndpoint::AboveRoot {
                // We're routing a capability from component within our constructed realm to
                // somewhere above it
                let source_moniker = route.source.unwrap_component_moniker();

                if let Ok(source_decl) = self.realm.get_decl_mut(&source_moniker) {
                    Self::add_expose_for_capability(
                        &mut source_decl.exposes,
                        &route,
                        None,
                        &source_moniker,
                    )?;
                    Self::add_capability_decl(&mut source_decl.capabilities, &route.capability)?;
                }

                let mut current_ancestor = source_moniker;
                while !current_ancestor.is_root() {
                    let child_name = current_ancestor.child_name().unwrap().clone();
                    current_ancestor = current_ancestor.parent().unwrap();

                    let decl = self.realm.get_decl_mut(&current_ancestor)?;
                    Self::add_expose_for_capability(
                        &mut decl.exposes,
                        &route,
                        Some(&child_name),
                        &current_ancestor,
                    )?;
                }
            } else if route.source == RouteEndpoint::AboveRoot {
                // We're routing a capability from above our constructed realm to a component
                // eithin it
                let target_moniker = target.unwrap_component_moniker();

                if let Ok(target_decl) = self.realm.get_decl_mut(&target_moniker) {
                    target_decl.uses.push(Self::new_use_decl(&route.capability));
                }

                let mut current_ancestor = target_moniker;
                while !current_ancestor.is_root() {
                    let child_name = current_ancestor.child_name().unwrap().clone();
                    current_ancestor = current_ancestor.parent().unwrap();

                    let decl = self.realm.get_decl_mut(&current_ancestor)?;
                    Self::add_offer_for_capability(
                        &mut decl.offers,
                        &route,
                        OfferSource::Parent,
                        &child_name,
                        &current_ancestor,
                    )?;
                }
            } else {
                // We're routing a capability from one component within our constructed realm to
                // another
                let source_moniker = route.source.unwrap_component_moniker();
                let target_moniker = target.unwrap_component_moniker();

                if let Ok(target_decl) = self.realm.get_decl_mut(&target_moniker) {
                    target_decl.uses.push(Self::new_use_decl(&route.capability));
                }
                if let Ok(source_decl) = self.realm.get_decl_mut(&source_moniker) {
                    Self::add_capability_decl(&mut source_decl.capabilities, &route.capability)?;
                }

                let mut offering_child_name = target_moniker.child_name().unwrap().clone();
                let mut offering_ancestor = target_moniker.parent().unwrap();
                while offering_ancestor != source_moniker
                    && !offering_ancestor.is_ancestor_of(&source_moniker)
                {
                    let decl = self.realm.get_decl_mut(&offering_ancestor)?;
                    Self::add_offer_for_capability(
                        &mut decl.offers,
                        &route,
                        OfferSource::Parent,
                        &offering_child_name,
                        &offering_ancestor,
                    )?;

                    offering_child_name = offering_ancestor.child_name().unwrap().clone();
                    offering_ancestor = offering_ancestor.parent().unwrap();
                }

                if offering_ancestor == source_moniker {
                    // We don't need to add an expose chain, we reached the source moniker solely
                    // by walking up the tree
                    let decl = self.realm.get_decl_mut(&offering_ancestor)?;
                    Self::add_offer_for_capability(
                        &mut decl.offers,
                        &route,
                        OfferSource::Self_,
                        &offering_child_name,
                        &offering_ancestor,
                    )?;
                    return Ok(self);
                }

                // We need an expose chain to descend down the tree to our source.

                if let Ok(source_decl) = self.realm.get_decl_mut(&source_moniker) {
                    Self::add_expose_for_capability(
                        &mut source_decl.exposes,
                        &route,
                        None,
                        &source_moniker,
                    )?;
                }

                let mut exposing_child_name = source_moniker.child_name().unwrap().clone();
                let mut exposing_ancestor = source_moniker.parent().unwrap();
                while exposing_ancestor != offering_ancestor {
                    let decl = self.realm.get_decl_mut(&exposing_ancestor)?;
                    Self::add_expose_for_capability(
                        &mut decl.exposes,
                        &route,
                        Some(&exposing_child_name),
                        &exposing_ancestor,
                    )?;

                    exposing_child_name = exposing_ancestor.child_name().unwrap().clone();
                    exposing_ancestor = exposing_ancestor.parent().unwrap();
                }

                let decl = self.realm.get_decl_mut(&offering_ancestor)?;
                Self::add_offer_for_capability(
                    &mut decl.offers,
                    &route,
                    OfferSource::Child(exposing_child_name.clone()),
                    &offering_child_name,
                    &offering_ancestor,
                )?;
            }
        }
        Ok(self)
    }

    /// Builds a new [`Realm`] from this builder.
    // TODO: rename? the building is done by this point
    pub fn build(self) -> Realm {
        self.realm
    }

    fn add_capability_decl(
        capability_decls: &mut Vec<cm_rust::CapabilityDecl>,
        capability: &Capability,
    ) -> Result<(), BuilderError> {
        let capability_decl = match capability {
            Capability::Protocol(name) => {
                Some(cm_rust::CapabilityDecl::Protocol(cm_rust::ProtocolDecl {
                    name: name.as_str().try_into().unwrap(),
                    source_path: format!("/svc/{}", name).as_str().try_into().unwrap(),
                }))
            }
            Capability::Directory(name, path, rights) => {
                Some(cm_rust::CapabilityDecl::Directory(cm_rust::DirectoryDecl {
                    name: name.as_str().try_into().unwrap(),
                    source_path: path.as_str().try_into().unwrap(),
                    rights: rights.clone(),
                }))
            }
            Capability::Storage(name, _) => {
                return Err(BuilderError::StorageMustComeFromAboveRoot(name.clone()))
            }
            Capability::Event(_, _) => None,
        };
        if let Some(decl) = capability_decl {
            if !capability_decls.contains(&decl) {
                capability_decls.push(decl);
            }
        }
        Ok(())
    }

    fn new_use_decl(capability: &Capability) -> cm_rust::UseDecl {
        match capability {
            Capability::Protocol(name) => cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                source: cm_rust::UseSource::Parent,
                source_name: name.clone().try_into().unwrap(),
                target_path: format!("/svc/{}", name).as_str().try_into().unwrap(),
            }),
            Capability::Directory(name, path, rights) => {
                cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
                    source: cm_rust::UseSource::Parent,
                    source_name: name.as_str().try_into().unwrap(),
                    target_path: path.as_str().try_into().unwrap(),
                    rights: rights.clone(),
                    subdir: None,
                })
            }
            Capability::Storage(name, path) => cm_rust::UseDecl::Storage(cm_rust::UseStorageDecl {
                source_name: name.as_str().try_into().unwrap(),
                target_path: path.as_str().try_into().unwrap(),
            }),
            Capability::Event(event, mode) => cm_rust::UseDecl::Event(cm_rust::UseEventDecl {
                source: cm_rust::UseSource::Parent,
                source_name: event.name().into(),
                target_name: event.name().into(),
                filter: event.filter(),
                mode: mode.clone(),
            }),
        }
    }

    fn add_offer_for_capability(
        offers: &mut Vec<cm_rust::OfferDecl>,
        route: &CapabilityRoute,
        offer_source: OfferSource,
        target_name: &str,
        moniker: &Moniker,
    ) -> Result<(), Error> {
        let offer_target = cm_rust::OfferTarget::Child(target_name.to_string());
        match &route.capability {
            Capability::Protocol(name) => {
                let offer_source = match offer_source {
                    OfferSource::Parent => cm_rust::OfferSource::Parent,
                    OfferSource::Self_ => cm_rust::OfferSource::Self_,
                    OfferSource::Child(n) => cm_rust::OfferSource::Child(n),
                };
                for offer in offers.iter() {
                    if let cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source,
                        target_name,
                        target,
                        ..
                    }) = offer
                    {
                        if name == &target_name.str() && *target == offer_target {
                            if *source != offer_source {
                                return Err(BuilderError::ConflictingOffers(
                                    route.clone(),
                                    moniker.clone(),
                                    target.clone(),
                                    format!("{:?}", source),
                                )
                                .into());
                            } else {
                                // The offer we want already exists
                                return Ok(());
                            }
                        }
                    }
                }
                offers.push(cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: offer_source,
                    source_name: name.clone().into(),
                    target: cm_rust::OfferTarget::Child(target_name.to_string()),
                    target_name: name.clone().into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                }));
            }
            Capability::Directory(name, _, _) => {
                let offer_source = match offer_source {
                    OfferSource::Parent => cm_rust::OfferSource::Parent,
                    OfferSource::Self_ => cm_rust::OfferSource::Self_,
                    OfferSource::Child(n) => cm_rust::OfferSource::Child(n),
                };
                for offer in offers.iter() {
                    if let cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                        source,
                        target_name,
                        target,
                        ..
                    }) = offer
                    {
                        if name == &target_name.str() && *target == offer_target {
                            if *source != offer_source {
                                return Err(BuilderError::ConflictingOffers(
                                    route.clone(),
                                    moniker.clone(),
                                    target.clone(),
                                    format!("{:?}", source),
                                )
                                .into());
                            } else {
                                // The offer we want already exists
                                return Ok(());
                            }
                        }
                    }
                }
                offers.push(cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                    source: offer_source,
                    source_name: name.clone().into(),
                    target: cm_rust::OfferTarget::Child(target_name.to_string()),
                    target_name: name.clone().into(),
                    rights: None,
                    subdir: None,
                    dependency_type: cm_rust::DependencyType::Strong,
                }));
            }
            Capability::Storage(name, _) => {
                let offer_source = match offer_source {
                    OfferSource::Parent => cm_rust::OfferSource::Parent,
                    OfferSource::Self_ => cm_rust::OfferSource::Self_,
                    OfferSource::Child(_) => {
                        return Err(BuilderError::StorageCannotBeOfferedFromChild(
                            name.clone(),
                            route.clone(),
                        )
                        .into());
                    }
                };
                for offer in offers.iter() {
                    if let cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                        source,
                        target_name,
                        target,
                        ..
                    }) = offer
                    {
                        if name == &target_name.str() && *target == offer_target {
                            if *source != offer_source {
                                return Err(BuilderError::ConflictingOffers(
                                    route.clone(),
                                    moniker.clone(),
                                    target.clone(),
                                    format!("{:?}", source),
                                )
                                .into());
                            } else {
                                // The offer we want already exists
                                return Ok(());
                            }
                        }
                    }
                }
                offers.push(cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                    source: offer_source,
                    source_name: name.clone().into(),
                    target: cm_rust::OfferTarget::Child(target_name.to_string()),
                    target_name: name.clone().into(),
                }));
            }
            Capability::Event(event, mode) => {
                let offer_source = match offer_source {
                    OfferSource::Parent => cm_rust::OfferSource::Parent,
                    OfferSource::Self_ => cm_rust::OfferSource::Framework,
                    OfferSource::Child(_) => {
                        return Err(BuilderError::EventCannotBeOfferedFromChild(
                            event.name().to_string(),
                            route.clone(),
                        )
                        .into());
                    }
                };
                for offer in offers.iter() {
                    if let cm_rust::OfferDecl::Event(cm_rust::OfferEventDecl {
                        source,
                        target_name,
                        target,
                        ..
                    }) = offer
                    {
                        if event.name() == target_name.str() && *target == offer_target {
                            if *source != offer_source {
                                return Err(BuilderError::ConflictingOffers(
                                    route.clone(),
                                    moniker.clone(),
                                    target.clone(),
                                    format!("{:?}", source),
                                )
                                .into());
                            } else {
                                // The offer we want already exists
                                return Ok(());
                            }
                        }
                    }
                }
                offers.push(cm_rust::OfferDecl::Event(cm_rust::OfferEventDecl {
                    source: offer_source,
                    source_name: event.name().into(),
                    target: cm_rust::OfferTarget::Child(target_name.to_string()),
                    target_name: event.name().into(),
                    filter: event.filter(),
                    mode: mode.clone(),
                }));
            }
        }
        Ok(())
    }

    // Adds an expose decl for `route.capability` from `source` to `exposes`, checking first that
    // there aren't any conflicting exposes.
    fn add_expose_for_capability(
        exposes: &mut Vec<cm_rust::ExposeDecl>,
        route: &CapabilityRoute,
        source: Option<&str>,
        moniker: &Moniker,
    ) -> Result<(), Error> {
        let expose_source = source
            .map(|s| cm_rust::ExposeSource::Child(s.to_string()))
            .unwrap_or(cm_rust::ExposeSource::Self_);
        let target = cm_rust::ExposeTarget::Parent;

        // Check to see if this expose decl already exists. If it does, and it has the same source,
        // we're good. If it does and it has a different source, then we have an error.
        for expose in exposes.iter() {
            match (&route.capability, expose) {
                (
                    Capability::Protocol(name),
                    cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                        source_name,
                        source,
                        ..
                    }),
                ) if name == &source_name.str() && *source != expose_source => {
                    return Err(BuilderError::ConflictingExposes(
                        route.clone(),
                        moniker.clone(),
                        source.clone(),
                    )
                    .into())
                }
                (
                    Capability::Directory(name, _, _),
                    cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                        source_name,
                        source,
                        ..
                    }),
                ) if name == &source_name.str() && *source != expose_source => {
                    return Err(BuilderError::ConflictingExposes(
                        route.clone(),
                        moniker.clone(),
                        source.clone(),
                    )
                    .into())
                }
                _ => (),
            }
        }

        let new_decl = {
            match &route.capability {
                Capability::Protocol(name) => {
                    cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                        source: expose_source,
                        source_name: name.clone().into(),
                        target,
                        target_name: name.clone().into(),
                    })
                }
                Capability::Directory(name, _, _) => {
                    cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                        source: expose_source,
                        source_name: name.as_str().into(),
                        target,
                        target_name: name.as_str().into(),
                        rights: None,
                        subdir: None,
                    })
                }
                Capability::Storage(name, _) => {
                    return Err(BuilderError::StorageCannotBeExposed(name.clone()).into());
                }
                Capability::Event(event, _) => {
                    return Err(
                        BuilderError::EventsCannotBeExposed(event.name().to_string()).into()
                    );
                }
            }
        };
        if !exposes.contains(&new_decl) {
            exposes.push(new_decl);
        }

        Ok(())
    }
}

// This is needed because there are different enums for offer source depending on capability
enum OfferSource {
    Parent,
    Self_,
    Child(String),
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::error, cm_rust::*, fidl::endpoints::create_proxy,
        fidl_fuchsia_data as fdata, fidl_fuchsia_realm_builder as ftrb, fuchsia_async as fasync,
        futures::TryStreamExt, matches::assert_matches,
    };

    fn realm_with_mock_framework_intermediary() -> (fasync::Task<()>, Realm) {
        let (framework_intermediary_proxy, framework_intermediary_server_end) =
            create_proxy::<ftrb::FrameworkIntermediaryMarker>().unwrap();

        let framework_intermediary_task = fasync::Task::local(async move {
            let mut framework_intermediary_stream =
                framework_intermediary_server_end.into_stream().unwrap();
            let mut mock_counter: u64 = 0;
            while let Some(request) = framework_intermediary_stream.try_next().await.unwrap() {
                match request {
                    ftrb::FrameworkIntermediaryRequest::RegisterDecl { responder, .. } => {
                        responder.send(&mut Ok("some-fake-url://foobar".to_string())).unwrap()
                    }
                    ftrb::FrameworkIntermediaryRequest::RegisterMock { responder } => {
                        responder.send(&format!("{}", mock_counter)).unwrap();
                        mock_counter += 1;
                    }
                }
            }
        });
        let realm_with_mock_framework_intermediary =
            Realm::new_with_framework_intermediary_proxy(framework_intermediary_proxy).unwrap();

        (framework_intermediary_task, realm_with_mock_framework_intermediary)
    }

    fn mocked_builder() -> (fasync::Task<()>, RealmBuilder) {
        let (task, realm) = realm_with_mock_framework_intermediary();
        (task, RealmBuilder::build_on(realm))
    }

    async fn build_and_check_results(
        builder: RealmBuilder,
        expected_results: Vec<(&'static str, ComponentDecl)>,
    ) {
        assert!(!expected_results.is_empty(), "can't build an empty realm");

        let mut built_realm = builder.build();

        for (component, decl) in expected_results {
            assert_eq!(
                *built_realm
                    .get_decl_mut(&component.into())
                    .expect("component is missing from realm"),
                decl,
                "decl in realm doesn't match expectations for component  {:?}",
                component
            );
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn component_already_exists_error() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_component("a", ComponentSource::url("fuchsia-pkg://b"))
            .await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Builder(BuilderError::ComponentAlreadyExists(m)))
                if m == "a".into() =>
            {
                ()
            }
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn added_non_leaf_nodes_error() {
        {
            let (_mocks_task, mut builder) = mocked_builder();

            let res = builder
                .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
                .await
                .unwrap()
                .add_component("a/b", ComponentSource::url("fuchsia-pkg://b"))
                .await;

            match res {
                Ok(_) => panic!("builder commands should have errored"),
                Err(error::Error::Realm(RealmError::ComponentNotModifiable(m)))
                    if m == "a".into() =>
                {
                    ()
                }
                Err(e) => panic!("unexpected error: {:?}", e),
            }
        }

        {
            let (_mocks_task, mut builder) = mocked_builder();

            let res = builder
                .add_component("a/b", ComponentSource::url("fuchsia-pkg://a"))
                .await
                .unwrap()
                .add_eager_component("a", ComponentSource::url("fuchsia-pkg://b"))
                .await;

            match res {
                Ok(_) => panic!("builder commands should have errored"),
                Err(error::Error::Builder(BuilderError::ComponentAlreadyExists(m)))
                    if m == "a".into() =>
                {
                    ()
                }
                Err(e) => panic!("unexpected error: {:?}", e),
            }
        }

        {
            let (_mocks_task, mut builder) = mocked_builder();

            let res = builder
                .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
                .await
                .unwrap()
                .add_eager_component(
                    "",
                    ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                        Box::pin(async move { Ok(()) })
                    })),
                )
                .await;

            match res {
                Ok(_) => panic!("builder commands should have errored"),
                Err(error::Error::Builder(BuilderError::ComponentAlreadyExists(m)))
                    if m == "".into() =>
                {
                    ()
                }
                Err(e) => panic!("unexpected error: {:?}", e),
            }
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn missing_route_source_error() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("b"),
                targets: vec![RouteEndpoint::component("a")],
            });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Builder(BuilderError::MissingRouteSource(m))) if m == "b".into() => {
                ()
            }
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn empty_route_targets() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("a"),
                targets: vec![],
            });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(e) => assert_matches!(e, error::Error::Builder(BuilderError::EmptyRouteTargets)),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_offer_same_source() {
        let (_mocks_task, mut builder) = mocked_builder();

        builder
            .add_component("1/src", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_component("2/target_1", ComponentSource::url("fuchsia-pkg://b"))
            .await
            .unwrap()
            .add_component("2/target_2", ComponentSource::url("fuchsia-pkg://c"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("1/src"),
                targets: vec![
                    RouteEndpoint::component("2/target_1"),
                    RouteEndpoint::component("2/target_2"),
                ],
            })
            .unwrap();
        builder.build().initialize().await.unwrap();
    }

    #[fasync::run_until_stalled(test)]
    async fn same_capability_from_different_sources_in_same_node_error() {
        {
            let (_mocks_task, mut builder) = mocked_builder();

            let res = builder
                .add_component("1/a", ComponentSource::url("fuchsia-pkg://a"))
                .await
                .unwrap()
                .add_component("1/b", ComponentSource::url("fuchsia-pkg://b"))
                .await
                .unwrap()
                .add_component("2/c", ComponentSource::url("fuchsia-pkg://c"))
                .await
                .unwrap()
                .add_component("2/d", ComponentSource::url("fuchsia-pkg://d"))
                .await
                .unwrap()
                .add_route(CapabilityRoute {
                    capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                    source: RouteEndpoint::component("1/a"),
                    targets: vec![RouteEndpoint::component("2/c")],
                })
                .unwrap()
                .add_route(CapabilityRoute {
                    capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                    source: RouteEndpoint::component("1/b"),
                    targets: vec![RouteEndpoint::component("2/d")],
                });

            match res {
                Err(error::Error::Builder(BuilderError::ConflictingExposes(
                    route,
                    moniker,
                    source,
                ))) => {
                    assert_eq!(
                        route,
                        CapabilityRoute {
                            capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                            source: RouteEndpoint::component("1/b"),
                            targets: vec![RouteEndpoint::component("2/d")],
                        }
                    );
                    assert_eq!(moniker, "1".into());
                    assert_eq!(source, cm_rust::ExposeSource::Child("a".to_string()));
                }
                Err(e) => panic!("unexpected error: {:?}", e),
                Ok(_) => panic!("builder commands should have errored"),
            }
        }

        {
            let (_mocks_task, mut builder) = mocked_builder();

            builder
                .add_component("1/a", ComponentSource::url("fuchsia-pkg://a"))
                .await
                .unwrap()
                .add_component("1/b", ComponentSource::url("fuchsia-pkg://b"))
                .await
                .unwrap()
                .add_component("2/c", ComponentSource::url("fuchsia-pkg://c"))
                .await
                .unwrap()
                .add_component("2/d", ComponentSource::url("fuchsia-pkg://d"))
                .await
                .unwrap()
                .add_route(CapabilityRoute {
                    capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                    source: RouteEndpoint::component("1/a"),
                    targets: vec![RouteEndpoint::component("1/b")],
                })
                .unwrap()
                .add_route(CapabilityRoute {
                    capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                    source: RouteEndpoint::component("2/c"),
                    targets: vec![RouteEndpoint::component("2/d")],
                })
                .unwrap();
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn missing_route_target_error() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::component("b")],
            });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Builder(BuilderError::MissingRouteTarget(m))) if m == "b".into() => {
                ()
            }
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn route_source_and_target_both_above_root_error() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder.add_route(CapabilityRoute {
            capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::AboveRoot],
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(e) => assert_matches!(
                e,
                error::Error::Builder(BuilderError::RouteSourceAndTargetMatch(_))
            ),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn expose_event_from_child_error() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::Event(Event::Started, cm_rust::EventMode::Async),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::AboveRoot],
            });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Builder(BuilderError::EventsCannotBeExposed(e)))
                if e == "started" => {}
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn offer_event_from_child_error() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_component("b", ComponentSource::url("fuchsia-pkg://b"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::Event(Event::Started, cm_rust::EventMode::Async),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::component("b")],
            });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Builder(BuilderError::EventCannotBeOfferedFromChild(e, _)))
                if e == "started" => {}
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn expose_storage_from_child_error() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::storage("foo", "/foo"),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::AboveRoot],
            });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Builder(BuilderError::StorageCannotBeExposed(s))) if s == "foo" => {}
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn offer_storage_from_child_error() {
        let (_mocks_task, mut builder) = mocked_builder();

        let res = builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_component("b", ComponentSource::url("fuchsia-pkg://b"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::storage("foo", "/foo"),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::component("b")],
            });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Builder(BuilderError::StorageCannotBeOfferedFromChild(s, _)))
                if s == "foo" => {}
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn verify_events_routing() {
        let (_mocks_task, mut builder) = mocked_builder();
        builder
            .add_component(
                "a",
                ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                    Box::pin(async move { Ok(()) })
                })),
            )
            .await
            .unwrap()
            .add_component(
                "a/b",
                ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                    Box::pin(async move { Ok(()) })
                })),
            )
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::Event(Event::Started, cm_rust::EventMode::Sync),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("a")],
            })
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::Event(
                    Event::directory_ready("diagnostics"),
                    cm_rust::EventMode::Async,
                ),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::component("a/b")],
            })
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::Event(
                    Event::capability_requested("fuchsia.logger.LogSink"),
                    cm_rust::EventMode::Async,
                ),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("a/b")],
            })
            .unwrap();

        build_and_check_results(
            builder,
            vec![
                (
                    "",
                    ComponentDecl {
                        offers: vec![
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "started".into(),
                                target: cm_rust::OfferTarget::Child("a".to_string()),
                                target_name: "started".into(),
                                mode: EventMode::Sync,
                                filter: None,
                            }),
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "capability_requested".into(),
                                target: cm_rust::OfferTarget::Child("a".to_string()),
                                target_name: "capability_requested".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.logger.LogSink".to_string()))),
                            }),
                        ],
                        children: vec![
                                // Mock children aren't inserted into the decls at this point, as
                                // their URLs are unknown until registration with the framework
                                // intemediary, and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![UseDecl::Event(UseEventDecl {
                            source: UseSource::Parent,
                            source_name: "started".into(),
                            target_name: "started".into(),
                            filter: None,
                            mode: EventMode::Sync,
                        })],
                        offers: vec![
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Framework,
                                source_name: "directory_ready".into(),
                                target: cm_rust::OfferTarget::Child("b".to_string()),
                                target_name: "directory_ready".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "diagnostics".to_string()))),
                            }),
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "capability_requested".into(),
                                target: cm_rust::OfferTarget::Child("b".to_string()),
                                target_name: "capability_requested".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.logger.LogSink".to_string()))),
                            }),
                        ],
                        children: vec![
                                // Mock children aren't inserted into the decls at this point, as
                                // their URLs are unknown until registration with the framework
                                // intemediary, and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a/b",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "1".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![
                            UseDecl::Event(UseEventDecl {
                                source: UseSource::Parent,
                                source_name: "directory_ready".into(),
                                target_name: "directory_ready".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "diagnostics".to_string()))),
                            }),
                            UseDecl::Event(UseEventDecl {
                                source: UseSource::Parent,
                                source_name: "capability_requested".into(),
                                target_name: "capability_requested".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.logger.LogSink".to_string()))),
                            }),
                        ],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn verify_storage_routing() {
        let (_mocks_task, mut builder) = mocked_builder();
        builder
            .add_component(
                "a",
                ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                    Box::pin(async move { Ok(()) })
                })),
            )
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::storage("foo", "/bar"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::component("a")],
            })
            .unwrap();

        build_and_check_results(
            builder,
            vec![
                (
                    "",
                    ComponentDecl {
                        offers: vec![OfferDecl::Storage(OfferStorageDecl {
                            source: cm_rust::OfferSource::Parent,
                            source_name: "foo".into(),
                            target: cm_rust::OfferTarget::Child("a".to_string()),
                            target_name: "foo".into(),
                        })],
                        children: vec![
                                // Mock children aren't inserted into the decls at this point, as their
                                // URLs are unknown until registration with the framework intemediary,
                                // and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![UseDecl::Storage(UseStorageDecl {
                            source_name: "foo".into(),
                            target_path: "/bar".try_into().unwrap(),
                        })],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn two_sibling_realm_no_mocks() {
        let (_mocks_task, mut builder) = mocked_builder();

        builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_eager_component("b", ComponentSource::url("fuchsia-pkg://b"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::component("b")],
            })
            .unwrap();

        build_and_check_results(
            builder,
            vec![(
                "",
                ComponentDecl {
                    offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                        source: cm_rust::OfferSource::Child("a".to_string()),
                        source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                        target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        dependency_type: DependencyType::Strong,
                    })],
                    children: vec![
                        ChildDecl {
                            name: "a".to_string(),
                            url: "fuchsia-pkg://a".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                        },
                        ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                        },
                    ],
                    ..ComponentDecl::default()
                },
            )],
        )
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn two_sibling_realm_both_mocks() {
        let (_mocks_task, mut builder) = mocked_builder();

        builder
            .add_component(
                "a",
                ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                    Box::pin(async move { Ok(()) })
                })),
            )
            .await
            .unwrap()
            .add_eager_component(
                "b",
                ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                    Box::pin(async move { Ok(()) })
                })),
            )
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::component("b")],
            })
            .unwrap();

        build_and_check_results(
            builder,
            vec![
                (
                    "",
                    ComponentDecl {
                        offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                            source: cm_rust::OfferSource::Child("a".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: OfferTarget::Child("b".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intemediary,
                            // and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        capabilities: vec![CapabilityDecl::Protocol(ProtocolDecl {
                            name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            source_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        exposes: vec![ExposeDecl::Protocol(ExposeProtocolDecl {
                            source: ExposeSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "b",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "1".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![UseDecl::Protocol(UseProtocolDecl {
                            source: UseSource::Parent,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn mock_with_child() {
        let (_mocks_task, mut builder) = mocked_builder();

        builder
            .add_component(
                "a",
                ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                    Box::pin(async move { Ok(()) })
                })),
            )
            .await
            .unwrap()
            .add_eager_component("a/b", ComponentSource::url("fuchsia-pkg://b"))
            .await
            .unwrap()
            .add_protocol_route::<fidl_fidl_examples_routing_echo::EchoMarker>(
                RouteEndpoint::component("a"),
                vec![RouteEndpoint::component("a/b")],
            )
            .unwrap();

        build_and_check_results(
            builder,
            vec![
                (
                    "",
                    ComponentDecl {
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intemediary,
                            // and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        capabilities: vec![CapabilityDecl::Protocol(ProtocolDecl {
                            name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            source_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                            source: cm_rust::OfferSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: OfferTarget::Child("b".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                        })],
                        children: vec![ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                        }],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn three_sibling_realm_one_mock() {
        let (_mocks_task, mut builder) = mocked_builder();

        builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_component(
                "b",
                ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                    Box::pin(async move { Ok(()) })
                })),
            )
            .await
            .unwrap()
            .add_eager_component("c", ComponentSource::url("fuchsia-pkg://c"))
            .await
            .unwrap()
            .add_protocol_route::<fidl_fidl_examples_routing_echo::EchoMarker>(
                RouteEndpoint::component("a"),
                vec![RouteEndpoint::component("b")],
            )
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::directory("example-dir", "/example", fio2::RW_STAR_DIR),
                source: RouteEndpoint::component("b"),
                targets: vec![RouteEndpoint::component("c")],
            })
            .unwrap();

        build_and_check_results(
            builder,
            vec![
                (
                    "",
                    ComponentDecl {
                        offers: vec![
                            OfferDecl::Protocol(OfferProtocolDecl {
                                source: cm_rust::OfferSource::Child("a".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: OfferTarget::Child("b".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                            }),
                            OfferDecl::Directory(OfferDirectoryDecl {
                                source: cm_rust::OfferSource::Child("b".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: OfferTarget::Child("c".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intemediary,
                            // and that happens during Realm::create
                            ChildDecl {
                                name: "a".to_string(),
                                url: "fuchsia-pkg://a".to_string(),
                                startup: fsys::StartupMode::Lazy,
                                environment: None,
                            },
                            ChildDecl {
                                name: "c".to_string(),
                                url: "fuchsia-pkg://c".to_string(),
                                startup: fsys::StartupMode::Eager,
                                environment: None,
                            },
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "b",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![UseDecl::Protocol(UseProtocolDecl {
                            source: UseSource::Parent,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        capabilities: vec![CapabilityDecl::Directory(DirectoryDecl {
                            name: "example-dir".try_into().unwrap(),
                            source_path: "/example".try_into().unwrap(),
                            rights: fio2::RW_STAR_DIR,
                        })],
                        exposes: vec![ExposeDecl::Directory(ExposeDirectoryDecl {
                            source: ExposeSource::Self_,
                            source_name: "example-dir".try_into().unwrap(),
                            target: ExposeTarget::Parent,
                            target_name: "example-dir".try_into().unwrap(),
                            rights: None,
                            subdir: None,
                        })],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn three_siblings_two_targets() {
        let (_mocks_task, mut builder) = mocked_builder();

        builder
            .add_eager_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_component("b", ComponentSource::url("fuchsia-pkg://b"))
            .await
            .unwrap()
            .add_eager_component("c", ComponentSource::url("fuchsia-pkg://c"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("b"),
                targets: vec![RouteEndpoint::component("a"), RouteEndpoint::component("c")],
            })
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::directory("example-dir", "/example", fio2::RW_STAR_DIR),
                source: RouteEndpoint::component("b"),
                targets: vec![RouteEndpoint::component("a"), RouteEndpoint::component("c")],
            })
            .unwrap();

        build_and_check_results(
            builder,
            vec![(
                "",
                ComponentDecl {
                    offers: vec![
                        OfferDecl::Protocol(OfferProtocolDecl {
                            source: cm_rust::OfferSource::Child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: OfferTarget::Child("a".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                        }),
                        OfferDecl::Protocol(OfferProtocolDecl {
                            source: cm_rust::OfferSource::Child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: OfferTarget::Child("c".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                        }),
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: cm_rust::OfferSource::Child("b".to_string()),
                            source_name: "example-dir".try_into().unwrap(),
                            target: OfferTarget::Child("a".to_string()),
                            target_name: "example-dir".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                            rights: None,
                            subdir: None,
                        }),
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: cm_rust::OfferSource::Child("b".to_string()),
                            source_name: "example-dir".try_into().unwrap(),
                            target: OfferTarget::Child("c".to_string()),
                            target_name: "example-dir".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                            rights: None,
                            subdir: None,
                        }),
                    ],
                    children: vec![
                        ChildDecl {
                            name: "a".to_string(),
                            url: "fuchsia-pkg://a".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                        },
                        ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                        },
                        ChildDecl {
                            name: "c".to_string(),
                            url: "fuchsia-pkg://c".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                        },
                    ],
                    ..ComponentDecl::default()
                },
            )],
        )
        .await;
    }

    #[fasync::run_until_stalled(test)]
    async fn two_cousins_realm_one_mock() {
        let (_mocks_task, mut builder) = mocked_builder();

        builder
            .add_component("a/b", ComponentSource::url("fuchsia-pkg://a-b"))
            .await
            .unwrap()
            .add_eager_component(
                "c/d",
                ComponentSource::Mock(mock::Mock::new(|_: mock::MockHandles| {
                    Box::pin(async move { Ok(()) })
                })),
            )
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::directory("example-dir", "/example", fio2::RW_STAR_DIR),
                source: RouteEndpoint::component("a/b"),
                targets: vec![RouteEndpoint::component("c/d")],
            })
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("a/b"),
                targets: vec![RouteEndpoint::component("c/d")],
            })
            .unwrap();

        build_and_check_results(
            builder,
            vec![
                (
                    "",
                    ComponentDecl {
                        offers: vec![
                            OfferDecl::Directory(OfferDirectoryDecl {
                                source: cm_rust::OfferSource::Child("a".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: OfferTarget::Child("c".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                            OfferDecl::Protocol(OfferProtocolDecl {
                                source: cm_rust::OfferSource::Child("a".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: OfferTarget::Child("c".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                            }),
                        ],
                        children: vec![
                            // Generated children aren't inserted into the decls at this point, as
                            // their URLs are unknown until registration with the framework
                            // intemediary, and that happens during Realm::create
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    ComponentDecl {
                        exposes: vec![
                            ExposeDecl::Directory(ExposeDirectoryDecl {
                                source: ExposeSource::Child("b".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: ExposeTarget::Parent,
                                target_name: "example-dir".try_into().unwrap(),
                                rights: None,
                                subdir: None,
                            }),
                            ExposeDecl::Protocol(ExposeProtocolDecl {
                                source: ExposeSource::Child("b".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: ExposeTarget::Parent,
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            }),
                        ],
                        children: vec![ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://a-b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                        }],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "c",
                    ComponentDecl {
                        offers: vec![
                            OfferDecl::Directory(OfferDirectoryDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "example-dir".try_into().unwrap(),
                                target: OfferTarget::Child("d".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                            OfferDecl::Protocol(OfferProtocolDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: OfferTarget::Child("d".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                            }),
                        ],
                        ..ComponentDecl::default()
                    },
                ),
                (
                    "c/d",
                    ComponentDecl {
                        program: Some(ProgramDecl {
                            runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![fdata::DictionaryEntry {
                                    key: mock::MOCK_ID_KEY.to_string(),
                                    value: Some(Box::new(fdata::DictionaryValue::Str(
                                        "0".to_string(),
                                    ))),
                                }]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
                        uses: vec![
                            UseDecl::Directory(UseDirectoryDecl {
                                source: UseSource::Parent,
                                source_name: "example-dir".try_into().unwrap(),
                                target_path: "/example".try_into().unwrap(),
                                rights: fio2::RW_STAR_DIR,
                                subdir: None,
                            }),
                            UseDecl::Protocol(UseProtocolDecl {
                                source: UseSource::Parent,
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target_path: "/svc/fidl.examples.routing.echo.Echo"
                                    .try_into()
                                    .unwrap(),
                            }),
                        ],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }
}
