// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{directory_broker, model::testing::mocks::*, model::*},
    cm_rust::{
        Capability, CapabilityPath, ChildDecl, ComponentDecl, ExposeDecl, ExposeSource, OfferDecl,
        OfferSource, UseDecl,
    },
    fidl::endpoints::{self, ClientEnd, ServerEnd},
    fidl_fidl_examples_echo::{self as echo, EchoMarker, EchoRequest, EchoRequestStream},
    fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, NodeMarker, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_DIRECTORY,
        MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::{
        directory, directory::entry::DirectoryEntry, file::simple::read_only, pseudo_directory,
    },
    fuchsia_zircon as zx,
    fuchsia_zircon::HandleBased,
    futures::lock::Mutex,
    futures::TryStreamExt,
    std::{
        collections::{HashMap, HashSet},
        convert::{TryFrom, TryInto},
        ffi::CString,
        iter,
        path::PathBuf,
        ptr,
        sync::Arc,
    },
};

/// Return all monikers of the children of the given `realm`.
pub async fn get_children(realm: &Realm) -> HashSet<ChildMoniker> {
    await!(realm.state.lock()).child_realms.as_ref().unwrap().keys().cloned().collect()
}

/// Return the child realm of the given `realm` with moniker `child`.
pub async fn get_child_realm<'a>(realm: &'a Realm, child: &'a str) -> Arc<Realm> {
    await!(realm.state.lock()).child_realms.as_ref().unwrap()[&child.into()].clone()
}

/// Construct a capability for the hippo service.
pub fn default_service_capability() -> Capability {
    Capability::Service(CapabilityPath::try_from("/svc/hippo").unwrap())
}

pub fn new_service_capability(path: &str) -> Capability {
    Capability::Service(CapabilityPath::try_from(path).unwrap())
}

/// Construct a capability for the hippo directory.
pub fn default_directory_capability() -> Capability {
    Capability::Directory(CapabilityPath::try_from("/data/hippo").unwrap())
}

pub fn new_directory_capability(path: &str) -> Capability {
    Capability::Directory(CapabilityPath::try_from(path).unwrap())
}

/// Returns an empty component decl for an executable component.
pub fn default_component_decl() -> ComponentDecl {
    ComponentDecl {
        program: Some(fdata::Dictionary { entries: vec![] }),
        uses: Vec::new(),
        exposes: Vec::new(),
        offers: Vec::new(),
        children: Vec::new(),
        collections: Vec::new(),
        facets: None,
        storage: Vec::new(),
    }
}

/// A test for capability routing.
///
/// All string arguments are referring to component names, not URLs, ex: "a", not "test:///a" or
/// "test:///a_resolved".
pub struct RoutingTest {
    components: Vec<(&'static str, ComponentDecl)>,
    model: Model,
    namespaces: Namespaces,
}

impl RoutingTest {
    /// Initializes a new test.
    pub fn new(
        root_component: &'static str,
        components: Vec<(&'static str, ComponentDecl)>,
        ambient: Box<dyn AmbientEnvironment>,
    ) -> Self {
        let mut resolver = ResolverRegistry::new();
        let mut runner = MockRunner::new();

        let mut mock_resolver = MockResolver::new();
        for (name, decl) in &components {
            Self::host_capabilities(name, decl.clone(), &mut runner);
            mock_resolver.add_component(name, decl.clone());
        }
        resolver.register("test".to_string(), Box::new(mock_resolver));

        let namespaces = runner.namespaces.clone();
        let model = Model::new(ModelParams {
            ambient,
            root_component_url: format!("test:///{}", root_component),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
            hooks: Vec::new(),
        });
        Self { components, model, namespaces }
    }

    /// Installs a new directory at /hippo in our namespace. Does nothing if this directory already
    /// exists.
    pub fn install_hippo_dir(&self) {
        let (client_chan, server_chan) = zx::Channel::create().unwrap();

        let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
        let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
        if status != zx::sys::ZX_OK {
            panic!(
                "bad status returned for fdio_ns_get_installed: {}",
                zx::Status::from_raw(status)
            );
        }
        let cstr = CString::new("/hippo").unwrap();
        let status =
            unsafe { fdio::fdio_sys::fdio_ns_bind(ns_ptr, cstr.as_ptr(), client_chan.into_raw()) };
        if status != zx::sys::ZX_OK && status != zx::sys::ZX_ERR_ALREADY_EXISTS {
            panic!("bad status returned for fdio_ns_bind: {}", zx::Status::from_raw(status));
        }
        let mut out_dir = OutDir::new();
        out_dir.add_directory();
        out_dir.host_fn()(ServerEnd::new(server_chan));
    }

    /// Creates a dynamic child `child_decl` in `moniker`'s `collection`.
    pub async fn create_dynamic_child<'a>(
        &'a self,
        moniker: AbsoluteMoniker,
        collection: &'a str,
        decl: ChildDecl,
    ) {
        let component_name = await!(Self::bind_instance(&self.model, &moniker));
        let component_resolved_url = Self::resolved_url(&component_name);
        await!(Self::check_namespace(
            component_name,
            self.namespaces.clone(),
            self.components.clone()
        ));
        await!(capability_util::call_create_child(
            component_resolved_url,
            self.namespaces.clone(),
            collection,
            decl
        ));
    }

