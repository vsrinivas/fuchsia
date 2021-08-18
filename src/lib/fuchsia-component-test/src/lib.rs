// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::*,
    anyhow::{format_err, Context as _},
    cm_rust::{self, FidlIntoNative, NativeIntoFidl},
    fidl::endpoints::{self, ClientEnd, DiscoverableProtocolMarker, Proxy, ServerEnd},
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio, fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_realm_builder as frealmbuilder, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    fuchsia_zircon as zx,
    futures::{FutureExt, TryFutureExt},
    log::*,
    rand::Rng,
    std::{
        convert::TryInto,
        fmt::{self, Display},
    },
};

/// The default name of the child component collection that contains built topologies.
pub const DEFAULT_COLLECTION_NAME: &'static str = "fuchsia_component_test_collection";
const FRAMEWORK_INTERMEDIARY_CHILD_NAME: &'static str =
    "fuchsia_component_test_framework_intermediary";

pub mod builder;
pub mod error;
pub mod mock;

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
                    destroy_waiter.await.expect("failed to wait for realm destruction");
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
        destroy_waiter.await.expect("failed to wait for realm destruction");
        Ok(())
    }
}

/// A custom built realm, which can be created at runtime in a component collection
pub struct Realm {
    framework_intermediary_proxy: frealmbuilder::FrameworkIntermediaryProxy,
    mocks_runner: mock::MocksRunner,
    collection_name: String,

    /// The builder's `add_route` function used to be non-async, but then this logic was moved into
    /// a synchronous operation with the intermediary, which means we need `async` to add routes.
    /// To not break existing clients, store routes they wanted to add here and add them in on
    /// `.initialize()`. Clients can then be slowly migrated to using `Realm::add_route` instead of
    /// `RealmBuilder::add_route`.
    routes_to_add: Vec<builder::CapabilityRoute>,
}

impl Realm {
    pub async fn new() -> Result<Self, Error> {
        let realm_proxy = fclient::connect_to_protocol::<fsys::RealmMarker>()
            .map_err(RealmError::ConnectToRealmService)?;
        let (exposed_dir_proxy, exposed_dir_server_end) =
            endpoints::create_proxy::<fio::DirectoryMarker>().map_err(RealmError::CreateProxy)?;
        realm_proxy
            .open_exposed_dir(
                &mut fsys::ChildRef {
                    name: FRAMEWORK_INTERMEDIARY_CHILD_NAME.to_string(),
                    collection: None,
                },
                exposed_dir_server_end,
            )
            .await
            .map_err(RealmError::FailedToUseRealm)?
            .map_err(RealmError::FailedBindToFrameworkIntermediary)?;
        let framework_intermediary_proxy = fclient::connect_to_protocol_at_dir_root::<
            frealmbuilder::FrameworkIntermediaryMarker,
        >(&exposed_dir_proxy)
        .map_err(RealmError::ConnectToFrameworkIntermediaryService)?;

        let pkg_dir_proxy = io_util::open_directory_in_namespace(
            "/pkg",
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_EXECUTABLE,
        )
        .map_err(Error::FailedToOpenPkgDir)?;
        framework_intermediary_proxy
            .init(ClientEnd::from(pkg_dir_proxy.into_channel().unwrap().into_zx_channel()))
            .await?
            .map_err(Error::FailedToSetPkgDir)?;

        Realm::new_with_framework_intermediary_proxy(framework_intermediary_proxy)
    }

    fn new_with_framework_intermediary_proxy(
        framework_intermediary_proxy: frealmbuilder::FrameworkIntermediaryProxy,
    ) -> Result<Self, Error> {
        let mocks_runner = mock::MocksRunner::new(framework_intermediary_proxy.take_event_stream());
        Ok(Self {
            framework_intermediary_proxy,
            mocks_runner,
            collection_name: DEFAULT_COLLECTION_NAME.to_string(),
            routes_to_add: vec![],
        })
    }

