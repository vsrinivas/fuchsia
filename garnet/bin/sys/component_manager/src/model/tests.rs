// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    crate::{directory_broker, io_util},
    fdio,
    fidl::endpoints::ServerEnd,
    fidl_fidl_examples_echo::{self as echo, EchoMarker, EchoRequest, EchoRequestStream},
    fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::{DirectoryMarker, NodeMarker},
    fidl_fuchsia_io::{MODE_TYPE_DIRECTORY, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::{
        directory::entry::DirectoryEntry, file::simple::read_only, pseudo_directory,
    },
    fuchsia_zircon as zx,
    fuchsia_zircon::HandleBased,
    futures::future::FutureObj,
    futures::lock::Mutex,
    futures::TryStreamExt,
    lazy_static::lazy_static,
    regex::Regex,
    std::collections::{HashMap, HashSet},
    std::ffi::CString,
    std::iter,
    std::path::PathBuf,
    std::ptr,
    std::sync::Arc,
};

struct ChildInfo {
    name: String,
    startup: fsys::StartupMode,
}

// TODO: impl MockResolver::new(), use it instead of always filling in these fields manually
struct MockResolver {
    // Map of parent component instance to its children component instances.
    children: Arc<Mutex<HashMap<String, Vec<ChildInfo>>>>,
    // Map of component instance to vec of UseDecls of the component instance.
    uses: Arc<Mutex<HashMap<String, Option<Vec<fsys::UseDecl>>>>>,
    // Map of component instance to vec of OfferDecls of the component instance.
    offers: Arc<Mutex<HashMap<String, Option<Vec<fsys::OfferDecl>>>>>,
    // Map of component instance to vec of ExposeDecls of the component instance.
    exposes: Arc<Mutex<HashMap<String, Option<Vec<fsys::ExposeDecl>>>>>,
    // All the component instances that contain no `program` (non-executable realms).
    no_execute: Arc<Mutex<HashSet<String>>>,
}

lazy_static! {
    static ref NAME_RE: Regex = Regex::new(r"test:///([0-9a-z\-\._]+)$").unwrap();
}

impl MockResolver {
    fn new() -> MockResolver {
        MockResolver {
            children: Arc::new(Mutex::new(HashMap::new())),
            uses: Arc::new(Mutex::new(HashMap::new())),
            offers: Arc::new(Mutex::new(HashMap::new())),
            exposes: Arc::new(Mutex::new(HashMap::new())),
            no_execute: Arc::new(Mutex::new(HashSet::new())),
        }
    }

    async fn resolve_async(&self, component_uri: String) -> Result<fsys::Component, ResolverError> {
        let caps = NAME_RE.captures(&component_uri).unwrap();
        let name = &caps[1];
        let children = await!(self.children.lock());
        let mut decl = new_component_decl();
        if let Some(children) = children.get(name) {
            decl.children = Some(
                children
                    .iter()
                    .map(|c| fsys::ChildDecl {
                        name: Some(c.name.clone()),
                        uri: Some(format!("test:///{}", c.name)),
                        startup: Some(c.startup),
                    })
                    .collect(),
            );
        }
        let uses = await!(self.uses.lock());
        if let Some(uses) = uses.get(name) {
            // There's no clone function on this, so copy over the fields manually
            let mut decl_uses = vec![];
            for u in uses.as_ref().unwrap_or(&vec![]) {
                decl_uses.push(fsys::UseDecl {
                    type_: u.type_.clone(),
                    source_path: u.source_path.clone(),
                    target_path: u.target_path.clone(),
                });
            }
            decl.uses = Some(decl_uses);
        }
        let offers = await!(self.offers.lock());
        if let Some(offers) = offers.get(name) {
            // There's no clone function on this, so copy over the fields manually
            let mut decl_offers = vec![];
            for o in offers.as_ref().unwrap_or(&vec![]) {
                let mut offer = fsys::OfferDecl {
                    type_: o.type_.clone(),
                    source_path: o.source_path.clone(),
                    source: Some(fsys::RelativeId {
                        relation: o.source.as_ref().unwrap().relation.clone(),
                        child_name: o.source.as_ref().unwrap().child_name.clone(),
                    }),
                    targets: None,
                };
                let mut offer_targets = vec![];
                for t in o.targets.as_ref().unwrap_or(&vec![]) {
                    offer_targets.push(fsys::OfferTarget {
                        target_path: t.target_path.clone(),
                        child_name: t.child_name.clone(),
                    });
                }
                offer.targets = Some(offer_targets);
                decl_offers.push(offer);
            }
            decl.offers = Some(decl_offers);
        }
        let exposes = await!(self.exposes.lock());
        if let Some(exposes) = exposes.get(name) {
            let mut decl_exposes = vec![];
            for e in exposes.as_ref().unwrap_or(&vec![]) {
                decl_exposes.push(fsys::ExposeDecl {
                    type_: e.type_.clone(),
                    source_path: e.source_path.clone(),
                    source: Some(fsys::RelativeId {
                        relation: e.source.as_ref().unwrap().relation.clone(),
                        child_name: e.source.as_ref().unwrap().child_name.clone(),
                    }),
                    target_path: e.target_path.clone(),
                });
            }
            decl.exposes = Some(decl_exposes);
        }
        let no_execute = await!(self.no_execute.lock());
        decl.program = if no_execute.contains(name) {
            None
        } else {
            Some(fdata::Dictionary { entries: vec![] })
        };
        Ok(fsys::Component {
            resolved_uri: Some(format!("test:///{}_resolved", name)),
            decl: Some(decl),
            package: None,
        })
    }
}