    /// Checks a `use` declaration at `moniker` by trying to use `capability`.
    pub async fn check_use(
        &self,
        moniker: AbsoluteMoniker,
        capability: Capability,
        should_succeed: bool,
    ) {
        let component_name = await!(Self::bind_instance(&self.model, &moniker));
        let component_resolved_url = Self::resolved_url(&component_name);
        await!(Self::check_namespace(
            component_name,
            self.namespaces.clone(),
            self.components.clone()
        ));
        match capability {
            Capability::Service(path) => {
                await!(capability_util::call_echo_svc_from_namespace(
                    path,
                    component_resolved_url,
                    self.namespaces.clone(),
                    should_succeed
                ));
            }
            Capability::Directory(path) => await!(capability_util::read_data_from_namespace(
                path,
                component_resolved_url,
                self.namespaces.clone(),
                should_succeed
            )),
            Capability::Storage(_) => panic!("storage capabilities are not supported"),
        }
    }

    /// Checks using a capability from a component's exposed directory.
    pub async fn check_use_exposed_dir(
        &self,
        moniker: AbsoluteMoniker,
        capability: Capability,
        should_succeed: bool,
    ) {
        match capability {
            Capability::Service(path) => {
                await!(capability_util::call_echo_svc_from_exposed_dir(
                    path,
                    &moniker,
                    &self.model,
                    should_succeed
                ));
            }
            Capability::Directory(path) => {
                await!(capability_util::read_data_from_exposed_dir(
                    path,
                    &moniker,
                    &self.model,
                    should_succeed
                ));
            }
            Capability::Storage(_) => panic!("storage capabilities are not supported"),
        }
    }

    /// check_namespace will ensure that the paths in `namespaces` for `component_name` match the use
    /// declarations for the the component by the same name in `components`.
    async fn check_namespace(
        component_name: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        components: Vec<(&str, ComponentDecl)>,
    ) {
        let (_, decl) = components
            .into_iter()
            .find(|(name, _)| name == &component_name)
            .expect("component not in component decl list");
        // Two services installed into the same dir will cause duplicates, so use a HashSet to remove
        // them.
        let expected_paths_hs: HashSet<String> = decl
            .uses
            .into_iter()
            .map(|u| match u {
                UseDecl::Directory(d) => d.target_path.to_string(),
                UseDecl::Service(s) => s.target_path.dirname,
                UseDecl::Storage(_) => panic!("storage capabilites are not supported"),
            })
            .collect();
        let mut expected_paths = vec![];
        expected_paths.extend(expected_paths_hs.into_iter());

        let namespaces = await!(namespaces.lock());
        let ns = namespaces
            .get(&format!("test:///{}_resolved", component_name))
            .expect("component not in namespace");
        let mut actual_paths = ns.paths.clone();

        expected_paths.sort_unstable();
        actual_paths.sort_unstable();
        assert_eq!(expected_paths, actual_paths);
    }

    /// Checks a `use /svc/fuchsia.sys2.Realm` declaration at `moniker` by calling
    /// `BindChild`.
    pub async fn check_use_realm(
        &self,
        moniker: AbsoluteMoniker,
        bind_calls: Arc<Mutex<Vec<String>>>,
    ) {
        let component_name = await!(Self::bind_instance(&self.model, &moniker));
        let component_resolved_url = Self::resolved_url(&component_name);
        let path = "/svc/fuchsia.sys2.Realm".try_into().unwrap();
        await!(Self::check_namespace(
            component_name,
            self.namespaces.clone(),
            self.components.clone()
        ));
        await!(capability_util::call_realm_svc(
            path,
            component_resolved_url,
            self.namespaces.clone(),
            bind_calls.clone(),
        ));
    }

