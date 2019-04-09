// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{directory_broker, io_util, model::tests::mocks::*, model::*},
    cm_rust::{ComponentDecl, RelativeId, UseDecl},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fidl_examples_echo::{self as echo, EchoMarker, EchoRequest, EchoRequestStream},
    fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, NodeMarker, MODE_TYPE_DIRECTORY, MODE_TYPE_SERVICE,
        OPEN_RIGHT_READABLE,
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
        ffi::CString,
        iter,
        path::PathBuf,
        ptr,
        sync::Arc,
    },
};

/// Returns an empty component decl for an executable component.
pub fn default_component_decl() -> ComponentDecl {
    ComponentDecl {
        program: Some(fdata::Dictionary { entries: vec![] }),
        uses: Vec::new(),
        exposes: Vec::new(),
        offers: Vec::new(),
        children: Vec::new(),
        facets: None,
    }
}

/// Returns a cloned DirectoryProxy to the dir `dir_string` inside the namespace of `resolved_uri`.
pub async fn get_dir(
    dir_string: &str,
    resolved_uri: String,
    namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
) -> DirectoryProxy {
    let mut ns_guard = await!(namespaces.lock());
    let ns = ns_guard.get_mut(&resolved_uri).unwrap();

    // Find the index of our directory in the namespace, and remove the directory and path. The
    // path is removed so that the paths/dirs aren't shuffled in the namespace.
    let index = ns.paths.iter().position(|path| path == dir_string).expect("didn't find dir");
    let directory = ns.directories.remove(index);
    let path = ns.paths.remove(index);

    // Clone our directory, and then put the directory and path back on the end of the namespace so
    // that the namespace is (somewhat) unmodified.
    let dir_proxy = directory.into_proxy().unwrap();
    let dir_proxy_clone = io_util::clone_directory(&dir_proxy).unwrap();
    ns.directories.push(ClientEnd::new(dir_proxy.into_channel().unwrap().into_zx_channel()));
    ns.paths.push(path);

    dir_proxy_clone
}

/// OutDir can be used to construct and then host an out directory, containing a directory
/// structure with 0 or 1 read-only files, and 0 or 1 services.
struct OutDir {
    // TODO: it would be great if this struct held a `directory::simple::Simple` that was mutated
    // by the `add_*` functions, but this is not possible because `directory::simple::Simple`
    // doesn't have `Send` and `Sync` on its internal fields, which is needed for the returned
    // function by `host_fn`. This logic should be updated to directly work on a directory once a
    // multithreaded rust vfs is implemented.
    host_service: bool,
    host_directory: bool,
}

impl OutDir {
    fn new() -> OutDir {
        OutDir { host_service: false, host_directory: false }
    }
    /// Adds `svc/foo` to the out directory, which implements `fidl.examples.echo.Echo`.
    fn add_service(&mut self) {
        self.host_service = true;
    }
    /// Adds `data/foo/hippo` to the out directory, which contains the string `hippo`
    fn add_directory(&mut self) {
        self.host_directory = true;
    }
    /// Returns a function that will host this outgoing directory on the given ServerEnd.
    fn host_fn(&self) -> Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync> {
        let host_service = self.host_service;
        let host_directory = self.host_directory;
        Box::new(move |server_end: ServerEnd<DirectoryMarker>| {
            fasync::spawn(
                async move {
                    let mut pseudo_dir = directory::simple::empty();
                    if host_service {
                        pseudo_dir.add_entry("svc",
                            pseudo_directory! {
                                "foo" =>
                                    directory_broker::DirectoryBroker::new_service_broker(Box::new(echo_server_fn)),
                            })
                        .map_err(|(s,_)| s)
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
                        OPEN_RIGHT_READABLE,
                        MODE_TYPE_DIRECTORY,
                        &mut iter::empty(),
                        ServerEnd::new(server_end.into_channel()),
                    );

                    let _ = await!(pseudo_dir);

                    panic!("the pseudo dir exited!");
                },
            );
        })
    }
}

/// Hosts a new service on server_end that implements fidl.examples.echo.Echo
fn echo_server_fn(server_end: ServerEnd<NodeMarker>) {
    fasync::spawn(
        async move {
            let server_end: ServerEnd<EchoMarker> = ServerEnd::new(server_end.into_channel());
            let mut stream: EchoRequestStream = server_end.into_stream().unwrap();
            while let Some(EchoRequest::EchoString { value, responder }) =
                await!(stream.try_next()).unwrap()
            {
                responder.send(value.as_ref().map(|s| &**s)).unwrap();
            }
        },
    );
}