impl Resolver for MockResolver {
    fn resolve(&self, component_uri: &str) -> FutureObj<Result<fsys::Component, ResolverError>> {
        FutureObj::new(Box::new(self.resolve_async(component_uri.to_string())))
    }
}

struct MockRunner {
    uris_run: Arc<Mutex<Vec<String>>>,
    namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    host_fns: Arc<Mutex<HashMap<String, Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync>>>>,
}

impl MockRunner {
    async fn start_async(&self, start_info: fsys::ComponentStartInfo) -> Result<(), RunnerError> {
        let resolved_uri = start_info.resolved_uri.unwrap();
        await!(self.uris_run.lock()).push(resolved_uri.clone());
        await!(self.namespaces.lock()).insert(resolved_uri.clone(), start_info.ns.unwrap());
        // If no host_fn was provided, then start_info.outgoing_dir will be
        // automatically closed once it goes out of scope at the end of this
        // function.
        let host_fns_guard = await!(self.host_fns.lock());
        let host_fn = host_fns_guard.get(&resolved_uri);
        if let Some(host_fn) = host_fn {
            host_fn(start_info.outgoing_dir.unwrap());
        }
        Ok(())
    }
}

impl Runner for MockRunner {
    fn start(&self, start_info: fsys::ComponentStartInfo) -> FutureObj<Result<(), RunnerError>> {
        FutureObj::new(Box::new(self.start_async(start_info)))
    }
}

