// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::*,
    anyhow::{format_err, Context as _},
    cm_rust::{self, FidlIntoNative, NativeIntoFidl},
    component_events::{
        events::{Event as CeEvent, EventMode, EventSource, EventSubscription, Started},
        matcher::EventMatcher,
    },
    fidl::endpoints::{self, ClientEnd, DiscoverableProtocolMarker, Proxy, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_component_test as ftest, fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fidl_fuchsia_io2 as fio2, fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, FutureExt, TryFutureExt},
    maplit::hashmap,
    rand::Rng,
    std::{
        collections::HashMap,
        fmt::{self, Display},
    },
    tracing::*,
};

/// The default name of the child component collection that contains built topologies.
pub const DEFAULT_COLLECTION_NAME: &'static str = "fuchsia_component_test_collection";
const REALM_BUILDER_SERVER_CHILD_NAME: &'static str = "realm_builder_server";

pub mod error;
mod event;
mod local_component_runner;
pub mod mock;

pub use local_component_runner::LocalComponentHandles;

/// The path from the root component in a constructed realm to a component. For example, given the
/// following realm:
///
/// ```
/// <root>
///   |
///  foo
///   |
///  bar
/// ```
///
/// the monikers for each of three components in the realm is as follows:
///
/// - "" (the root)
/// - "foo"
/// - "foo/bar"
#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Moniker {
    path: Vec<String>,
}

impl From<&str> for Moniker {
    fn from(s: &str) -> Self {
        Moniker {
            path: match s {
                "" => vec![],
                _ => s.split('/').map(|s| s.to_string()).collect(),
            },
        }
    }
}

impl From<String> for Moniker {
    fn from(s: String) -> Self {
        s.as_str().into()
    }
}

impl From<Vec<String>> for Moniker {
    fn from(path: Vec<String>) -> Self {
        Moniker { path }
    }
}

impl Display for Moniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.is_root() {
            write!(f, "<root of test realms>")
        } else {
            write!(f, "{}", self.path.join("/"))
        }
    }
}

impl Moniker {
    /// The moniker of the root component.
    pub fn root() -> Self {
        Moniker { path: vec![] }
    }

    pub fn to_string(&self) -> String {
        self.path.join("/")
    }

    fn is_root(&self) -> bool {
        return self.path.is_empty();
    }

    fn child_name(&self) -> Option<&String> {
        self.path.last()
    }

    fn child(&self, child_name: String) -> Self {
        let mut path = self.path.clone();
        path.push(child_name);
        Moniker { path }
    }

    fn parent(&self) -> Option<Self> {
        let mut path = self.path.clone();
        path.pop()?;
        Some(Moniker { path })
    }

    // If self is an ancestor of other_moniker, then returns the path to reach other_moniker from
    // self. Panics if self is not a parent of other_moniker.
    fn downward_path_to(&self, other_moniker: &Moniker) -> Vec<String> {
        let our_path = self.path.clone();
        let mut their_path = other_moniker.path.clone();
        for item in our_path {
            if Some(&item) != their_path.get(0) {
                panic!("downward_path_to called on non-ancestor moniker");
            }
            their_path.remove(0);
        }
        their_path
    }

    fn is_ancestor_of(&self, other_moniker: &Moniker) -> bool {
        if self.path.len() >= other_moniker.path.len() {
            return false;
        }
        for (element_from_us, element_from_them) in self.path.iter().zip(other_moniker.path.iter())
        {
            if element_from_us != element_from_them {
                return false;
            }
        }
        return true;
    }
}

/// The source or destination of a capability route.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum RouteEndpoint {
    /// One end of this capability route is a component in our custom realms. The value of this
    /// should be a moniker that was used in a prior [`RealmBuilder::add_child`] call.
    Component(String),

    /// One end of this capability route is above the root component in the generated realms
    AboveRoot,

    /// One end of this capability is routed from the debug section of the component's environment.
    Debug,
}

impl Into<ftest::RouteEndpoint> for RouteEndpoint {
    fn into(self) -> ftest::RouteEndpoint {
        match self {
            RouteEndpoint::AboveRoot => ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {}),
            RouteEndpoint::Debug => ftest::RouteEndpoint::Debug(ftest::Debug {}),
            RouteEndpoint::Component(moniker) => ftest::RouteEndpoint::Component(moniker),
        }
    }
}

impl From<ftest::RouteEndpoint> for RouteEndpoint {
    fn from(input: ftest::RouteEndpoint) -> Self {
        match input {
            ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {}) => RouteEndpoint::AboveRoot,
            ftest::RouteEndpoint::Debug(ftest::Debug {}) => RouteEndpoint::Debug,
            ftest::RouteEndpoint::Component(moniker) => RouteEndpoint::Component(moniker),
            _ => panic!("unexpected input"),
        }
    }
}

impl RouteEndpoint {
    pub fn component(path: impl Into<String>) -> Self {
        Self::Component(path.into())
    }

    pub fn above_root() -> Self {
        Self::AboveRoot
    }

    pub fn debug() -> Self {
        Self::Debug
    }

