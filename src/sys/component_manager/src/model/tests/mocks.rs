// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::ComponentDecl,
    failure::format_err,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys,
    futures::future::FutureObj,
    futures::lock::Mutex,
    lazy_static::lazy_static,
    regex::Regex,
    std::{collections::HashMap, convert::TryFrom, sync::Arc},
};

pub struct MockResolver {
    components: HashMap<String, ComponentDecl>,
}

lazy_static! {
    static ref NAME_RE: Regex = Regex::new(r"test:///([0-9a-z\-\._]+)$").unwrap();
}

impl MockResolver {
    pub fn new() -> MockResolver {
        MockResolver { components: HashMap::new() }
    }

    async fn resolve_async(&self, component_uri: String) -> Result<fsys::Component, ResolverError> {
        let caps = NAME_RE.captures(&component_uri).unwrap();
        let name = &caps[1];
        let decl = self.components.get(name.clone()).ok_or(
            ResolverError::component_not_available(name, format_err!("not in the hashmap")),
        )?;
        let fsys_decl =
            fsys::ComponentDecl::try_from(decl.clone()).expect("decl failed conversion");
        Ok(fsys::Component {
            resolved_uri: Some(format!("test:///{}_resolved", name)),
            decl: Some(fsys_decl),
            package: None,
        })
    }

    pub fn add_component(&mut self, name: &str, component: ComponentDecl) {
        self.components.insert(name.to_string(), component);
    }
}

impl Resolver for MockResolver {
    fn resolve(&self, component_uri: &str) -> FutureObj<Result<fsys::Component, ResolverError>> {
        FutureObj::new(Box::new(self.resolve_async(component_uri.to_string())))
    }
}

pub struct MockRunner {
    pub uris_run: Arc<Mutex<Vec<String>>>,
    pub namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    pub host_fns: HashMap<String, Box<Fn(ServerEnd<DirectoryMarker>) + Send + Sync>>,
}

impl MockRunner {
    pub fn new() -> MockRunner {
        MockRunner {
            uris_run: Arc::new(Mutex::new(vec![])),
            namespaces: Arc::new(Mutex::new(HashMap::new())),
            host_fns: HashMap::new(),
        }
    }

    async fn start_async(&self, start_info: fsys::ComponentStartInfo) -> Result<(), RunnerError> {
        let resolved_uri = start_info.resolved_uri.unwrap();
        await!(self.uris_run.lock()).push(resolved_uri.clone());
        await!(self.namespaces.lock()).insert(resolved_uri.clone(), start_info.ns.unwrap());
        // If no host_fn was provided, then start_info.outgoing_dir will be
        // automatically closed once it goes out of scope at the end of this
        // function.
        let host_fn = self.host_fns.get(&resolved_uri);
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