fn new_component_decl() -> fsys::ComponentDecl {
    fsys::ComponentDecl {
        program: None,
        uses: None,
        exposes: None,
        offers: None,
        facets: None,
        children: None,
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_root() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };
    resolver.register("test".to_string(), Box::new(MockResolver::new()));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    let res = await!(model.bind_instance(AbsoluteMoniker::root()));
    let expected_res: Result<(), ModelError> = Ok(());
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    let actual_uris = await!(uris_run.lock());
    let expected_uris = vec!["test:///root_resolved".to_string()];
    assert_eq!(*actual_uris, expected_uris);
    let root_realm = await!(model.root_realm.lock());
    let actual_children = get_children(&root_realm);
    assert!(actual_children.is_empty());
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_root_non_existent() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };
    resolver.register("test".to_string(), Box::new(MockResolver::new()));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
        "no-such-instance".to_string()
    )])));
    let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(
        AbsoluteMoniker::new(vec![ChildMoniker::new("no-such-instance".to_string())]),
    ));
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    let actual_uris = await!(uris_run.lock());
    let expected_uris: Vec<String> = vec![];
    assert_eq!(*actual_uris, expected_uris);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_child() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };
    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "root".to_string(),
        vec![
            ChildInfo { name: "system".to_string(), startup: fsys::StartupMode::Lazy },
            ChildInfo { name: "echo".to_string(), startup: fsys::StartupMode::Lazy },
        ],
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    // bind to system
    {
        let res = await!(model
            .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("system".to_string()),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris = vec!["test:///system_resolved".to_string()];
        assert_eq!(*actual_uris, expected_uris);
    }
    // Validate children. system is resolved, but not echo.
    {
        let actual_children = get_children(&*await!(model.root_realm.lock()));
        let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
        expected_children.insert(ChildMoniker::new("system".to_string()));
        expected_children.insert(ChildMoniker::new("echo".to_string()));
        assert_eq!(actual_children, expected_children);
    }
    {
        let system_realm = get_child_realm(&*await!(model.root_realm.lock()), "system");
        let echo_realm = get_child_realm(&*await!(model.root_realm.lock()), "echo");
        let actual_children = get_children(&*await!(system_realm.lock()));
        assert!(actual_children.is_empty());
        assert!(await!(echo_realm.lock()).instance.child_realms.is_none());
    }
    // bind to echo
    {
        let res =
            await!(model
                .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("echo".to_string()),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris =
            vec!["test:///system_resolved".to_string(), "test:///echo_resolved".to_string()];
        assert_eq!(*actual_uris, expected_uris);
    }
    // Validate children. Now echo is resolved.
    {
        let echo_realm = get_child_realm(&*await!(model.root_realm.lock()), "echo");
        let actual_children = get_children(&*await!(echo_realm.lock()));
        assert!(actual_children.is_empty());
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_child_non_existent() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };
    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "root".to_string(),
        vec![ChildInfo { name: "system".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    // bind to system
    {
        let res = await!(model
            .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("system".to_string()),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris = vec!["test:///system_resolved".to_string()];
        assert_eq!(*actual_uris, expected_uris);
    }
    // can't bind to logger: it does not exist
    {
        let moniker = AbsoluteMoniker::new(vec![
            ChildMoniker::new("system".to_string()),
            ChildMoniker::new("logger".to_string()),
        ]);
        let res = await!(model.bind_instance(moniker.clone()));
        let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(moniker));
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris = vec!["test:///system_resolved".to_string()];
        assert_eq!(*actual_uris, expected_uris);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_eager_child() {
    // Create a hierarchy of children. The two components in the middle enable eager binding.
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };
    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "root".to_string(),
        vec![ChildInfo { name: "a".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Eager }],
    );
    await!(mock_resolver.children.lock()).insert(
        "b".to_string(),
        vec![ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Eager }],
    );
    await!(mock_resolver.children.lock()).insert(
        "c".to_string(),
        vec![ChildInfo { name: "d".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });

    // Bind to the top component, and check that it and the eager components were started.
    {
        let res =
            await!(model
                .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("a".to_string()),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris = vec![
            "test:///a_resolved".to_string(),
            "test:///b_resolved".to_string(),
            "test:///c_resolved".to_string(),
        ];
        assert_eq!(*actual_uris, expected_uris);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_no_execute() {
    // Create a non-executable component with an eagerly-started child.
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };
    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "root".to_string(),
        vec![ChildInfo { name: "a".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Eager }],
    );
    await!(mock_resolver.no_execute.lock()).insert("a".to_string());
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });

    // Bind to the parent component. The child should be started. However, the parent component
    // is non-executable so it is not run.
    {
        let res =
            await!(model
                .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("a".to_string()),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris = vec!["test:///b_resolved".to_string()];
        assert_eq!(*actual_uris, expected_uris);
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn bind_instance_recursive_child() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };
    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "root".to_string(),
        vec![ChildInfo { name: "system".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.children.lock()).insert(
        "system".to_string(),
        vec![
            ChildInfo { name: "logger".to_string(), startup: fsys::StartupMode::Lazy },
            ChildInfo { name: "netstack".to_string(), startup: fsys::StartupMode::Lazy },
        ],
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    // bind to logger (before ever binding to system)
    {
        let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![
            ChildMoniker::new("system".to_string()),
            ChildMoniker::new("logger".to_string())
        ])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris = vec!["test:///logger_resolved".to_string()];
        assert_eq!(*actual_uris, expected_uris);
    }
    // bind to netstack
    {
        let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![
            ChildMoniker::new("system".to_string()),
            ChildMoniker::new("netstack".to_string()),
        ])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris =
            vec!["test:///logger_resolved".to_string(), "test:///netstack_resolved".to_string()];
        assert_eq!(*actual_uris, expected_uris);
    }
    // finally, bind to system
    {
        let res = await!(model
            .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("system".to_string()),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris = vec![
            "test:///logger_resolved".to_string(),
            "test:///netstack_resolved".to_string(),
            "test:///system_resolved".to_string(),
        ];
        assert_eq!(*actual_uris, expected_uris);
    }
    // validate children
    {
        let actual_children = get_children(&*await!(model.root_realm.lock()));
        let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
        expected_children.insert(ChildMoniker::new("system".to_string()));
        assert_eq!(actual_children, expected_children);
    }
    let system_realm = get_child_realm(&*await!(model.root_realm.lock()), "system");
    {
        let actual_children = get_children(&*await!(system_realm.lock()));
        let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
        expected_children.insert(ChildMoniker::new("logger".to_string()));
        expected_children.insert(ChildMoniker::new("netstack".to_string()));
        assert_eq!(actual_children, expected_children);
    }
    {
        let logger_realm = get_child_realm(&*await!(system_realm.lock()), "logger");
        let actual_children = get_children(&*await!(logger_realm.lock()));
        assert!(actual_children.is_empty());
    }
    {
        let netstack_realm = get_child_realm(&*await!(system_realm.lock()), "netstack");
        let actual_children = get_children(&*await!(netstack_realm.lock()));
        assert!(actual_children.is_empty());
    }
}

// Installs a new directory at /hippo in our namespace. Does nothing if this directory already
// exists.
fn install_hippo_dir() {
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
    host_data_foo_hippo(ServerEnd::new(server_chan));
}

#[fuchsia_async::run_singlethreaded(test)]
async fn check_namespace_from_using() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // The system component will request namespaces of "/foo" to its "/bar" and
    // "/baz/bar", and it will also request the "/svc/fidl.examples.echo.Echo" service
    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "root".to_string(),
        vec![ChildInfo { name: "system".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.uses.lock()).insert(
        "system".to_string(),
        Some(vec![
            fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/foo".to_string()),
                target_path: Some("/bar".to_string()),
            },
            fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/foo".to_string()),
                target_path: Some("/baz/bar".to_string()),
            },
            fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/fidl.examples.echo.Echo".to_string()),
                target_path: Some("/svc/fidl.examples.echo.Echo".to_string()),
            },
        ]),
    );

    // The root component will offer the "/hippo" directory from its realm as "/foo" and the
    // "/svc/fidl.examples.echo.Echo" service from its realm
    await!(mock_resolver.offers.lock()).insert(
        "root".to_string(),
        Some(vec![
            fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/hippo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Realm),
                    child_name: None,
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/foo".to_string()),
                    child_name: Some("system".to_string()),
                }]),
            },
            fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/fidl.examples.echo.Echo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Realm),
                    child_name: None,
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/svc/fidl.examples.echo.Echo".to_string()),
                    child_name: Some("system".to_string()),
                }]),
            },
        ]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///root".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    // bind to root and system
    let _ = await!(model.bind_instance(AbsoluteMoniker::new(vec![])));
    let res =
        await!(model
            .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("system".to_string()),])));
    let expected_res: Result<(), ModelError> = Ok(());
    assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    // Verify system has the expected namespaces.
    let mut actual_namespaces = await!(namespaces.lock());
    assert!(actual_namespaces.contains_key("test:///system_resolved"));
    let ns = actual_namespaces.get_mut("test:///system_resolved").unwrap();
    // "/pkg" is missing because we don't supply package in MockResolver.
    assert_eq!(ns.paths, ["/bar", "/baz/bar", "/svc"]);
    assert_eq!(ns.directories.len(), 3);

    // Install /hippo in our namespace
    install_hippo_dir();

    // /bar and /baz/bar are both pointed to the root component's /foo. This is not backed by a
    // real running component in this case, so we can't use the directory as nothing will
    // respond. The /svc/fidl.examples.echo.Echo service is backed by a realm component in
    // componentmgr's namespace, so we can use it.
    while let Some(dir) = ns.directories.pop() {
        match ns.paths.pop().as_ref().map(|s| s.as_str()) {
            Some("/bar") | Some("/baz/bar") => {
                let dir_proxy = dir.into_proxy().unwrap();
                let path = PathBuf::from("data/foo/hippo");
                let file_proxy =
                    io_util::open_file(&dir_proxy, &path).expect("failed to open file");
                assert_eq!(
                    "hippo",
                    await!(io_util::read_file(&file_proxy)).expect("failed to read file")
                );
                return;
            }
            Some("/svc") => {
                let dir_proxy = dir.into_proxy().unwrap();
                let path = PathBuf::from("fidl.examples.echo.Echo");
                let node_proxy = io_util::open_node(&dir_proxy, &path, MODE_TYPE_SERVICE)
                    .expect("failed to open echo service");
                let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
                let res = await!(echo_proxy.echo_string(Some("hippos")))
                    .expect("failed to use echo service");
                assert_eq!(res, Some("hippos".to_string()));
            }
            Some(_) | None => panic!("unknown directory in namespace"),
        }
    }
}