    fn unwrap_component_moniker(&self) -> Moniker {
        match self {
            RouteEndpoint::Component(m) => m.clone().into(),
            _ => panic!("capability source is not a component"),
        }
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

#[derive(Debug, Clone)]
enum CapabilityOrEvent {
    Capability(ftest::Capability),
    Event(Event, cm_rust::EventMode),
}

/// `RouteBuilder` can be used to construct a new route, for use with `Realm::add_route`.
#[derive(Clone, Debug)]
pub struct RouteBuilder {
    capability: CapabilityOrEvent,
    source: Option<ftest::RouteEndpoint>,
    targets: Vec<ftest::RouteEndpoint>,
    force_route: bool,
}

impl RouteBuilder {
    /// Creates a new RouteBuilder that routes the given service capability
    pub fn service(name: impl Into<String>) -> Self {
        ftest::Capability::Service(ftest::ServiceCapability {
            name: Some(name.into()),
            ..ftest::ServiceCapability::EMPTY
        })
        .into()
    }

    /// Creates a new RouteBuilder that routes the given protocol capability
    pub fn protocol(name: impl Into<String>) -> Self {
        ftest::Capability::Protocol(ftest::ProtocolCapability {
            name: Some(name.into()),
            ..ftest::ProtocolCapability::EMPTY
        })
        .into()
    }

    /// Creates a new RouteBuilder that routes the given protocol capability based on its marker
    pub fn protocol_marker<P: DiscoverableProtocolMarker>() -> Self {
        ftest::Capability::Protocol(ftest::ProtocolCapability {
            name: Some(P::PROTOCOL_NAME.into()),
            ..ftest::ProtocolCapability::EMPTY
        })
        .into()
    }

    /// Creates a new RouteBuilder that routes the given directory capability
    pub fn directory(
        name: impl Into<String>,
        path: impl Into<String>,
        rights: fio2::Operations,
    ) -> Self {
        ftest::Capability::Directory(ftest::DirectoryCapability {
            name: Some(name.into()),
            path: Some(path.into()),
            rights: Some(rights),
            ..ftest::DirectoryCapability::EMPTY
        })
        .into()
    }

    /// Creates a new RouteBuilder that routes the given storage capability
    pub fn storage(name: impl Into<String>, path: impl Into<String>) -> Self {
        ftest::Capability::Storage(ftest::StorageCapability {
            name: Some(name.into()),
            path: Some(path.into()),
            ..ftest::StorageCapability::EMPTY
        })
        .into()
    }

    /// Creates a new RouteBuilder that routes the given event capability
    pub fn event(event: Event, mode: cm_rust::EventMode) -> Self {
        Self {
            capability: CapabilityOrEvent::Event(event, mode),
            source: None,
            targets: vec![],
            force_route: false,
        }
    }

    /// Routes the capability from the given source
    pub fn source(mut self, source: impl Into<ftest::RouteEndpoint>) -> Self {
        self.source = Some(source.into());
        self
    }

    /// Routes the capability to the given target
    pub fn targets(mut self, targets: Vec<impl Into<ftest::RouteEndpoint>>) -> Self {
        self.targets = targets.into_iter().map(Into::into).collect();
        self
    }

    /// Causes this route to update components that were loaded from the test's package
    pub fn force(mut self) -> Self {
        self.force_route = true;
        self
    }
}

impl From<ftest::Capability> for RouteBuilder {
    fn from(capability: ftest::Capability) -> Self {
        Self {
            capability: CapabilityOrEvent::Capability(capability),
            source: None,
            targets: vec![],
            force_route: false,
        }
    }
}

impl Into<ftest::CapabilityRoute> for RouteBuilder {
    fn into(self) -> ftest::CapabilityRoute {
        if self.targets.is_empty() {
            panic!("targets was not specified for route");
        }
        match self.capability {
            CapabilityOrEvent::Capability(capability) => ftest::CapabilityRoute {
                capability: Some(capability),
                source: Some(self.source.expect("source wsa not specified for route")),
                targets: Some(self.targets),
                force_route: Some(self.force_route),
                ..ftest::CapabilityRoute::EMPTY
            },
            CapabilityOrEvent::Event(_, _) => panic!("cannot convert event route into FIDL"),
        }
    }
}

impl Into<ftest::Capability> for RouteBuilder {
    fn into(self) -> ftest::Capability {
        match self.capability {
            CapabilityOrEvent::Capability(capability) => capability,
            CapabilityOrEvent::Event(_, _) => panic!("cannot convert event capability into FIDL"),
        }
    }
}

pub trait IntoFidlOrEventRoute {
    fn into_fidl_or_event_route(
        self,
    ) -> (Option<ftest::CapabilityRoute>, Option<event::CapabilityRoute>);
}

impl IntoFidlOrEventRoute for ftest::CapabilityRoute {
    fn into_fidl_or_event_route(
        self,
    ) -> (Option<ftest::CapabilityRoute>, Option<event::CapabilityRoute>) {
        (Some(self), None)
    }
}

impl IntoFidlOrEventRoute for RouteBuilder {
    fn into_fidl_or_event_route(
        self,
    ) -> (Option<ftest::CapabilityRoute>, Option<event::CapabilityRoute>) {
        if let CapabilityOrEvent::Event(event, mode) = &self.capability {
            if self.targets.is_empty() {
                panic!("targets was not specified for route");
            }
            (
                None,
                Some(event::CapabilityRoute {
                    capability: event::Capability::Event(event.clone(), mode.clone()),
                    source: self.source.expect("source wsa not specified for route").into(),
                    targets: self.targets.into_iter().map(Into::into).collect(),
                }),
            )
        } else {
            (Some(self.into()), None)
        }
    }
}

/// A running instance of a created [`Realm`]. When this struct is dropped the child components
/// are destroyed.
pub struct RealmInstance {
    /// The root component of this realm instance, which can be used to access exposed capabilities
    /// from the realm.
    pub root: ScopedInstance,
    // We want to ensure that the mocks runner remains alive for as long as the realm exists, so
    // the ScopedInstance is bundled up into a struct along with the mocks runner.
    mocks_runner: mock::MocksRunner,
}

impl Drop for RealmInstance {
    /// To ensure component mocks are shutdown in an orderly manner (i.e. after their dependent
    /// clients) upon `drop`, keep the mocks_runner_task alive in an async task until the
    /// destroy_waiter synchronously destroys the realm.
    fn drop(&mut self) {
        match (self.mocks_runner.take_runner_task(), self.root.destroy_waiter_taken()) {
            (Some(mocks_runner_task), false) => {
                let destroy_waiter = self.root.take_destroy_waiter();
                fasync::Task::local(async move {
                    // move the mocks_runner into this block
                    let _mocks_runner_task = mocks_runner_task;
                    // There's nothing to be done if we fail to destroy the child, perhaps someone
                    // else already destroyed it for us. Ignore any error we could get here.
                    let _ = destroy_waiter.await;
                })
                .detach();
            }
            _ => (),
        }
    }
}

impl RealmInstance {
    /// Destroys the realm instance, returning only once realm destruction is complete.
    ///
    /// This function can be useful to call when it's important to ensure a realm accessing a
    /// global resource is stopped before proceeding, or to ensure that realm destruction doesn't
    /// race with process (and thus mock component) termination.
    pub async fn destroy(mut self) -> Result<(), Error> {
        let _mocks_runner_task =
            self.mocks_runner.take_runner_task().expect("mocks runner already taken");
        if self.root.destroy_waiter_taken() {
            return Err(Error::DestroyWaiterTaken);
        }
        let destroy_waiter = self.root.take_destroy_waiter();
        drop(self);
        destroy_waiter.await.map_err(RealmError::FailedToDestroyChild)?;
        Ok(())
    }
}

/// The properties for a child being added to a realm
#[derive(Debug, Clone)]
pub struct ChildOptions {
    startup: fdecl::StartupMode,
}

impl ChildOptions {
    pub fn new() -> Self {
        Self { startup: fdecl::StartupMode::Lazy }
    }

    pub fn eager(mut self) -> Self {
        self.startup = fdecl::StartupMode::Eager;
        self
    }
}

/// `RealmBuilder` takes as input a set of component definitions and routes between them and
/// produces a `RealmInstance`.
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
/// ```
///
/// Where `c` is a URL component and `b` is a mock component, `c` accesses the `fuchsia.foobar`
/// protocol from `b`, and the `artifacts` directory is exposed from `b` up through `a`. This
/// structure can be built with the following:
///
/// ```
/// let mut builder = RealmBuilder::new().await?;
/// builder
///     .add_child(
///         "c",
///         "fuchsia-pkg://fuchsia.com/d#meta/d.cm",
///         ChildOptions::new(),
///     ).await?
///     .add_mock_child(
///         "b",
///         move |h: MockHandles| { Box::pin(implementation_for_b(h)) },
///         ChildOptions::new(),
///     ).await?
///     .add_route(
///         RouteBuilder::protocol("fuchsia.foobar")
///             .source(RouteEndpoint::component("b"))
///             .targets(vec![RouteEndpoint::component("c/d")])
///     ).await?
///     .add_route(
///         RouteBuilder::directory("artifacts", "/path-for-artifacts", fio2::RW_STAR_DIR)
///             .source(RouteEndpoint::component("b"))
///             .targets(vec![RouteEndpoint::above_root()])
///     ).await?;
/// let realm_instance = builder.build().await?;
/// ```
///
/// Note that the root component in our imagined structure is actually unnamed when working with
/// `RealmBuilder`. The name is generated when the component is created in a collection.
#[derive(Debug)]
pub struct RealmBuilder {
    realm_builder_proxy: ftest::RealmBuilderProxy,
    mocks_runner: mock::MocksRunner,
    collection_name: String,
}

impl RealmBuilder {
    pub async fn new() -> Result<Self, Error> {
        let realm_proxy = fclient::connect_to_protocol::<fcomponent::RealmMarker>()
            .map_err(RealmError::ConnectToRealmService)?;
        let (exposed_dir_proxy, exposed_dir_server_end) =
            endpoints::create_proxy::<fio::DirectoryMarker>().map_err(RealmError::CreateProxy)?;
        realm_proxy
            .open_exposed_dir(
                &mut fdecl::ChildRef {
                    name: REALM_BUILDER_SERVER_CHILD_NAME.to_string(),
                    collection: None,
                },
                exposed_dir_server_end,
            )
            .await
            .map_err(RealmError::FailedToUseRealm)?
            .map_err(RealmError::FailedBindToRealmBuilder)?;
        let realm_builder_proxy = fclient::connect_to_protocol_at_dir_root::<
            ftest::RealmBuilderMarker,
        >(&exposed_dir_proxy)
        .map_err(RealmError::ConnectToRealmBuilderService)?;

        let pkg_dir_proxy = io_util::open_directory_in_namespace(
            "/pkg",
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_EXECUTABLE,
        )
        .map_err(Error::FailedToOpenPkgDir)?;
        realm_builder_proxy
            .init(ClientEnd::from(pkg_dir_proxy.into_channel().unwrap().into_zx_channel()))
            .await?
            .map_err(Error::FailedToSetPkgDir)?;

        Self::new_with_realm_builder_proxy(realm_builder_proxy)
    }

    fn new_with_realm_builder_proxy(
        realm_builder_proxy: ftest::RealmBuilderProxy,
    ) -> Result<Self, Error> {
        let mocks_runner = mock::MocksRunner::new(realm_builder_proxy.take_event_stream());
        Ok(Self {
            realm_builder_proxy,
            mocks_runner,
            collection_name: DEFAULT_COLLECTION_NAME.to_string(),
        })
    }

    /// Returns `true` if the given moniker is present in the realm being built.
    pub async fn contains(&self, moniker: impl Into<Moniker>) -> Result<bool, Error> {
        let moniker: Moniker = moniker.into();
        self.realm_builder_proxy.contains(&moniker.to_string()).await.map_err(Error::FidlError)
    }

    /// Adds a new mock component to the realm
    pub async fn add_mock_child<M>(
        &self,
        moniker: impl Into<Moniker>,
        mock_fn: M,
        properties: ChildOptions,
    ) -> Result<&Self, Error>
    where
        M: Fn(mock::MockHandles) -> BoxFuture<'static, Result<(), anyhow::Error>>
            + Sync
            + Send
            + 'static,
    {
        let moniker = moniker.into();
        if self.contains(moniker.clone()).await? {
            return Err(BuilderError::ComponentAlreadyExists(moniker).into());
        }
        let mock_id = self
            .realm_builder_proxy
            .set_mock_component(&moniker.to_string())
            .await
            .map_err(RealmError::FailedToUseRealmBuilder)?
            .map_err(|s| RealmError::FailedToSetMock(moniker.clone(), s))?;
        self.mocks_runner.register_mock(mock_id, mock::Mock::new(mock_fn)).await;

        if properties.startup == fdecl::StartupMode::Eager {
            self.realm_builder_proxy
                .mark_as_eager(&moniker.to_string())
                .await?
                .map_err(|s| Error::FailedToMarkAsEager(moniker.clone(), s))?;
        }
        Ok(&self)
    }

    /// Adds a new component to the realm by URL
    pub async fn add_child(
        &self,
        moniker: impl Into<Moniker>,
        url: impl Into<String>,
        properties: ChildOptions,
    ) -> Result<&Self, Error> {
        let moniker: Moniker = moniker.into();
        if self.contains(moniker.clone()).await? {
            return Err(BuilderError::ComponentAlreadyExists(moniker).into());
        }
        self.realm_builder_proxy
            .set_component(&moniker.to_string(), &mut ftest::Component::Url(url.into()))
            .await?
            .map_err(|s| Error::FailedToSetDecl(moniker.clone(), s))?;

        if properties.startup == fdecl::StartupMode::Eager {
            self.realm_builder_proxy
                .mark_as_eager(&moniker.to_string())
                .await?
                .map_err(|s| Error::FailedToMarkAsEager(moniker.clone(), s))?;
        }
        Ok(&self)
    }

    /// Adds a new legacy component to the realm
    pub async fn add_legacy_child(
        &self,
        moniker: impl Into<Moniker>,
        legacy_url: impl Into<String>,
        properties: ChildOptions,
    ) -> Result<&Self, Error> {
        let moniker: Moniker = moniker.into();
        if self.contains(moniker.clone()).await? {
            return Err(BuilderError::ComponentAlreadyExists(moniker).into());
        }
        self.realm_builder_proxy
            .set_component(
                &moniker.to_string(),
                &mut ftest::Component::LegacyUrl(legacy_url.into()),
            )
            .await?
            .map_err(|s| Error::FailedToSetDecl(moniker.clone(), s))?;

        if properties.startup == fdecl::StartupMode::Eager {
            self.realm_builder_proxy
                .mark_as_eager(&moniker.to_string())
                .await?
                .map_err(|s| Error::FailedToMarkAsEager(moniker.clone(), s))?;
        }
        Ok(&self)
    }

    /// Adds a new component to the realm with the given component declaration
    pub async fn add_child_from_decl(
        &self,
        moniker: impl Into<Moniker>,
        decl: cm_rust::ComponentDecl,
        properties: ChildOptions,
    ) -> Result<&Self, Error> {
        let moniker: Moniker = moniker.into();
        if self.contains(moniker.clone()).await? {
            return Err(BuilderError::ComponentAlreadyExists(moniker).into());
        }
        self.realm_builder_proxy
            .set_component(
                &moniker.to_string(),
                &mut ftest::Component::Decl(decl.native_into_fidl()),
            )
            .await?
            .map_err(|s| Error::FailedToSetDecl(moniker.clone(), s))?;

        if properties.startup == fdecl::StartupMode::Eager {
            self.realm_builder_proxy
                .mark_as_eager(&moniker.to_string())
                .await?
                .map_err(|s| Error::FailedToMarkAsEager(moniker.clone(), s))?;
        }
        Ok(&self)
    }

    /// Marks a component in the realm as eager
    pub async fn mark_as_eager(&self, moniker: impl Into<Moniker>) -> Result<&Self, Error> {
        let moniker: Moniker = moniker.into();
        self.realm_builder_proxy
            .mark_as_eager(&moniker.to_string())
            .await?
            .map_err(|s| Error::FailedToMarkAsEager(moniker.clone(), s))?;
        Ok(&self)
    }

    /// Returns a copy of a component decl in the realm.
    pub async fn get_decl(
        &self,
        moniker: impl Into<Moniker>,
    ) -> Result<cm_rust::ComponentDecl, Error> {
        let moniker: Moniker = moniker.into();
        let decl = self
            .realm_builder_proxy
            .get_component_decl(&moniker.to_string())
            .await?
            .map_err(|s| Error::FailedToGetDecl(moniker.clone(), s))?;
        Ok(decl.fidl_into_native())
    }

    /// Sets the component decl for a component in the realm.
    pub async fn set_decl(
        &self,
        moniker: impl Into<Moniker>,
        decl: cm_rust::ComponentDecl,
    ) -> Result<(), Error> {
        let moniker: Moniker = moniker.into();
        if !self.contains(moniker.clone()).await? {
            return Err(BuilderError::ComponentDoesNotExist(moniker).into());
        }
        let decl = decl.native_into_fidl();
        self.realm_builder_proxy
            .set_component(&moniker.to_string(), &mut ftest::Component::Decl(decl))
            .await?
            .map_err(|s| Error::FailedToSetDecl(moniker.clone(), s))
    }

    /// Sets the name of the collection that this realm will be created in
    pub fn set_collection_name(&mut self, collection_name: impl Into<String>) {
        self.collection_name = collection_name.into();
    }

    /// Adds a route between components within the realm
    pub async fn add_route(&self, route: impl IntoFidlOrEventRoute) -> Result<&Self, Error> {
        match route.into_fidl_or_event_route() {
            (Some(fidl_route), None) => self
                .realm_builder_proxy
                .route_capability(fidl_route)
                .await?
                .map_err(|s| Error::FailedToRoute(s))?,
            (None, Some(event_route)) => event::add_event_route(&self, event_route).await?,
            r => panic!("unexpected result from into_fidl_or_event_route: {:?}", r),
        }
        Ok(&self)
    }

    /// Initializes the realm, but doesn't create it. Returns the root URL, the collection name,
    /// and the mocks runner. The caller should pass the URL and collection name into
    /// `fuchsia.component.Realm#CreateChild`, and keep the mocks runner alive until after
    /// `fuchsia.component.Realm#DestroyChild` has been called.
    pub async fn initialize(self) -> Result<(String, String, mock::MocksRunner), Error> {
        let root_url =
            self.realm_builder_proxy.commit().await?.map_err(|s| Error::FailedToCommit(s))?;
        Ok((root_url, self.collection_name, self.mocks_runner))
    }

    /// Creates this realm in a child component collection, using an autogenerated name for the
    /// instance. By default this happens in the [`DEFAULT_COLLECTION_NAME`] collection.
    ///
    /// After creation it connects to the fuchsia.component.Binder protocol exposed from the root
    /// realm, which gets added automatically by the server.
    pub async fn build(self) -> Result<RealmInstance, Error> {
        let (root_url, collection_name, mocks_runner) = self.initialize().await?;
        let root = ScopedInstance::new(collection_name, root_url)
            .await
            .map_err(RealmError::FailedToCreateChild)?;
        root.connect_to_binder().map_err(Error::FailedToBind)?;
        Ok(RealmInstance { root, mocks_runner })
    }

    /// Creates this realm in a child component collection. By default this happens in the
    /// [`DEFAULT_COLLECTION_NAME`] collection.
    pub async fn build_with_name(self, child_name: String) -> Result<RealmInstance, Error> {
        let (root_url, collection_name, mocks_runner) = self.initialize().await?;
        let root = ScopedInstance::new_with_name(child_name, collection_name, root_url)
            .await
            .map_err(RealmError::FailedToCreateChild)?;
        root.connect_to_binder().map_err(Error::FailedToBind)?;
        Ok(RealmInstance { root, mocks_runner })
    }

    /// Launches a nested component manager which will run the created realm (along with any mocks
    /// in the realm). This component manager _must_ be referenced by a relative URL.
    ///
    /// Note that any routes with a source of `above_root` will need to also be used in component
    /// manager's manifest and listed as a namespace capability in its config.
    ///
    /// Note that any routes with a target of `above_root` will result in exposing the capability
    /// to component manager, which is rather useless by itself. Component manager does expose the
    /// hub though, which could be traversed to find an exposed capability.
    pub async fn build_in_nested_component_manager(
        self,
        component_manager_relative_url: &str,
    ) -> Result<RealmInstance, Error> {
        let (root_url, collection_name, mocks_runner) = self.initialize().await?;

        // We now have a root URL we could create in a collection, but instead we want to launch a
        // component manager and give that component manager this root URL. That flag is set with
        // command line arguments, so we can't just launch an unmodified component manager.
        //
        // Open a new connection to the realm builder server to begin creating a new realm. This
        // new realm will hold a single component: component manager. We will modify its manifest
        // such that the root component URL is set to the root_url we just obtained, and the nested
        // component manager will then fetch the manifest from realm builder itself.
        //
        // Note this presumes that component manager is pointed to a config with following line:
        //
        //     realm_builder_resolver_and_runner: "namespace",

        let component_manager_realm = Self::new().await?;
        component_manager_realm
            .add_child(
                Moniker::root(),
                component_manager_relative_url.to_string(),
                ChildOptions::new(),
            )
            .await?;
        let mut component_manager_decl = component_manager_realm.get_decl(Moniker::root()).await?;
        match **component_manager_decl
            .program
            .as_mut()
            .expect("component manager's manifest is lacking a program section")
            .info
            .entries
            .get_or_insert(vec![])
            .iter_mut()
            .find(|e| e.key == "args")
            .expect("component manager's manifest doesn't specify a config")
            .value
            .as_mut()
            .expect("component manager's manifest has a malformed 'args' section") {
                fdata::DictionaryValue::StrVec(ref mut v) => v.push(root_url),
                _ => panic!("component manager's manifest has a single value for 'args', but we were expecting a vector"),
        }
        component_manager_realm.set_decl(Moniker::root(), component_manager_decl).await?;

        for protocol_name in
            vec!["fuchsia.sys2.ComponentResolver", "fuchsia.component.runner.ComponentRunner"]
        {
            component_manager_realm
                .add_route(
                    RouteBuilder::protocol(protocol_name)
                        .source(RouteEndpoint::above_root())
                        .targets(vec![RouteEndpoint::component("")])
                        .force(),
                )
                .await?;
        }
        component_manager_realm
            .add_route(
                RouteBuilder::directory("hub", "/hub", fio2::RW_STAR_DIR)
                    .source(RouteEndpoint::component(""))
                    .targets(vec![RouteEndpoint::above_root()])
                    .force(),
            )
            .await?;

        let (component_manager_url, _, _) = component_manager_realm.initialize().await?;
        let root = ScopedInstance::new(collection_name, component_manager_url)
            .await
            .map_err(RealmError::FailedToCreateChild)?;
        root.connect_to_binder().map_err(Error::FailedToBind)?;
        Ok(RealmInstance { root, mocks_runner })
    }
}

/// Manages the creation of new components within a collection.
pub struct ScopedInstanceFactory {
    realm_proxy: Option<fcomponent::RealmProxy>,
    collection_name: String,
}

impl ScopedInstanceFactory {
    /// Creates a new factory that creates components in the specified collection.
    pub fn new(collection_name: impl Into<String>) -> Self {
        ScopedInstanceFactory { realm_proxy: None, collection_name: collection_name.into() }
    }

    /// Use `realm_proxy` instead of the fuchsia.component.Realm protocol in this component's
    /// incoming namespace. This can be used to start component's in a collection belonging
    /// to another component.
    pub fn with_realm_proxy(mut self, realm_proxy: fcomponent::RealmProxy) -> Self {
        self.realm_proxy = Some(realm_proxy);
        self
    }

    /// Creates and binds to a new component just like `new_named_instance`, but uses an
    /// autogenerated name for the instance.
    pub async fn new_instance(
        &self,
        url: impl Into<String>,
    ) -> Result<ScopedInstance, anyhow::Error> {
        let id: u64 = rand::thread_rng().gen();
        let child_name = format!("auto-{:x}", id);
        self.new_named_instance(child_name, url).await
    }

    /// Creates and binds to a new component named `child_name` with `url`.
    /// A ScopedInstance is returned on success, representing the component's lifetime and
    /// providing access to the component's exposed capabilities.
    ///
    /// When the ScopedInstance is dropped, the component will be asynchronously stopped _and_
    /// destroyed.
    ///
    /// This is useful for tests that wish to create components that should be torn down at the
    /// end of the test, or to explicitly control the lifecycle of a component.
    pub async fn new_named_instance(
        &self,
        child_name: impl Into<String>,
        url: impl Into<String>,
    ) -> Result<ScopedInstance, anyhow::Error> {
        let realm = if let Some(realm_proxy) = self.realm_proxy.as_ref() {
            realm_proxy.clone()
        } else {
            fclient::realm().context("Failed to connect to Realm service")?
        };
        let child_name = child_name.into();
        let mut collection_ref = fdecl::CollectionRef { name: self.collection_name.clone() };
        let child_decl = fdecl::Child {
            name: Some(child_name.clone()),
            url: Some(url.into()),
            startup: Some(fdecl::StartupMode::Lazy),
            ..fdecl::Child::EMPTY
        };
        let child_args = fcomponent::CreateChildArgs {
            numbered_handles: None,
            ..fcomponent::CreateChildArgs::EMPTY
        };
        let () = realm
            .create_child(&mut collection_ref, child_decl, child_args)
            .await
            .context("CreateChild FIDL failed.")?
            .map_err(|e| format_err!("Failed to create child: {:?}", e))?;
        let mut child_ref = fdecl::ChildRef {
            name: child_name.clone(),
            collection: Some(self.collection_name.clone()),
        };
        let (exposed_dir, server) = endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()
            .context("Failed to create directory proxy")?;
        let () = realm
            .open_exposed_dir(&mut child_ref, server)
            .await
            .context("OpenExposedDir FIDL failed.")?
            .map_err(|e| format_err!("Failed to open exposed dir of child: {:?}", e))?;
        Ok(ScopedInstance {
            realm,
            child_name,
            collection: self.collection_name.clone(),
            exposed_dir,
            destroy_channel: None,
        })
    }
}

/// RAII object that keeps a component instance alive until it's dropped, and provides convenience
/// functions for using the instance. Components v2 only.
#[must_use = "Dropping `ScopedInstance` will cause the component instance to be stopped and destroyed."]
pub struct ScopedInstance {
    realm: fcomponent::RealmProxy,
    child_name: String,
    collection: String,
    exposed_dir: fio::DirectoryProxy,
    destroy_channel: Option<
        futures::channel::oneshot::Sender<
            Result<
                fidl::client::QueryResponseFut<fcomponent::RealmDestroyChildResult>,
                anyhow::Error,
            >,
        >,
    >,
}

impl ScopedInstance {
    /// Creates and binds to a new component just like `new_with_name`, but uses an autogenerated
    /// name for the instance.
    pub async fn new(coll: String, url: String) -> Result<Self, anyhow::Error> {
        ScopedInstanceFactory::new(coll).new_instance(url).await
    }

    /// Creates and binds to a new component named `child_name` in a collection `coll` with `url`,
    /// and returning an object that represents the component's lifetime and can be used to access
    /// the component's exposed directory. When the object is dropped, it will be asynchronously
    /// stopped _and_ destroyed. This is useful for tests that wish to create components that
    /// should be torn down at the end of the test. Components v2 only.
    pub async fn new_with_name(
        child_name: String,
        collection: String,
        url: String,
    ) -> Result<Self, anyhow::Error> {
        ScopedInstanceFactory::new(collection).new_named_instance(child_name, url).await
    }

    /// Connect to exposed fuchsia.component.Binder protocol of instance, thus
    /// triggering it to start.
    /// Note: This will only work if the component exposes this protocol in its
    /// manifest.
    pub fn connect_to_binder(&self) -> Result<fcomponent::BinderProxy, anyhow::Error> {
        let binder: fcomponent::BinderProxy = self
            .connect_to_protocol_at_exposed_dir::<fcomponent::BinderMarker>()
            .context("failed to connect to fuchsia.component.Binder")?;

        Ok(binder)
    }

    /// Same as `connect_to_binder` except that it will block until the
    /// component has started.
    /// Note: This function expects that the instance has not been started yet.
    /// If the instance has been started before this method is invoked, then
    /// this method will block forever waiting for the Started event.
    /// REQUIRED: The manifest of the component executing this code must use
    /// the `fuchsia.sys2.EventSource` protocol from the framework and the
    /// "started" event.
    pub async fn start_with_binder_sync(&self) -> Result<(), anyhow::Error> {
        let event_source = EventSource::new().context("failed to create EventSource")?;
        let mut event_stream = event_source
            .subscribe(vec![EventSubscription::new(vec![Started::NAME], EventMode::Async)])
            .await
            .context("failed to subscribe to EventSource")?;

        let _ = self
            .connect_to_protocol_at_exposed_dir::<fcomponent::BinderMarker>()
            .context("failed to connect to fuchsia.component.Binder")?;

        let _ = EventMatcher::ok()
            .moniker_regex(self.child_name.to_owned())
            .wait::<Started>(&mut event_stream)
            .await
            .context("failed to observe Started event")?;

        Ok(())
    }

    /// Connect to an instance of a FIDL protocol hosted in the component's exposed directory`,
    pub fn connect_to_protocol_at_exposed_dir<P: DiscoverableProtocolMarker>(
        &self,
    ) -> Result<P::Proxy, anyhow::Error> {
        fclient::connect_to_protocol_at_dir_root::<P>(&self.exposed_dir)
    }

    /// Connect to an instance of a FIDL protocol hosted in the component's exposed directory`,
    pub fn connect_to_named_protocol_at_exposed_dir<P: DiscoverableProtocolMarker>(
        &self,
        protocol_name: &str,
    ) -> Result<P::Proxy, anyhow::Error> {
        fclient::connect_to_named_protocol_at_dir_root::<P>(&self.exposed_dir, protocol_name)
    }

    /// Connects to an instance of a FIDL protocol hosted in the component's exposed directory
    /// using the given `server_end`.
    pub fn connect_request_to_protocol_at_exposed_dir<P: DiscoverableProtocolMarker>(
        &self,
        server_end: ServerEnd<P>,
    ) -> Result<(), anyhow::Error> {
        self.connect_request_to_named_protocol_at_exposed_dir(
            P::PROTOCOL_NAME,
            server_end.into_channel(),
        )
    }

    /// Connects to an instance of a FIDL protocol called `protocol_name` hosted in the component's
    /// exposed directory using the given `server_end`.
    pub fn connect_request_to_named_protocol_at_exposed_dir(
        &self,
        protocol_name: &str,
        server_end: zx::Channel,
    ) -> Result<(), anyhow::Error> {
        self.exposed_dir
            .open(
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
                fidl_fuchsia_io::MODE_TYPE_SERVICE,
                protocol_name,
                ServerEnd::new(server_end),
            )
            .context("Failed to open protocol in directory")
    }

    /// Returns a reference to the component's read-only exposed directory.
    pub fn get_exposed_dir(&self) -> &fio::DirectoryProxy {
        &self.exposed_dir
    }

    /// Returns true if `take_destroy_waiter` has already been called.
    pub fn destroy_waiter_taken(&self) -> bool {
        self.destroy_channel.is_some()
    }

    /// Returns a future which can be awaited on for destruction to complete after the
    /// `ScopedInstance` is dropped. Panics if called multiple times.
    pub fn take_destroy_waiter(
        &mut self,
    ) -> impl futures::Future<Output = Result<(), anyhow::Error>> {
        if self.destroy_channel.is_some() {
            panic!("destroy waiter already taken");
        }
        let (sender, receiver) = futures::channel::oneshot::channel();
        self.destroy_channel = Some(sender);
        receiver.err_into().and_then(futures::future::ready).and_then(
            |fidl_fut: fidl::client::QueryResponseFut<_>| {
                fidl_fut.map(|r: Result<Result<(), fidl_fuchsia_component::Error>, fidl::Error>| {
                    r.context("DestroyChild FIDL error")?
                        .map_err(|e| format_err!("Failed to destroy child: {:?}", e))
                })
            },
        )
    }
    /// Return the name of this instance.
    pub fn child_name(&self) -> &str {
        self.child_name.as_str()
    }
}

impl Drop for ScopedInstance {
    fn drop(&mut self) {
        let Self { realm, collection, child_name, destroy_channel, exposed_dir: _ } = self;
        let mut child_ref =
            fdecl::ChildRef { name: child_name.clone(), collection: Some(collection.clone()) };
        // DestroyChild also stops the component.
        //
        // Calling destroy child within drop guarantees that the message
        // goes out to the realm regardless of there existing a waiter on
        // the destruction channel.
        let result = Ok(realm.destroy_child(&mut child_ref));
        if let Some(chan) = destroy_channel.take() {
            let () = chan.send(result).unwrap_or_else(|result| {
                warn!("Failed to send result for destroyed scoped instance. Result={:?}", result);
            });
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_component as fcomponent,
        futures::{channel::oneshot, future::pending, lock::Mutex, select},
        matches::assert_matches,
        std::sync::Arc,
    };

    struct SendsOnDrop {
        sender: Option<oneshot::Sender<()>>,
    }

    impl Drop for SendsOnDrop {
        fn drop(&mut self) {
            self.sender.take().expect("sender already taken").send(()).unwrap()
        }
    }

    impl SendsOnDrop {
        fn new() -> (Self, oneshot::Receiver<()>) {
            let (sender, receiver) = oneshot::channel();
            (SendsOnDrop { sender: Some(sender) }, receiver)
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn double_add_a_component() {
        let builder = RealmBuilder::new().await.unwrap();
        builder
            .add_child("a", "fuchsia-pkg://fuchsia.com/a#meta/a.cm", ChildOptions::new())
            .await
            .unwrap();

        let res = builder
            .add_child("a", "fuchsia-pkg://fuchsia.com/a#meta/a.cm", ChildOptions::new())
            .await;
        assert_matches!(
            res,
            Err(Error::Builder(BuilderError::ComponentAlreadyExists(m))) if m == "a".into()
        );

        let res = builder
            .add_mock_child(
                "a",
                |_h: mock::MockHandles| async move { Ok(()) }.boxed(),
                ChildOptions::new(),
            )
            .await;
        assert_matches!(res, Err(Error::Builder(BuilderError::ComponentAlreadyExists(_))));

        let res = builder
            .add_legacy_child("a", "fuchsia-pkg://fuchsia.com/a#meta/a.cmx", ChildOptions::new())
            .await;
        assert_matches!(res, Err(Error::Builder(BuilderError::ComponentAlreadyExists(_))));

        let res = builder
            .add_child_from_decl("a", cm_rust::ComponentDecl::default(), ChildOptions::new())
            .await;
        assert_matches!(res, Err(Error::Builder(BuilderError::ComponentAlreadyExists(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn realm_destroy() {
        let (component_1_sends_on_drop, mut component_1_drop_receiver) = SendsOnDrop::new();
        let (component_2_sends_on_drop, mut component_2_drop_receiver) = SendsOnDrop::new();
        let component_1_sends_on_drop = Arc::new(Mutex::new(Some(component_1_sends_on_drop)));
        let component_2_sends_on_drop = Arc::new(Mutex::new(Some(component_2_sends_on_drop)));

        let (component_1_start_sender, component_1_start_receiver) = oneshot::channel();
        let (component_2_start_sender, component_2_start_receiver) = oneshot::channel();
        let component_1_start_sender = Arc::new(Mutex::new(Some(component_1_start_sender)));
        let component_2_start_sender = Arc::new(Mutex::new(Some(component_2_start_sender)));

        let builder = RealmBuilder::new().await.unwrap();
        builder
            .add_mock_child(
                "component_1",
                move |_mh: mock::MockHandles| {
                    let component_1_start_sender = component_1_start_sender.clone();
                    let component_1_sends_on_drop = component_1_sends_on_drop.clone();
                    Box::pin(async move {
                        component_1_start_sender.lock().await.take().unwrap().send(()).unwrap();
                        let _sends_on_drop = component_1_sends_on_drop.lock().await.take().unwrap();
                        let () = pending().await;
                        Ok(())
                    })
                },
                ChildOptions::new().eager(),
            )
            .await
            .expect("failed to add component_1")
            .add_mock_child(
                "component_2",
                move |_mh: mock::MockHandles| {
                    let component_2_start_sender = component_2_start_sender.clone();
                    let component_2_sends_on_drop = component_2_sends_on_drop.clone();
                    Box::pin(async move {
                        component_2_start_sender.lock().await.take().unwrap().send(()).unwrap();
                        let _sends_on_drop = component_2_sends_on_drop.lock().await.take().unwrap();
                        let () = pending().await;
                        Ok(())
                    })
                },
                ChildOptions::new().eager(),
            )
            .await
            .expect("failed to add component_2");

        let realm_instance = builder.build().await.expect("failed to create the realm");

        let _ = realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<fcomponent::BinderMarker>()
            .unwrap();

        // Wait for the component to report that they started
        component_1_start_receiver.await.expect("component_1 never started");
        component_2_start_receiver.await.expect("component_2 never started");

        // Confirm that the components are still running
        select! {
            res = component_1_drop_receiver => panic!(
                "component_1 should still be running, but we received this: {:?}", res),
            res = component_2_drop_receiver => panic!(
                "component_2 should still be running, but we received this: {:?}", res),
            default => (),
        };

        // Destroy the realm, which will stop the components
        realm_instance.destroy().await.expect("failed to destroy realm");

        // Check that the components reported themselves being dropped
        component_1_drop_receiver.await.expect("failed to receive stop notificaiton");
        component_2_drop_receiver.await.expect("failed to receive stop notificaiton");
        // TODO(82021): reenable the following
        //select! {
        //    res = component_1_drop_receiver => res.expect("failed to receive stop notification"),
        //    default => panic!("component_1 should have stopped by now"),
        //}
        //select! {
        //    res = component_2_drop_receiver => res.expect("failed to receive stop notification"),
        //    default => panic!("component_2 should have stopped by now"),
        //}
    }

    #[fasync::run_singlethreaded(test)]
    async fn out_of_band_child_destruction_doesnt_panic_on_destroy() {
        let builder = RealmBuilder::new().await.unwrap();
        builder
            .add_mock_child(
                "component_1",
                move |_mh: mock::MockHandles| {
                    Box::pin(async move {
                        std::future::pending::<()>().await;
                        Ok(())
                    })
                },
                ChildOptions::new().eager(),
            )
            .await
            .expect("failed to add component_1");

        let realm_instance = builder.build().await.expect("failed to make realm");

        let realm_proxy = fclient::realm().expect("failed to connect to realm");
        realm_proxy
            .destroy_child(&mut fdecl::ChildRef {
                name: realm_instance.root.child_name.clone(),
                collection: Some(DEFAULT_COLLECTION_NAME.to_string()),
            })
            .await
            .expect("failed to send destroy child")
            .expect("destroy child returned an error");

        match realm_instance.destroy().await {
            Err(Error::Realm(RealmError::FailedToDestroyChild(_))) => (),
            Ok(()) => panic!("destroy should have errored"),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn out_of_band_child_destruction_doesnt_panic_on_drop() {
        let builder = RealmBuilder::new().await.unwrap();
        builder
            .add_mock_child(
                "component_1",
                move |_mh: mock::MockHandles| {
                    Box::pin(async move {
                        std::future::pending::<()>().await;
                        Ok(())
                    })
                },
                ChildOptions::new(),
            )
            .await
            .expect("failed to add component_1");

        let realm_instance = builder.build().await.expect("failed to make realm");

        let realm_proxy = fclient::realm().expect("failed to connect to realm");
        realm_proxy
            .destroy_child(&mut fdecl::ChildRef {
                name: realm_instance.root.child_name.clone(),
                collection: Some(DEFAULT_COLLECTION_NAME.to_string()),
            })
            .await
            .expect("failed to send destroy child")
            .expect("destroy child returned an error");

        drop(realm_instance);
        // We want to test that a fuchsia_async::Task spawned by the realm_instance's drop impl
        // doesn't panic in this case, but we don't have a good way to know when the task is
        // complete. Let's give it a second to execute, which should be more than enough time.
        fasync::Timer::new(std::time::Duration::from_secs(1)).await;
    }
}
