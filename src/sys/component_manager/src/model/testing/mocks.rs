// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{CapabilityPath, ComponentDecl, ExposeDecl, UseDecl},
    directory_broker::RoutingFn,
    failure::format_err,
    fidl::endpoints::ServerEnd,
    fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream},
    fidl_fuchsia_io::{DirectoryMarker, NodeMarker},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::{
        directory::{self, entry::DirectoryEntry},
        file::simple::read_only,
    },
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, lock::Mutex, prelude::*},
    std::{
        boxed::Box,
        collections::{HashMap, HashSet},
        convert::TryFrom,
        sync::{Arc, Mutex as SyncMutex},
    },
};

/// Creates a routing function factory for `UseDecl` that does the following:
/// - Redirects all directory capabilities to a directory with the file "hello".
/// - Redirects all service capabilities to the echo service.
pub fn proxy_use_routing_factory() -> impl Fn(AbsoluteMoniker, UseDecl) -> RoutingFn {
    move |_abs_moniker: AbsoluteMoniker, use_decl: UseDecl| new_proxy_routing_fn(use_decl.into())
}

/// Creates a routing function factory for `ExposeDecl` that does the following:
/// - Redirects all directory capabilities to a directory with the file "hello".
/// - Redirects all service capabilities to the echo service.
pub fn proxy_expose_routing_factory() -> impl Fn(AbsoluteMoniker, ExposeDecl) -> RoutingFn {
    move |_abs_moniker: AbsoluteMoniker, expose_decl: ExposeDecl| {
        new_proxy_routing_fn(expose_decl.into())
    }
}

enum CapabilityType {
    Service,
    LegacyService,
    Directory,
    Storage,
    Runner,
}

impl From<UseDecl> for CapabilityType {
    fn from(use_: UseDecl) -> Self {
        match use_ {
            UseDecl::Service(_) => CapabilityType::Service,
            UseDecl::LegacyService(_) => CapabilityType::LegacyService,
            UseDecl::Directory(_) => CapabilityType::Directory,
            UseDecl::Storage(_) => CapabilityType::Storage,
            UseDecl::Runner(_) => CapabilityType::Runner,
        }
    }
}

impl From<ExposeDecl> for CapabilityType {
    fn from(expose: ExposeDecl) -> Self {
        match expose {
            ExposeDecl::Service(_) => CapabilityType::Service,
            ExposeDecl::LegacyService(_) => CapabilityType::LegacyService,
            ExposeDecl::Directory(_) => CapabilityType::Directory,
            ExposeDecl::Runner(_) => CapabilityType::Runner,
        }
    }
}

fn new_proxy_routing_fn(ty: CapabilityType) -> RoutingFn {
    Box::new(
        move |flags: u32, mode: u32, relative_path: String, server_end: ServerEnd<NodeMarker>| {
            match ty {
                CapabilityType::Service => panic!("service capability unsupported"),
                CapabilityType::LegacyService => {
                    fasync::spawn(async move {
                        let server_end: ServerEnd<EchoMarker> =
                            ServerEnd::new(server_end.into_channel());
                        let mut stream: EchoRequestStream = server_end.into_stream().unwrap();
                        while let Some(EchoRequest::EchoString { value, responder }) =
                            stream.try_next().await.unwrap()
                        {
                            responder.send(value.as_ref().map(|s| &**s)).unwrap();
                        }
                    });
                }
                CapabilityType::Directory | CapabilityType::Storage => {
                    let mut sub_dir = directory::simple::empty();
                    sub_dir
                        .add_entry("hello", { read_only(move || Ok(b"friend".to_vec())) })
                        .map_err(|(s, _)| s)
                        .expect("Failed to add 'hello' entry");
                    sub_dir.open(flags, mode, &mut relative_path.split("/"), server_end);
                    fasync::spawn(async move {
                        let _ = sub_dir.await;
                    });
                }
                CapabilityType::Runner => {
                    // TODO(fxb/4761): Implement routing for runner caps.
                    panic!("runner capability unsupported");
                }
            }
        },
    )
}

pub struct MockResolver {
    components: HashMap<String, ComponentDecl>,
}

impl MockResolver {
    pub fn new() -> Self {
        MockResolver { components: HashMap::new() }
    }

    async fn resolve_async(&self, component_url: String) -> Result<fsys::Component, ResolverError> {
        const NAME_PREFIX: &str = "test:///";
        debug_assert!(component_url.starts_with(NAME_PREFIX), "invalid component url");
        let (_, name) = component_url.split_at(NAME_PREFIX.len());
        let decl = self.components.get(name).ok_or(ResolverError::component_not_available(
            name.to_string(),
            format_err!("not in the hashmap"),
        ))?;
        let fsys_decl =
            fsys::ComponentDecl::try_from(decl.clone()).expect("decl failed conversion");
        Ok(fsys::Component {
            resolved_url: Some(format!("test:///{}_resolved", name)),
            decl: Some(fsys_decl),
            package: None,
        })
    }

    pub fn add_component(&mut self, name: &str, component: ComponentDecl) {
        self.components.insert(name.to_string(), component);
    }
}