fn get_children(realm: &Realm) -> HashSet<ChildMoniker> {
    realm.instance.child_realms.as_ref().unwrap().keys().map(|m| m.clone()).collect()
}

fn get_child_realm(realm: &Realm, child: &str) -> Arc<Mutex<Realm>> {
    realm.instance.child_realms.as_ref().unwrap()[&ChildMoniker::new(child.to_string())].clone()
}

fn host_svc_foo_hippo(server_end: ServerEnd<DirectoryMarker>) {
    let echo_service_fn = Box::new(move |server_end: ServerEnd<NodeMarker>| {
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
    });
    fasync::spawn(
        async move {
            let mut pseudo_dir = pseudo_directory! {
                "svc" => pseudo_directory! {
                    "foo" =>
                        directory_broker::DirectoryBroker::new_service_broker(echo_service_fn),
                },
            };
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
}

async fn call_svc_hippo(ns: &mut fsys::ComponentNamespace) {
    while let Some(dir) = ns.directories.pop() {
        if let Some("/svc") = ns.paths.pop().as_ref().map(|s| s.as_str()) {
            let dir_proxy = dir.into_proxy().unwrap();
            let path = PathBuf::from("hippo");
            let node_proxy = io_util::open_node(&dir_proxy, &path, MODE_TYPE_SERVICE)
                .expect("failed to open echo service");
            let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
            let res =
                await!(echo_proxy.echo_string(Some("hippos"))).expect("failed to use echo service");
            assert_eq!(res, Some("hippos".to_string()));
            return;
        }
    }
    panic!("didn't find /svc");
}

// TODO: cover services and directories in the same tests below, as in
// "use_service_and_directory_from_parent".
///   a
///    \
///     b
///
/// a: offers service /svc/foo from self as /svc/bar
/// b: uses service /svc/bar as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_service_from_parent() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // Host /svc/foo from a's outgoing directory
    await!(host_fns.lock()).insert("test:///a_resolved".to_string(), Box::new(host_svc_foo_hippo));

    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.uses.lock()).insert(
        "b".to_string(),
        Some(vec![fsys::UseDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/bar".to_string()),
            target_path: Some("/svc/hippo".to_string()),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "a".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/foo".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None,
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/svc/bar".to_string()),
                child_name: Some("b".to_string()),
            }]),
        }]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///a".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    //TODO: remove these binds once we have lazy binding.
    assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).is_ok());
    assert!(await!(
        model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
    )
    .is_ok());

    // use /svc/baz from b's incoming namespace
    let mut namespaces = await!(namespaces.lock());
    await!(call_svc_hippo(namespaces.get_mut("test:///b_resolved").unwrap()));
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers service /svc/foo from self as /svc/bar
/// b: offers service /svc/bar from realm as /svc/baz
/// c: uses /svc/baz as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_service_from_grandparent() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // Host /svc/foo from a's outgoing directory
    await!(host_fns.lock()).insert("test:///a_resolved".to_string(), Box::new(host_svc_foo_hippo));

    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.children.lock()).insert(
        "b".to_string(),
        vec![ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.uses.lock()).insert(
        "c".to_string(),
        Some(vec![fsys::UseDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/baz".to_string()),
            target_path: Some("/svc/hippo".to_string()),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "a".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/foo".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None,
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/svc/bar".to_string()),
                child_name: Some("b".to_string()),
            }]),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "b".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/bar".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Realm),
                child_name: None,
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/svc/baz".to_string()),
                child_name: Some("c".to_string()),
            }]),
        }]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///a".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    //TODO: remove these binds once we have lazy binding.
    assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).is_ok());
    assert!(await!(
        model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
    )
    .is_ok());
    assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![
        ChildMoniker::new("b".to_string()),
        ChildMoniker::new("c".to_string()),
    ])))
    .is_ok());

    // use /svc/hippo from c's incoming namespace
    let mut namespaces = await!(namespaces.lock());
    await!(call_svc_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
}