    /// Adds a new mocked component to the realm. When the component is supposed to run the
    /// provided [`Mock`] is called with the component's handles.
    pub async fn add_mocked_component(
        &self,
        moniker: Moniker,
        mock: mock::Mock,
    ) -> Result<(), Error> {
        let mock_id = self
            .framework_intermediary_proxy
            .new_mock_id()
            .await
            .map_err(RealmError::FailedToUseFrameworkIntermediary)?;
        self.mocks_runner.register_mock(mock_id.clone(), mock).await;
        let decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(mock::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: mock::MOCK_ID_KEY.to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(mock_id))),
                    }]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            ..cm_rust::ComponentDecl::default()
        };
        self.set_component(&moniker, decl).await
    }

    // TODO: new comment
    /// Adds a new component to the realm. Note that the provided `ComponentDecl` should not have
    /// child declarations for other components described in this `Realm`, as those will be filled
    /// in when [`Realm::create`] is called.
    pub async fn set_component(
        &self,
        moniker: &Moniker,
        decl: cm_rust::ComponentDecl,
    ) -> Result<(), Error> {
        let decl = decl.native_into_fidl();
        self.framework_intermediary_proxy
            .set_component(&moniker.to_string(), &mut frealmbuilder::Component::Decl(decl))
            .await?
            .map_err(|s| Error::FailedToSetDecl(moniker.clone(), s))
    }

    pub async fn set_component_url(&self, moniker: &Moniker, url: String) -> Result<(), Error> {
        self.framework_intermediary_proxy
            .set_component(&moniker.to_string(), &mut frealmbuilder::Component::Url(url))
            .await?
            .map_err(|s| Error::FailedToSetDecl(moniker.clone(), s))
    }

    pub async fn set_component_legacy_url(
        &self,
        moniker: &Moniker,
        url: String,
    ) -> Result<(), Error> {
        self.framework_intermediary_proxy
            .set_component(&moniker.to_string(), &mut frealmbuilder::Component::LegacyUrl(url))
            .await?
            .map_err(|s| Error::FailedToSetDecl(moniker.clone(), s))
    }

    /// Returns whether or not the given component exists in this realm. This will return true if
    /// the component exists in the realm tree itself, or if the parent contains a child
    /// declaration for the moniker.
    pub async fn contains(&self, moniker: &Moniker) -> Result<bool, Error> {
        self.framework_intermediary_proxy
            .contains(&moniker.to_string())
            .await
            .map_err(Error::FidlError)
    }

    /// Returns a copy of a component decl in the realm.
    pub async fn get_decl(&mut self, moniker: &Moniker) -> Result<cm_rust::ComponentDecl, Error> {
        self.flush_routes().await?;
        let decl = self
            .framework_intermediary_proxy
            .get_component_decl(&moniker.to_string())
            .await?
            .map_err(|s| Error::FailedToGetDecl(moniker.clone(), s))?;
        Ok(decl.fidl_into_native())
    }

    /// Applies any routes that were added to the RealmBuilder that produced this Realm.
    async fn flush_routes(&mut self) -> Result<(), Error> {
        let routes: Vec<_> = self.routes_to_add.drain(..).collect();
        for route in routes {
            self.add_route(route).await?;
        }
        Ok(())
    }

    /// Marks the target component as eager.
    ///
    /// If the target component is a component that was added to this realm with
    /// [`Realm::add_component`], then the component is marked as eager in the Realm's
    /// internal structure. If the target component is a component referenced in an added
    /// component's [`cm_rust::ComponentDecl`], then the `ChildDecl` for the component is modified.
    pub async fn mark_as_eager(&self, moniker: &Moniker) -> Result<(), Error> {
        self.framework_intermediary_proxy
            .mark_as_eager(&moniker.to_string())
            .await?
            .map_err(|s| Error::FailedToMarkAsEager(moniker.clone(), s))
    }

    /// Sets the name of the collection that this realm will be created in
    pub fn set_collection_name(&mut self, collection_name: impl Into<String>) {
        self.collection_name = collection_name.into();
    }

    pub async fn add_route(&mut self, route: builder::CapabilityRoute) -> Result<(), Error> {
        if let builder::Capability::Event(_, _) = &route.capability {
            return builder::RealmBuilder::add_event_route(self, route).await;
        }

        let capability = match route.capability {
            builder::Capability::Protocol(name) => {
                frealmbuilder::Capability::Protocol(frealmbuilder::ProtocolCapability {
                    name: Some(name),
                    ..frealmbuilder::ProtocolCapability::EMPTY
                })
            }
            builder::Capability::Directory(name, path, rights) => {
                frealmbuilder::Capability::Directory(frealmbuilder::DirectoryCapability {
                    name: Some(name),
                    path: Some(path),
                    rights: Some(rights),
                    ..frealmbuilder::DirectoryCapability::EMPTY
                })
            }
            builder::Capability::Storage(name, path) => {
                frealmbuilder::Capability::Storage(frealmbuilder::StorageCapability {
                    name: Some(name),
                    path: Some(path),
                    ..frealmbuilder::StorageCapability::EMPTY
                })
            }
            builder::Capability::Event(_, _) => unreachable!(),
        };

        let source = route.source.to_frealmbuilder();
        let targets =
            route.targets.into_iter().map(builder::RouteEndpoint::to_frealmbuilder).collect();
        let route = frealmbuilder::CapabilityRoute {
            capability: Some(capability),
            source: Some(source),
            targets: Some(targets),
            ..frealmbuilder::CapabilityRoute::EMPTY
        };
        self.framework_intermediary_proxy
            .route_capability(route)
            .await?
            .map_err(|s| Error::FailedToRoute(s))
    }

    /// Initializes the realm, but doesn't create it. Returns the root URL, the collection name,
    /// and the mocks runner. The caller should pass the URL and collection name into
    /// `fuchsial.sys2.Realm#CreateChild`, and keep the mocks runner alive until after
    /// `fuchsia.sys2.Realm#DestroyChild` has been called.
    pub async fn initialize(mut self) -> Result<(String, String, mock::MocksRunner), Error> {
        self.flush_routes().await?;
        let root_url = self
            .framework_intermediary_proxy
            .commit()
            .await?
            .map_err(|s| Error::FailedToCommit(s))?;
        Ok((root_url, self.collection_name, self.mocks_runner))
    }

    /// Creates this realm in a child component collection, using an autogenerated name for the
    /// instance. By default this happens in the [`DEFAULT_COLLECTION_NAME`] collection.
    pub async fn create(self) -> Result<RealmInstance, Error> {
        let (root_url, collection_name, mocks_runner) = self.initialize().await?;
        let root = ScopedInstance::new(collection_name, root_url)
            .await
            .map_err(RealmError::FailedToCreateChild)?;
        Ok(RealmInstance { root, mocks_runner })
    }

    /// Creates this realm in a child component collection. By default this happens in the
    /// [`DEFAULT_COLLECTION_NAME`] collection.
    pub async fn create_with_name(self, child_name: String) -> Result<RealmInstance, Error> {
        let (root_url, collection_name, mocks_runner) = self.initialize().await?;
        let root = ScopedInstance::new_with_name(child_name, collection_name, root_url)
            .await
            .map_err(RealmError::FailedToCreateChild)?;
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
    pub async fn create_in_nested_component_manager(
        self,
        component_manager_relative_url: &str,
    ) -> Result<RealmInstance, Error> {
        let (root_url, collection_name, mocks_runner) = self.initialize().await?;

        // We now have a root URL we could create in a collection, but instead we want to launch a
        // component manager and give that component manager this root URL. That flag is set with
        // command line arguments, so we can't just launch an unmodified component manager.
        //
        // Open a new connection to the framework intermediary to begin creating a new realm. This
        // new realm will hold a single component: component manager. We will modify its manifest
        // such that the root component URL is set to the root_url we just obtained, and the nested
        // component manager will then fetch the manifest from realm builder itself.
        //
        // Note this presumes that component manager is pointed to a config with following line:
        //
        //     realm_builder_resolver_and_runner: "namespace",

        let mut component_manager_realm = Self::new().await?;
        component_manager_realm
            .set_component_url(&Moniker::root(), component_manager_relative_url.to_string())
            .await?;
        let mut component_manager_decl = component_manager_realm.get_decl(&Moniker::root()).await?;
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
        component_manager_realm.set_component(&Moniker::root(), component_manager_decl).await?;

        for protocol_name in
            vec!["fuchsia.sys2.ComponentResolver", "fuchsia.component.runner.ComponentRunner"]
        {
            component_manager_realm
                .add_route(builder::CapabilityRoute {
                    capability: builder::Capability::protocol(protocol_name),
                    source: builder::RouteEndpoint::above_root(),
                    targets: vec![builder::RouteEndpoint::component("")],
                })
                .await?;
        }
        component_manager_realm
            .add_route(builder::CapabilityRoute {
                capability: builder::Capability::directory("hub", "/hub", fio2::RW_STAR_DIR),
                source: builder::RouteEndpoint::component(""),
                targets: vec![builder::RouteEndpoint::above_root()],
            })
            .await?;

        let (component_manager_url, _, _) = component_manager_realm.initialize().await?;
        let root = ScopedInstance::new(collection_name, component_manager_url)
            .await
            .map_err(RealmError::FailedToCreateChild)?;
        Ok(RealmInstance { root, mocks_runner })
    }
}

