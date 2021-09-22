// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Construct component realms by listing the components and the routes between them

use {
    crate::{error::*, mock, Moniker, Realm},
    anyhow, cm_rust,
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_io2 as fio2, fidl_fuchsia_realm_builder as frealmbuilder,
    futures::{future::BoxFuture, FutureExt},
    maplit::hashmap,
    std::{collections::HashMap, fmt},
};

pub use crate::RouteEndpoint;

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

impl Into<frealmbuilder::Capability> for Capability {
    fn into(self) -> frealmbuilder::Capability {
        match self {
            Capability::Protocol(name) => {
                frealmbuilder::Capability::Protocol(frealmbuilder::ProtocolCapability {
                    name: Some(name),
                    ..frealmbuilder::ProtocolCapability::EMPTY
                })
            }
            Capability::Directory(name, path, rights) => {
                frealmbuilder::Capability::Directory(frealmbuilder::DirectoryCapability {
                    name: Some(name),
                    path: Some(path),
                    rights: Some(rights),
                    ..frealmbuilder::DirectoryCapability::EMPTY
                })
            }
            Capability::Storage(name, path) => {
                frealmbuilder::Capability::Storage(frealmbuilder::StorageCapability {
                    name: Some(name),
                    path: Some(path),
                    ..frealmbuilder::StorageCapability::EMPTY
                })
            }
            Capability::Event(_, _) => {
                panic!("routes for event capabilities must be provided to RealmBuilder, not Realm")
            }
        }
    }
}

impl From<frealmbuilder::Capability> for Capability {
    fn from(input: frealmbuilder::Capability) -> Self {
        match input {
            frealmbuilder::Capability::Protocol(frealmbuilder::ProtocolCapability {
                name, ..
            }) => Capability::Protocol(name.unwrap()),
            frealmbuilder::Capability::Directory(frealmbuilder::DirectoryCapability {
                name,
                path,
                rights,
                ..
            }) => Capability::Directory(name.unwrap(), path.unwrap(), rights.unwrap()),
            frealmbuilder::Capability::Storage(frealmbuilder::StorageCapability {
                name,
                path,
                ..
            }) => Capability::Storage(name.unwrap(), path.unwrap()),
            _ => panic!("unexpected input"),
        }
    }
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

/// A capability route from one source component to one or more target components.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CapabilityRoute {
    pub capability: Capability,
    pub source: RouteEndpoint,
    pub targets: Vec<RouteEndpoint>,
}

impl Into<frealmbuilder::CapabilityRoute> for CapabilityRoute {
    fn into(self) -> frealmbuilder::CapabilityRoute {
        frealmbuilder::CapabilityRoute {
            capability: Some(self.capability.into()),
            source: Some(self.source.into()),
            targets: Some(self.targets.into_iter().map(Into::into).collect()),
            force_route: Some(false),
            ..frealmbuilder::CapabilityRoute::EMPTY
        }
    }
}

impl From<crate::RouteBuilder> for CapabilityRoute {
    fn from(input: crate::RouteBuilder) -> Self {
        let frealmbuilder_route: frealmbuilder::CapabilityRoute = input.into();
        if frealmbuilder_route.force_route.unwrap() {
            panic!("please provide routes with the `force` flag to `Realm::add_route`");
        }
        Self {
            capability: frealmbuilder_route.capability.unwrap().into(),
            source: frealmbuilder_route.source.unwrap().into(),
            targets: frealmbuilder_route.targets.unwrap().into_iter().map(Into::into).collect(),
        }
    }
}

/// The source for a component
#[derive(Clone)]
pub enum ComponentSource {
    /// A component URL, such as `fuchsia-pkg://fuchsia.com/package-name#meta/manifest.cm`
    Url(String),

    /// An in-process component mock
    Mock(mock::Mock),

    /// A v1 component URL, such as `fuchsia-pkg://fuchsia.com/package-name#meta/manifest.cmx`
    LegacyUrl(String),
}