///   a
///  / \
/// b   c
///
/// b: exposes service /svc/foo from self as /svc/bar
/// a: offers service /svc/bar from b as /svc/baz to c
/// c: uses /svc/baz as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_service_from_sibling() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // Host /svc/foo from b's outgoing directory
    await!(host_fns.lock()).insert("test:///b_resolved".to_string(), Box::new(host_svc_foo_hippo));

    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![
            ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy },
            ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy },
        ],
    );
    await!(mock_resolver.uses.lock()).insert(
        "c".to_string(),
        Some(vec![fsys::UseDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/baz".to_string()),
            target_path: Some("/svc/hippo".to_string()),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "a".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/bar".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Child),
                child_name: Some("b".to_string()),
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/svc/baz".to_string()),
                child_name: Some("c".to_string()),
            }]),
        }]),
    );
    await!(mock_resolver.exposes.lock()).insert(
        "b".to_string(),
        Some(vec![fsys::ExposeDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/foo".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None,
            }),
            target_path: Some("/svc/bar".to_string()),
        }]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///a".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    //TODO: remove these binds once we have lazy binding.
    await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).expect("failed to bind to root");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),])))
        .expect("failed to bind to b");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("c".to_string()),])))
        .expect("failed to bind to c");

    // use /svc/hippo from c's incoming namespace
    let mut namespaces = await!(namespaces.lock());
    await!(call_svc_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
}