/// Manages the creation of new components within a collection.
pub struct ScopedInstanceFactory {
    realm_proxy: Option<fsys::RealmProxy>,
    collection_name: String,
}

impl ScopedInstanceFactory {
    /// Creates a new factory that creates components in the specified collection.
    pub fn new(collection_name: impl Into<String>) -> Self {
        ScopedInstanceFactory { realm_proxy: None, collection_name: collection_name.into() }
    }

    /// Use `realm_proxy` instead of the fuchsia.sys2.Realm protocol in this component's
    /// incoming namespace. This can be used to start component's in a collection belonging
    /// to another component.
    pub fn with_realm_proxy(mut self, realm_proxy: fsys::RealmProxy) -> Self {
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
        let child_name = format!("auto-{}", id);
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
        let mut collection_ref = fsys::CollectionRef { name: self.collection_name.clone() };
        let child_decl = fsys::ChildDecl {
            name: Some(child_name.clone()),
            url: Some(url.into()),
            startup: Some(fsys::StartupMode::Lazy),
            ..fsys::ChildDecl::EMPTY
        };
        let child_args =
            fsys::CreateChildArgs { numbered_handles: None, ..fsys::CreateChildArgs::EMPTY };
        let () = realm
            .create_child(&mut collection_ref, child_decl, child_args)
            .await
            .context("CreateChild FIDL failed.")?
            .map_err(|e| format_err!("Failed to create child: {:?}", e))?;
        let mut child_ref = fsys::ChildRef {
            name: child_name.clone(),
            collection: Some(self.collection_name.clone()),
        };
        let (exposed_dir, server) = endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>()
            .context("Failed to create directory proxy")?;
        let () = realm
            .bind_child(&mut child_ref, server)
            .await
            .context("BindChild FIDL failed.")?
            .map_err(|e| format_err!("Failed to bind to child: {:?}", e))?;
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
    realm: fsys::RealmProxy,
    child_name: String,
    collection: String,
    exposed_dir: fio::DirectoryProxy,
    destroy_channel: Option<
        futures::channel::oneshot::Sender<
            Result<
                fidl::client::QueryResponseFut<fidl_fuchsia_sys2::RealmDestroyChildResult>,
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
            fsys::ChildRef { name: child_name.clone(), collection: Some(collection.clone()) };
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
        crate::builder::*,
        futures::{channel::oneshot, future::pending, lock::Mutex, select},
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
    async fn realm_destroy() {
        let (component_1_sends_on_drop, mut component_1_drop_receiver) = SendsOnDrop::new();
        let (component_2_sends_on_drop, mut component_2_drop_receiver) = SendsOnDrop::new();
        let component_1_sends_on_drop = Arc::new(Mutex::new(Some(component_1_sends_on_drop)));
        let component_2_sends_on_drop = Arc::new(Mutex::new(Some(component_2_sends_on_drop)));

        let (component_1_start_sender, component_1_start_receiver) = oneshot::channel();
        let (component_2_start_sender, component_2_start_receiver) = oneshot::channel();
        let component_1_start_sender = Arc::new(Mutex::new(Some(component_1_start_sender)));
        let component_2_start_sender = Arc::new(Mutex::new(Some(component_2_start_sender)));

        let mut builder = RealmBuilder::new().await.unwrap();
        builder
            .add_eager_component(
                "component_1",
                ComponentSource::mock(move |_mh: mock::MockHandles| {
                    let component_1_start_sender = component_1_start_sender.clone();
                    let component_1_sends_on_drop = component_1_sends_on_drop.clone();
                    Box::pin(async move {
                        component_1_start_sender.lock().await.take().unwrap().send(()).unwrap();
                        let _sends_on_drop = component_1_sends_on_drop.lock().await.take().unwrap();
                        let () = pending().await;
                        Ok(())
                    })
                }),
            )
            .await
            .expect("failed to add component_1")
            .add_eager_component(
                "component_2",
                ComponentSource::mock(move |_mh: mock::MockHandles| {
                    let component_2_start_sender = component_2_start_sender.clone();
                    let component_2_sends_on_drop = component_2_sends_on_drop.clone();
                    Box::pin(async move {
                        component_2_start_sender.lock().await.take().unwrap().send(()).unwrap();
                        let _sends_on_drop = component_2_sends_on_drop.lock().await.take().unwrap();
                        let () = pending().await;
                        Ok(())
                    })
                }),
            )
            .await
            .expect("failed to add component_2");

        let realm_instance = builder.build().create().await.expect("failed to create the realm");

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
        select! {
            res = component_1_drop_receiver => res.expect("failed to receive stop notification"),
            default => panic!("component_1 should have stopped by now"),
        }
        select! {
            res = component_2_drop_receiver => res.expect("failed to receive stop notification"),
            default => panic!("component_2 should have stopped by now"),
        }
    }
}