impl ComponentSource {
    pub fn url(url: impl Into<String>) -> Self {
        let url: String = url.into();
        assert!(
            !url.ends_with(".cmx"),
            "You are referencing a legacy component and should use ComponentSource::legacy_url"
        );
        Self::Url(url)
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

    pub fn legacy_url(url: impl Into<String>) -> Self {
        let url: String = url.into();
        assert!(
            !url.ends_with(".cm"),
            "You are referencing a modern component and should use ComponentSource::url"
        );
        Self::LegacyUrl(url)
    }
}

impl fmt::Debug for ComponentSource {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ComponentSource::Url(url) => f.debug_tuple("Componentsource::Url").field(url).finish(),
            ComponentSource::Mock(_) => {
                f.debug_tuple("Componentsource::Mock").field(&"<mock fn>".to_string()).finish()
            }
            ComponentSource::LegacyUrl(legacy_url) => {
                f.debug_tuple("Componentsource::LegacyUrl").field(legacy_url).finish()
            }
        }
    }
}

#[derive(Clone)]
struct Component {
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    source: ComponentSource,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
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
        if self.realm.contains(&moniker).await? {
            return Err(BuilderError::ComponentAlreadyExists(moniker).into());
        }

        match source {
            ComponentSource::Url(url) => {
                self.realm.set_component_url(&moniker, url).await?;
            }
            ComponentSource::Mock(mock) => {
                self.realm.add_mocked_component(moniker, mock).await?;
            }
            ComponentSource::LegacyUrl(url) => {
                self.realm.set_component_legacy_url(&moniker, url).await?;
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
        self.realm.mark_as_eager(&moniker).await?;
        Ok(self)
    }

    /// Identical to `add_component`, but overrides an existing component instead of adding a new
    /// one.
    pub async fn override_component<M>(
        &mut self,
        moniker: M,
        source: ComponentSource,
    ) -> Result<&mut Self, Error>
    where
        M: Into<Moniker>,
    {
        let moniker = moniker.into();
        if !self.realm.contains(&moniker).await? {
            return Err(BuilderError::ComponentDoesNotExists(moniker).into());
        }

        match source {
            ComponentSource::Url(url) => {
                self.realm.set_component_url(&moniker, url).await?;
            }
            ComponentSource::LegacyUrl(url) => {
                self.realm.set_component_legacy_url(&moniker, url).await?;
            }
            ComponentSource::Mock(mock) => {
                self.realm.add_mocked_component(moniker, mock).await?;
            }
        }
        Ok(self)
    }

    /// Adds a protocol capability route between the `source` endpoint and
    /// the provided `targets`.
    pub fn add_protocol_route<P: DiscoverableProtocolMarker>(
        &mut self,
        source: RouteEndpoint,
        targets: Vec<RouteEndpoint>,
    ) -> Result<&mut Self, Error> {
        self.add_route(CapabilityRoute {
            capability: Capability::protocol(P::PROTOCOL_NAME),
            source,
            targets,
        })
    }

    /// Adds a capability route between two points in the realm. Does nothing if the route
    /// already exists.
    pub fn add_route(&mut self, route: impl Into<CapabilityRoute>) -> Result<&mut Self, Error> {
        self.realm.routes_to_add.push(route.into());
        Ok(self)
    }

    /// Builds a new [`Realm`] from this builder.
    pub fn build(self) -> Realm {
        self.realm
    }

    /// Adds a capability route between two points in the realm. Only works for event capabilities,
    /// will otherwise panic.
    pub(crate) fn add_event_route(
        realm: &mut Realm,
        route: CapabilityRoute,
    ) -> BoxFuture<'static, Result<(), Error>> {
        // There's a cycle between futures, which is disallowed, between the following functions:
        //
        // - Realm::get_decl
        // - Realm::flush_routes
        // - Realm::add_route
        // - RealmBuilder::add_event_route
        //
        // This function returns a BoxFuture to break this cycle
        let mut realm = crate::Realm {
            framework_intermediary_proxy: realm.framework_intermediary_proxy.clone(),
            mocks_runner: crate::mock::MocksRunner {
                mocks: realm.mocks_runner.mocks.clone(),
                event_stream_handling_task: None,
            },
            collection_name: realm.collection_name.clone(),
            routes_to_add: vec![],
        };
        async move {
            if let RouteEndpoint::Component(moniker) = &route.source {
                let moniker = moniker.clone().into();
                if !realm.contains(&moniker).await? {
                    return Err(EventError::MissingRouteSource(moniker).into());
                }
            }
            if route.targets.is_empty() {
                return Err(EventError::EmptyRouteTargets.into());
            }
            for target in &route.targets {
                if &route.source == target {
                    return Err(EventError::RouteSourceAndTargetMatch(route.clone()).into());
                }
                if let RouteEndpoint::Component(moniker) = target {
                    let moniker = moniker.clone().into();
                    if !realm.contains(&moniker).await? {
                        return Err(EventError::MissingRouteTarget(moniker).into());
                    }
                }
            }

            for target in &route.targets {
                if *target == RouteEndpoint::AboveRoot {
                    return Self::add_event_route_to_above_root(&mut realm, route).await;
                } else if route.source == RouteEndpoint::AboveRoot {
                    let target = target.clone();
                    return Self::add_event_route_from_above_root(&mut realm, route, target).await;
                } else {
                    let target = target.clone();
                    return Self::add_event_route_between_components(&mut realm, route, target)
                        .await;
                }
            }
            Ok(())
        }
        .boxed()
    }