    /// Host all capabilities in `decl` that come from `self`.
    fn host_capabilities(name: &str, decl: ComponentDecl, runner: &mut MockRunner) {
        // if this decl is offering/exposing something from `Self`, let's host it
        let mut out_dir = None;
        for expose in decl.exposes.iter() {
            let source = match expose {
                ExposeDecl::Service(s) => &s.source,
                ExposeDecl::Directory(d) => &d.source,
            };
            if *source == ExposeSource::Self_ {
                Self::host_capability(&mut out_dir, &expose.clone().into());
            }
        }
        for offer in decl.offers.iter() {
            let source = match offer {
                OfferDecl::Service(s) => &s.source,
                OfferDecl::Directory(d) => &d.source,
                OfferDecl::Storage(_) => panic!("storage capabilities are not supported"),
            };
            if *source == OfferSource::Self_ {
                Self::host_capability(&mut out_dir, &offer.clone().into());
            }
        }
        if let Some(out_dir) = out_dir {
            runner.host_fns.insert(format!("test:///{}_resolved", name), out_dir.host_fn());
        }
    }

    fn host_capability(out_dir: &mut Option<OutDir>, capability: &Capability) {
        match capability {
            Capability::Service(_) => out_dir.get_or_insert(OutDir::new()).add_service(),
            Capability::Directory(_) => out_dir.get_or_insert(OutDir::new()).add_directory(),
            Capability::Storage(_) => panic!("storage capabilities are not supported"),
        }
    }

    async fn bind_instance<'a>(model: &'a Model, moniker: &'a AbsoluteMoniker) -> String {
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(
            format!("{:?}", await!(model.look_up_and_bind_instance(moniker.clone()))),
            format!("{:?}", expected_res),
        );
        moniker.path().last().expect("didn't expect a root component").name().to_string()
    }

    fn resolved_url(component_name: &str) -> String {
        format!("test:///{}_resolved", component_name)
    }
}

/// Contains functions to use capabilities in routing tests.
mod capability_util {
    use super::*;
    use cm_rust::NativeIntoFidl;

    /// Looks up `resolved_url` in the namespace, and attempts to read ${path}/hippo. The file
    /// should contain the string "hippo".
    pub async fn read_data_from_namespace(
        path: CapabilityPath,
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        should_succeed: bool,
    ) {
        let path = path.to_string();
        let dir_proxy = await!(get_dir_from_namespace(&path, resolved_url, namespaces));
        let file = PathBuf::from("hippo");
        let file_proxy = io_util::open_file(&dir_proxy, &file, OPEN_RIGHT_READABLE)
            .expect("failed to open file");
        let res = await!(io_util::read_file(&file_proxy));

        match should_succeed {
            true => assert_eq!("hippo", res.expect("failed to read file")),
            false => {
                let err = res.expect_err("read file successfully when it should fail");
                assert_eq!(format!("{:?}", err), "ClientRead(Status(PEER_CLOSED))");
            }
        }
    }

    /// Looks up `resolved_url` in the namespace, and attempts to use `path`. Expects the service
    /// to be fidl.examples.echo.Echo.
    pub async fn call_echo_svc_from_namespace(
        path: CapabilityPath,
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        should_succeed: bool,
    ) {
        let dir_proxy = await!(get_dir_from_namespace(&path.dirname, resolved_url, namespaces));
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &PathBuf::from(path.basename),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )
        .expect("failed to open echo service");
        let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = await!(echo_proxy.echo_string(Some("hippos")));