///     a
///    / \
///   b   c
///  /
/// d
///
/// d: exposes service /svc/foo from self as /svc/bar
/// b: exposes service /svc/bar from d as /svc/baz
/// a: offers service /svc/baz from b as /svc/foobar to c
/// c: uses /svc/foobar as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_service_from_niece() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // Host /svc/foo from d's outgoing directory
    await!(host_fns.lock()).insert("test:///d_resolved".to_string(), Box::new(host_svc_foo_hippo));

    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![
            ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy },
            ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy },
        ],
    );
    await!(mock_resolver.children.lock()).insert(
        "b".to_string(),
        vec![ChildInfo { name: "d".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.uses.lock()).insert(
        "c".to_string(),
        Some(vec![fsys::UseDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/foobar".to_string()),
            target_path: Some("/svc/hippo".to_string()),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "a".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/baz".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Child),
                child_name: Some("b".to_string()),
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/svc/foobar".to_string()),
                child_name: Some("c".to_string()),
            }]),
        }]),
    );
    await!(mock_resolver.exposes.lock()).insert(
        "b".to_string(),
        Some(vec![fsys::ExposeDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/bar".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Child),
                child_name: Some("d".to_string()),
            }),
            target_path: Some("/svc/baz".to_string()),
        }]),
    );
    await!(mock_resolver.exposes.lock()).insert(
        "d".to_string(),
        Some(vec![fsys::ExposeDecl {
            type_: Some(fsys::CapabilityType::Service),
            source_path: Some("/svc/foo".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None,
            }),
            target_path: Some("/svc/bar".to_string()),
        }]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///a".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    //TODO: remove these binds once we have lazy binding.
    await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).expect("failed to bind to root");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),])))
        .expect("failed to bind to b");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("c".to_string()),])))
        .expect("failed to bind to c");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![
        ChildMoniker::new("b".to_string()),
        ChildMoniker::new("d".to_string()),
    ])))
    .expect("failed to bind to b/d");

    // use /svc/hippo from c's incoming namespace
    let mut namespaces = await!(namespaces.lock());
    await!(call_svc_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
}