    async fn add_event_route_to_above_root(
        _realm: &mut Realm,
        route: CapabilityRoute,
    ) -> Result<(), Error> {
        if let Capability::Event(event, _) = &route.capability {
            return Err(EventError::EventsCannotBeExposed(event.name().to_string()).into());
        } else {
            panic!("non-event capability given to add_event_route: {:?}", &route.capability);
        }
    }

    async fn add_event_route_from_above_root(
        realm: &mut Realm,
        route: CapabilityRoute,
        target: RouteEndpoint,
    ) -> Result<(), Error> {
        // We're routing a capability from above our constructed realm to a component
        // within it
        let target_moniker = target.unwrap_component_moniker();

        if realm.contains(&target_moniker).await? {
            // We need to check if the target is mutable or immutable. We could just see if
            // `get_decl` succeeds, but if it's immutable then the intermediary will log that an
            // error occurred. Get the parent decl and check its ChildDecls, we can't mutate this
            // node if it's behind a child decl.
            if target_moniker.is_root()
                || !realm
                    .get_decl(&target_moniker.parent().unwrap())
                    .await?
                    .children
                    .iter()
                    .any(|c| &c.name == target_moniker.child_name().unwrap())
            {
                let mut target_decl = realm.get_decl(&target_moniker).await?;
                target_decl.uses.push(Self::new_use_decl(&route.capability));
                realm.set_component(&target_moniker, target_decl).await?;
            }
        }

        let mut current_ancestor = target_moniker;
        while !current_ancestor.is_root() {
            let child_name = current_ancestor.child_name().unwrap().clone();
            current_ancestor = current_ancestor.parent().unwrap();

            let mut decl = realm.get_decl(&current_ancestor).await?;
            Self::add_offer_for_capability(
                &mut decl.offers,
                &route,
                cm_rust::OfferSource::Parent,
                &child_name,
                &current_ancestor,
            )?;
            realm.set_component(&current_ancestor, decl).await?;
        }
        Ok(())
    }

    async fn add_event_route_between_components(
        realm: &mut Realm,
        route: CapabilityRoute,
        target: RouteEndpoint,
    ) -> Result<(), Error> {
        // We're routing a capability from one component within our constructed realm to
        // another
        let source_moniker = route.source.unwrap_component_moniker();
        let target_moniker = target.unwrap_component_moniker();

        if !source_moniker.is_ancestor_of(&target_moniker) {
            if let Capability::Event(event, _) = &route.capability {
                return Err(EventError::EventsCannotBeExposed(event.name().to_string()).into());
            } else {
                panic!("non-event capability given to add_event_route: {:?}", &route.capability);
            }
        }

        let mut current_ancestor = source_moniker.clone();
        for offer_child_name in source_moniker.downward_path_to(&target_moniker) {
            let mut decl = realm.get_decl(&current_ancestor).await?;
            Self::add_offer_for_capability(
                &mut decl.offers,
                &route,
                cm_rust::OfferSource::Framework,
                &offer_child_name,
                &current_ancestor,
            )?;
            realm.set_component(&current_ancestor, decl).await?;
            current_ancestor = current_ancestor.child(offer_child_name);
        }

        if realm.contains(&target_moniker).await? {
            // We need to check if the target is mutable or immutable. We could just see if
            // `get_decl` succeeds, but if it's immutable then the intermediary will log that an
            // error occurred. Get the parent decl and check its ChildDecls, we can't mutate this
            // node if it's behind a child decl.
            if target_moniker.is_root()
                || !realm
                    .get_decl(&target_moniker.parent().unwrap())
                    .await?
                    .children
                    .iter()
                    .any(|c| &c.name == target_moniker.child_name().unwrap())
            {
                let mut target_decl = realm.get_decl(&target_moniker).await?;
                target_decl.uses.push(Self::new_use_decl(&route.capability));
                realm.set_component(&target_moniker, target_decl).await?;
            }
        }
        Ok(())
    }

