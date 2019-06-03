// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::ComponentDecl,
    failure::{format_err, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys,
    futures::future::FutureObj,
    futures::lock::Mutex,
    futures::prelude::*,
    std::{collections::HashMap, convert::TryFrom, sync::Arc},
};

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
        let decl = self.components.get(name).ok_or(
            ResolverError::component_not_available(
                name.to_string(),
                format_err!("not in the hashmap"),
            ),
        )?;
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
    pub namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    pub host_fns: HashMap<String, Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync>>,
}

impl MockRunner {
    pub fn new() -> Self {
        MockRunner {
            urls_run: Arc::new(Mutex::new(vec![])),
            namespaces: Arc::new(Mutex::new(HashMap::new())),
            host_fns: HashMap::new(),
        }
    }

    async fn start_async(&self, start_info: fsys::ComponentStartInfo) -> Result<(), RunnerError> {
        let resolved_url = start_info.resolved_url.unwrap();
        await!(self.urls_run.lock()).push(resolved_url.clone());
        await!(self.namespaces.lock()).insert(resolved_url.clone(), start_info.ns.unwrap());
        // If no host_fn was provided, then start_info.outgoing_dir will be
        // automatically closed once it goes out of scope at the end of this
        // function.
        let host_fn = self.host_fns.get(&resolved_url);
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

pub struct MockAmbientEnvironment {
    /// List of calls to `BindChild` with component's relative moniker.
    pub bind_calls: Arc<Mutex<Vec<String>>>,
}

impl AmbientEnvironment for MockAmbientEnvironment {
    fn serve_realm_service(
        &self,
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