        match should_succeed {
            true => {
                assert_eq!(res.expect("failed to use echo service"), Some("hippos".to_string()))
            }
            false => {
                let err = res.expect_err("used echo service successfully when it should fail");
                if let fidl::Error::ClientRead(status) = err {
                    assert_eq!(status, zx::Status::PEER_CLOSED);
                } else {
                    panic!("unexpected error value: {}", err);
                }
            }
        }
    }

    /// Attempts to read ${path}/hippo in `abs_moniker`'s exposed directory. The file should
    /// contain the string "hippo".
    pub async fn read_data_from_exposed_dir<'a>(
        path: CapabilityPath,
        abs_moniker: &'a AbsoluteMoniker,
        model: &'a Model,
        should_succeed: bool,
    ) {
        let (node_proxy, server_end) = endpoints::create_proxy::<NodeMarker>().unwrap();
        await!(open_exposed_dir(&path, abs_moniker, model, MODE_TYPE_DIRECTORY, server_end));
        let dir_proxy = DirectoryProxy::new(node_proxy.into_channel().unwrap());
        let file = PathBuf::from("hippo");
        let file_proxy = io_util::open_file(&dir_proxy, &file, OPEN_RIGHT_READABLE)
            .expect("failed to open file");
        let res = await!(io_util::read_file(&file_proxy));

        match should_succeed {
            true => assert_eq!("hippo", res.expect("failed to read file")),
            false => {
                let err = res.expect_err("read file successfully when it should fail");
                assert_eq!(format!("{:?}", err), "ClientRead(Status(PEER_CLOSED))");
            }
        }
    }

    /// Attempts to use the fidl.examples.echo.Echo service at `path` in `abs_moniker`'s exposed
    /// directory.
    pub async fn call_echo_svc_from_exposed_dir<'a>(
        path: CapabilityPath,
        abs_moniker: &'a AbsoluteMoniker,
        model: &'a Model,
        should_succeed: bool,
    ) {
        let (node_proxy, server_end) = endpoints::create_proxy::<NodeMarker>().unwrap();
        await!(open_exposed_dir(&path, abs_moniker, model, MODE_TYPE_SERVICE, server_end));
        let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = await!(echo_proxy.echo_string(Some("hippos")));
        match should_succeed {
            true => {
                assert_eq!(res.expect("failed to use echo service"), Some("hippos".to_string()))
            }
            false => {
                let err = res.expect_err("used echo service successfully when it should fail");
                if let fidl::Error::ClientRead(status) = err {
                    assert_eq!(status, zx::Status::PEER_CLOSED);
                } else {
                    panic!("unexpected error value: {}", err);
                }
            }
        }
    }

    /// Looks up `resolved_url` in the namespace, and attempts to use `path`. Expects the service
    /// to be fuchsia.sys2.Realm.
    pub async fn call_realm_svc(
        path: CapabilityPath,
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        bind_calls: Arc<Mutex<Vec<String>>>,
    ) {
        let dir_proxy =
            await!(get_dir_from_namespace(&path.dirname, resolved_url.clone(), namespaces));
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &PathBuf::from(path.basename),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )
        .expect("failed to open realm service");
        let realm_proxy = fsys::RealmProxy::new(node_proxy.into_channel().unwrap());
        let child_ref = fsys::ChildRef { name: Some("my_child".to_string()), collection: None };
        let (_client_chan, server_chan) = zx::Channel::create().unwrap();
        let exposed_capabilities = ServerEnd::new(server_chan);
        let res = await!(realm_proxy.bind_child(child_ref, exposed_capabilities));

        // Check for side effects: ambient environment should have received the `bind_child`
        // call.
        let _ = res.expect("failed to use realm service");
        let bind_url =
            format!("test:///{}_resolved", await!(bind_calls.lock()).last().expect("no bind call"));
        assert_eq!(bind_url, resolved_url);
    }

    /// Call `fuchsia.sys2.Realm.CreateChild` to create a dynamic child.
    pub async fn call_create_child<'a>(
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        collection: &'a str,
        child_decl: ChildDecl,
    ) {
        let path = CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").expect("no realm service");
        let dir_proxy =
            await!(get_dir_from_namespace(&path.dirname, resolved_url.clone(), namespaces));
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &PathBuf::from(path.basename),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )
        .expect("failed to open realm service");
        let realm_proxy = fsys::RealmProxy::new(node_proxy.into_channel().unwrap());
        let collection_ref = fsys::CollectionRef { name: Some(collection.to_string()) };
        let child_decl = child_decl.native_into_fidl();
        let res = await!(realm_proxy.create_child(collection_ref, child_decl));
        let _ = res.expect("failed to create child");
    }

    /// Returns a cloned DirectoryProxy to the dir `dir_string` inside the namespace of
    /// `resolved_url`.
    async fn get_dir_from_namespace(
        dir_string: &str,
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    ) -> DirectoryProxy {
        let mut ns_guard = await!(namespaces.lock());
        let ns = ns_guard.get_mut(&resolved_url).unwrap();

        // Find the index of our directory in the namespace, and remove the directory and path. The
        // path is removed so that the paths/dirs aren't shuffled in the namespace.
        let index = ns.paths.iter().position(|path| path == dir_string).expect("didn't find dir");
        let directory = ns.directories.remove(index);
        let path = ns.paths.remove(index);

        // Clone our directory, and then put the directory and path back on the end of the namespace so
        // that the namespace is (somewhat) unmodified.
        let dir_proxy = directory.into_proxy().unwrap();
        let dir_proxy_clone = io_util::clone_directory(&dir_proxy, CLONE_FLAG_SAME_RIGHTS).unwrap();
        ns.directories.push(ClientEnd::new(dir_proxy.into_channel().unwrap().into_zx_channel()));
        ns.paths.push(path);

        dir_proxy_clone
    }

    /// Open the exposed dir for `abs_moniker`.
    async fn open_exposed_dir<'a>(
        path: &'a CapabilityPath,
        abs_moniker: &'a AbsoluteMoniker,
        model: &'a Model,
        open_mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let realm = await!(model.look_up_realm(abs_moniker))
            .expect(&format!("realm not found {}", abs_moniker));
        await!(model.bind_instance(realm.clone())).expect("failed to bind instance");
        let state = await!(realm.state.lock());
        let execution = state.execution.as_ref().expect("no execution");
        let flags = OPEN_RIGHT_READABLE;
        await!(execution.exposed_dir.root_dir.open(flags, open_mode, path.split(), server_end))
            .expect("failed to open exposed dir");
    }
}

