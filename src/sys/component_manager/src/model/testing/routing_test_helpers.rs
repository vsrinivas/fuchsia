// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        directory_broker, klog, model::routing::generate_storage_path,
        model::testing::memfs::Memfs, model::testing::mocks::*, model::*,
    },
    cm_rust::{
        CapabilityPath, ChildDecl, ComponentDecl, ExposeDecl, ExposeSource, OfferDecl,
        OfferDirectorySource, OfferServiceSource, StorageDirectorySource, UseDecl, UseStorageDecl,
    },
    fidl::endpoints::{self, create_proxy, ClientEnd, ServerEnd},
    fidl_fidl_examples_echo::{self as echo, EchoMarker, EchoRequest, EchoRequestStream},
    fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileEvent, FileMarker, FileObject, FileProxy, NodeInfo,
        NodeMarker, CLONE_FLAG_SAME_RIGHTS, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, MODE_TYPE_SERVICE,
        OPEN_FLAG_CREATE, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
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
        convert::TryInto,
        ffi::CString,
        iter,
        path::Path,
        ptr,
        sync::Arc,
    },
};

/// Return the child of the given realm.
pub async fn get_child<'a>(realm: &'a Realm, moniker: &'a str) -> Arc<Realm> {
    let state = realm.state.lock().await;
    state.get_child_realms()[&moniker.into()].clone()
}

/// Return all monikers of the children of the given `realm`.
pub async fn get_children(realm: &Realm) -> HashSet<ChildMoniker> {
    realm.state.lock().await.get_child_realms().keys().cloned().collect()
}

/// Return the child realm of the given `realm` with moniker `child`.
pub async fn get_child_realm<'a>(realm: &'a Realm, child: &'a str) -> Arc<Realm> {
    realm.state.lock().await.get_child_realms()[&child.into()].clone()
}

/// Construct a capability path for the hippo service.
pub fn default_service_capability() -> CapabilityPath {
    "/svc/hippo".try_into().unwrap()
}