    fn new_use_decl(capability: &Capability) -> cm_rust::UseDecl {
        match capability {
            Capability::Event(event, mode) => cm_rust::UseDecl::Event(cm_rust::UseEventDecl {
                source: cm_rust::UseSource::Parent,
                source_name: event.name().into(),
                target_name: event.name().into(),
                filter: event.filter(),
                mode: mode.clone(),
                dependency_type: cm_rust::DependencyType::Strong,
            }),
            _ => panic!(
                "attempting to do local routing for a non-event capability: {:?}",
                capability
            ),
        }
    }

    fn add_offer_for_capability(
        offers: &mut Vec<cm_rust::OfferDecl>,
        route: &CapabilityRoute,
        offer_source: cm_rust::OfferSource,
        target_name: &str,
        moniker: &Moniker,
    ) -> Result<(), Error> {
        let offer_target = cm_rust::OfferTarget::static_child(target_name.to_string());
        match &route.capability {
            Capability::Event(event, mode) => {
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
                                return Err(EventError::ConflictingOffers(
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
                    target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                    target_name: event.name().into(),
                    filter: event.filter(),
                    mode: mode.clone(),
                }));
            }
            _ => panic!(
                "attempting to do local routing for a non-event capability: {:?}",
                route.capability
            ),
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::error, cm_rust::*, fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
        fuchsia_async as fasync, matches::assert_matches, std::convert::TryInto,
    };

    async fn build_and_check_results(
        builder: RealmBuilder,
        expected_results: Vec<(&'static str, ComponentDecl)>,
    ) {
        assert!(!expected_results.is_empty(), "can't build an empty realm");

        let mut built_realm = builder.build();

        for (component, local_decl) in expected_results {
            let mut remote_decl = built_realm
                .get_decl(&component.into())
                .await
                .expect("component is missing from realm");

            // The assigned mock IDs may not be stable across test runs, so reset them all to 0 before
            // we compare against expected results.
            if let Some(program) = &mut remote_decl.program {
                if let Some(entries) = &mut program.info.entries {
                    for entry in entries.iter_mut() {
                        if entry.key == crate::mock::MOCK_ID_KEY {
                            entry.value =
                                Some(Box::new(fdata::DictionaryValue::Str("0".to_string())));
                        }
                    }
                }
            }

            assert_eq!(
                remote_decl, local_decl,
                "decl in realm doesn't match expectations for component  {:?}",
                component
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn component_already_exists_error() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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

    #[fasync::run_singlethreaded(test)]
    async fn added_non_leaf_nodes_error() {
        {
            let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

            let res = builder
                .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
                .await
                .unwrap()
                .add_component("a/b", ComponentSource::url("fuchsia-pkg://b"))
                .await;

            match res {
                Ok(_) => panic!("builder commands should have errored"),
                Err(error::Error::FailedToSetDecl(
                    _,
                    frealmbuilder::RealmBuilderError::NodeBehindChildDecl,
                )) => (),
                Err(e) => panic!("unexpected error: {:?}", e),
            }
        }

        {
            let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
            let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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

    #[fasync::run_singlethreaded(test)]
    async fn missing_route_source_error() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("b"),
                targets: vec![RouteEndpoint::component("a")],
            })
            .unwrap();
        let mut realm = builder.build();
        let res = realm.flush_routes().await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::FailedToRoute(
                frealmbuilder::RealmBuilderError::MissingRouteSource,
            )) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn empty_route_targets() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("a"),
                targets: vec![],
            })
            .unwrap();
        let mut realm = builder.build();
        let res = realm.flush_routes().await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(e) => assert_matches!(
                e,
                error::Error::FailedToRoute(frealmbuilder::RealmBuilderError::RouteTargetsEmpty)
            ),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn multiple_offer_same_source() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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

    #[fasync::run_singlethreaded(test)]
    async fn same_capability_from_different_sources_in_same_node_error() {
        {
            let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
                    targets: vec![RouteEndpoint::component("2/c")],
                })
                .unwrap()
                .add_route(CapabilityRoute {
                    capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                    source: RouteEndpoint::component("1/b"),
                    targets: vec![RouteEndpoint::component("2/d")],
                })
                .unwrap();
            let res = builder.build().initialize().await;

            match res {
                Err(error::Error::FailedToCommit(
                    frealmbuilder::RealmBuilderError::ValidationError,
                )) => (),
                Err(e) => panic!("unexpected error: {:?}", e),
                Ok(_) => panic!("builder commands should have errored"),
            }
        }