/// Looks up `resolved_uri` in the namespace, and attempts to read /data/hippo/hippo. The file
/// should contain the string "hippo".
pub async fn read_data_hippo_hippo(
    resolved_uri: String,
    namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
) {
    let dir_proxy = await!(get_dir("/data/hippo", resolved_uri, namespaces));
    let path = PathBuf::from("hippo");
    let file_proxy = io_util::open_file(&dir_proxy, &path).expect("failed to open file");
    assert_eq!("hippo", await!(io_util::read_file(&file_proxy)).expect("failed to read file"));
}

/// Looks up `resolved_uri` in the namespace, and attempts to use /svc/hippo. Expects the service
/// to be fidl.examples.echo.Echo.
async fn call_svc_hippo(
    resolved_uri: String,
    namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
) {
    let dir_proxy = await!(get_dir("/svc", resolved_uri, namespaces));
    let path = PathBuf::from("hippo");
    let node_proxy = io_util::open_node(&dir_proxy, &path, MODE_TYPE_SERVICE)
        .expect("failed to open echo service");
    let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
    let res = await!(echo_proxy.echo_string(Some("hippos"))).expect("failed to use echo service");
    assert_eq!(res, Some("hippos".to_string()));
}

// Installs a new directory at /hippo in our namespace. Does nothing if this directory already
// exists.
pub fn install_hippo_dir() {
    let (client_chan, server_chan) = zx::Channel::create().unwrap();

    let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
    let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
    if status != zx::sys::ZX_OK {
        panic!("bad status returned for fdio_ns_get_installed: {}", zx::Status::from_raw(status));
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
        .map(|UseDecl { type_, target_path, .. }| match type_ {
            fsys::CapabilityType::Directory => target_path.to_string(),
            fsys::CapabilityType::Service => target_path.dirname,
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

/// The inputs for a test in which a service is hosted from one component and accessed by another.
///
/// All string arguments are referring to component names, not URLs, ex: "a", not "test:///a" or
/// "test:///a_resolved".
pub struct TestInputs<'a> {
    /// The name of the root component.
    pub root_component: &'a str,
    /// The use decls on a component which should be checked. Service capabilities are assumed to
    /// be installed at `/svc/hippo` and directory capabilities are assumed to be installed at
    /// `/data/hippo`.
    pub users_to_check: Vec<(AbsoluteMoniker, fsys::CapabilityType)>,
    /// The component declarations that comprise the component tree
    pub components: Vec<(&'a str, ComponentDecl)>,
}

/// construct the given component topology, host `/svc/foo` and `/data/foo` from the outgoing
/// directory of any component offering or exposing from `Myself`, and attempt to use the use
/// declarations referenced in `test.users_to_check`
pub async fn run_routing_test<'a>(test: TestInputs<'a>) {
    let mut resolver = ResolverRegistry::new();
    let mut runner = MockRunner::new();
    let namespaces = runner.namespaces.clone();

    let mut mock_resolver = MockResolver::new();
    for (name, decl) in test.components.clone() {
        // if this decl is offer/exposing something from Myself, let's host it
        let source_iter = decl
            .offers
            .iter()
            .map(|o| (o.type_.clone(), o.source.clone()))
            .chain(decl.exposes.iter().map(|e| (e.type_.clone(), e.source.clone())));
        let mut out_dir = None;
        for (type_, source) in source_iter {
            if source == RelativeId::Myself {
                match type_ {
                    fsys::CapabilityType::Service => {
                        out_dir.get_or_insert(OutDir::new()).add_service()
                    }
                    fsys::CapabilityType::Directory => {
                        out_dir.get_or_insert(OutDir::new()).add_directory()
                    }
                }
            }
        }
        if let Some(out_dir) = out_dir {
            runner.host_fns.insert(format!("test:///{}_resolved", name), out_dir.host_fn());
        }

        mock_resolver.add_component(name, decl);
    }
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: format!("test:///{}", test.root_component),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    for (moniker, type_) in test.users_to_check {
        assert!(await!(model.look_up_and_bind_instance(moniker.clone())).is_ok());
        let component_name =
            moniker.path().last().expect("didn't expect a root component").name().to_string();
        let component_resolved_url = format!("test:///{}_resolved", &component_name);
        await!(check_namespace(component_name, namespaces.clone(), test.components.clone()));
        match type_ {
            fsys::CapabilityType::Service => {
                await!(call_svc_hippo(component_resolved_url, namespaces.clone()))
            }
            fsys::CapabilityType::Directory => {
                await!(read_data_hippo_hippo(component_resolved_url, namespaces.clone()))
            }
        };
    }
}