/// Construct a capability path for the hippo directory.
pub fn default_directory_capability() -> CapabilityPath {
    "/data/hippo".try_into().unwrap()
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

pub enum CheckUse {
    Service {
        path: CapabilityPath,
        should_succeed: bool,
    },
    LegacyService {
        path: CapabilityPath,
        should_succeed: bool,
    },
    Directory {
        path: CapabilityPath,
        should_succeed: bool,
    },
    Storage {
        type_: fsys::StorageType,
        // The relative moniker from the storage declaration to the use declaration. If None, this
        // storage use is expected to fail.
        storage_relation: Option<RelativeMoniker>,
    },
}

/// A test for capability routing.
///
/// All string arguments are referring to component names, not URLs, ex: "a", not "test:///a" or
/// "test:///a_resolved".
pub struct RoutingTest {
    components: Vec<(&'static str, ComponentDecl)>,
    model: Model,
    namespaces: Namespaces,
    memfs: Memfs,
}

impl RoutingTest {
    /// Initializes a new test.
    pub fn new(
        root_component: &'static str,
        components: Vec<(&'static str, ComponentDecl)>,
        framework_services: Box<dyn FrameworkServiceHost>,
    ) -> Self {
        // Ensure that kernel logging has been set up
        let _ = klog::KernelLogger::init();

        let mut resolver = ResolverRegistry::new();
        let mut runner = MockRunner::new();

        let memfs = Memfs::new();

        let mut mock_resolver = MockResolver::new();
        for (name, decl) in &components {
            Self::host_capabilities(name, decl.clone(), &mut runner, &memfs);
            mock_resolver.add_component(name, decl.clone());
        }
        resolver.register("test".to_string(), Box::new(mock_resolver));

        let namespaces = runner.namespaces.clone();
        let model = Model::new(ModelParams {
            framework_services: framework_services.into(),
            root_component_url: format!("test:///{}", root_component),
            root_resolver_registry: resolver,
            root_default_runner: Arc::new(runner),
            hooks: Vec::new(),
            config: ModelConfig::default(),
        });
        Self { components, model, namespaces, memfs }
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
        out_dir.add_service();
        out_dir.add_directory(&self.memfs);
        out_dir.host_fn()(ServerEnd::new(server_chan));
    }

    /// Creates a dynamic child `child_decl` in `moniker`'s `collection`.
    pub async fn create_dynamic_child<'a>(
        &'a self,
        moniker: AbsoluteMoniker,
        collection: &'a str,
        decl: ChildDecl,
    ) {
        let component_name = Self::bind_instance(&self.model, &moniker).await;
        let component_resolved_url = Self::resolved_url(&component_name);
        Self::check_namespace(
            component_name,
            self.namespaces.clone(),
            self.components.clone(),
        ).await;
        capability_util::call_create_child(
            component_resolved_url,
            self.namespaces.clone(),
            collection,
            decl,
        ).await;
    }

    /// Checks a `use` declaration at `moniker` by trying to use `capability`.
    pub async fn check_use(&self, moniker: AbsoluteMoniker, check: CheckUse) {
        let component_name = Self::bind_instance(&self.model, &moniker).await;
        let component_resolved_url = Self::resolved_url(&component_name);
        Self::check_namespace(
            component_name,
            self.namespaces.clone(),
            self.components.clone(),
        ).await;
        match check {
            CheckUse::Service { .. } => panic!("service capability unsupported"),
            CheckUse::LegacyService { path, should_succeed } => {
                capability_util::call_echo_svc_from_namespace(
                    path,
                    component_resolved_url,
                    self.namespaces.clone(),
                    should_succeed,
                ).await;
            }
            CheckUse::Directory { path, should_succeed } => {
                capability_util::read_data_from_namespace(
                    path,
                    component_resolved_url,
                    self.namespaces.clone(),
                    should_succeed,
                ).await
            }
            CheckUse::Storage { type_: fsys::StorageType::Meta, storage_relation } => {
                capability_util::write_file_to_meta_storage(
                    &self.model,
                    moniker,
                    storage_relation.is_some(),
                ).await;
                if let Some(relative_moniker) = storage_relation {
                    capability_util::check_file_in_storage(
                        fsys::StorageType::Meta,
                        relative_moniker,
                        &self.memfs,
                    ).await;
                }
            }
            CheckUse::Storage { type_, storage_relation } => {
                capability_util::write_file_to_storage(
                    "/storage".try_into().unwrap(),
                    component_resolved_url,
                    self.namespaces.clone(),
                    storage_relation.is_some(),
                ).await;
                if let Some(relative_moniker) = storage_relation {
                    capability_util::check_file_in_storage(
                        type_,
                        relative_moniker,
                        &self.memfs,
                    ).await;
                }
            }
        }
    }

    /// Checks using a capability from a component's exposed directory.
    pub async fn check_use_exposed_dir(&self, moniker: AbsoluteMoniker, check: CheckUse) {
        match check {
            CheckUse::Service { .. } => panic!("service capability unsupported"),
            CheckUse::LegacyService { path, should_succeed } => {
                capability_util::call_echo_svc_from_exposed_dir(
                    path,
                    &moniker,
                    &self.model,
                    should_succeed,
                ).await;
            }
            CheckUse::Directory { path, should_succeed } => {
                capability_util::read_data_from_exposed_dir(
                    path,
                    &moniker,
                    &self.model,
                    should_succeed,
                ).await;
            }
            CheckUse::Storage { .. } => {
                panic!("storage capabilities can't be exposed");
            }
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
            .filter_map(|u| match u {
                UseDecl::Directory(d) => Some(d.target_path.to_string()),
                UseDecl::Service(s) => Some(s.target_path.to_string()),
                UseDecl::LegacyService(s) => Some(s.target_path.dirname),
                UseDecl::Storage(UseStorageDecl::Data(p)) => Some(p.to_string()),
                UseDecl::Storage(UseStorageDecl::Cache(p)) => Some(p.to_string()),
                UseDecl::Storage(UseStorageDecl::Meta) => None,
            })
            .collect();
        let mut expected_paths = vec![];
        expected_paths.extend(expected_paths_hs.into_iter());

        let namespaces = namespaces.lock().await;
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
        let component_name = Self::bind_instance(&self.model, &moniker).await;
        let component_resolved_url = Self::resolved_url(&component_name);
        let path = "/svc/fuchsia.sys2.Realm".try_into().unwrap();
        Self::check_namespace(
            component_name,
            self.namespaces.clone(),
            self.components.clone(),
        ).await;
        capability_util::call_realm_svc(
            path,
            component_resolved_url,
            self.namespaces.clone(),
            bind_calls.clone(),
        ).await;
    }

    /// Checks that a use declaration of `path` at `moniker` can be opened with
    /// Fuchsia file operations.
    pub async fn check_open_file(&self, moniker: AbsoluteMoniker, path: CapabilityPath) {
        let component_name = Self::bind_instance(&self.model, &moniker).await;
        let component_resolved_url = Self::resolved_url(&component_name);
        Self::check_namespace(
            component_name,
            self.namespaces.clone(),
            self.components.clone(),
        ).await;
        capability_util::call_file_svc_from_namespace(
            path,
            component_resolved_url,
            self.namespaces.clone(),
        ).await;
    }

    /// Host all capabilities in `decl` that come from `self`.
    fn host_capabilities(name: &str, decl: ComponentDecl, runner: &mut MockRunner, memfs: &Memfs) {
        // if this decl is offering/exposing something from `Self`, let's host it
        let mut out_dir = None;
        for expose in decl.exposes.iter() {
            match expose {
                ExposeDecl::Service(s) if s.source == ExposeSource::Self_ => {
                    panic!("service capability unsupported")
                }
                ExposeDecl::LegacyService(s) if s.source == ExposeSource::Self_ => {
                    out_dir.get_or_insert(OutDir::new()).add_service()
                }
                ExposeDecl::Directory(d) if d.source == ExposeSource::Self_ => {
                    out_dir.get_or_insert(OutDir::new()).add_directory(memfs)
                }
                _ => (),
            }
        }
        for offer in decl.offers.iter() {
            match offer {
                OfferDecl::Service(s) if s.source == OfferServiceSource::Self_ => {
                    panic!("service capability unsupported")
                }
                OfferDecl::LegacyService(s) if s.source == OfferServiceSource::Self_ => {
                    out_dir.get_or_insert(OutDir::new()).add_service()
                }
                OfferDecl::Directory(d) if d.source == OfferDirectorySource::Self_ => {
                    out_dir.get_or_insert(OutDir::new()).add_directory(memfs)
                }
                _ => (),
            }
        }
        for storage in decl.storage.iter() {
            // Storage capabilities can have a source of "self", so there are situations we want to
            // test where a storage capability is offered and used and there's no directory
            // capability in the manifest, so we must host the directory structure for this case in
            // addition to directory offers.
            if storage.source == StorageDirectorySource::Self_ {
                out_dir.get_or_insert(OutDir::new()).add_directory(memfs)
            }
        }
        if let Some(out_dir) = out_dir {
            runner.host_fns.insert(format!("test:///{}_resolved", name), out_dir.host_fn());
        }
    }

    async fn bind_instance<'a>(model: &'a Model, moniker: &'a AbsoluteMoniker) -> String {
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(
            format!("{:?}", model.look_up_and_bind_instance(moniker.clone()).await),
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
        let dir_proxy = get_dir_from_namespace(&path, resolved_url, namespaces).await;
        let file = Path::new("hippo");
        let file_proxy = io_util::open_file(&dir_proxy, &file, OPEN_RIGHT_READABLE)
            .expect("failed to open file");
        let res = io_util::read_file(&file_proxy).await;

        match should_succeed {
            true => assert_eq!("hippo", res.expect("failed to read file")),
            false => {
                let err = res.expect_err("read file successfully when it should fail");
                assert_eq!(format!("{:?}", err), "ClientRead(Status(PEER_CLOSED))");
            }
        }
    }

    pub async fn write_file_to_storage(
        path: CapabilityPath,
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        should_succeed: bool,
    ) {
        let dir_string = path.to_string();
        let dir_proxy =
            get_dir_from_namespace(dir_string.as_str(), resolved_url, namespaces).await;
        write_hippo_file_to_directory(&dir_proxy, should_succeed).await;
    }

    pub async fn write_file_to_meta_storage(
        model: &Model,
        moniker: AbsoluteMoniker,
        should_succeed: bool,
    ) {
        let realm = model.look_up_realm(&moniker).await.expect("failed to look up realm");
        let meta_dir_res = realm.resolve_meta_dir(model).await;
        match (meta_dir_res, should_succeed) {
            (Ok(Some(meta_dir)), true) => write_hippo_file_to_directory(&meta_dir, true).await,
            (Err(ModelError::CapabilityDiscoveryError { .. }), false) => (),
            (Ok(Some(_)), false) => panic!("meta dir present when usage was expected to fail"),
            (Ok(None), true) => panic!("meta dir missing when usage was expected to succeed"),
            (Ok(None), false) => panic!("meta dir missing when resolution should return an error"),
            (Err(_), true) => panic!("meta dir resolution failed usage was expected to succeed"),
            (Err(_), false) => panic!("unexpected error when attempting meta dir resolution"),
        }
    }

    async fn write_hippo_file_to_directory(dir_proxy: &DirectoryProxy, should_succeed: bool) {
        let (file_proxy, server_end) = create_proxy::<FileMarker>().unwrap();
        let flags = OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE;
        dir_proxy
            .open(flags, MODE_TYPE_FILE, "hippos", ServerEnd::new(server_end.into_channel()))
            .expect("failed to open file on storage");
        let res = file_proxy.write(&mut b"hippos can be stored here".to_vec().drain(..)).await;
        match (res, should_succeed) {
            (Ok((s, _)),true) => assert_eq!(zx::Status::OK, zx::Status::from_raw(s)),
            (Err(_),false) => (),
            (Ok((s, _)),false) => panic!("we shouldn't be able to access storage, but we opened a file! failed write with status {}", zx::Status::from_raw(s)),
            (Err(e),true) => panic!("failed to write to file when we expected to be able to! {:?}", e),
        }
    }

    pub async fn check_file_in_storage(
        type_: fsys::StorageType,
        relation: RelativeMoniker,
        memfs: &Memfs,
    ) {
        let memfs_proxy = memfs.clone_root_handle();

        let mut dir_path = generate_storage_path(type_, relation);

        dir_path.push("hippos");

        let file_proxy = io_util::open_file(&memfs_proxy, &dir_path, io_util::OPEN_RIGHT_READABLE)
            .expect("failed to open file");
        let res = io_util::read_file(&file_proxy).await;
        assert_eq!("hippos can be stored here".to_string(), res.expect("failed to read file"));
    }

    /// Looks up `resolved_url` in the namespace, and attempts to use `path`. Expects the service
    /// to be fidl.examples.echo.Echo.
    pub async fn call_echo_svc_from_namespace(
        path: CapabilityPath,
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        should_succeed: bool,
    ) {
        let dir_proxy = get_dir_from_namespace(&path.dirname, resolved_url, namespaces).await;
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &Path::new(&path.basename),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )
        .expect("failed to open echo service");
        let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = echo_proxy.echo_string(Some("hippos")).await;

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

    /// Looks up `resolved_url` in the namespace, and attempts to use `path`.
    /// Expects the service to work like a fuchsia.io service, and respond with
    /// an OnOpen event when opened with OPEN_FLAG_DESCRIBE.
    pub async fn call_file_svc_from_namespace(
        path: CapabilityPath,
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    ) {
        let dir_proxy = get_dir_from_namespace(&path.dirname, resolved_url, namespaces).await;
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &Path::new(&path.basename),
            OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
            // This should be MODE_TYPE_SERVICE, but we implement the underlying
            // service as a file for convenience in testing.
            MODE_TYPE_FILE,
        )
        .expect("failed to open file service");

        let file_proxy = FileProxy::new(node_proxy.into_channel().unwrap());
        let mut event_stream = file_proxy.take_event_stream();
        let event = event_stream.try_next().await.unwrap();
        let FileEvent::OnOpen_ { s, info } = event.expect("failed to received file event");
        assert_eq!(s, zx::sys::ZX_OK);
        assert_eq!(
            *info.expect("failed to receive node info"),
            NodeInfo::File(FileObject { event: None })
        );
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
        open_exposed_dir(&path, abs_moniker, model, MODE_TYPE_DIRECTORY, server_end).await;
        let dir_proxy = DirectoryProxy::new(node_proxy.into_channel().unwrap());
        let file = Path::new("hippo");
        let file_proxy = io_util::open_file(&dir_proxy, &file, OPEN_RIGHT_READABLE)
            .expect("failed to open file");
        let res = io_util::read_file(&file_proxy).await;

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
        open_exposed_dir(&path, abs_moniker, model, MODE_TYPE_SERVICE, server_end).await;
        let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = echo_proxy.echo_string(Some("hippos")).await;
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
            get_dir_from_namespace(&path.dirname, resolved_url.clone(), namespaces).await;
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &Path::new(&path.basename),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )
        .expect("failed to open realm service");
        let realm_proxy = fsys::RealmProxy::new(node_proxy.into_channel().unwrap());
        let mut child_ref = fsys::ChildRef { name: "my_child".to_string(), collection: None };
        let (_client_chan, server_chan) = zx::Channel::create().unwrap();
        let exposed_capabilities = ServerEnd::new(server_chan);
        let res = realm_proxy.bind_child(&mut child_ref, exposed_capabilities).await;

        // Check for side effects: realm service should have received the `bind_child` call.
        let _ = res.expect("failed to use realm service");
        let bind_url =
            format!("test:///{}_resolved", bind_calls.lock().await.last().expect("no bind call"));
        assert_eq!(bind_url, resolved_url);
    }

    /// Call `fuchsia.sys2.Realm.CreateChild` to create a dynamic child.
    pub async fn call_create_child<'a>(
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        collection: &'a str,
        child_decl: ChildDecl,
    ) {
        let path: CapabilityPath = "/svc/fuchsia.sys2.Realm".try_into().expect("no realm service");
        let dir_proxy =
            get_dir_from_namespace(&path.dirname, resolved_url.clone(), namespaces).await;
        let node_proxy = io_util::open_node(
            &dir_proxy,
            &Path::new(&path.basename),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )
        .expect("failed to open realm service");
        let realm_proxy = fsys::RealmProxy::new(node_proxy.into_channel().unwrap());
        let mut collection_ref = fsys::CollectionRef { name: collection.to_string() };
        let child_decl = child_decl.native_into_fidl();
        let res = realm_proxy.create_child(&mut collection_ref, child_decl).await;
        let _ = res.expect("failed to create child");
    }

    /// Returns a cloned DirectoryProxy to the dir `dir_string` inside the namespace of
    /// `resolved_url`.
    async fn get_dir_from_namespace(
        dir_string: &str,
        resolved_url: String,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    ) -> DirectoryProxy {
        let mut ns_guard = namespaces.lock().await;
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
        let realm = model.look_up_realm(abs_moniker).await
            .expect(&format!("realm not found {}", abs_moniker));
        model.bind_instance(realm.clone()).await.expect("failed to bind instance");
        let state = realm.state.lock().await;
        let execution = state.execution.as_ref().expect("no execution");
        let flags = OPEN_RIGHT_READABLE;
        execution.exposed_dir.root_dir.open(flags, open_mode, path.split(), server_end).await
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
    memfs_proxy: Option<Arc<DirectoryProxy>>,
}

impl OutDir {
    pub fn new() -> OutDir {
        OutDir { host_service: false, memfs_proxy: None }
    }
    /// Adds `svc/foo` to the out directory, which implements `fidl.examples.echo.Echo`.
    pub fn add_service(&mut self) {
        self.host_service = true;
    }
    /// Adds `data/foo/hippo` to the out directory, which contains the string `hippo`, and adds
    /// `storage` to the out directory, which contains a directory broker that reroutes connections
    /// to a locally hosted memfs.
    pub fn add_directory(&mut self, memfs: &Memfs) {
        self.memfs_proxy = Some(Arc::new(memfs.clone_root_handle()));
    }
    /// Returns a function that will host this outgoing directory on the given ServerEnd.
    pub fn host_fn(&self) -> Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        let host_service = self.host_service;
        let memfs_proxy = self.memfs_proxy.clone();
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            let memfs_proxy = memfs_proxy.clone();
            fasync::spawn(async move {
                let mut pseudo_dir = directory::simple::empty();
                if host_service {
                    pseudo_dir
                        .add_entry(
                            "svc",
                            pseudo_directory! {
                                "file" => read_only(|| Ok(b"hippos".to_vec())),
                                "foo" =>
                                    directory_broker::DirectoryBroker::new(Box::new(
                                            Self::echo_server_fn)),
                            },
                        )
                        .map_err(|(s, _)| s)
                        .expect("failed to add svc entry");
                }
                if let Some(memfs_proxy) = memfs_proxy {
                    Self::initialize_foo_hippo_in_memfs(&memfs_proxy).await;

                    let memfs_proxy =
                        io_util::clone_directory(&memfs_proxy, CLONE_FLAG_SAME_RIGHTS).unwrap();
                    pseudo_dir
                        .add_entry(
                            "data",
                            directory_broker::DirectoryBroker::new(Box::new(
                                move |flags, mode, path, server_end| {
                                    if path == "" {
                                        memfs_proxy
                                            .clone(
                                                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                                                server_end,
                                            )
                                            .expect("failed to clone cache subdir");
                                    } else {
                                        memfs_proxy
                                            .open(flags, mode, &path, server_end)
                                            .expect("failed to open cache subdir");
                                    }
                                },
                            )),
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

                let _ = pseudo_dir.await;

                panic!("the pseudo dir exited!");
            });
        })
    }

    async fn initialize_foo_hippo_in_memfs(memfs_proxy: &DirectoryProxy) {
        let foo_proxy = io_util::open_directory(
            &memfs_proxy,
            &Path::new("foo"),
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .unwrap();
        let hippo_proxy = io_util::open_file(
            &foo_proxy,
            &Path::new("hippo"),
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .unwrap();
        let (s, _) = hippo_proxy.write(&mut b"hippo".to_vec().drain(..)).await
            .expect("failed to write to file");
        assert_eq!(zx::Status::OK, zx::Status::from_raw(s));
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
                stream.try_next().await.unwrap()
            {
                responder.send(value.as_ref().map(|s| &**s)).unwrap();
            }
        });
    }
}