fn host_data_foo_hippo(server_end: ServerEnd<DirectoryMarker>) {
    fasync::spawn(
        async move {
            let mut pseudo_dir = pseudo_directory! {
                "data" => pseudo_directory! {
                    "foo" => pseudo_directory! {
                        "hippo" => read_only(|| Ok(b"hippo".to_vec())),
                    },
                },
            };
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
}

async fn read_data_hippo_hippo(ns: &mut fsys::ComponentNamespace) {
    while let Some(dir) = ns.directories.pop() {
        if let Some("/data/hippo") = ns.paths.pop().as_ref().map(|s| s.as_str()) {
            let dir_proxy = dir.into_proxy().unwrap();
            let path = PathBuf::from("hippo");
            let file_proxy = io_util::open_file(&dir_proxy, &path).expect("failed to open file");
            assert_eq!(
                "hippo",
                await!(io_util::read_file(&file_proxy)).expect("failed to read file")
            );
            return;
        }
    }
    panic!("didn't find /data/hippo");
}

///   a
///    \
///     b
///
/// a: offers directory /data/foo from self as /data/bar
/// b: uses directory /data/bar as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_directory_from_parent() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // Host /data/foo from a's outgoing directory
    await!(host_fns.lock()).insert("test:///a_resolved".to_string(), Box::new(host_data_foo_hippo));

    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.uses.lock()).insert(
        "b".to_string(),
        Some(vec![fsys::UseDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/bar".to_string()),
            target_path: Some("/data/hippo".to_string()),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "a".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/foo".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None,
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/data/bar".to_string()),
                child_name: Some("b".to_string()),
            }]),
        }]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///a".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    //TODO: remove these binds once we have lazy binding.
    assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).is_ok());
    assert!(await!(
        model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
    )
    .is_ok());

    // use /data/hippo from b's incoming namespace
    let mut namespaces = await!(namespaces.lock());
    await!(read_data_hippo_hippo(namespaces.get_mut("test:///b_resolved").unwrap()));
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data/foo from self as /data/bar
/// b: offers directory /data/bar from realm as /data/baz
/// c: uses /svc/baz as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_directory_from_grandparent() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // Host /data/foo from a's outgoing directory
    await!(host_fns.lock()).insert("test:///a_resolved".to_string(), Box::new(host_data_foo_hippo));

    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.children.lock()).insert(
        "b".to_string(),
        vec![ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.uses.lock()).insert(
        "c".to_string(),
        Some(vec![fsys::UseDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/baz".to_string()),
            target_path: Some("/data/hippo".to_string()),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "a".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/foo".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None,
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/data/bar".to_string()),
                child_name: Some("b".to_string()),
            }]),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "b".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/bar".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Realm),
                child_name: None,
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/data/baz".to_string()),
                child_name: Some("c".to_string()),
            }]),
        }]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///a".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    //TODO: remove these binds once we have lazy binding.
    assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).is_ok());
    assert!(await!(
        model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
    )
    .is_ok());
    assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![
        ChildMoniker::new("b".to_string()),
        ChildMoniker::new("c".to_string()),
    ])))
    .is_ok());

    // use /data/hippo from c's incoming namespace
    let mut namespaces = await!(namespaces.lock());
    await!(read_data_hippo_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
}