        {
            let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
            let mut realm = builder.build();
            realm.flush_routes().await.unwrap();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn missing_route_target_error() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::component("b")],
            })
            .unwrap();
        let mut realm = builder.build();
        let res = realm.flush_routes().await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::FailedToRoute(
                frealmbuilder::RealmBuilderError::MissingRouteTarget,
            )) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn route_source_and_target_both_above_root_error() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
            .add_route(CapabilityRoute {
                capability: Capability::protocol("fidl.examples.routing.echo.Echo"),
                source: RouteEndpoint::AboveRoot,
                targets: vec![RouteEndpoint::AboveRoot],
            })
            .unwrap();
        let mut realm = builder.build();
        let res = realm.flush_routes().await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(e) => assert_matches!(
                e,
                error::Error::FailedToRoute(
                    frealmbuilder::RealmBuilderError::RouteSourceAndTargetMatch
                )
            ),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn expose_event_from_child_error() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::Event(Event::Started, cm_rust::EventMode::Async),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::AboveRoot],
            })
            .unwrap();
        let mut realm = builder.build();
        let res = realm.flush_routes().await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Event(error::EventError::EventsCannotBeExposed(_))) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn offer_event_from_child_error() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
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
            })
            .unwrap();
        let mut realm = builder.build();
        let res = realm.flush_routes().await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::Event(error::EventError::EventsCannotBeExposed(_))) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn expose_storage_from_child_error() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
            .add_component("a", ComponentSource::url("fuchsia-pkg://a"))
            .await
            .unwrap()
            .add_route(CapabilityRoute {
                capability: Capability::storage("foo", "/foo"),
                source: RouteEndpoint::component("a"),
                targets: vec![RouteEndpoint::AboveRoot],
            })
            .unwrap();
        let mut realm = builder.build();
        let res = realm.flush_routes().await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::FailedToRoute(frealmbuilder::RealmBuilderError::UnableToExpose)) => {
                ()
            }
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn offer_storage_from_child_error() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

        builder
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
            })
            .unwrap();
        let mut realm = builder.build();
        let res = realm.flush_routes().await;

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(error::Error::FailedToRoute(frealmbuilder::RealmBuilderError::UnableToExpose)) => {
                ()
            }
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_events_routing() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");
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
                                target: cm_rust::OfferTarget::static_child("a".to_string()),
                                target_name: "started".into(),
                                mode: EventMode::Sync,
                                filter: None,
                            }),
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "capability_requested".into(),
                                target: cm_rust::OfferTarget::static_child("a".to_string()),
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
                            dependency_type: DependencyType::Strong,
                        })],
                        offers: vec![
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Framework,
                                source_name: "directory_ready".into(),
                                target: cm_rust::OfferTarget::static_child("b".to_string()),
                                target_name: "directory_ready".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "diagnostics".to_string()))),
                            }),
                            OfferDecl::Event(OfferEventDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "capability_requested".into(),
                                target: cm_rust::OfferTarget::static_child("b".to_string()),
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
                                        "0".to_string(),
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
                                dependency_type: DependencyType::Strong,
                            }),
                            UseDecl::Event(UseEventDecl {
                                source: UseSource::Parent,
                                source_name: "capability_requested".into(),
                                target_name: "capability_requested".into(),
                                mode: EventMode::Async,
                                filter: Some(hashmap!(
                                    "name".to_string() => DictionaryValue::Str(
                                        "fuchsia.logger.LogSink".to_string()))),
                                dependency_type: DependencyType::Strong,
                            }),
                        ],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_storage_routing() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");
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
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
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

    #[fasync::run_singlethreaded(test)]
    async fn two_sibling_realm_no_mocks() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
                        source: cm_rust::OfferSource::static_child("a".to_string()),
                        source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        target: OfferTarget::static_child("b".to_string()),
                        target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        dependency_type: DependencyType::Strong,
                    })],
                    children: vec![
                        ChildDecl {
                            name: "a".to_string(),
                            url: "fuchsia-pkg://a".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        },
                        ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        },
                    ],
                    ..ComponentDecl::default()
                },
            )],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn two_sibling_realm_both_mocks() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
                            source: cm_rust::OfferSource::static_child("a".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: OfferTarget::static_child("b".to_string()),
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
                            source_path: Some(
                                "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            ),
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
                            dependency_type: DependencyType::Strong,
                        })],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn mock_with_child() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
                            source_path: Some(
                                "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            ),
                        })],
                        offers: vec![OfferDecl::Protocol(OfferProtocolDecl {
                            source: cm_rust::OfferSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: OfferTarget::static_child("b".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                        })],
                        children: vec![ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        }],
                        ..ComponentDecl::default()
                    },
                ),
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn three_sibling_realm_one_mock() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: OfferTarget::static_child("b".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                            }),
                            OfferDecl::Directory(OfferDirectoryDecl {
                                source: cm_rust::OfferSource::static_child("b".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: OfferTarget::static_child("c".to_string()),
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
                                on_terminate: None,
                            },
                            ChildDecl {
                                name: "c".to_string(),
                                url: "fuchsia-pkg://c".to_string(),
                                startup: fsys::StartupMode::Eager,
                                environment: None,
                                on_terminate: None,
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
                            dependency_type: DependencyType::Strong,
                        })],
                        capabilities: vec![CapabilityDecl::Directory(DirectoryDecl {
                            name: "example-dir".try_into().unwrap(),
                            source_path: Some("/example".try_into().unwrap()),
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

    #[fasync::run_singlethreaded(test)]
    async fn three_siblings_two_targets() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: OfferTarget::static_child("a".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                        }),
                        OfferDecl::Protocol(OfferProtocolDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: OfferTarget::static_child("c".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                        }),
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "example-dir".try_into().unwrap(),
                            target: OfferTarget::static_child("a".to_string()),
                            target_name: "example-dir".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                            rights: None,
                            subdir: None,
                        }),
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "example-dir".try_into().unwrap(),
                            target: OfferTarget::static_child("c".to_string()),
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
                            on_terminate: None,
                        },
                        ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        },
                        ChildDecl {
                            name: "c".to_string(),
                            url: "fuchsia-pkg://c".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        },
                    ],
                    ..ComponentDecl::default()
                },
            )],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn two_cousins_realm_one_mock() {
        let mut builder = RealmBuilder::new().await.expect("failed to make RealmBuilder");

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
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: OfferTarget::static_child("c".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                            OfferDecl::Protocol(OfferProtocolDecl {
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: OfferTarget::static_child("c".to_string()),
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
                            on_terminate: None,
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
                                target: OfferTarget::static_child("d".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                            OfferDecl::Protocol(OfferProtocolDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: OfferTarget::static_child("d".to_string()),
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
                                dependency_type: DependencyType::Strong,
                            }),
                            UseDecl::Protocol(UseProtocolDecl {
                                source: UseSource::Parent,
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target_path: "/svc/fidl.examples.routing.echo.Echo"
                                    .try_into()
                                    .unwrap(),
                                dependency_type: DependencyType::Strong,
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
