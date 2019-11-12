// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{ComponentDecl, ExposeDecl, UseDecl},
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
    futures::future::BoxFuture,
    futures::lock::Mutex,
    futures::prelude::*,
    std::{
        boxed::Box,
        collections::{HashMap, HashSet},
        convert::TryFrom,
        sync::Arc,
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

pub struct MockRunner {
    pub urls_run: Arc<Mutex<Vec<String>>>,
    pub namespaces: Namespaces,
    pub host_fns: HashMap<String, Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync>>,
    pub runtime_host_fns: HashMap<String, Box<dyn Fn(ServerEnd<DirectoryMarker>) + Send + Sync>>,
    failing_urls: HashSet<String>,
}

pub type Namespaces = Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>;

impl MockRunner {
    pub fn new() -> Self {
        MockRunner {
            urls_run: Arc::new(Mutex::new(vec![])),
            namespaces: Arc::new(Mutex::new(HashMap::new())),
            host_fns: HashMap::new(),
            runtime_host_fns: HashMap::new(),
            failing_urls: HashSet::new(),
        }
    }

    pub fn cause_failure(&mut self, name: &str) {
        self.failing_urls.insert(format!("test:///{}_resolved", name));
    }

    async fn start_async(
        &self,
        start_info: fsys::ComponentStartInfo,
        _server_end: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> Result<(), RunnerError> {
        let resolved_url = start_info.resolved_url.unwrap();
        if self.failing_urls.contains(&resolved_url) {
            return Err(RunnerError::component_launch_error(resolved_url, format_err!("ouch")));
        }
        self.urls_run.lock().await.push(resolved_url.clone());
        self.namespaces.lock().await.insert(resolved_url.clone(), start_info.ns.unwrap());
        // If no host_fn was provided, then start_info.outgoing_dir will be
        // automatically closed once it goes out of scope at the end of this
        // function.
        let host_fn = self.host_fns.get(&resolved_url);
        if let Some(host_fn) = host_fn {
            host_fn(start_info.outgoing_dir.unwrap());
        }

        let runtime_host_fn = self.runtime_host_fns.get(&resolved_url);
        if let Some(runtime_host_fn) = runtime_host_fn {
            runtime_host_fn(start_info.runtime_dir.unwrap());
        }
        Ok(())
    }
}

impl Runner for MockRunner {
    fn start(
        &self,
        start_info: fsys::ComponentStartInfo,
        server_chan: ServerEnd<fsys::ComponentControllerMarker>,
    ) -> BoxFuture<Result<(), RunnerError>> {
        Box::pin(self.start_async(start_info, server_chan))
    }
}