///   a
///  / \
/// b   c
///
/// b: exposes directory /data/foo from self as /data/bar
/// a: offers directory /data/bar from b as /data/baz to c
/// c: uses /data/baz as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_directory_from_sibling() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // Host /data/foo from b's outgoing directory
    await!(host_fns.lock()).insert("test:///b_resolved".to_string(), Box::new(host_data_foo_hippo));

    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![
            ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy },
            ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy },
        ],
    );
    await!(mock_resolver.uses.lock()).insert(
        "c".to_string(),
        Some(vec![fsys::UseDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/baz".to_string()),
            target_path: Some("/data/hippo".to_string()),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "a".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/bar".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Child),
                child_name: Some("b".to_string()),
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/data/baz".to_string()),
                child_name: Some("c".to_string()),
            }]),
        }]),
    );
    await!(mock_resolver.exposes.lock()).insert(
        "b".to_string(),
        Some(vec![fsys::ExposeDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/foo".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None,
            }),
            target_path: Some("/data/bar".to_string()),
        }]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///a".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    //TODO: remove these binds once we have lazy binding.
    await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).expect("failed to bind to root");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),])))
        .expect("failed to bind to b");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("c".to_string()),])))
        .expect("failed to bind to c");

    // use /data/hippo from c's incoming namespace
    let mut namespaces = await!(namespaces.lock());
    await!(read_data_hippo_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
}

///     a
///    / \
///   b   c
///  /
/// d
///
/// d: exposes directory /data/foo from self as /data/bar
/// b: exposes directory /data/bar from d as /data/baz
/// a: offers directory /data/baz from b as /data/foobar to c
/// c: uses /data/foobar as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_directory_from_niece() {
    let mut resolver = ResolverRegistry::new();
    let uris_run = Arc::new(Mutex::new(vec![]));
    let namespaces = Arc::new(Mutex::new(HashMap::new()));
    let host_fns = Arc::new(Mutex::new(HashMap::new()));
    let runner = MockRunner {
        uris_run: uris_run.clone(),
        namespaces: namespaces.clone(),
        host_fns: host_fns.clone(),
    };

    // Host /data/foo from d's outgoing directory
    await!(host_fns.lock()).insert("test:///d_resolved".to_string(), Box::new(host_data_foo_hippo));

    let mock_resolver = MockResolver::new();
    await!(mock_resolver.children.lock()).insert(
        "a".to_string(),
        vec![
            ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy },
            ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy },
        ],
    );
    await!(mock_resolver.children.lock()).insert(
        "b".to_string(),
        vec![ChildInfo { name: "d".to_string(), startup: fsys::StartupMode::Lazy }],
    );
    await!(mock_resolver.uses.lock()).insert(
        "c".to_string(),
        Some(vec![fsys::UseDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/foobar".to_string()),
            target_path: Some("/data/hippo".to_string()),
        }]),
    );
    await!(mock_resolver.offers.lock()).insert(
        "a".to_string(),
        Some(vec![fsys::OfferDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/baz".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Child),
                child_name: Some("b".to_string()),
            }),
            targets: Some(vec![fsys::OfferTarget {
                target_path: Some("/data/foobar".to_string()),
                child_name: Some("c".to_string()),
            }]),
        }]),
    );
    await!(mock_resolver.exposes.lock()).insert(
        "b".to_string(),
        Some(vec![fsys::ExposeDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/bar".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Child),
                child_name: Some("d".to_string()),
            }),
            target_path: Some("/data/baz".to_string()),
        }]),
    );
    await!(mock_resolver.exposes.lock()).insert(
        "d".to_string(),
        Some(vec![fsys::ExposeDecl {
            type_: Some(fsys::CapabilityType::Directory),
            source_path: Some("/data/foo".to_string()),
            source: Some(fsys::RelativeId {
                relation: Some(fsys::Relation::Myself),
                child_name: None,
            }),
            target_path: Some("/data/bar".to_string()),
        }]),
    );
    resolver.register("test".to_string(), Box::new(mock_resolver));
    let model = Model::new(ModelParams {
        root_component_uri: "test:///a".to_string(),
        root_resolver_registry: resolver,
        root_default_runner: Box::new(runner),
    });
    //TODO: remove these binds once we have lazy binding.
    await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).expect("failed to bind to root");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),])))
        .expect("failed to bind to b");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("c".to_string()),])))
        .expect("failed to bind to c");
    await!(model.bind_instance(AbsoluteMoniker::new(vec![
        ChildMoniker::new("b".to_string()),
        ChildMoniker::new("d".to_string()),
    ])))
    .expect("failed to bind to b/d");

    // use /svc/hippo from c's incoming namespace
    let mut namespaces = await!(namespaces.lock());
    await!(read_data_hippo_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
}