impl Resolver for MockResolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
        Box::pin(self.resolve_async(component_url.to_string()))
    }
}

pub type HostFn = Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync>;

pub type ManagedNamespace = Mutex<fsys::ComponentNamespace>;

struct MockRunnerInner {
    /// List of URLs started by this runner instance.
    urls_run: Vec<String>,

    /// Namespace for each component, mapping resolved URL to the component's namespace.
    namespaces: HashMap<String, Arc<Mutex<fsys::ComponentNamespace>>>,

    /// Functions for serving the `outgoing` and `runtime` directories
    /// of a given compoment. When a component is started, these
    /// functions will be called with the server end of the directories.
    outgoing_host_fns: HashMap<String, Arc<HostFn>>,
    runtime_host_fns: HashMap<String, Arc<HostFn>>,

    /// Set of URLs that the MockRunner will fail the `start` call for.
    failing_urls: HashSet<String>,
}

pub struct MockRunner {
    // The internal runner state.
    //
    // Inner state is guarded by a std::sync::Mutex to avoid helper
    // functions needing "async" (and propagating to callers).
    // std::sync::MutexGuard doesn't have the "Send" trait, so the
    // compiler will prevent us calling ".await" while holding the lock.
    inner: SyncMutex<MockRunnerInner>,
}

impl MockRunner {
    pub fn new() -> Self {
        MockRunner {
            inner: SyncMutex::new(MockRunnerInner {
                urls_run: vec![],
                namespaces: HashMap::new(),
                outgoing_host_fns: HashMap::new(),
                runtime_host_fns: HashMap::new(),
                failing_urls: HashSet::new(),
            }),
        }
    }

    /// Cause the component `name` to return an error when started.
    pub fn cause_failure(&self, name: &str) {
        self.inner.lock().unwrap().failing_urls.insert(format!("test:///{}_resolved", name));
    }

    /// Return a list of URLs that have been run by this runner.
    pub fn urls_run(&self) -> Vec<String> {
        self.inner.lock().unwrap().urls_run.clone()
    }

    /// Register `function` to serve the outgoing directory of component `name`
    pub fn add_host_fn(&self, name: &str, function: HostFn) {
        self.inner.lock().unwrap().outgoing_host_fns.insert(name.to_string(), Arc::new(function));
    }

    /// Register `function` to serve the runtime directory of component `name`
    pub fn add_runtime_host_fn(&self, name: &str, function: HostFn) {
        self.inner.lock().unwrap().runtime_host_fns.insert(name.to_string(), Arc::new(function));
    }

    /// Get the input namespace for component `name`.
    pub fn get_namespace(&self, name: &str) -> Option<Arc<ManagedNamespace>> {
        self.inner.lock().unwrap().namespaces.get(name).map(Arc::clone)
    }
}

impl Runner for MockRunner {
    fn start(
        &self,
        start_info: fsys::ComponentStartInfo,
        _server_chan: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> BoxFuture<Result<(), RunnerError>> {
        let outgoing_host_fn;
        let runtime_host_fn;

        {
            let mut state = self.inner.lock().unwrap();

            // Resolve the URL, and trigger a failure if previously requested.
            let resolved_url = start_info.resolved_url.unwrap();
            if state.failing_urls.contains(&resolved_url) {
                return Box::pin(futures::future::ready(Err(RunnerError::component_launch_error(
                    resolved_url,
                    format_err!("ouch"),
                ))));
            }

            // Record that this URL has been started.
            state.urls_run.push(resolved_url.clone());

            // Create a namespace for the component.
            state
                .namespaces
                .insert(resolved_url.clone(), Arc::new(Mutex::new(start_info.ns.unwrap())));

            // Fetch host functions, which will provide the outgoing and runtime directories
            // for the component.
            //
            // If functions were provided, then start_info.outgoing_dir will be
            // automatically closed once it goes out of scope at the end of this
            // function.
            outgoing_host_fn = state.outgoing_host_fns.get(&resolved_url).map(Arc::clone);
            runtime_host_fn = state.runtime_host_fns.get(&resolved_url).map(Arc::clone);
        }

        // Start serving the outgoing/runtime directories.
        if let Some(outgoing_host_fn) = outgoing_host_fn {
            outgoing_host_fn(start_info.outgoing_dir.unwrap());
        }
        if let Some(runtime_host_fn) = runtime_host_fn {
            runtime_host_fn(start_info.runtime_dir.unwrap());
        }

        Box::pin(futures::future::ready(Ok(())))
    }
}

/// A fake `OutgoingBinder` implementation that always returns `Ok(())` in a `BoxFuture`.
pub struct FakeOutgoingBinder;

impl FakeOutgoingBinder {
    pub fn new() -> Arc<dyn OutgoingBinder> {
        Arc::new(Self {})
    }
}

impl OutgoingBinder for FakeOutgoingBinder {
    fn bind_open_outgoing<'a>(
        &'a self,
        _realm: Arc<Realm>,
        _flags: u32,
        _open_mode: u32,
        _path: &'a CapabilityPath,
        _server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async move { Ok(()) })
    }
}