/// OutDir can be used to construct and then host an out directory, containing a directory
/// structure with 0 or 1 read-only files, and 0 or 1 services.
pub struct OutDir {
    // TODO: it would be great if this struct held a `directory::simple::Simple` that was mutated
    // by the `add_*` functions, but this is not possible because `directory::simple::Simple`
    // doesn't have `Send` and `Sync` on its internal fields, which is needed for the returned
    // function by `host_fn`. This logic should be updated to directly work on a directory once a
    // multithreaded rust vfs is implemented.
    host_service: bool,
    host_directory: bool,
}

impl OutDir {
    pub fn new() -> OutDir {
        OutDir { host_service: false, host_directory: false }
    }
    /// Adds `svc/foo` to the out directory, which implements `fidl.examples.echo.Echo`.
    pub fn add_service(&mut self) {
        self.host_service = true;
    }
    /// Adds `data/foo/hippo` to the out directory, which contains the string `hippo`
    pub fn add_directory(&mut self) {
        self.host_directory = true;
    }
    /// Returns a function that will host this outgoing directory on the given ServerEnd.
    pub fn host_fn(&self) -> Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        let host_service = self.host_service;
        let host_directory = self.host_directory;
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            fasync::spawn(async move {
                let mut pseudo_dir = directory::simple::empty();
                if host_service {
                    pseudo_dir
                        .add_entry(
                            "svc",
                            pseudo_directory! {
                                "foo" =>
                                    directory_broker::DirectoryBroker::new(Box::new(
                                            Self::echo_server_fn)),
                            },
                        )
                        .map_err(|(s, _)| s)
                        .expect("failed to add svc entry");
                }
                if host_directory {
                    pseudo_dir
                        .add_entry(
                            "data",
                            pseudo_directory! {
                                "foo" => pseudo_directory! {
                                    "hippo" => read_only(|| Ok(b"hippo".to_vec())),
                                },
                            },
                        )
                        .map_err(|(s, _)| s)
                        .expect("failed to add data entry");
                }
                pseudo_dir.open(
                    OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    MODE_TYPE_DIRECTORY,
                    &mut iter::empty(),
                    ServerEnd::new(server_end.into_channel()),
                );

                let _ = await!(pseudo_dir);

                panic!("the pseudo dir exited!");
            });
        })
    }

    /// Hosts a new service on server_end that implements fidl.examples.echo.Echo
    fn echo_server_fn(
        _flags: u32,
        _mode: u32,
        _relative_path: String,
        server_end: ServerEnd<NodeMarker>,
    ) {
        fasync::spawn(async move {
            let server_end: ServerEnd<EchoMarker> = ServerEnd::new(server_end.into_channel());
            let mut stream: EchoRequestStream = server_end.into_stream().unwrap();
            while let Some(EchoRequest::EchoString { value, responder }) =
                await!(stream.try_next()).unwrap()
            {
                responder.send(value.as_ref().map(|s| &**s)).unwrap();
            }
        });
    }
}
