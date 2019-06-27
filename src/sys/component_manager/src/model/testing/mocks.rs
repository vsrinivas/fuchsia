// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::directory_broker::RoutingFn,
    crate::model::*,
    cm_rust::{Capability, ComponentDecl},
    failure::{format_err, Error},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, NodeMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::{
        directory::{self, entry::DirectoryEntry},
        file::simple::read_only,
    },
    fuchsia_zircon as zx,
    futures::future::FutureObj,
    futures::lock::Mutex,
    futures::prelude::*,
    std::{
        collections::{HashMap, HashSet},
        convert::TryFrom,
        iter,
        sync::Arc,
    },
};

/// Creates a routing function generator that does the following:
/// - Redirects all directory capabilities to a directory with the file "hello".
/// - Redirects all service capabilities to the echo service.
pub fn proxying_routing_factory() -> impl Fn(AbsoluteMoniker, Capability) -> RoutingFn {
    let mut sub_dir = directory::simple::empty();
    let (sub_dir_client, sub_dir_server) = zx::Channel::create().unwrap();
    sub_dir.open(
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        MODE_TYPE_DIRECTORY,
        &mut iter::empty(),
        ServerEnd::<NodeMarker>::new(sub_dir_server.into()),
    );
    sub_dir
        .add_entry("hello", { read_only(move || Ok(b"friend".to_vec())) })
        .map_err(|(s, _)| s)
        .expect("Failed to add 'hello' entry");
    let sub_dir_proxy = ClientEnd::<DirectoryMarker>::new(sub_dir_client)
        .into_proxy()
        .expect("failed to create directory proxy");
    fasync::spawn(async move {
        let _ = await!(sub_dir);
    });

    move |_abs_moniker: AbsoluteMoniker, capability: Capability| {
        // Create a DirectoryProxy for use by every route function we stamp out.
        let (client_chan, server_chan) = zx::Channel::create().unwrap();
        let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;
        sub_dir_proxy
            .clone(flags, ServerEnd::<NodeMarker>::new(server_chan.into()))
            .expect("Unable to clone root directory.");
        let dir = ClientEnd::<DirectoryMarker>::new(client_chan)
            .into_proxy()
            .expect("failed to create directory proxy");
        let capability = capability.clone();
        Box::new(
            move |flags: u32,
                  mode: u32,
                  relative_path: String,
                  server_end: ServerEnd<NodeMarker>| {
                serve_capability(flags, mode, relative_path, server_end, &dir, &capability);
            },
        )
    }
}

fn serve_capability(
    flags: u32,
    mode: u32,
    relative_path: String,
    server_end: ServerEnd<NodeMarker>,
    dir: &DirectoryProxy,
    capability: &Capability,
) {
    match capability {
        Capability::Service(_) => {
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
        Capability::Directory(_) => {
            if !relative_path.is_empty() {
                dir.open(flags, mode, &relative_path, server_end).expect("Unable to open 'dir'.");
            } else {
                dir.clone(flags, server_end).expect("Unable to clone 'dir'.");
            }
        }
        _ => {
            panic!("unexpected capability type");
        }
    }
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
    fn resolve(&self, component_url: &str) -> FutureObj<Result<fsys::Component, ResolverError>> {
        FutureObj::new(Box::new(self.resolve_async(component_url.to_string())))
    }
}

pub struct MockRunner {
    pub urls_run: Arc<Mutex<Vec<String>>>,
    pub namespaces: Namespaces,
    pub host_fns: HashMap<String, Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync>>,
    pub runtime_host_fns: HashMap<String, Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync>>,
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

    async fn start_async(&self, start_info: fsys::ComponentStartInfo) -> Result<(), RunnerError> {
        let resolved_url = start_info.resolved_url.unwrap();
        if self.failing_urls.contains(&resolved_url) {
            return Err(RunnerError::component_launch_error(resolved_url, format_err!("ouch")));
        }
        await!(self.urls_run.lock()).push(resolved_url.clone());
        await!(self.namespaces.lock()).insert(resolved_url.clone(), start_info.ns.unwrap());
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
    fn start(&self, start_info: fsys::ComponentStartInfo) -> FutureObj<Result<(), RunnerError>> {
        FutureObj::new(Box::new(self.start_async(start_info)))
    }
}

pub struct MockAmbientEnvironment {
    /// List of calls to `BindChild` with component's relative moniker.
    pub bind_calls: Arc<Mutex<Vec<String>>>,
}

impl AmbientEnvironment for MockAmbientEnvironment {
    fn serve_realm_service(
        &self,
        _model: Model,
        realm: Arc<Realm>,
        stream: fsys::RealmRequestStream,
    ) -> FutureObj<Result<(), AmbientError>> {
        FutureObj::new(Box::new(async move {
            await!(self.do_serve_realm_service(realm, stream))
                .expect(&format!("serving {} failed", REALM_SERVICE.to_string()));
            Ok(())
        }))
    }
}

impl MockAmbientEnvironment {
    pub fn new() -> Self {
        MockAmbientEnvironment { bind_calls: Arc::new(Mutex::new(vec![])) }
    }

    async fn do_serve_realm_service(
        &self,
        realm: Arc<Realm>,
        mut stream: fsys::RealmRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = await!(stream.try_next())? {
            match request {
                fsys::RealmRequest::BindChild { responder, .. } => {
                    await!(self.bind_calls.lock()).push(
                        realm
                            .abs_moniker
                            .path()
                            .last()
                            .expect("did not expect root component")
                            .name()
                            .to_string(),
                    );
                    responder.send(&mut Ok(()))?;
                }
                _ => {}
            }
        }
        Ok(())
    }
}
