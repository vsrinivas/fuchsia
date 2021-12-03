// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    cm_rust::{FidlIntoNative, NativeIntoFidl},
    fidl::endpoints::{ProtocolMarker, RequestStream, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fcdecl,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_component_test as ftest,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio, fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::server as fserver,
    fuchsia_zircon_status as zx_status,
    futures::{future::BoxFuture, join, lock::Mutex, FutureExt, StreamExt, TryStreamExt},
    io_util,
    lazy_static::lazy_static,
    std::{
        collections::HashMap,
        convert::{TryFrom, TryInto},
        fmt::{self, Display},
        ops::{Deref, DerefMut},
        path::PathBuf,
        sync::Arc,
    },
    thiserror::{self, Error},
    tracing::*,
    url::Url,
    vfs::execution_scope::ExecutionScope,
};

mod resolver;
mod runner;

lazy_static! {
    pub static ref BINDER_PROTOCOL_CAPABILITY: ftest::Capability =
        ftest::Capability::Protocol(ftest::ProtocolCapability {
            name: Some(fcomponent::BinderMarker::DEBUG_NAME.to_owned()),
            ..ftest::ProtocolCapability::EMPTY
        });
    pub static ref BINDER_EXPOSE_DECL: cm_rust::ExposeDecl =
        cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
            source: cm_rust::ExposeSource::Framework,
            source_name: fcomponent::BinderMarker::DEBUG_NAME.into(),
            target: cm_rust::ExposeTarget::Parent,
            target_name: fcomponent::BinderMarker::DEBUG_NAME.into(),
        },);
}

#[fuchsia::component]
async fn main() {
    info!("started");

    let mut fs = fserver::ServiceFs::new_local();
    let registry = resolver::Registry::new();
    let runner = runner::Runner::new();

    let registry_clone = registry.clone();
    fs.dir("svc").add_fidl_service(move |stream| registry_clone.run_resolver_service(stream));

    let runner_clone = runner.clone();
    fs.dir("svc").add_fidl_service(move |stream| runner_clone.run_runner_service(stream));

    let execution_scope = ExecutionScope::new();

    let registry_clone = registry.clone();
    let runner_clone = runner.clone();
    let execution_scope_clone = execution_scope.clone();
    fs.dir("svc").add_fidl_service(move |stream| {
        let registry = registry_clone.clone();
        let runner = runner_clone.clone();
        execution_scope_clone.spawn(async move {
            if let Err(e) = handle_realm_builder_stream(stream, registry, runner).await {
                error!("error encountered while running realm builder service: {:?}", e);
            }
        });
    });

    let execution_scope_clone = execution_scope.clone();
    fs.dir("svc").add_fidl_service(move |stream| {
        let registry = registry.clone();
        let runner = runner.clone();
        let execution_scope_clone_2 = execution_scope_clone.clone();
        execution_scope_clone.spawn(async move {
            if let Err(e) = handle_realm_builder_factory_stream(
                stream,
                registry,
                runner,
                execution_scope_clone_2,
            )
            .await
            {
                error!("error encountered while running realm builder service: {:?}", e);
            }
        });
    });

    fs.take_and_serve_directory_handle().expect("did not receive directory handle");

    join!(execution_scope.wait(), fs.collect::<()>());
}

async fn handle_realm_builder_factory_stream(
    mut stream: ftest::RealmBuilderFactoryRequestStream,
    registry: Arc<resolver::Registry>,
    runner: Arc<runner::Runner>,
    execution_scope: ExecutionScope,
) -> Result<(), anyhow::Error> {
    while let Some(req) = stream.try_next().await? {
        match req {
            ftest::RealmBuilderFactoryRequest::Create {
                pkg_dir_handle,
                realm_server_end,
                builder_server_end,
                responder,
            } => {
                let new_realm = RealmNode2::new();
                let pkg_dir = pkg_dir_handle
                    .into_proxy()
                    .context("failed to convert pkg_dir ClientEnd to proxy")?;

                let runner_proxy_placeholder = Arc::new(Mutex::new(None));

                let realm_stream = realm_server_end
                    .into_stream()
                    .context("failed to convert realm_server_end to stream")?;

                let realm = Realm {
                    pkg_dir: Clone::clone(&pkg_dir),
                    realm_node: new_realm.clone(),
                    registry: registry.clone(),
                    runner: runner.clone(),
                    runner_proxy_placeholder: runner_proxy_placeholder.clone(),
                    realm_path: vec![],
                    execution_scope: execution_scope.clone(),
                };

                execution_scope.spawn(async move {
                    if let Err(e) = realm.handle_stream(realm_stream).await {
                        error!("error encountered while handling Realm requests: {:?}", e);
                    }
                });

                let builder_stream = builder_server_end
                    .into_stream()
                    .context("failed to convert builder_server_end to stream")?;

                let builder = Builder {
                    pkg_dir: Clone::clone(&pkg_dir),
                    realm_node: new_realm.clone(),
                    registry: registry.clone(),
                    runner_proxy_placeholder: runner_proxy_placeholder.clone(),
                };
                execution_scope.spawn(async move {
                    if let Err(e) = builder.handle_stream(builder_stream).await {
                        error!("error encountered while handling Builder requests: {:?}", e);
                    }
                });
                responder.send()?;
            }
        }
    }
    Ok(())
}

struct Builder {
    pkg_dir: fio::DirectoryProxy,
    realm_node: RealmNode2,
    registry: Arc<resolver::Registry>,
    runner_proxy_placeholder: Arc<Mutex<Option<fcrunner::ComponentRunnerProxy>>>,
}

impl Builder {
    async fn handle_stream(
        &self,
        mut stream: ftest::BuilderRequestStream,
    ) -> Result<(), anyhow::Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                ftest::BuilderRequest::Build { runner, responder } => {
                    let runner_proxy = runner
                        .into_proxy()
                        .context("failed to convert runner ClientEnd into proxy")?;
                    *self.runner_proxy_placeholder.lock().await = Some(runner_proxy);
                    let res = self
                        .realm_node
                        .build(self.registry.clone(), vec![], Clone::clone(&self.pkg_dir))
                        .await;
                    match res {
                        Ok(url) => responder.send(&mut Ok(url))?,
                        Err(e) => {
                            warn!("unable to build the realm the client requested: {:?}", e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

#[allow(unused)]
struct Realm {
    pkg_dir: fio::DirectoryProxy,
    realm_node: RealmNode2,
    registry: Arc<resolver::Registry>,
    runner: Arc<runner::Runner>,
    runner_proxy_placeholder: Arc<Mutex<Option<fcrunner::ComponentRunnerProxy>>>,
    realm_path: Vec<String>,
    execution_scope: ExecutionScope,
}

impl Realm {
    async fn handle_stream(
        &self,
        mut stream: ftest::RealmRequestStream,
    ) -> Result<(), anyhow::Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                ftest::RealmRequest::AddChild { name, url, options, responder } => {
                    match self.add_child(name.clone(), url.clone(), options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!(
                                "unable to add child {:?} with url {:?} to realm: {:?}",
                                name, url, e
                            );
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddLegacyChild { name, legacy_url, options, responder } => {
                    match self.add_legacy_child(name.clone(), legacy_url.clone(), options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!(
                                "unable to add legacy child {:?} with url {:?} to realm: {:?}",
                                name, legacy_url, e
                            );
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddChildFromDecl { name, decl, options, responder } => {
                    match self.add_child_from_decl(name.clone(), decl, options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to add child {:?} from decl to realm: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                // TODO(88423)
                ftest::RealmRequest::AddLocalChild { name, options, responder } => {
                    match self.add_local_child(name.clone(), options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to add local child {:?} to realm: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddChildRealm { name, options, child_realm, responder } => {
                    match self.add_child_realm(name.clone(), options, child_realm).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to add child realm {:?}: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                // TODO(dgonyeo): add a test for this
                ftest::RealmRequest::GetComponentDecl { name, responder } => {
                    match self.get_component_decl(name.clone()).await {
                        Ok(decl) => responder.send(&mut Ok(decl))?,
                        Err(e) => {
                            warn!("unable to get component decl for child {:?}: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                // TODO(dgonyeo): add a test for this
                ftest::RealmRequest::ReplaceComponentDecl { name, component_decl, responder } => {
                    match self.replace_component_decl(name.clone(), component_decl).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to replace component decl for child {:?}: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddRoute { capabilities, from, to, responder } => {
                    match self.realm_node.route_capabilities(capabilities, from, to).await {
                        Ok(()) => {
                            responder.send(&mut Ok(()))?;
                        }
                        Err(e) => {
                            warn!("unable to add route: {:?}", e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
            }
        }
        Ok(())
    }

    async fn add_child(
        &self,
        name: String,
        url: String,
        options: ftest::ChildOptions,
    ) -> Result<(), RealmBuilderError> {
        if is_legacy_url(&url) {
            return Err(RealmBuilderError::InvalidManifestExtension);
        }
        if is_relative_url(&url) {
            let child_realm_node =
                RealmNode2::load_from_pkg(url, Clone::clone(&self.pkg_dir)).await?;
            self.realm_node.add_child(name.clone(), options, child_realm_node).await
        } else {
            self.realm_node.add_child_decl(name, url, options).await
        }
    }

    async fn add_legacy_child(
        &self,
        name: String,
        legacy_url: String,
        options: ftest::ChildOptions,
    ) -> Result<(), RealmBuilderError> {
        if !is_legacy_url(&legacy_url) {
            return Err(RealmBuilderError::InvalidManifestExtension);
        }
        let child_realm_node = RealmNode2::new_from_decl(
            new_decl_with_program_entries(vec![(runner::LEGACY_URL_KEY.to_string(), legacy_url)]),
            true,
        );
        self.realm_node.add_child(name, options, child_realm_node).await
    }

    async fn add_child_from_decl(
        &self,
        name: String,
        component_decl: fcdecl::Component,
        options: ftest::ChildOptions,
    ) -> Result<(), RealmBuilderError> {
        if let Err(e) = cm_fidl_validator::fdecl::validate(&component_decl) {
            return Err(RealmBuilderError::InvalidComponentDecl(name, e));
        }
        let child_realm_node = RealmNode2::new_from_decl(component_decl.fidl_into_native(), true);
        self.realm_node.add_child(name, options, child_realm_node).await
    }

    async fn add_local_child(
        &self,
        name: String,
        options: ftest::ChildOptions,
    ) -> Result<(), RealmBuilderError> {
        let local_component_id =
            self.runner.register_local_component(self.runner_proxy_placeholder.clone()).await;
        let mut child_path = self.realm_path.clone();
        child_path.push(name.clone());
        let child_realm_node = RealmNode2::new_from_decl(
            new_decl_with_program_entries(vec![
                (runner::LOCAL_COMPONENT_ID_KEY.to_string(), local_component_id.into()),
                (runner::LOCAL_COMPONENT_NAME_KEY.to_string(), child_path.join("/").to_string()),
            ]),
            true,
        );
        self.realm_node.add_child(name, options, child_realm_node).await
    }

    // `Realm::handle_stream` calls `Realm::add_child_realm` which calls `Realm::handle_stream`.
    // Cycles are not allowed in constructed futures, so we need to place this in a `BoxFuture` to
    // break the cycle.
    fn add_child_realm(
        &self,
        name: String,
        options: ftest::ChildOptions,
        child_realm_server_end: ServerEnd<ftest::RealmMarker>,
    ) -> BoxFuture<'static, Result<(), RealmBuilderError>> {
        let mut child_path = self.realm_path.clone();
        child_path.push(name.clone());

        let child_realm_node = RealmNode2::new();

        let child_realm = Realm {
            pkg_dir: Clone::clone(&self.pkg_dir),
            realm_node: child_realm_node.clone(),
            registry: self.registry.clone(),
            runner: self.runner.clone(),
            runner_proxy_placeholder: self.runner_proxy_placeholder.clone(),
            realm_path: child_path.clone(),
            execution_scope: self.execution_scope.clone(),
        };

        let self_realm_node = self.realm_node.clone();
        let self_execution_scope = self.execution_scope.clone();

        async move {
            let child_realm_stream = child_realm_server_end
                .into_stream()
                .map_err(RealmBuilderError::InvalidChildRealmHandle)?;
            self_realm_node.add_child(name, options, child_realm_node).await?;

            self_execution_scope.spawn(async move {
                if let Err(e) = child_realm.handle_stream(child_realm_stream).await {
                    error!(
                        "error encountered while handling requests for child realm {:?}: {:?}",
                        child_path.join("/"),
                        e,
                    );
                }
            });

            Ok(())
        }
        .boxed()
    }

    async fn get_component_decl(
        &self,
        name: String,
    ) -> Result<fcdecl::Component, RealmBuilderError> {
        let child_node = self.realm_node.get_sub_realm(&name).await?;
        Ok(child_node.get_decl().await.native_into_fidl())
    }

    async fn replace_component_decl(
        &self,
        name: String,
        component_decl: fcdecl::Component,
    ) -> Result<(), RealmBuilderError> {
        if let Err(e) = cm_fidl_validator::fdecl::validate(&component_decl) {
            return Err(RealmBuilderError::InvalidComponentDecl(name, e));
        }
        let child_node = self.realm_node.get_sub_realm(&name).await?;
        child_node.replace_decl(component_decl.fidl_into_native()).await
    }
}

fn new_decl_with_program_entries(entries: Vec<(String, String)>) -> cm_rust::ComponentDecl {
    cm_rust::ComponentDecl {
        program: Some(cm_rust::ProgramDecl {
            runner: Some(runner::RUNNER_NAME.try_into().unwrap()),
            info: fdata::Dictionary {
                entries: Some(
                    entries
                        .into_iter()
                        .map(|(key, val)| fdata::DictionaryEntry {
                            key: key,
                            value: Some(Box::new(fdata::DictionaryValue::Str(val))),
                        })
                        .collect(),
                ),
                ..fdata::Dictionary::EMPTY
            },
        }),
        ..cm_rust::ComponentDecl::default()
    }
}

#[derive(Debug, Clone, Default)]
struct RealmNodeState {
    decl: cm_rust::ComponentDecl,

    /// Children stored in this HashMap can be mutated. Children stored in `decl.children` can not.
    /// Any children stored in `mutable_children` do NOT have a corresponding `ChildDecl` stored in
    /// `decl.children`, the two should be fully mutually exclusive.
    ///
    /// Suitable `ChildDecl`s for the contents of `mutable_children` are generated and added to
    /// `decl.children` when `commit()` is called.
    mutable_children: HashMap<String, (ftest::ChildOptions, RealmNode2)>,

    // TODO: comment better
    // set to true when this node has been processed by Builder.Build
    finalized: bool,
}

impl RealmNodeState {
    // Returns true if a child with the given name exists either as a mutable child or as a
    // ChildDecl in this node's ComponentDecl.
    fn contains_child(&self, child_name: &String) -> bool {
        self.decl.children.iter().any(|c| &c.name == child_name)
            || self.mutable_children.contains_key(child_name)
    }

    fn add_child_decl(
        &mut self,
        child_name: String,
        child_url: String,
        child_options: ftest::ChildOptions,
    ) {
        self.decl.children.push(cm_rust::ChildDecl {
            name: child_name,
            url: child_url,
            startup: match child_options.startup {
                Some(fcdecl::StartupMode::Lazy) => fsys::StartupMode::Lazy,
                Some(fcdecl::StartupMode::Eager) => fsys::StartupMode::Eager,
                None => fsys::StartupMode::Lazy,
            },
            environment: child_options.environment,
            on_terminate: match child_options.on_terminate {
                Some(fcdecl::OnTerminate::None) => Some(fsys::OnTerminate::None),
                Some(fcdecl::OnTerminate::Reboot) => Some(fsys::OnTerminate::Reboot),
                None => None,
            },
        });
    }

    // Returns children whose manifest must be updated during invocations to
    // AddRoute.
    fn get_updateable_children(&mut self) -> HashMap<String, &mut RealmNode2> {
        self.mutable_children
            .iter_mut()
            .map(|(key, (_options, child))| (key.clone(), child))
            .filter(|(_k, c)| c.update_decl_in_add_route)
            .collect::<HashMap<_, _>>()
    }
}

#[derive(Debug, Clone)]
struct RealmNode2 {
    state: Arc<Mutex<RealmNodeState>>,

    /// We shouldn't mutate component decls that are loaded from the test package. Track the source
    /// of this component declaration here, so we know when to treat it as immutable.
    component_loaded_from_pkg: bool,

    /// Flag used to determine if this component's manifest should be updated
    /// when a capability is routed to or from it during invocations of AddRoute.
    pub update_decl_in_add_route: bool,
}

impl RealmNode2 {
    fn new() -> Self {
        Self {
            state: Arc::new(Mutex::new(RealmNodeState::default())),
            component_loaded_from_pkg: false,
            update_decl_in_add_route: false,
        }
    }

    fn new_from_decl(decl: cm_rust::ComponentDecl, update_decl_in_add_route: bool) -> Self {
        Self {
            state: Arc::new(Mutex::new(RealmNodeState { decl, ..RealmNodeState::default() })),
            component_loaded_from_pkg: false,
            update_decl_in_add_route,
        }
    }

    async fn get_decl(&self) -> cm_rust::ComponentDecl {
        self.state.lock().await.decl.clone()
    }

    async fn replace_decl(
        &self,
        new_decl: cm_rust::ComponentDecl,
    ) -> Result<(), RealmBuilderError> {
        let mut state_guard = self.state.lock().await;
        if state_guard.finalized {
            return Err(RealmBuilderError::BuildAlreadyCalled);
        }
        for child in &new_decl.children {
            if state_guard.contains_child(&child.name) {
                return Err(RealmBuilderError::ChildAlreadyExists(child.name.clone()));
            }
        }
        if state_guard.decl.program.as_ref().and_then(|p| p.runner.as_ref())
            == Some(&runner::RUNNER_NAME.into())
        {
            if state_guard.decl.program != new_decl.program {
                return Err(RealmBuilderError::ImmutableProgram);
            }
        }
        state_guard.decl = new_decl;
        Ok(())
    }

    async fn add_child(
        &self,
        child_name: String,
        child_options: ftest::ChildOptions,
        node: RealmNode2,
    ) -> Result<(), RealmBuilderError> {
        let mut state_guard = self.state.lock().await;
        if state_guard.finalized {
            return Err(RealmBuilderError::BuildAlreadyCalled);
        }
        if state_guard.contains_child(&child_name) {
            return Err(RealmBuilderError::ChildAlreadyExists(child_name));
        }
        state_guard.mutable_children.insert(child_name, (child_options, node));
        Ok(())
    }

    async fn add_child_decl(
        &self,
        child_name: String,
        child_url: String,
        child_options: ftest::ChildOptions,
    ) -> Result<(), RealmBuilderError> {
        let mut state_guard = self.state.lock().await;
        if state_guard.finalized {
            return Err(RealmBuilderError::BuildAlreadyCalled);
        }
        if state_guard.contains_child(&child_name) {
            return Err(RealmBuilderError::ChildAlreadyExists(child_name));
        }
        state_guard.add_child_decl(child_name, child_url, child_options);
        Ok(())
    }

    fn load_from_pkg(
        relative_url: String,
        test_pkg_dir: fio::DirectoryProxy,
    ) -> BoxFuture<'static, Result<RealmNode2, RealmBuilderError>> {
        async move {
            let path = relative_url.trim_start_matches('#');

            let file_proxy_res =
                io_util::directory::open_file(&test_pkg_dir, &path, io_util::OPEN_RIGHT_READABLE)
                    .await;
            let file_proxy = match file_proxy_res {
                Ok(file_proxy) => file_proxy,
                Err(io_util::node::OpenError::OpenError(zx_status::Status::NOT_FOUND)) => {
                    return Err(RealmBuilderError::DeclNotFound(relative_url.clone()))
                }
                Err(e) => {
                    return Err(RealmBuilderError::DeclReadError(relative_url.clone(), e.into()))
                }
            };

            let fidl_decl = io_util::read_file_fidl::<fcdecl::Component>(&file_proxy)
                .await
                .map_err(|e| RealmBuilderError::DeclReadError(relative_url.clone(), e))?;
            cm_fidl_validator::fdecl::validate(&fidl_decl)
                .map_err(|e| RealmBuilderError::InvalidComponentDecl(relative_url, e))?;

            let mut self_ = RealmNode2::new_from_decl(fidl_decl.fidl_into_native(), false);
            self_.component_loaded_from_pkg = true;
            let mut state_guard = self_.state.lock().await;

            let children = state_guard.decl.children.drain(..).collect::<Vec<_>>();
            for child in children {
                if !is_relative_url(&child.url) {
                    state_guard.decl.children.push(child);
                } else {
                    let child_node =
                        RealmNode2::load_from_pkg(child.url, Clone::clone(&test_pkg_dir)).await?;
                    let child_options = ftest::ChildOptions {
                        startup: match child.startup {
                            fsys::StartupMode::Lazy => Some(fcdecl::StartupMode::Lazy),
                            fsys::StartupMode::Eager => Some(fcdecl::StartupMode::Eager),
                        },
                        environment: child.environment,
                        on_terminate: match child.on_terminate {
                            Some(fsys::OnTerminate::None) => Some(fcdecl::OnTerminate::None),
                            Some(fsys::OnTerminate::Reboot) => Some(fcdecl::OnTerminate::Reboot),
                            None => None,
                        },
                        ..ftest::ChildOptions::EMPTY
                    };
                    state_guard.mutable_children.insert(child.name, (child_options, child_node));
                }
            }

            drop(state_guard);
            Ok(self_)
        }
        .boxed()
    }

    async fn get_sub_realm(&self, child_name: &String) -> Result<RealmNode2, RealmBuilderError> {
        let state_guard = self.state.lock().await;
        if state_guard.decl.children.iter().any(|c| &c.name == child_name) {
            return Err(RealmBuilderError::ChildDeclNotVisible(child_name.clone()));
        }
        state_guard
            .mutable_children
            .get(child_name)
            .cloned()
            .map(|(_, r)| r)
            .ok_or(RealmBuilderError::NoSuchChild(child_name.clone()))
    }

    async fn route_capabilities(
        &self,
        capabilities: Vec<ftest::Capability2>,
        from: fcdecl::Ref,
        to: Vec<fcdecl::Ref>,
    ) -> Result<(), RealmBuilderError> {
        if capabilities.is_empty() {
            return Err(RealmBuilderError::CapabilitiesEmpty);
        }

        let mut state_guard = self.state.lock().await;
        if !contains_child(state_guard.deref(), &from) {
            return Err(RealmBuilderError::NoSuchSource(from));
        }

        for capability in capabilities {
            for target in &to {
                if &from == target {
                    return Err(RealmBuilderError::SourceAndTargetMatch(from));
                }

                if !contains_child(state_guard.deref(), target) {
                    return Err(RealmBuilderError::NoSuchTarget(target.clone()));
                }

                if is_parent_ref(&target) {
                    let decl = create_expose_decl(capability.clone(), from.clone())?;
                    push_if_not_present(&mut state_guard.decl.exposes, decl);
                } else {
                    let decl = create_offer_decl(capability.clone(), from.clone(), target.clone())?;
                    push_if_not_present(&mut state_guard.decl.offers, decl);
                }

                let () = add_use_decl_if_needed(
                    state_guard.deref_mut(),
                    target.clone(),
                    capability.clone(),
                )
                .await?;
            }

            let () = add_expose_decl_if_needed(
                state_guard.deref_mut(),
                from.clone(),
                capability.clone(),
            )
            .await?;
        }

        Ok(())
    }

    fn build(
        &self,
        registry: Arc<resolver::Registry>,
        walked_path: Vec<String>,
        package_dir: fio::DirectoryProxy,
    ) -> BoxFuture<'static, Result<String, RealmBuilderError>> {
        // This function is much cleaner written recursively, but we can't construct recursive
        // futures as the size isn't knowable to rustc at compile time. Put the recursive call
        // into a boxed future, as the redirection makes this possible
        let self_state = self.state.clone();
        async move {
            let mut state_guard = self_state.lock().await;
            if state_guard.finalized {
                return Err(RealmBuilderError::BuildAlreadyCalled);
            }
            state_guard.finalized = true;
            // Expose the fuchsia.component.Binder protocol from root in order to give users the ability to manually
            // start the realm.
            if walked_path.is_empty() {
                state_guard.decl.exposes.push(BINDER_EXPOSE_DECL.clone());
            }

            let mut mutable_children = state_guard.mutable_children.drain().collect::<Vec<_>>();
            mutable_children.sort_unstable_by_key(|t| t.0.clone());
            for (child_name, (child_options, node)) in mutable_children {
                let mut new_path = walked_path.clone();
                new_path.push(child_name.clone());

                let child_url =
                    node.build(registry.clone(), new_path, Clone::clone(&package_dir)).await?;
                state_guard.add_child_decl(child_name, child_url, child_options);
            }

            let name =
                if walked_path.is_empty() { "root".to_string() } else { walked_path.join("-") };
            let decl = state_guard.decl.clone().native_into_fidl();
            match registry.validate_and_register(decl, name, Some(Clone::clone(&package_dir))).await
            {
                Ok(url) => Ok(url),
                Err(e) => {
                    warn!(
                        "manifest validation failed during build step for component {:?}: {:?}",
                        walked_path, e
                    );
                    Err(RealmBuilderError::InvalidComponentDecl(walked_path.join("/"), e))
                }
            }
        }
        .boxed()
    }
}

async fn add_use_decl_if_needed(
    realm: &mut RealmNodeState,
    ref_: fcdecl::Ref,
    capability: ftest::Capability2,
) -> Result<(), RealmBuilderError> {
    if let fcdecl::Ref::Child(child) = ref_ {
        if let Some(child) = realm.get_updateable_children().get(&child.name) {
            let mut decl = child.get_decl().await;
            push_if_not_present(&mut decl.uses, create_use_decl(capability)?);
            let () = child.replace_decl(decl).await?;
        }
    }

    Ok(())
}

async fn add_expose_decl_if_needed(
    realm: &mut RealmNodeState,
    ref_: fcdecl::Ref,
    capability: ftest::Capability2,
) -> Result<(), RealmBuilderError> {
    if let fcdecl::Ref::Child(child) = ref_ {
        if let Some(child) = realm.get_updateable_children().get(&child.name) {
            let mut decl = child.get_decl().await;
            push_if_not_present(
                &mut decl.capabilities,
                create_capability_decl(capability.clone())?,
            );
            push_if_not_present(
                &mut decl.exposes,
                create_expose_decl(capability, fcdecl::Ref::Self_(fcdecl::SelfRef {}))?,
            );
            let () = child.replace_decl(decl).await?;
        }
    }

    Ok(())
}

fn create_capability_decl(
    capability: ftest::Capability2,
) -> Result<cm_rust::CapabilityDecl, RealmBuilderError> {
    Ok(match capability {
        ftest::Capability2::Protocol(protocol) => {
            let name = protocol.name.as_ref().ok_or(RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!("capability `name` received was empty: {:?}", protocol.clone()),
            ))?;
            cm_rust::CapabilityDecl::Protocol(cm_rust::ProtocolDecl {
                name: cm_rust::CapabilityName(name.clone()),
                source_path: Some(to_capability_path(protocol.as_, "/svc", name.clone())?),
            })
        }
        ftest::Capability2::Directory(directory) => {
            let name = directory.name.as_ref().ok_or(RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!(
                    "capability `name` received was empty: {:?}",
                    directory.clone()
                ),
            ))?;
            cm_rust::CapabilityDecl::Directory(cm_rust::DirectoryDecl {
                name: cm_rust::CapabilityName(name.clone()),
                source_path: Some(to_capability_path(directory.as_, "/", name.clone())?),
                rights: directory.rights.unwrap_or(fio2::RW_STAR_DIR),
            })
        }
        _ => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "encountered unsupported capability variant: {:?}",
                capability.clone()
            )));
        }
    })
}

fn create_offer_decl(
    capability: ftest::Capability2,
    source: fcdecl::Ref,
    target: fcdecl::Ref,
) -> Result<cm_rust::OfferDecl, RealmBuilderError> {
    let source: cm_rust::OfferSource = source.fidl_into_native();
    let target: cm_rust::OfferTarget = target.fidl_into_native();

    Ok(match capability {
        ftest::Capability2::Protocol(protocol) => {
            let name = protocol.name.as_ref().ok_or(RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!("capability `name` received was empty: {:?}", protocol.clone()),
            ))?;
            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source,
                source_name: cm_rust::CapabilityName(name.clone()),
                target,
                target_name: cm_rust::CapabilityName(protocol.as_.unwrap_or(name.clone())),
                dependency_type: cm_rust::DependencyType::Strong,
            })
        }
        ftest::Capability2::Directory(directory) => {
            let name = directory.name.as_ref().ok_or(RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!(
                    "capability `name` received was empty: {:?}",
                    directory.clone()
                ),
            ))?;
            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                source,
                source_name: cm_rust::CapabilityName(name.clone()),
                target,
                target_name: cm_rust::CapabilityName(directory.as_.unwrap_or(name.clone())),
                rights: directory.rights,
                subdir: directory.subdir.map(PathBuf::from),
                dependency_type: cm_rust::DependencyType::Strong,
            })
        }
        _ => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "encountered unsupported capability variant: {:?}",
                capability.clone()
            )));
        }
    })
}

fn create_expose_decl(
    capability: ftest::Capability2,
    source: fcdecl::Ref,
) -> Result<cm_rust::ExposeDecl, RealmBuilderError> {
    let source: cm_rust::ExposeSource = source.fidl_into_native();

    Ok(match capability {
        ftest::Capability2::Protocol(protocol) => {
            let name = protocol.name.as_ref().ok_or(RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!("capability `name` received was empty: {:?}", protocol.clone()),
            ))?;
            cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                source: source.clone(),
                source_name: cm_rust::CapabilityName(name.clone()),
                target: cm_rust::ExposeTarget::Parent,
                target_name: cm_rust::CapabilityName(protocol.as_.unwrap_or(name.clone())),
            })
        }
        ftest::Capability2::Directory(directory) => {
            let name = directory.name.as_ref().ok_or(RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!(
                    "capability `name` received was empty: {:?}",
                    directory.clone()
                ),
            ))?;
            cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                source: source.clone(),
                source_name: cm_rust::CapabilityName(name.clone()),
                target: cm_rust::ExposeTarget::Parent,
                target_name: cm_rust::CapabilityName(directory.as_.unwrap_or(name.clone())),
                rights: directory.rights,
                subdir: directory.subdir.map(PathBuf::from),
            })
        }
        _ => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "encountered unsupported capability variant: {:?}",
                capability.clone()
            )));
        }
    })
}

fn create_use_decl(capability: ftest::Capability2) -> Result<cm_rust::UseDecl, RealmBuilderError> {
    Ok(match capability {
        ftest::Capability2::Protocol(protocol) => {
            let name = protocol.name.as_ref().ok_or(RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!("capability `name` received was empty: {:?}", protocol.clone()),
            ))?;
            cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                source: cm_rust::UseSource::Parent,
                source_name: cm_rust::CapabilityName(name.clone()),
                target_path: to_capability_path(protocol.as_, "/svc", name.clone())?,
                dependency_type: cm_rust::DependencyType::Strong,
            })
        }
        ftest::Capability2::Directory(directory) => {
            let name = directory.name.as_ref().ok_or(RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!(
                    "capability `name` received was empty: {:?}",
                    directory.clone()
                ),
            ))?;
            cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
                source: cm_rust::UseSource::Parent,
                source_name: cm_rust::CapabilityName(name.clone()),
                target_path: to_capability_path(directory.as_, "/", name.clone())?,
                rights: directory.rights.unwrap_or(fio2::RW_STAR_DIR),
                subdir: directory.subdir.map(PathBuf::from),
                dependency_type: cm_rust::DependencyType::Strong,
            })
        }
        _ => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "encountered unsupported capability variant: {:?}",
                capability.clone()
            )));
        }
    })
}

fn contains_child(realm: &RealmNodeState, ref_: &fcdecl::Ref) -> bool {
    match ref_ {
        fcdecl::Ref::Child(child) => {
            let children = realm
                .decl
                .children
                .iter()
                .map(|c| c.name.clone())
                .chain(realm.mutable_children.iter().map(|(name, _)| name.clone()))
                .collect::<Vec<_>>();
            children.contains(&child.name)
        }
        _ => true,
    }
}

fn is_parent_ref(ref_: &fcdecl::Ref) -> bool {
    match ref_ {
        fcdecl::Ref::Parent(_) => true,
        _ => false,
    }
}

fn push_if_not_present<T: PartialEq>(container: &mut Vec<T>, value: T) {
    if !container.contains(&value) {
        container.push(value);
    }
}

fn to_capability_path(
    as_: Option<String>,
    dirname: &str,
    default: String,
) -> Result<cm_rust::CapabilityPath, RealmBuilderError> {
    let path = format!("{}/{}", dirname, as_.unwrap_or(default));
    path.as_str().try_into().map_err(|e| {
        RealmBuilderError::CapabilityInvalid(anyhow::format_err!("invalid_path: {:?}", e))
    })
}

async fn handle_realm_builder_stream(
    mut stream: ftest::RealmBuilderRequestStream,
    registry: Arc<resolver::Registry>,
    runner: Arc<runner::Runner>,
) -> Result<(), anyhow::Error> {
    let mut realm_tree = RealmNode::default();
    let mut test_pkg_dir = None;
    while let Some(req) = stream.try_next().await? {
        match req {
            ftest::RealmBuilderRequest::Init { pkg_dir_handle, responder } => {
                if test_pkg_dir.is_some() {
                    responder.send(&mut Err(Error::PkgDirAlreadySet.log_and_convert()))?;
                } else {
                    test_pkg_dir = Some(
                        pkg_dir_handle.into_proxy().expect("failed to convert ClientEnd to proxy"),
                    );
                    responder.send(&mut Ok(()))?;
                }
            }
            ftest::RealmBuilderRequest::SetComponent { moniker, component, responder } => {
                match realm_tree
                    .set_component(moniker.clone().into(), component.clone(), &test_pkg_dir)
                    .await
                {
                    Ok(()) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        warn!(
                            "error occurred when setting component {:?} to {:?}",
                            moniker, component
                        );
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
            ftest::RealmBuilderRequest::SetMockComponent { moniker, responder } => {
                let mock_id = runner.register_mock(stream.control_handle()).await;
                match realm_tree.set_mock_component(moniker.clone().into(), mock_id.clone()).await {
                    Ok(()) => responder.send(&mut Ok(mock_id.into()))?,
                    Err(e) => {
                        warn!("error occurred when setting mock component {:?}: {:?}", moniker, e);
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
            ftest::RealmBuilderRequest::GetComponentDecl { moniker, responder } => {
                match realm_tree.get_component_decl(moniker.clone().into()) {
                    Ok(decl) => responder.send(&mut Ok(decl.native_into_fidl()))?,
                    Err(e) => {
                        warn!("error occurred when getting decl for component {:?}", moniker);
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
            ftest::RealmBuilderRequest::RouteCapability { route, responder } => {
                match realm_tree.route_capability(route.clone()) {
                    Ok(()) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        warn!("error occurred when routing capability: {:?}", route);
                        responder.send(&mut Err(e.log_and_convert()))?
                    }
                }
            }
            ftest::RealmBuilderRequest::MarkAsEager { moniker, responder } => {
                match realm_tree.mark_as_eager(moniker.clone().into()) {
                    Ok(()) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        warn!("error occurred when marking {:?} as eager", moniker);
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
            ftest::RealmBuilderRequest::Contains { moniker, responder } => {
                responder.send(realm_tree.contains(moniker.clone().into()))?;
            }
            ftest::RealmBuilderRequest::Commit { responder } => {
                match realm_tree
                    .clone()
                    .commit(registry.clone(), vec![], test_pkg_dir.clone())
                    .await
                {
                    Ok(url) => responder.send(&mut Ok(url))?,
                    Err(e) => {
                        warn!("error occurred when committing");
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
        }
    }
    Ok(())
}

#[allow(unused)]
#[derive(Debug, Error)]
enum RealmBuilderError {
    /// Child cannot be added to the realm, as there is already a child in the realm with that
    /// name.
    #[error("unable to add child to the realm because a child already exists with the name {0:?}")]
    ChildAlreadyExists(String),

    /// A legacy component URL was given to `AddChild`, or a modern component url was given to
    /// `AddLegacyChild`.
    #[error("manifest extension was inappropriate for the function, use AddChild for `.cm` URLs and AddLegacyChild for `.cmx` URLs")]
    InvalidManifestExtension,

    /// A component declaration failed validation.
    #[error("the manifest for component {0:?} failed validation: {1:?}")]
    InvalidComponentDecl(String, cm_fidl_validator::error::ErrorList),

    /// The referenced child does not exist.
    #[error("there is no component named {0:?} in this realm")]
    NoSuchChild(String),

    /// The component declaration for the referenced child cannot be viewed nor manipulated by
    /// RealmBuilder because the child was added to the realm using an URL that was neither a
    /// relative nor a legacy URL.
    #[error("the component declaration for {0:?} cannot be viewed nor manipulated")]
    ChildDeclNotVisible(String),

    /// The source does not exist.
    #[error("the source for the route does not exist: {0:?}")]
    NoSuchSource(fcdecl::Ref),

    /// A target does not exist.
    #[error("the target for the route does not exist: {0:?}")]
    NoSuchTarget(fcdecl::Ref),

    /// The `capabilities` field is empty.
    #[error("the \"capabilities\" field is empty")]
    CapabilitiesEmpty,

    /// The `targets` field is empty.
    #[error("the \"targets\" field is empty")]
    TargetsEmpty,

    /// The `from` value is equal to one of the elements in `to`.
    #[error("one of the targets is equal to the source: {0:?}")]
    SourceAndTargetMatch(fcdecl::Ref),

    /// The test package does not contain the component declaration referenced by a relative URL.
    #[error("the test package does not contain this component: {0:?}")]
    DeclNotFound(String),

    /// Encountered an I/O error when attempting to read a component declaration referenced by a
    /// relative URL from the test package.
    #[error("failed to read the manifest for {0:?} from the test package: {1:?}")]
    DeclReadError(String, anyhow::Error),

    /// The `Build` function has been called multiple times on this channel.
    #[error("the build function was called multiple times")]
    BuildAlreadyCalled,

    #[error("invalid capability received: {0:?}")]
    CapabilityInvalid(anyhow::Error),

    /// The handle the client provided is not usable
    #[error("unable to use the provided handle: {0:?}")]
    InvalidChildRealmHandle(fidl::Error),

    /// `ReplaceComponentDecl` was called on a legacy or local component with a program declaration
    /// that did not match the one from the old component declaration. This could render a legacy
    /// or local component non-functional, and is disallowed.
    #[error(
        "the program section of the manifest for a legacy or local component cannot be changed"
    )]
    ImmutableProgram,
}

impl From<RealmBuilderError> for ftest::RealmBuilderError2 {
    fn from(err: RealmBuilderError) -> Self {
        match err {
            RealmBuilderError::ChildAlreadyExists(_) => Self::ChildAlreadyExists,
            RealmBuilderError::InvalidManifestExtension => Self::InvalidManifestExtension,
            RealmBuilderError::InvalidComponentDecl(_, _) => Self::InvalidComponentDecl,
            RealmBuilderError::NoSuchChild(_) => Self::NoSuchChild,
            RealmBuilderError::ChildDeclNotVisible(_) => Self::ChildDeclNotVisible,
            RealmBuilderError::NoSuchSource(_) => Self::NoSuchSource,
            RealmBuilderError::NoSuchTarget(_) => Self::NoSuchTarget,
            RealmBuilderError::CapabilitiesEmpty => Self::CapabilitiesEmpty,
            RealmBuilderError::TargetsEmpty => Self::TargetsEmpty,
            RealmBuilderError::SourceAndTargetMatch(_) => Self::SourceAndTargetMatch,
            RealmBuilderError::DeclNotFound(_) => Self::DeclNotFound,
            RealmBuilderError::DeclReadError(_, _) => Self::DeclReadError,
            RealmBuilderError::BuildAlreadyCalled => Self::BuildAlreadyCalled,
            RealmBuilderError::CapabilityInvalid(_) => Self::CapabilityInvalid,
            RealmBuilderError::InvalidChildRealmHandle(_) => Self::InvalidChildRealmHandle,
            RealmBuilderError::ImmutableProgram => Self::ImmutableProgram,
        }
    }
}

#[derive(Debug, Error)]
enum Error {
    #[error("unable to access components behind ChildDecls: {0}")]
    NodeBehindChildDecl(Moniker),

    #[error("component child doesn't exist: {0}")]
    NoSuchChild(String),

    #[error("unable to set the root component to a URL")]
    RootCannotBeSetToUrl,

    #[error("unable to set the root component as eager")]
    RootCannotBeEager,

    #[error("received malformed FIDL")]
    BadFidl,

    #[error("bad request: missing field {0}")]
    MissingField(&'static str),

    #[error("route targets cannot be empty")]
    RouteTargetsEmpty,

    #[error("the route source does not exist: {0}")]
    MissingRouteSource(Moniker),

    #[error("the route target does not exist: {0}")]
    MissingRouteTarget(Moniker),

    #[error("a route's target cannot be equal to its source: {0:?}")]
    RouteSourceAndTargetMatch(ftest::RouteEndpoint),

    #[error("can only use protocols from debug: {0:?}")]
    InvalidCapabilityFromDebug(Moniker),

    #[error("the component decl for {0} failed validation: {1:?}")]
    ValidationError(Moniker, cm_fidl_validator::error::ErrorList),

    #[error("{0} capabilities cannot be exposed")]
    UnableToExpose(&'static str),

    #[error("storage capabilities must come from above root")]
    StorageSourceInvalid,

    #[error("component with moniker {0} does not exist")]
    MonikerNotFound(Moniker),

    #[error("the package directory has already been set for this connection")]
    PkgDirAlreadySet,

    #[error("unable to load component from package, the package dir is not set")]
    PkgDirNotSet,

    #[error("failed to load component from package due to IO error")]
    PkgDirIoError(io_util::node::OpenError),

    #[error("failed to load component decl")]
    FailedToLoadComponentDecl(anyhow::Error),
}

impl Error {
    fn log_and_convert(self) -> ftest::RealmBuilderError {
        warn!("sending error to client: {:?}", self);
        match self {
            Error::NodeBehindChildDecl(_) => ftest::RealmBuilderError::NodeBehindChildDecl,
            Error::NoSuchChild(_) => ftest::RealmBuilderError::NoSuchChild,
            Error::RootCannotBeSetToUrl => ftest::RealmBuilderError::RootCannotBeSetToUrl,
            Error::RootCannotBeEager => ftest::RealmBuilderError::RootCannotBeEager,
            Error::BadFidl => ftest::RealmBuilderError::BadFidl,
            Error::MissingField(_) => ftest::RealmBuilderError::MissingField,
            Error::RouteTargetsEmpty => ftest::RealmBuilderError::RouteTargetsEmpty,
            Error::MissingRouteSource(_) => ftest::RealmBuilderError::MissingRouteSource,
            Error::MissingRouteTarget(_) => ftest::RealmBuilderError::MissingRouteTarget,
            Error::RouteSourceAndTargetMatch(_) => {
                ftest::RealmBuilderError::RouteSourceAndTargetMatch
            }
            Error::ValidationError(_, _) => ftest::RealmBuilderError::ValidationError,
            Error::UnableToExpose(_) => ftest::RealmBuilderError::UnableToExpose,
            Error::StorageSourceInvalid => ftest::RealmBuilderError::StorageSourceInvalid,
            Error::MonikerNotFound(_) => ftest::RealmBuilderError::MonikerNotFound,
            Error::PkgDirAlreadySet => ftest::RealmBuilderError::PkgDirAlreadySet,
            Error::PkgDirNotSet => ftest::RealmBuilderError::PkgDirNotSet,
            Error::PkgDirIoError(_) => ftest::RealmBuilderError::PkgDirIoError,
            Error::FailedToLoadComponentDecl(_) => {
                ftest::RealmBuilderError::FailedToLoadComponentDecl
            }
            Error::InvalidCapabilityFromDebug(_) => {
                ftest::RealmBuilderError::InvalidCapabilityFromDebug
            }
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
struct RealmNode {
    decl: cm_rust::ComponentDecl,
    eager: bool,
    environment: Option<String>,

    /// When a component decl comes directly from the test package directory, we should check the
    /// component's manifest during route generation to see if it matches our expectations, instead
    /// of blindly pushing things into it. This way we can detect common issues like "the source
    /// component doesn't declare that capability".
    component_loaded_from_pkg: bool,

    /// Children stored in this HashMap can be mutated. Children stored in `decl.children` can not.
    /// Any children stored in `mutable_children` do NOT have a corresponding `ChildDecl` stored in
    /// `decl.children`, the two should be fully mutually exclusive.
    ///
    /// Suitable `ChildDecl`s for the contents of `mutable_children` are generated and added to
    /// `decl.children` when `commit()` is called.
    mutable_children: HashMap<String, RealmNode>,
}

#[derive(PartialEq)]
enum GetBehavior {
    CreateIfMissing,
    ErrorIfMissing,
}

impl RealmNode {
    fn child<'a>(&'a mut self, child_name: &String) -> Result<&'a mut Self, Error> {
        self.mutable_children.get_mut(child_name).ok_or(Error::NoSuchChild(child_name.clone()))
    }

    fn child_create_if_missing<'a>(&'a mut self, child_name: &String) -> &'a mut Self {
        if !self.mutable_children.contains_key(child_name) {
            self.mutable_children.insert(child_name.clone(), RealmNode::default());
        }
        self.child(child_name).unwrap()
    }

    /// Calls `cm_fidl_validator` on this node's decl, filtering out any errors caused by
    /// missing ChildDecls, as these children may be added to the mutable_children list at a later
    /// point. These decls are re-validated (without filtering out errors) during `commit()`.
    /// `moniker` is used for error reporting.
    fn validate(&self, moniker: &Moniker) -> Result<(), Error> {
        if let Err(mut e) = cm_fidl_validator::fsys::validate(&self.decl.clone().native_into_fidl())
        {
            e.errs = e
                .errs
                .into_iter()
                .filter(|e| match e {
                    cm_fidl_validator::error::Error::InvalidChild(_, _) => false,
                    _ => true,
                })
                .collect();
            if !e.errs.is_empty() {
                return Err(Error::ValidationError(moniker.clone(), e));
            }
        }
        Ok(())
    }

    fn get_node_mut<'a>(
        &'a mut self,
        moniker: &Moniker,
        behavior: GetBehavior,
    ) -> Result<&'a mut RealmNode, Error> {
        let mut current_node = self;
        for part in moniker.path() {
            if current_node.decl.children.iter().any(|c| c.name == part.to_string()) {
                return Err(Error::NodeBehindChildDecl(moniker.clone()));
            }
            current_node = match behavior {
                GetBehavior::CreateIfMissing => current_node.child_create_if_missing(part),
                GetBehavior::ErrorIfMissing => current_node.child(part)?,
            }
        }
        Ok(current_node)
    }

    /// Returns true if the component exists in this realm.
    fn contains(&mut self, moniker: Moniker) -> bool {
        // The root node is an edge case. If the client hasn't set or modified the root component
        // in any way it should expect the server to state that the root component doesn't exist
        // yet, but in this implementation the root node _always_ exists. If we're checking for the
        // root component and we're equal to the default RealmNode (aka there are no children and
        // our decl is empty), then we return false.
        if moniker.is_root() && self == &mut RealmNode::default() {
            return false;
        }
        if let Ok(_) = self.get_node_mut(&moniker, GetBehavior::ErrorIfMissing) {
            return true;
        }
        // `get_node_mut` only returns `Ok` for mutable nodes. This node could still be in our
        // realm but be immutable, so let's check for that.
        if let Some(parent_moniker) = moniker.parent() {
            if let Ok(parent_node) = self.get_node_mut(&parent_moniker, GetBehavior::ErrorIfMissing)
            {
                let child_name = moniker.child_name().unwrap().to_string();
                let res = parent_node.decl.children.iter().any(|c| c.name == child_name);
                return res;
            }
            // If the parent node doesn't exist, then the component itself obviously does not
            // either.
            return false;
        } else {
            // The root component always exists
            return true;
        }
    }

    /// Sets the component to the provided component source. If the source is
    /// a `Component::decl` then a new node is added to the internal tree
    /// structure maintained for this connection. If the source is a
    /// `Component::url` then a new ChildDecl is added to the parent of the
    /// moniker. If any parents for the component do not exist then they are
    /// added. If a different component already exists under this moniker,
    /// then it is replaced.
    async fn set_component(
        &mut self,
        moniker: Moniker,
        component: ftest::Component,
        test_pkg_dir: &Option<fio::DirectoryProxy>,
    ) -> Result<(), Error> {
        match component {
            ftest::Component::Decl(decl) => {
                if let Some(parent_moniker) = moniker.parent() {
                    let parent_node =
                        self.get_node_mut(&parent_moniker, GetBehavior::CreateIfMissing)?;
                    let child_name = moniker.child_name().unwrap().to_string();
                    parent_node.decl.children = parent_node
                        .decl
                        .children
                        .iter()
                        .filter(|c| c.name != child_name)
                        .cloned()
                        .collect();
                }
                let node = self.get_node_mut(&moniker, GetBehavior::CreateIfMissing)?;
                node.decl = decl.fidl_into_native();
                node.validate(&moniker)?;
            }
            ftest::Component::Url(url) => {
                if is_relative_url(&url) {
                    return self
                        .load_decl_from_pkg(
                            moniker,
                            url,
                            test_pkg_dir.as_ref().cloned().ok_or(Error::PkgDirNotSet)?,
                        )
                        .await;
                }
                if moniker.is_root() {
                    return Err(Error::RootCannotBeSetToUrl);
                }
                let parent_node =
                    self.get_node_mut(&moniker.parent().unwrap(), GetBehavior::CreateIfMissing)?;
                let child_name = moniker.child_name().unwrap().to_string();
                parent_node.mutable_children.remove(&child_name);
                parent_node.decl.children = parent_node
                    .decl
                    .children
                    .iter()
                    .filter(|c| c.name != child_name)
                    .cloned()
                    .collect();
                parent_node.decl.children.push(cm_rust::ChildDecl {
                    name: child_name,
                    url,
                    startup: fsys::StartupMode::Lazy,
                    environment: None,
                    on_terminate: None,
                });
            }
            ftest::Component::LegacyUrl(url) => {
                if let Some(parent_moniker) = moniker.parent() {
                    let parent_node =
                        self.get_node_mut(&parent_moniker, GetBehavior::CreateIfMissing)?;
                    let child_name = moniker.child_name().unwrap().to_string();
                    parent_node.decl.children = parent_node
                        .decl
                        .children
                        .iter()
                        .filter(|c| c.name != child_name)
                        .cloned()
                        .collect();
                }
                let node = self.get_node_mut(&moniker, GetBehavior::CreateIfMissing)?;
                node.decl = cm_rust::ComponentDecl {
                    program: Some(cm_rust::ProgramDecl {
                        runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                        info: fdata::Dictionary {
                            entries: Some(vec![fdata::DictionaryEntry {
                                key: runner::LEGACY_URL_KEY.to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str(url))),
                            }]),
                            ..fdata::Dictionary::EMPTY
                        },
                    }),
                    ..cm_rust::ComponentDecl::default()
                };
                node.validate(&moniker)?;
            }
            _ => return Err(Error::BadFidl),
        }
        Ok(())
    }

    /// Sets the component to be a mock component. A new ComponentDecl is generated for the
    /// component, and assigned a new mock id.
    async fn set_mock_component(
        &mut self,
        moniker: Moniker,
        mock_id: runner::LocalComponentId,
    ) -> Result<(), Error> {
        if let Some(parent_moniker) = moniker.parent() {
            let parent_node = self.get_node_mut(&parent_moniker, GetBehavior::CreateIfMissing)?;
            let child_name = moniker.child_name().unwrap().to_string();
            parent_node.decl.children = parent_node
                .decl
                .children
                .iter()
                .filter(|c| c.name != child_name)
                .cloned()
                .collect();
        }
        let decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(runner::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: runner::LOCAL_COMPONENT_ID_KEY.to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(mock_id.into()))),
                    }]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            ..cm_rust::ComponentDecl::default()
        };
        let node = self.get_node_mut(&moniker, GetBehavior::CreateIfMissing)?;
        node.decl = decl;
        node.validate(&moniker)?;
        Ok(())
    }

    /// Loads the file referenced by the relative url `url` from `test_pkg_dir`, and sets it as the
    /// decl for the component referred to by `moniker`. Also loads in the declarations for any
    /// additional relative URLs in the new decl in the same manner, and so forth until all
    /// relative URLs have been processed.
    async fn load_decl_from_pkg(
        &mut self,
        moniker: Moniker,
        url: String,
        test_pkg_dir: fio::DirectoryProxy,
    ) -> Result<(), Error> {
        // This can't be written recursively, because we need async here and the resulting
        // BoxFuture would have to hold on to `&mut self`, which isn't possible because the
        // reference is not `'static`.
        //
        // This is also written somewhat inefficiently, because holding a reference to the current
        // working node in the stack would result to multiple mutable references from `&mut self`
        // being held at the same time, which is disallowed. As a result, this re-fetches the
        // current working node from the root of the tree on each iteration.
        let mut relative_urls_to_process = vec![(moniker, url)];
        while let Some((current_moniker, relative_url)) = relative_urls_to_process.pop() {
            let current_node = self.get_node_mut(&current_moniker, GetBehavior::CreateIfMissing)?;

            // Load the decl and validate it
            let path = relative_url.trim_start_matches('#');
            let file_proxy =
                io_util::directory::open_file(&test_pkg_dir, &path, io_util::OPEN_RIGHT_READABLE)
                    .await
                    .map_err(Error::PkgDirIoError)?;
            let fidl_decl = io_util::read_file_fidl::<fsys::ComponentDecl>(&file_proxy)
                .await
                .map_err(Error::FailedToLoadComponentDecl)?;
            current_node.decl = fidl_decl.fidl_into_native();
            current_node.component_loaded_from_pkg = true;
            current_node.validate(&current_moniker)?;

            // Look through the new decl's children. If there are any relative URLs, we need to
            // handle those too.
            let mut child_decls_to_keep = vec![];
            let mut child_decls_to_load = vec![];
            for child in current_node.decl.children.drain(..) {
                if is_relative_url(&child.url) {
                    child_decls_to_load.push(child);
                } else {
                    child_decls_to_keep.push(child);
                }
            }
            current_node.decl.children = child_decls_to_keep;

            for child in child_decls_to_load {
                let child_node = current_node.child_create_if_missing(&child.name);
                let child_moniker = current_moniker.child(child.name.clone());
                if child.startup == fsys::StartupMode::Eager {
                    child_node.eager = true;
                }
                child_node.environment = child.environment;
                relative_urls_to_process.push((child_moniker, child.url));
            }
        }
        Ok(())
    }

    /// Returns the current value of a component decl in the realm being
    /// constructed. Note that this cannot retrieve decls through external
    /// URLs, so for example if `SetComponent` is called with `Component::url`
    /// and then `GetComponentDecl` is called with the same moniker, an error
    /// will be returned.
    fn get_component_decl(&mut self, moniker: Moniker) -> Result<cm_rust::ComponentDecl, Error> {
        Ok(self.get_node_mut(&moniker, GetBehavior::ErrorIfMissing)?.decl.clone())
    }

    /// Marks the component and any ancestors of it as eager, ensuring that the
    /// component is started immediately once the realm is bound to.
    fn mark_as_eager(&mut self, moniker: Moniker) -> Result<(), Error> {
        if moniker.is_root() {
            return Err(Error::RootCannotBeEager);
        }
        if !self.contains(moniker.clone()) {
            return Err(Error::MonikerNotFound(moniker.clone()));
        }
        // The component we want to mark as eager could be either mutable or immutable. Mutable
        // components are retrievable with `self.get_node_mut`, whereas immutable components are
        // found in a ChildDecl in the decl of the node's parent.
        if let Ok(node) = self.get_node_mut(&moniker, GetBehavior::ErrorIfMissing) {
            node.eager = true;
        }
        let parent_node =
            self.get_node_mut(&moniker.parent().unwrap(), GetBehavior::ErrorIfMissing)?;
        if let Some(child_decl) =
            parent_node.decl.children.iter_mut().find(|c| &c.name == moniker.child_name().unwrap())
        {
            child_decl.startup = fsys::StartupMode::Eager;
        }
        for ancestor in moniker.ancestry() {
            let ancestor_node = self.get_node_mut(&ancestor, GetBehavior::ErrorIfMissing)?;
            ancestor_node.eager = true;
        }
        Ok(())
    }

    /// Adds a capability route to the realm being constructed, adding any
    /// necessary offers, exposes, uses, and capability declarations to any
    /// component involved in the route. Note that components added with
    /// `Component::url` can not be modified, and they are presumed to already
    /// have the declarations needed for the route to be valid. If an error is
    /// returned some of the components in the route may have been updated while
    /// others were not.
    fn route_capability(&mut self, route: ftest::CapabilityRoute) -> Result<(), Error> {
        let capability = route.capability.ok_or(Error::MissingField("capability"))?;
        let source = route.source.ok_or(Error::MissingField("source"))?;
        let targets = route.targets.ok_or(Error::MissingField("targets"))?;
        if targets.is_empty() {
            return Err(Error::RouteTargetsEmpty);
        }
        if let ftest::RouteEndpoint::Component(moniker) = &source {
            let moniker: Moniker = moniker.clone().into();
            if !self.contains(moniker.clone()) {
                return Err(Error::MissingRouteSource(moniker.clone()));
            }
        }
        for target in &targets {
            if &source == target {
                return Err(Error::RouteSourceAndTargetMatch(source));
            }
            if let ftest::RouteEndpoint::Component(target_moniker) = target {
                let target_moniker: Moniker = target_moniker.clone().into();
                if !self.contains(target_moniker.clone()) {
                    return Err(Error::MissingRouteTarget(target_moniker));
                }
            }
        }
        let force_route = route.force_route.unwrap_or(false);
        for target in targets {
            if let ftest::RouteEndpoint::AboveRoot(_) = target {
                // We're routing a capability from component within our constructed realm to
                // somewhere above it
                self.route_capability_to_above_root(
                    &capability,
                    source.clone().try_into()?,
                    force_route,
                    cm_rust::ExposeSource::Self_,
                )?;
            } else if let ftest::RouteEndpoint::AboveRoot(_) = &source {
                // We're routing a capability from above our constructed realm to a component
                // within it
                self.route_capability_from_above_root(
                    &capability,
                    target.try_into()?,
                    force_route,
                )?;
            } else if let ftest::RouteEndpoint::Debug(_) = &source {
                // We're routing a capability from the debug section of the component's environment.
                self.route_capability_from_debug(&capability, target.try_into()?, force_route)?;
            } else {
                // We're routing a capability from one component within our constructed realm to
                // another
                let source_moniker = source.clone().try_into()?;
                let target_moniker: Moniker = target.try_into()?;
                if target_moniker.is_ancestor_of(&source_moniker) {
                    // The target is an ancestor of the source, so this is a "use from child"
                    // scenario
                    self.route_capability_use_from_child(
                        &capability,
                        source_moniker,
                        target_moniker,
                        force_route,
                    )?;
                } else {
                    // The target is _not_ an ancestor of the source, so this is a classic "routing
                    // between two components" scenario, where the target uses the capability from
                    // its parent.
                    self.route_capability_between_components(
                        &capability,
                        source_moniker,
                        target_moniker,
                        force_route,
                    )?;
                }
            }
        }
        Ok(())
    }

    fn route_capability_to_above_root(
        &mut self,
        capability: &ftest::Capability,
        source_moniker: Moniker,
        force_route: bool,
        from: cm_rust::ExposeSource,
    ) -> Result<(), Error> {
        let mut current_ancestor = self.get_node_mut(&Moniker::root(), GetBehavior::ErrorIfMissing);
        let mut current_moniker = Moniker::root();
        for child_name in source_moniker.path() {
            let current = current_ancestor?;
            current.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Child(child_name.to_string()),
                force_route,
            )?;

            current_ancestor = current.child(&child_name);
            current_moniker = current_moniker.child(child_name.clone());
        }

        if let Ok(source_node) = self.get_node_mut(&source_moniker, GetBehavior::ErrorIfMissing) {
            source_node.add_expose_for_capability(&capability, from, force_route)?;
            source_node.add_capability_decl(&capability, force_route)?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //source_node.validate(&source_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already declares
            // and exposes thecapability
        }
        Ok(())
    }

    fn route_capability_from_above_root(
        &mut self,
        capability: &ftest::Capability,
        target_moniker: Moniker,
        force_route: bool,
    ) -> Result<(), Error> {
        let mut current_ancestor = self.get_node_mut(&Moniker::root(), GetBehavior::ErrorIfMissing);
        let mut current_moniker = Moniker::root();
        for child_name in target_moniker.path() {
            let current = current_ancestor?;
            current.add_offer_for_capability(
                &capability,
                cm_rust::OfferSource::Parent,
                &child_name,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //current.validate(&current_moniker)?;

            current_ancestor = current.child(&child_name);
            current_moniker = current_moniker.child(child_name.clone());
        }

        if let Ok(target_node) = self.get_node_mut(&target_moniker, GetBehavior::ErrorIfMissing) {
            target_node.add_use_for_capability(
                &capability,
                cm_rust::UseSource::Parent,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //target_node.validate(&target_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already uses
            // the capability.
        }
        Ok(())
    }

    fn route_capability_from_debug(
        &mut self,
        capability: &ftest::Capability,
        target_moniker: Moniker,
        force_route: bool,
    ) -> Result<(), Error> {
        match &capability {
            ftest::Capability::Protocol(_) => { /*only this is supported */ }
            _ => return Err(Error::InvalidCapabilityFromDebug(target_moniker)),
        }
        if let Ok(target_node) = self.get_node_mut(&target_moniker, GetBehavior::ErrorIfMissing) {
            target_node.add_use_for_capability(
                &capability,
                cm_rust::UseSource::Debug,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //target_node.validate(&target_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already uses
            // the capability.
        }
        Ok(())
    }

    // This will panic if `target_moniker.is_ancestor_of(source_moniker)` returns false
    fn route_capability_use_from_child(
        &mut self,
        capability: &ftest::Capability,
        source_moniker: Moniker,
        target_moniker: Moniker,
        force_route: bool,
    ) -> Result<(), Error> {
        let target_node = self.get_node_mut(&target_moniker, GetBehavior::ErrorIfMissing)?;
        let child_source = target_moniker.downward_path_to(&source_moniker).get(0).unwrap().clone();
        target_node.add_use_for_capability(
            &capability,
            cm_rust::UseSource::Child(child_source),
            force_route,
        )?;
        // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
        //target_node.validate(&target_moniker)?;

        let mut path_to_source = target_moniker.downward_path_to(&source_moniker);
        let first_expose_name = path_to_source.remove(0);
        let mut current_moniker = target_moniker.child(first_expose_name.clone());
        let mut current_node = target_node.child(&first_expose_name);
        for child_name in path_to_source {
            let current = current_node?;
            current.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Child(child_name.to_string()),
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //current.validate(&current_moniker)?;
            current_node = current.child(&child_name);
            current_moniker = current_moniker.child(child_name);
        }
        if let Ok(source_node) = current_node {
            source_node.add_capability_decl(&capability, force_route)?;
            source_node.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Self_,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //source_node.validate(&current_moniker)?;
        } else {
            // `RealmNode::child` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already declares
            // the capability.
        }
        Ok(())
    }

    fn route_capability_between_components(
        &mut self,
        capability: &ftest::Capability,
        source_moniker: Moniker,
        target_moniker: Moniker,
        force_route: bool,
    ) -> Result<(), Error> {
        if let Ok(target_node) = self.get_node_mut(&target_moniker, GetBehavior::ErrorIfMissing) {
            target_node.add_use_for_capability(
                &capability,
                cm_rust::UseSource::Parent,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //target_node.validate(&target_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already uses
            // the capability.
        }
        if let Ok(source_node) = self.get_node_mut(&source_moniker, GetBehavior::ErrorIfMissing) {
            source_node.add_capability_decl(&capability, force_route)?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //target_node.validate(&target_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already uses
            // the capability.
        }

        let mut common_ancestor_moniker = target_moniker.parent().unwrap();
        while common_ancestor_moniker != source_moniker
            && !common_ancestor_moniker.is_ancestor_of(&source_moniker)
        {
            common_ancestor_moniker = common_ancestor_moniker.parent().unwrap();
        }
        let common_ancestor =
            self.get_node_mut(&common_ancestor_moniker, GetBehavior::ErrorIfMissing)?;

        let mut path_to_target = common_ancestor_moniker.downward_path_to(&target_moniker);
        let first_offer_name = path_to_target.remove(0);
        let mut current_ancestor_moniker = common_ancestor_moniker.child(first_offer_name.clone());

        let mut current_node = common_ancestor.child(&first_offer_name);

        for child_name in path_to_target {
            let current = current_node?;
            current.add_offer_for_capability(
                &capability,
                cm_rust::OfferSource::Parent,
                &child_name,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //current.validate(&current_ancestor_moniker)?;
            current_node = current.child(&child_name);
            current_ancestor_moniker = current_ancestor_moniker.child(child_name.clone());
        }

        if common_ancestor_moniker == source_moniker {
            // We don't need to add an expose chain, we reached the source moniker solely
            // by walking up the tree
            let common_ancestor =
                self.get_node_mut(&common_ancestor_moniker, GetBehavior::ErrorIfMissing)?;
            common_ancestor.add_offer_for_capability(
                &capability,
                cm_rust::OfferSource::Self_,
                &first_offer_name,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //common_ancestor.validate(&common_ancestor_moniker)?;
            return Ok(());
        }

        // We need an expose chain to descend down the tree to our source.

        let mut path_to_target = common_ancestor_moniker.downward_path_to(&source_moniker);
        let first_expose_name = path_to_target.remove(0);
        let mut current_ancestor_moniker = common_ancestor_moniker.child(first_expose_name.clone());
        let mut current_node = common_ancestor.child(&first_expose_name);

        for child_name in path_to_target {
            let current = current_node?;
            current.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Child(child_name.to_string()),
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //current.validate(&current_ancestor_moniker)?;
            current_node = current.child(&child_name);
            current_ancestor_moniker = current_ancestor_moniker.child(child_name.clone());
        }

        if let Ok(source_node) = current_node {
            source_node.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Self_,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //source_node.validate(&current_ancestor_moniker)?;
        } else {
            // `RealmNode::child` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already exposes
            // the capability.
        }

        common_ancestor.add_offer_for_capability(
            &capability,
            cm_rust::OfferSource::static_child(first_expose_name.clone()),
            &first_offer_name,
            force_route,
        )?;
        // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
        //common_ancestor.validate(&common_ancestor_moniker)?;
        Ok(())
    }

    /// Assembles the realm being constructed and returns the URL for the root
    /// component in the realm, which may then be used to create a new component
    /// in any collection where fuchsia-test-component is properly set up.
    fn commit(
        mut self,
        registry: Arc<resolver::Registry>,
        walked_path: Vec<String>,
        package_dir: Option<fio::DirectoryProxy>,
    ) -> BoxFuture<'static, Result<String, Error>> {
        // This function is much cleaner written recursively, but we can't construct recursive
        // futures as the size isn't knowable to rustc at compile time. Put the recursive call
        // into a boxed future, as the redirection makes this possible
        async move {
            // Expose the fuchsia.component.Binder protocol from root in order to give users the ability to manually
            // start the realm.
            if walked_path.is_empty() {
                let () = self.route_capability_to_above_root(
                    &*BINDER_PROTOCOL_CAPABILITY,
                    Moniker::root(),
                    true,
                    cm_rust::ExposeSource::Framework,
                )?;
            }

            let mut mutable_children = self.mutable_children.into_iter().collect::<Vec<_>>();
            mutable_children.sort_unstable_by_key(|t| t.0.clone());
            for (name, node) in mutable_children {
                let mut new_path = walked_path.clone();
                new_path.push(name.clone());

                let startup =
                    if node.eager { fsys::StartupMode::Eager } else { fsys::StartupMode::Lazy };
                let environment = node.environment.clone();
                let url = node.commit(registry.clone(), new_path, package_dir.clone()).await?;
                self.decl.children.push(cm_rust::ChildDecl {
                    name,
                    url,
                    startup,
                    environment,
                    on_terminate: None,
                });
            }

            let name =
                if walked_path.is_empty() { "root".to_string() } else { walked_path.join("-") };
            let decl = self.decl.native_into_fidl();
            registry
                .validate_and_register(decl, name, package_dir.clone())
                .await
                .map_err(|e| Error::ValidationError(walked_path.into(), e))
        }
        .boxed()
    }

    /// This call ensures that an expose for the given capability exists in this component's decl.
    /// If `self.component_loaded_from_pkg && !force_route` is true, we don't do anything.
    fn add_expose_for_capability(
        &mut self,
        capability: &ftest::Capability,
        source: cm_rust::ExposeSource,
        force_route: bool,
    ) -> Result<(), Error> {
        if self.component_loaded_from_pkg && !force_route {
            // We don't modify package-local components unless force_route is true
            return Ok(());
        }
        let capability_name = get_capability_name(&capability)?;
        let new_decl = {
            match &capability {
                ftest::Capability::Service(_) => {
                    cm_rust::ExposeDecl::Service(cm_rust::ExposeServiceDecl {
                        source,
                        source_name: capability_name.clone().into(),
                        target: cm_rust::ExposeTarget::Parent,
                        target_name: capability_name.into(),
                    })
                }
                ftest::Capability::Protocol(_) => {
                    cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                        source,
                        source_name: capability_name.clone().into(),
                        target: cm_rust::ExposeTarget::Parent,
                        target_name: capability_name.into(),
                    })
                }
                ftest::Capability::Directory(_) => {
                    cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                        source,
                        source_name: capability_name.clone().into(),
                        target: cm_rust::ExposeTarget::Parent,
                        target_name: capability_name.into(),
                        rights: None,
                        subdir: None,
                    })
                }
                ftest::Capability::Storage(ftest::StorageCapability { .. }) => {
                    return Err(Error::UnableToExpose("storage"));
                }
                _ => return Err(Error::BadFidl),
            }
        };
        // A decl with the same source and name but different options will be caught during decl
        // validation later
        if !self.decl.exposes.contains(&new_decl) {
            self.decl.exposes.push(new_decl);
        }

        Ok(())
    }

    /// This call ensures that a declaration for the given capability and source exists in this
    /// component's decl. If `self.component_loaded_from_pkg && !force_route` is true, we don't do
    /// anything.
    fn add_capability_decl(
        &mut self,
        capability: &ftest::Capability,
        force_route: bool,
    ) -> Result<(), Error> {
        if self.component_loaded_from_pkg && !force_route {
            // We don't modify package-local components unless force_route is true
            return Ok(());
        }
        let capability_name = get_capability_name(&capability)?;
        let capability_decl = match capability {
            ftest::Capability::Service(_) => {
                Some(cm_rust::CapabilityDecl::Service(cm_rust::ServiceDecl {
                    name: capability_name.as_str().try_into().unwrap(),
                    source_path: Some(
                        format!("/svc/{}", capability_name).as_str().try_into().unwrap(),
                    ),
                }))
            }
            ftest::Capability::Protocol(_) => {
                Some(cm_rust::CapabilityDecl::Protocol(cm_rust::ProtocolDecl {
                    name: capability_name.as_str().try_into().unwrap(),
                    source_path: Some(
                        format!("/svc/{}", capability_name).as_str().try_into().unwrap(),
                    ),
                }))
            }
            ftest::Capability::Directory(ftest::DirectoryCapability { path, rights, .. }) => {
                Some(cm_rust::CapabilityDecl::Directory(cm_rust::DirectoryDecl {
                    name: capability_name.as_str().try_into().unwrap(),
                    source_path: Some(path.as_ref().unwrap().as_str().try_into().unwrap()),
                    rights: rights.as_ref().unwrap().clone(),
                }))
            }
            ftest::Capability::Storage(_) => {
                return Err(Error::StorageSourceInvalid);
            }
            _ => return Err(Error::BadFidl),
        };
        if let Some(decl) = capability_decl {
            // A decl with the same source and name but different options will be caught during
            // decl validation later
            if !self.decl.capabilities.contains(&decl) {
                self.decl.capabilities.push(decl);
            }
        }
        Ok(())
    }

    /// This call ensures that a use for the given capability exists in this component's decl. If
    /// `self.component_loaded_from_pkg && !force_route` is true, we don't do anything.
    fn add_use_for_capability(
        &mut self,
        capability: &ftest::Capability,
        use_source: cm_rust::UseSource,
        force_route: bool,
    ) -> Result<(), Error> {
        if self.component_loaded_from_pkg && !force_route {
            // We don't modify package-local components unless force_route is true
            return Ok(());
        }
        let capability_name = get_capability_name(&capability)?;
        let use_decl = match capability {
            ftest::Capability::Service(_) => cm_rust::UseDecl::Service(cm_rust::UseServiceDecl {
                source: use_source,
                source_name: capability_name.as_str().try_into().unwrap(),
                target_path: format!("/svc/{}", capability_name).as_str().try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            }),
            ftest::Capability::Protocol(_) => {
                cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                    source: use_source,
                    source_name: capability_name.as_str().try_into().unwrap(),
                    target_path: format!("/svc/{}", capability_name).as_str().try_into().unwrap(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })
            }
            ftest::Capability::Directory(ftest::DirectoryCapability { path, rights, .. }) => {
                cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
                    source: use_source,
                    source_name: capability_name.as_str().try_into().unwrap(),
                    target_path: path.as_ref().unwrap().as_str().try_into().unwrap(),
                    rights: rights.as_ref().unwrap().clone(),
                    subdir: None,
                    dependency_type: cm_rust::DependencyType::Strong,
                })
            }
            ftest::Capability::Storage(ftest::StorageCapability { path, .. }) => {
                if use_source != cm_rust::UseSource::Parent {
                    return Err(Error::UnableToExpose("storage"));
                }
                cm_rust::UseDecl::Storage(cm_rust::UseStorageDecl {
                    source_name: capability_name.as_str().try_into().unwrap(),
                    target_path: path.as_ref().unwrap().as_str().try_into().unwrap(),
                })
            }
            _ => return Err(Error::BadFidl),
        };
        if !self.decl.uses.contains(&use_decl) {
            self.decl.uses.push(use_decl);
        }
        Ok(())
    }

    /// This call ensures that a given offer for the given capability exists in this component's
    /// decl. If `self.component_loaded_from_pkg && !force_route` is true, we don't do anything.
    fn add_offer_for_capability(
        &mut self,
        capability: &ftest::Capability,
        offer_source: cm_rust::OfferSource,
        target_name: &str,
        force_route: bool,
    ) -> Result<(), Error> {
        if self.component_loaded_from_pkg && !force_route {
            // We don't modify package-local components unless force_route is true
            return Ok(());
        }
        if let cm_rust::OfferSource::Child(_) = &offer_source {
            if let ftest::Capability::Storage(_) = capability {
                return Err(Error::UnableToExpose("storage"));
            }
        }
        let capability_name = get_capability_name(&capability)?;

        let offer_decl = match &capability {
            ftest::Capability::Service(_) => {
                cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                    source: offer_source,
                    source_name: capability_name.clone().into(),
                    target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                    target_name: capability_name.into(),
                })
            }
            ftest::Capability::Protocol(_) => {
                cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: offer_source,
                    source_name: capability_name.clone().into(),
                    target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                    target_name: capability_name.into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })
            }
            ftest::Capability::Directory(_) => {
                cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                    source: offer_source,
                    source_name: capability_name.clone().into(),
                    target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                    target_name: capability_name.into(),
                    rights: None,
                    subdir: None,
                    dependency_type: cm_rust::DependencyType::Strong,
                })
            }
            ftest::Capability::Storage(_) => {
                cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                    source: offer_source,
                    source_name: capability_name.clone().into(),
                    target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                    target_name: capability_name.into(),
                })
            }
            _ => return Err(Error::BadFidl),
        };
        if !self.decl.offers.contains(&offer_decl) {
            self.decl.offers.push(offer_decl);
        }
        Ok(())
    }
}

// TODO(77771): use the moniker crate once there's an id-free version of it.
#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord, Default)]
struct Moniker {
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

impl TryFrom<ftest::RouteEndpoint> for Moniker {
    type Error = Error;

    fn try_from(route_endpoint: ftest::RouteEndpoint) -> Result<Self, Error> {
        match route_endpoint {
            ftest::RouteEndpoint::AboveRoot(_) => {
                panic!("tried to convert RouteEndpoint::AboveRoot into a moniker")
            }
            ftest::RouteEndpoint::Component(moniker) => Ok(moniker.into()),
            _ => Err(Error::BadFidl),
        }
    }
}

impl Display for Moniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.is_root() {
            write!(f, "<root of test realm>")
        } else {
            write!(f, "{}", self.path.join("/"))
        }
    }
}

impl Moniker {
    pub fn root() -> Self {
        Moniker { path: vec![] }
    }

    fn is_root(&self) -> bool {
        return self.path.is_empty();
    }

    fn child_name(&self) -> Option<&String> {
        self.path.last()
    }

    fn path(&self) -> &Vec<String> {
        &self.path
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

    /// Returns the list of components comprised of this component's parent, then that component's
    /// parent, and so on. This list does not include the root component.
    ///
    /// For example, `"a/b/c/d".into().ancestry()` would return `vec!["a/b/c".into(), "a/b".into(),
    /// "a".into()]`
    fn ancestry(&self) -> Vec<Moniker> {
        let mut current_moniker = Moniker { path: vec![] };
        let mut res = vec![];
        let mut parent_path = self.path.clone();
        parent_path.pop();
        for part in parent_path {
            current_moniker.path.push(part.clone());
            res.push(current_moniker.clone());
        }
        res
    }

    fn parent(&self) -> Option<Self> {
        let mut path = self.path.clone();
        path.pop()?;
        Some(Moniker { path })
    }

    fn child(&self, child_name: String) -> Self {
        let mut path = self.path.clone();
        path.push(child_name);
        Moniker { path }
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

fn is_relative_url(url: &str) -> bool {
    if url.len() == 0 || url.chars().nth(0) != Some('#') {
        return false;
    }
    if Url::parse(url) != Err(url::ParseError::RelativeUrlWithoutBase) {
        return false;
    }
    true
}

fn is_legacy_url(url: &str) -> bool {
    url.trim().ends_with(".cmx")
}

fn get_capability_name(capability: &ftest::Capability) -> Result<String, Error> {
    match &capability {
        ftest::Capability::Service(ftest::ServiceCapability { name, .. }) => {
            Ok(name.as_ref().unwrap().clone())
        }
        ftest::Capability::Protocol(ftest::ProtocolCapability { name, .. }) => {
            Ok(name.as_ref().unwrap().clone())
        }
        ftest::Capability::Directory(ftest::DirectoryCapability { name, .. }) => {
            Ok(name.as_ref().unwrap().clone())
        }
        ftest::Capability::Storage(ftest::StorageCapability { name, .. }) => {
            Ok(name.as_ref().unwrap().clone())
        }
        _ => Err(Error::BadFidl),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{
            create_endpoints, create_proxy, create_proxy_and_stream, create_request_stream,
            ClientEnd,
        },
        fidl_fuchsia_io2 as fio2, fuchsia_async as fasync,
        matches::assert_matches,
        std::convert::TryInto,
        test_case::test_case,
    };

    const EXAMPLE_LEGACY_URL: &'static str = "fuchsia-pkg://fuchsia.com/a#meta/a.cmx";

    #[derive(Debug, Clone, PartialEq)]
    struct ComponentTree {
        decl: cm_rust::ComponentDecl,
        children: Vec<(String, ftest::ChildOptions, ComponentTree)>,
    }

    impl ComponentTree {
        fn new_from_resolver(
            url: String,
            registry: Arc<resolver::Registry>,
        ) -> BoxFuture<'static, Option<ComponentTree>> {
            async move {
                let decl_from_resolver = match registry.get_decl_for_url(&url).await {
                    Some(decl) => decl,
                    None => return None,
                };

                let mut self_ = ComponentTree { decl: decl_from_resolver, children: vec![] };
                let children = self_.decl.children.drain(..).collect::<Vec<_>>();
                for child in children {
                    match Self::new_from_resolver(child.url.clone(), registry.clone()).await {
                        None => {
                            self_.decl.children.push(child);
                        }
                        Some(child_tree) => {
                            let child_options = ftest::ChildOptions {
                                startup: match child.startup {
                                    fsys::StartupMode::Eager => Some(fcdecl::StartupMode::Eager),
                                    fsys::StartupMode::Lazy => None,
                                },
                                environment: child.environment,
                                on_terminate: match child.on_terminate {
                                    Some(fsys::OnTerminate::None) => {
                                        Some(fcdecl::OnTerminate::None)
                                    }
                                    Some(fsys::OnTerminate::Reboot) => {
                                        Some(fcdecl::OnTerminate::Reboot)
                                    }
                                    None => None,
                                },
                                ..ftest::ChildOptions::EMPTY
                            };
                            self_.children.push((child.name, child_options, child_tree));
                        }
                    }
                }
                Some(self_)
            }
            .boxed()
        }

        // Adds the `BINDER_EXPOSE_DECL` to the root component in the tree
        fn add_binder_expose(&mut self) {
            self.decl.exposes.push(BINDER_EXPOSE_DECL.clone());
        }
    }

    fn tree_to_realm_node(tree: ComponentTree) -> BoxFuture<'static, RealmNode2> {
        async move {
            let node = RealmNode2::new_from_decl(tree.decl, false);
            for (child_name, options, tree) in tree.children {
                let child_node = tree_to_realm_node(tree).await;
                node.state.lock().await.mutable_children.insert(child_name, (options, child_node));
            }
            node
        }
        .boxed()
    }

    // Builds the given ComponentTree, and returns the root URL and the resolver that holds the
    // built declarations
    async fn build_tree(
        tree: &mut ComponentTree,
    ) -> Result<(String, Arc<resolver::Registry>), ftest::RealmBuilderError2> {
        let res = build_tree_helper(tree.clone()).await;

        // We want to be able to check our component tree against the registry later, but the
        // builder automatically puts stuff into the root realm when building. Add that to our
        // local tree here, so that our tree looks the same as what hopefully got put in the
        // resolver.
        tree.add_binder_expose();

        res
    }

    fn launch_builder_task(
        realm_node: RealmNode2,
        registry: Arc<resolver::Registry>,
        runner_proxy_placeholder: Arc<Mutex<Option<fcrunner::ComponentRunnerProxy>>>,
    ) -> (ftest::BuilderProxy, fasync::Task<()>) {
        let (pkg_dir, pkg_dir_stream) = create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();
        drop(pkg_dir_stream);

        let builder = Builder { pkg_dir, realm_node, registry, runner_proxy_placeholder };

        let (builder_proxy, builder_stream) =
            create_proxy_and_stream::<ftest::BuilderMarker>().unwrap();

        let builder_stream_task = fasync::Task::local(async move {
            builder.handle_stream(builder_stream).await.expect("failed to handle builder stream");
        });
        (builder_proxy, builder_stream_task)
    }

    async fn build_tree_helper(
        tree: ComponentTree,
    ) -> Result<(String, Arc<resolver::Registry>), ftest::RealmBuilderError2> {
        let realm_node = tree_to_realm_node(tree).await;

        let registry = resolver::Registry::new();
        let (builder_proxy, _builder_stream_task) =
            launch_builder_task(realm_node, registry.clone(), Arc::new(Mutex::new(None)));

        let (runner_client_end, runner_server_end) = create_endpoints().unwrap();
        drop(runner_server_end);
        let res =
            builder_proxy.build(runner_client_end).await.expect("failed to send build command");
        match res {
            Ok(url) => Ok((url, registry)),
            Err(e) => Err(e),
        }
    }

    // Holds the task for handling a new realm stream and a new builder stream, along with proxies
    // for those streams and the registry and runner the tasks will manipulate.
    #[allow(unused)]
    struct RealmAndBuilderTask {
        realm_proxy: ftest::RealmProxy,
        builder_proxy: ftest::BuilderProxy,
        registry: Arc<resolver::Registry>,
        runner: Arc<runner::Runner>,
        _realm_and_builder_task: fasync::Task<()>,
        runner_stream: fcrunner::ComponentRunnerRequestStream,
        runner_client_end: Option<ClientEnd<fcrunner::ComponentRunnerMarker>>,
    }

    impl RealmAndBuilderTask {
        fn new() -> Self {
            let (realm_proxy, realm_stream) =
                create_proxy_and_stream::<ftest::RealmMarker>().unwrap();
            let pkg_dir = io_util::open_directory_in_namespace(
                "/pkg",
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
            )
            .unwrap();
            let realm_root = RealmNode2::new();

            let registry = resolver::Registry::new();
            let runner = runner::Runner::new();
            let runner_proxy_placeholder = Arc::new(Mutex::new(None));

            let (builder_proxy, builder_task) = launch_builder_task(
                realm_root.clone(),
                registry.clone(),
                runner_proxy_placeholder.clone(),
            );

            let realm = Realm {
                pkg_dir,
                realm_node: realm_root,
                registry: registry.clone(),
                runner: runner.clone(),
                runner_proxy_placeholder,
                realm_path: vec![],
                execution_scope: ExecutionScope::new(),
            };

            let realm_and_builder_task = fasync::Task::local(async move {
                realm.handle_stream(realm_stream).await.expect("failed to handle realm stream");
                builder_task.await;
            });
            let (runner_client_end, runner_stream) =
                create_request_stream::<fcrunner::ComponentRunnerMarker>().unwrap();
            Self {
                realm_proxy,
                builder_proxy,
                registry,
                runner,
                _realm_and_builder_task: realm_and_builder_task,
                runner_stream,
                runner_client_end: Some(runner_client_end),
            }
        }

        async fn call_build(&mut self) -> Result<String, ftest::RealmBuilderError2> {
            self.builder_proxy
                .build(self.runner_client_end.take().expect("call_build called twice"))
                .await
                .expect("failed to send build command")
        }

        // Calls `Builder.Build` on `self.builder_proxy`, which should populate `self.registry`
        // with the contents of the realm and then return the URL for the root of this realm. That
        // URL is then used to look up the `ComponentTree` that ended up in the resolver, which can
        // be `assert_eq`'d against what the tree is expected to be.
        async fn call_build_and_get_tree(&mut self) -> ComponentTree {
            let url = self.call_build().await.expect("builder unexpectedly returned an error");
            ComponentTree::new_from_resolver(url, self.registry.clone())
                .await
                .expect("tree missing from resolver")
        }

        async fn add_child_or_panic(&self, name: &str, url: &str, options: ftest::ChildOptions) {
            let () = self
                .realm_proxy
                .add_child(name, url, options)
                .await
                .expect("failed to make Realm.AddChild call")
                .expect("failed to add child");
        }

        async fn add_route_or_panic(
            &self,
            mut capabilities: Vec<ftest::Capability2>,
            mut from: fcdecl::Ref,
            mut tos: Vec<fcdecl::Ref>,
        ) {
            let () = self
                .realm_proxy
                .add_route(&mut capabilities.iter_mut(), &mut from, &mut tos.iter_mut())
                .await
                .expect("failed to make Realm.AddRoute call")
                .expect("failed to add route");
        }
    }

    #[fuchsia::test]
    async fn build_called_twice() {
        let realm_node = RealmNode2::new();

        let (builder_proxy, _builder_stream_task) =
            launch_builder_task(realm_node, resolver::Registry::new(), Arc::new(Mutex::new(None)));

        let (runner_client_end, runner_server_end) = create_endpoints().unwrap();
        drop(runner_server_end);
        let res =
            builder_proxy.build(runner_client_end).await.expect("failed to send build command");
        assert!(res.is_ok());

        let (runner_client_end, runner_server_end) = create_endpoints().unwrap();
        drop(runner_server_end);
        let res =
            builder_proxy.build(runner_client_end).await.expect("failed to send build command");
        assert_eq!(Err(ftest::RealmBuilderError2::BuildAlreadyCalled), res);
    }

    #[fuchsia::test]
    async fn build_empty_realm() {
        let mut tree = ComponentTree { decl: cm_rust::ComponentDecl::default(), children: vec![] };
        let (root_url, registry) = build_tree(&mut tree).await.expect("failed to build tree");
        let tree_from_resolver = ComponentTree::new_from_resolver(root_url, registry).await;
        assert_eq!(Some(tree), tree_from_resolver);
    }

    #[fuchsia::test]
    async fn building_invalid_realm_errors() {
        let mut tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: cm_rust::OfferSource::Parent,
                    source_name: "fuchsia.logger.LogSink".into(),
                    target_name: "fuchsia.logger.LogSink".into(),
                    dependency_type: cm_rust::DependencyType::Strong,

                    // This doesn't exist
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![],
        };
        let error = build_tree(&mut tree).await.expect_err("builder didn't notice invalid decl");
        assert_eq!(error, ftest::RealmBuilderError2::InvalidComponentDecl);
    }

    #[fuchsia::test]
    async fn build_realm_with_child_decl() {
        let mut tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: cm_rust::OfferSource::Parent,
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    source_name: "fuchsia.logger.LogSink".into(),
                    target_name: "fuchsia.logger.LogSink".into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })],
                children: vec![cm_rust::ChildDecl {
                    name: "a".to_string(),
                    url: "test://a".to_string(),
                    startup: fsys::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                }],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![],
        };
        let (root_url, registry) = build_tree(&mut tree).await.expect("failed to build tree");
        let tree_from_resolver = ComponentTree::new_from_resolver(root_url, registry).await;
        assert_eq!(Some(tree), tree_from_resolver);
    }

    #[fuchsia::test]
    async fn build_realm_with_mutable_child() {
        let mut tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: cm_rust::OfferSource::Parent,
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    source_name: "fuchsia.logger.LogSink".into(),
                    target_name: "fuchsia.logger.LogSink".into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: cm_rust::ComponentDecl::default(), children: vec![] },
            )],
        };
        let (root_url, registry) = build_tree(&mut tree).await.expect("failed to build tree");
        let tree_from_resolver = ComponentTree::new_from_resolver(root_url, registry).await;
        assert_eq!(Some(tree), tree_from_resolver);
    }

    #[fuchsia::test]
    async fn build_realm_with_child_decl_and_mutable_child() {
        let mut tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: cm_rust::OfferSource::Parent,
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    source_name: "fuchsia.logger.LogSink".into(),
                    target_name: "fuchsia.logger.LogSink".into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })],
                children: vec![cm_rust::ChildDecl {
                    name: "a".to_string(),
                    url: "test://a".to_string(),
                    startup: fsys::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                }],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "b".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: cm_rust::ComponentDecl::default(), children: vec![] },
            )],
        };
        let (root_url, registry) = build_tree(&mut tree).await.expect("failed to build tree");
        let tree_from_resolver = ComponentTree::new_from_resolver(root_url, registry).await;
        assert_eq!(Some(tree), tree_from_resolver);
    }

    #[fuchsia::test]
    async fn build_realm_with_mutable_grandchild() {
        let mut tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: cm_rust::OfferSource::Parent,
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    source_name: "fuchsia.logger.LogSink".into(),
                    target_name: "fuchsia.logger.LogSink".into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree {
                    decl: cm_rust::ComponentDecl {
                        offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::Parent,
                            target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                                name: "b".to_string(),
                                collection: None,
                            }),
                            source_name: "fuchsia.logger.LogSink".into(),
                            target_name: "fuchsia.logger.LogSink".into(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                    children: vec![(
                        "b".to_string(),
                        ftest::ChildOptions::EMPTY,
                        ComponentTree { decl: cm_rust::ComponentDecl::default(), children: vec![] },
                    )],
                },
            )],
        };
        let (root_url, registry) = build_tree(&mut tree).await.expect("failed to build tree");
        let tree_from_resolver = ComponentTree::new_from_resolver(root_url, registry).await;
        assert_eq!(Some(tree), tree_from_resolver);
    }

    #[fuchsia::test]
    async fn build_realm_with_eager_mutable_child() {
        let mut tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: cm_rust::OfferSource::Parent,
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    source_name: "fuchsia.logger.LogSink".into(),
                    target_name: "fuchsia.logger.LogSink".into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions {
                    startup: Some(fcdecl::StartupMode::Eager),
                    ..ftest::ChildOptions::EMPTY
                },
                ComponentTree { decl: cm_rust::ComponentDecl::default(), children: vec![] },
            )],
        };
        let (root_url, registry) = build_tree(&mut tree).await.expect("failed to build tree");
        let tree_from_resolver = ComponentTree::new_from_resolver(root_url, registry).await;
        assert_eq!(Some(tree), tree_from_resolver);
    }

    #[fuchsia::test]
    async fn build_realm_with_mutable_child_in_a_new_environment() {
        let mut tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: cm_rust::OfferSource::Parent,
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    source_name: "fuchsia.logger.LogSink".into(),
                    target_name: "fuchsia.logger.LogSink".into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })],
                environments: vec![cm_rust::EnvironmentDecl {
                    name: "new-env".to_string(),
                    extends: fsys::EnvironmentExtends::None,
                    resolvers: vec![cm_rust::ResolverRegistration {
                        resolver: "test".try_into().unwrap(),
                        source: cm_rust::RegistrationSource::Parent,
                        scheme: "test".to_string(),
                    }],
                    runners: vec![],
                    debug_capabilities: vec![],
                    stop_timeout_ms: Some(1),
                }],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions {
                    environment: Some("new-env".to_string()),
                    ..ftest::ChildOptions::EMPTY
                },
                ComponentTree { decl: cm_rust::ComponentDecl::default(), children: vec![] },
            )],
        };
        let (root_url, registry) = build_tree(&mut tree).await.expect("failed to build tree");
        let tree_from_resolver = ComponentTree::new_from_resolver(root_url, registry).await;
        assert_eq!(Some(tree), tree_from_resolver);
    }

    #[fuchsia::test]
    async fn build_realm_with_mutable_child_with_on_terminate() {
        let mut tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: cm_rust::OfferSource::Parent,
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    source_name: "fuchsia.logger.LogSink".into(),
                    target_name: "fuchsia.logger.LogSink".into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions {
                    on_terminate: Some(fcdecl::OnTerminate::Reboot),
                    ..ftest::ChildOptions::EMPTY
                },
                ComponentTree { decl: cm_rust::ComponentDecl::default(), children: vec![] },
            )],
        };
        let (root_url, registry) = build_tree(&mut tree).await.expect("failed to build tree");
        let tree_from_resolver = ComponentTree::new_from_resolver(root_url, registry).await;
        assert_eq!(Some(tree), tree_from_resolver);
    }

    #[fuchsia::test]
    async fn build_fills_in_the_runner_proxy() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();

        // Add two local children
        realm_and_builder_task
            .realm_proxy
            .add_local_child("a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_local_child returned an error");
        realm_and_builder_task
            .realm_proxy
            .add_local_child("b", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_local_child")
            .expect("add_local_child returned an error");

        // Confirm that the local component runner has entries for the two children we just added
        let local_component_proxies = realm_and_builder_task.runner.local_component_proxies().await;
        // "a" was added first, so it gets 0
        assert!(local_component_proxies.contains_key(&"0".to_string()));
        // "b" was added second, so it gets 1
        assert!(local_component_proxies.contains_key(&"1".to_string()));

        // Confirm that the entries in the local_components runner for these children does not have a
        // `fcrunner::ComponentRunnerProxy` for these children, as this value is supposed to be
        // populated with the channel provided by `Builder.Build`, and we haven't called that yet.
        let get_runner_proxy =
            |local_component_proxies: &HashMap<_, _>, id: &str| match local_component_proxies
                .clone()
                .remove(&id.to_string())
            {
                Some(runner::ControlHandleOrRunnerProxy::RunnerProxy(rp)) => rp,
                Some(runner::ControlHandleOrRunnerProxy::ControlHandle(_)) => {
                    panic!("unexpected control handle")
                }
                None => panic!("value unexpectedly missing"),
            };

        assert!(get_runner_proxy(&local_component_proxies, "0").lock().await.is_none());
        assert!(get_runner_proxy(&local_component_proxies, "1").lock().await.is_none());

        // Call `Builder.Build`, and confirm that the entries for our local children in the local
        // component runner now has a `fcrunner::ComponentRunnerProxy`.
        let _ = realm_and_builder_task.call_build().await.expect("build failed");

        assert!(get_runner_proxy(&local_component_proxies, "0").lock().await.is_some());
        assert!(get_runner_proxy(&local_component_proxies, "1").lock().await.is_some());

        // Confirm that the `fcrunner::ComponentRunnerProxy` for one of the local children has the
        // value we expect, by writing a value into it and seeing the same value come out on the
        // other side of our channel.
        let example_program = fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: "hippos".to_string(),
                value: Some(Box::new(fdata::DictionaryValue::Str("rule!".to_string()))),
            }]),
            ..fdata::Dictionary::EMPTY
        };

        let (_controller_client_end, controller_server_end) =
            create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();
        let runner_proxy_for_a =
            get_runner_proxy(&local_component_proxies, "0").lock().await.clone().unwrap();
        runner_proxy_for_a
            .start(
                fcrunner::ComponentStartInfo {
                    program: Some(example_program.clone()),
                    ..fcrunner::ComponentStartInfo::EMPTY
                },
                controller_server_end,
            )
            .expect("failed to write start message");
        assert_matches!(
            realm_and_builder_task
                .runner_stream
                .try_next()
                .await
                .expect("failed to read from runner_stream"),
            Some(fcrunner::ComponentRunnerRequest::Start { start_info, .. })
                if start_info.program == Some(example_program)
        );
    }

    #[fuchsia::test]
    async fn add_child() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                children: vec![cm_rust::ChildDecl {
                    name: "a".to_string(),
                    url: "test:///a".to_string(),
                    startup: fsys::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                }],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_child_with_invalid_manifest_extension() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        let err = realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a.cmx", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_child was supposed to return an error");
        assert_eq!(err, ftest::RealmBuilderError2::InvalidManifestExtension);
    }

    #[fuchsia::test]
    async fn add_absolute_child_that_conflicts_with_child_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_child was supposed to return an error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_absolute_child_that_conflicts_with_mutable_child() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "#meta/realm_builder_server_unit_tests.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_child was supposed to return an error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_relative_child_that_conflicts_with_child_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_child("a", "#meta/realm_builder_server_unit_tests.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_child was supposed to return an error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_relative_child_that_conflicts_with_mutable_child() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "#meta/realm_builder_server_unit_tests.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_child("a", "#meta/realm_builder_server_unit_tests.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_child was supposed to return an error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_relative_child() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "#meta/realm_builder_server_unit_tests.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;

        let a_decl_file = io_util::open_file_in_namespace(
            "/pkg/meta/realm_builder_server_unit_tests.cm",
            fio::OPEN_RIGHT_READABLE,
        )
        .expect("failed to open manifest");
        let a_decl = io_util::read_file_fidl::<fcdecl::Component>(&a_decl_file)
            .await
            .expect("failed to read manifest")
            .fidl_into_native();

        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl::default(),
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: a_decl, children: vec![] },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_relative_child_with_child() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child(
                "realm_with_child",
                "#meta/realm_with_child.cm",
                ftest::ChildOptions {
                    startup: Some(fcdecl::StartupMode::Eager),
                    ..ftest::ChildOptions::EMPTY
                },
            )
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;

        let a_decl_file =
            io_util::open_file_in_namespace("/pkg/meta/a.cm", fio::OPEN_RIGHT_READABLE)
                .expect("failed to open manifest");
        let a_decl = io_util::read_file_fidl::<fcdecl::Component>(&a_decl_file)
            .await
            .expect("failed to read manifest")
            .fidl_into_native();

        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl::default(),
            children: vec![(
                "realm_with_child".to_string(),
                ftest::ChildOptions {
                    startup: Some(fcdecl::StartupMode::Eager),
                    ..ftest::ChildOptions::EMPTY
                },
                ComponentTree {
                    decl: cm_rust::ComponentDecl {
                        children: vec![cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "test:///b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            on_terminate: None,
                            environment: None,
                        }],
                        ..cm_rust::ComponentDecl::default()
                    },
                    children: vec![(
                        "a".to_string(),
                        ftest::ChildOptions::EMPTY,
                        ComponentTree { decl: a_decl, children: vec![] },
                    )],
                },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_legacy_child() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_legacy_child("a", EXAMPLE_LEGACY_URL, ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let expected_a_decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: runner::LEGACY_URL_KEY.to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(
                            EXAMPLE_LEGACY_URL.to_string(),
                        ))),
                    }]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            ..cm_rust::ComponentDecl::default()
        };
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl::default(),
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: expected_a_decl, children: vec![] },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_legacy_child_that_conflicts_with_child_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_legacy_child("a", EXAMPLE_LEGACY_URL, ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_legacy_child was supposed to error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_legacy_child_that_conflicts_with_mutable_child() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "#meta/realm_builder_server_unit_tests.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_legacy_child("a", EXAMPLE_LEGACY_URL, ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_legacy_child was supposed to error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_legacy_child_with_modern_url_returns_error() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        let err = realm_and_builder_task
            .realm_proxy
            .add_legacy_child("a", "#meta/a.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_legacy_child was supposed to error");
        assert_eq!(err, ftest::RealmBuilderError2::InvalidManifestExtension);
    }

    #[fuchsia::test]
    async fn add_child_with_legacy_url_returns_error() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        let err = realm_and_builder_task
            .realm_proxy
            .add_child("a", EXAMPLE_LEGACY_URL, ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_legacy_child was supposed to error");
        assert_eq!(err, ftest::RealmBuilderError2::InvalidManifestExtension);
    }

    #[fuchsia::test]
    async fn add_child_from_decl() {
        let a_decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some("hippo".try_into().unwrap()),
                info: fdata::Dictionary::EMPTY,
            }),
            uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                source: cm_rust::UseSource::Parent,
                source_name: "example.Hippo".into(),
                target_path: "/svc/example.Hippo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };

        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child_from_decl("a", a_decl.clone().native_into_fidl(), ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child_from_decl returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl::default(),
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: a_decl, children: vec![] },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_child_from_decl_that_conflicts_with_child_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_child_from_decl("a", fcdecl::Component::EMPTY, ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_legacy_child was supposed to error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_child_from_decl_that_conflicts_with_mutable_child() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "#meta/realm_builder_server_unit_tests.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_child_from_decl("a", fcdecl::Component::EMPTY, ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_legacy_child was supposed to error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_local_child() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_local_child("a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let a_decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![
                        fdata::DictionaryEntry {
                            key: runner::LOCAL_COMPONENT_ID_KEY.to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("0".to_string()))),
                        },
                        fdata::DictionaryEntry {
                            key: runner::LOCAL_COMPONENT_NAME_KEY.to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("a".to_string()))),
                        },
                    ]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            ..cm_rust::ComponentDecl::default()
        };
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl::default(),
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: a_decl, children: vec![] },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
        assert!(realm_and_builder_task
            .runner
            .local_component_proxies()
            .await
            .contains_key(&"0".to_string()));
    }

    #[fuchsia::test]
    async fn add_local_child_that_conflicts_with_child_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_local_child("a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_local_child was supposed to error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_local_child_that_conflicts_with_mutable_child() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "#meta/realm_builder_server_unit_tests.cm", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_local_child("a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect_err("add_local_child was supposed to error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildAlreadyExists);
    }

    #[fuchsia::test]
    async fn add_route() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .add_child_or_panic("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await;
        realm_and_builder_task
            .add_child_or_panic("b", "test:///b", ftest::ChildOptions::EMPTY)
            .await;

        // Assert that parent -> child capabilities generate proper offer decls.
        realm_and_builder_task
            .add_route_or_panic(
                vec![
                    ftest::Capability2::Protocol(ftest::Protocol {
                        name: Some("fuchsia.examples.Hippo".to_owned()),
                        as_: Some("fuchsia.examples.Elephant".to_owned()),
                        type_: Some(fcdecl::DependencyType::Strong),
                        ..ftest::Protocol::EMPTY
                    }),
                    ftest::Capability2::Directory(ftest::Directory {
                        name: Some("config-data".to_owned()),
                        rights: Some(fio2::RW_STAR_DIR),
                        subdir: Some("component".to_owned()),
                        ..ftest::Directory::EMPTY
                    }),
                ],
                fcdecl::Ref::Parent(fcdecl::ParentRef {}),
                vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "a".to_owned(),
                    collection: None,
                })],
            )
            .await;

        // Assert that child -> child capabilities generate proper offer decls.
        realm_and_builder_task
            .add_route_or_panic(
                vec![ftest::Capability2::Protocol(ftest::Protocol {
                    name: Some("fuchsia.examples.Echo".to_owned()),
                    ..ftest::Protocol::EMPTY
                })],
                fcdecl::Ref::Child(fcdecl::ChildRef { name: "a".to_owned(), collection: None }),
                vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "b".to_owned(),
                    collection: None,
                })],
            )
            .await;

        // Assert that child -> parent capabilities generate proper expose decls.
        realm_and_builder_task
            .add_route_or_panic(
                vec![ftest::Capability2::Protocol(ftest::Protocol {
                    name: Some("fuchsia.examples.Echo".to_owned()),
                    type_: Some(fcdecl::DependencyType::Weak),
                    ..ftest::Protocol::EMPTY
                })],
                fcdecl::Ref::Child(fcdecl::ChildRef { name: "a".to_owned(), collection: None }),
                vec![fcdecl::Ref::Parent(fcdecl::ParentRef {})],
            )
            .await;

        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                children: vec![
                    cm_rust::ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                    cm_rust::ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                ],
                offers: vec![
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: cm_rust::CapabilityName("fuchsia.examples.Hippo".to_owned()),
                        target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                            name: "a".to_owned(),
                            collection: None,
                        }),
                        target_name: cm_rust::CapabilityName(
                            "fuchsia.examples.Elephant".to_owned(),
                        ),
                        dependency_type: cm_rust::DependencyType::Strong,
                    }),
                    cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: cm_rust::CapabilityName("config-data".to_owned()),
                        target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                            name: "a".to_owned(),
                            collection: None,
                        }),
                        target_name: cm_rust::CapabilityName("config-data".to_owned()),
                        dependency_type: cm_rust::DependencyType::Strong,
                        rights: Some(fio2::RW_STAR_DIR),
                        subdir: Some(PathBuf::from("component")),
                    }),
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Child(cm_rust::ChildRef {
                            name: "a".to_owned(),
                            collection: None,
                        }),
                        source_name: cm_rust::CapabilityName("fuchsia.examples.Echo".to_owned()),
                        target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                            name: "b".to_owned(),
                            collection: None,
                        }),
                        target_name: cm_rust::CapabilityName("fuchsia.examples.Echo".to_owned()),
                        dependency_type: cm_rust::DependencyType::Strong,
                    }),
                ],
                exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                    source: cm_rust::ExposeSource::Child("a".to_owned()),
                    source_name: cm_rust::CapabilityName("fuchsia.examples.Echo".to_owned()),
                    target: cm_rust::ExposeTarget::Parent,
                    target_name: cm_rust::CapabilityName("fuchsia.examples.Echo".to_owned()),
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_route_duplicate_decls() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child_from_decl("a", fcdecl::Component::EMPTY, ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call AddChildFromDecl")
            .expect("call failed");
        realm_and_builder_task
            .add_child_or_panic("b", "test:///b", ftest::ChildOptions::EMPTY)
            .await;
        realm_and_builder_task
            .add_child_or_panic("c", "test:///c", ftest::ChildOptions::EMPTY)
            .await;

        // Routing protocol from `a` should yield one and only one ExposeDecl.
        realm_and_builder_task
            .add_route_or_panic(
                vec![ftest::Capability2::Protocol(ftest::Protocol {
                    name: Some("fuchsia.examples.Hippo".to_owned()),
                    ..ftest::Protocol::EMPTY
                })],
                fcdecl::Ref::Child(fcdecl::ChildRef { name: "a".to_owned(), collection: None }),
                vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "b".to_owned(),
                    collection: None,
                })],
            )
            .await;
        realm_and_builder_task
            .add_route_or_panic(
                vec![ftest::Capability2::Protocol(ftest::Protocol {
                    name: Some("fuchsia.examples.Hippo".to_owned()),
                    ..ftest::Protocol::EMPTY
                })],
                fcdecl::Ref::Child(fcdecl::ChildRef { name: "a".to_owned(), collection: None }),
                vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "c".to_owned(),
                    collection: None,
                })],
            )
            .await;

        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                children: vec![
                    cm_rust::ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                    cm_rust::ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                ],
                offers: vec![
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Child(cm_rust::ChildRef {
                            name: "a".to_owned(),
                            collection: None,
                        }),
                        source_name: cm_rust::CapabilityName("fuchsia.examples.Hippo".to_owned()),
                        target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                            name: "b".to_owned(),
                            collection: None,
                        }),
                        target_name: cm_rust::CapabilityName("fuchsia.examples.Hippo".to_owned()),
                        dependency_type: cm_rust::DependencyType::Strong,
                    }),
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Child(cm_rust::ChildRef {
                            name: "a".to_owned(),
                            collection: None,
                        }),
                        source_name: cm_rust::CapabilityName("fuchsia.examples.Hippo".to_owned()),
                        target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                            name: "c".to_owned(),
                            collection: None,
                        }),
                        target_name: cm_rust::CapabilityName("fuchsia.examples.Hippo".to_owned()),
                        dependency_type: cm_rust::DependencyType::Strong,
                    }),
                ],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "a".to_owned(),
                ftest::ChildOptions::EMPTY,
                ComponentTree {
                    decl: cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: cm_rust::CapabilityName("fuchsia.examples.Hippo".to_owned()),
                                source_path: Some(cm_rust::CapabilityPath {
                                    dirname: "/svc".to_owned(),
                                    basename: "fuchsia.examples.Hippo".to_owned(),
                                }),
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Self_,
                            source_name: cm_rust::CapabilityName(
                                "fuchsia.examples.Hippo".to_owned(),
                            ),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: cm_rust::CapabilityName(
                                "fuchsia.examples.Hippo".to_owned(),
                            ),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                    children: vec![],
                },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_route_mutates_decl() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();

        realm_and_builder_task
            .realm_proxy
            .add_child_from_decl("a", fcdecl::Component::EMPTY, ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call AddChildFromDecl")
            .expect("call failed");
        realm_and_builder_task
            .add_child_or_panic("b", "test:///b", ftest::ChildOptions::EMPTY)
            .await;
        realm_and_builder_task
            .add_child_or_panic("c", "test:///c", ftest::ChildOptions::EMPTY)
            .await;
        realm_and_builder_task
            .add_route_or_panic(
                vec![ftest::Capability2::Protocol(ftest::Protocol {
                    name: Some("fuchsia.examples.Echo".to_owned()),
                    ..ftest::Protocol::EMPTY
                })],
                fcdecl::Ref::Child(fcdecl::ChildRef { name: "a".to_owned(), collection: None }),
                vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "b".to_owned(),
                    collection: None,
                })],
            )
            .await;
        realm_and_builder_task
            .add_route_or_panic(
                vec![ftest::Capability2::Protocol(ftest::Protocol {
                    name: Some("fuchsia.examples.RandonNumberGenerator".to_owned()),
                    ..ftest::Protocol::EMPTY
                })],
                fcdecl::Ref::Child(fcdecl::ChildRef { name: "c".to_owned(), collection: None }),
                vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "a".to_owned(),
                    collection: None,
                })],
            )
            .await;

        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                children: vec![
                    cm_rust::ChildDecl {
                        name: "b".to_owned(),
                        url: "test:///b".to_owned(),
                        startup: fsys::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                    cm_rust::ChildDecl {
                        name: "c".to_owned(),
                        url: "test:///c".to_owned(),
                        startup: fsys::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                ],
                offers: vec![
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Child(cm_rust::ChildRef {
                            name: "a".to_owned(),
                            collection: None,
                        }),
                        source_name: cm_rust::CapabilityName("fuchsia.examples.Echo".to_owned()),
                        target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                            name: "b".to_owned(),
                            collection: None,
                        }),
                        target_name: cm_rust::CapabilityName("fuchsia.examples.Echo".to_owned()),
                        dependency_type: cm_rust::DependencyType::Strong,
                    }),
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Child(cm_rust::ChildRef {
                            name: "c".to_owned(),
                            collection: None,
                        }),
                        source_name: cm_rust::CapabilityName(
                            "fuchsia.examples.RandonNumberGenerator".to_owned(),
                        ),
                        target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                            name: "a".to_owned(),
                            collection: None,
                        }),
                        target_name: cm_rust::CapabilityName(
                            "fuchsia.examples.RandonNumberGenerator".to_owned(),
                        ),
                        dependency_type: cm_rust::DependencyType::Strong,
                    }),
                ],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "a".to_owned(),
                ftest::ChildOptions::EMPTY,
                ComponentTree {
                    decl: cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: cm_rust::CapabilityName("fuchsia.examples.Echo".to_owned()),
                                source_path: Some(cm_rust::CapabilityPath {
                                    dirname: "/svc".to_owned(),
                                    basename: "fuchsia.examples.Echo".to_owned(),
                                }),
                            },
                        )],
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Parent,
                            source_name: cm_rust::CapabilityName(
                                "fuchsia.examples.RandonNumberGenerator".to_owned(),
                            ),
                            target_path: cm_rust::CapabilityPath {
                                dirname: "/svc".to_owned(),
                                basename: "fuchsia.examples.RandonNumberGenerator".to_owned(),
                            },
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Self_,
                            source_name: cm_rust::CapabilityName(
                                "fuchsia.examples.Echo".to_owned(),
                            ),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: cm_rust::CapabilityName(
                                "fuchsia.examples.Echo".to_owned(),
                            ),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                    children: vec![],
                },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_child_to_child_realm() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        let (child_realm_proxy, child_realm_server_end) =
            create_proxy::<ftest::RealmMarker>().unwrap();
        realm_and_builder_task
            .realm_proxy
            .add_child_realm("a", ftest::ChildOptions::EMPTY, child_realm_server_end)
            .await
            .expect("failed to call add_child_realm")
            .expect("add_child_realm returned an error");
        child_realm_proxy
            .add_child("b", "test:///b", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl::default(),
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree {
                    decl: cm_rust::ComponentDecl {
                        children: vec![cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "test:///b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            on_terminate: None,
                            environment: None,
                        }],
                        ..cm_rust::ComponentDecl::default()
                    },
                    children: vec![],
                },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn get_component_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_local_child("a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let a_decl = realm_and_builder_task
            .realm_proxy
            .get_component_decl("a")
            .await
            .expect("failed to call get_component_decl")
            .expect("get_component_decl returned an error");
        assert_eq!(
            a_decl,
            cm_rust::ComponentDecl {
                program: Some(cm_rust::ProgramDecl {
                    runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                    info: fdata::Dictionary {
                        entries: Some(vec![
                            fdata::DictionaryEntry {
                                key: runner::LOCAL_COMPONENT_ID_KEY.to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str("0".to_string()))),
                            },
                            fdata::DictionaryEntry {
                                key: runner::LOCAL_COMPONENT_NAME_KEY.to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str("a".to_string()))),
                            },
                        ]),
                        ..fdata::Dictionary::EMPTY
                    },
                }),
                ..cm_rust::ComponentDecl::default()
            }
            .native_into_fidl(),
        );
    }

    #[fuchsia::test]
    async fn get_component_decl_for_nonexistent_child() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        let err = realm_and_builder_task
            .realm_proxy
            .get_component_decl("a")
            .await
            .expect("failed to call get_component_decl")
            .expect_err("get_component_decl did not return an error");
        assert_eq!(err, ftest::RealmBuilderError2::NoSuchChild);
    }

    #[fuchsia::test]
    async fn get_component_decl_for_child_behind_child_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .get_component_decl("a")
            .await
            .expect("failed to call get_component_decl")
            .expect_err("get_component_decl did not return an error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildDeclNotVisible);
    }

    #[fuchsia::test]
    async fn replace_component_decl() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_local_child("a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let mut a_decl = realm_and_builder_task
            .realm_proxy
            .get_component_decl("a")
            .await
            .expect("failed to call get_component_decl")
            .expect("get_component_decl returned an error")
            .fidl_into_native();
        a_decl.uses.push(cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
            source: cm_rust::UseSource::Parent,
            source_name: "example.Hippo".into(),
            target_path: "/svc/example.Hippo".try_into().unwrap(),
            dependency_type: cm_rust::DependencyType::Strong,
        }));
        realm_and_builder_task
            .realm_proxy
            .replace_component_decl("a", a_decl.clone().native_into_fidl())
            .await
            .expect("failed to call replace_component_decl")
            .expect("replace_component_decl returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl::default(),
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: a_decl, children: vec![] },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[test_case(vec![
        create_valid_capability()],
        fcdecl::Ref::Child(fcdecl::ChildRef {
            name: "unknown".to_owned(),
            collection: None
        }),
        vec![],
        ftest::RealmBuilderError2::NoSuchSource ; "no_such_source")]
    #[test_case(vec![
        create_valid_capability()],
        fcdecl::Ref::Child(fcdecl::ChildRef {
            name: "a".to_owned(),
            collection: None
        }),
        vec![
            fcdecl::Ref::Child(fcdecl::ChildRef {
                name: "unknown".to_owned(),
                collection: None
            }),
        ],
        ftest::RealmBuilderError2::NoSuchTarget ; "no_such_target")]
    #[test_case(vec![
        create_valid_capability()],
        fcdecl::Ref::Child(fcdecl::ChildRef {
            name: "a".to_owned(),
            collection: None
        }),
        vec![
            fcdecl::Ref::Child(fcdecl::ChildRef {
                name: "a".to_owned(),
                collection: None
            }),
        ],
        ftest::RealmBuilderError2::SourceAndTargetMatch ; "source_and_target_match")]
    #[test_case(vec![],
        fcdecl::Ref::Child(fcdecl::ChildRef {
            name: "a".to_owned(),
            collection: None
        }),
        vec![fcdecl::Ref::Parent(fcdecl::ParentRef {})],
        ftest::RealmBuilderError2::CapabilitiesEmpty ; "capabilities_empty")]
    #[test_case(vec![ftest::Capability2::unknown(100, vec![])],
        fcdecl::Ref::Child(fcdecl::ChildRef {
            name: "a".to_owned(),
            collection: None
        }),
        vec![fcdecl::Ref::Parent(fcdecl::ParentRef {})],
        ftest::RealmBuilderError2::CapabilityInvalid ; "invalid_capability")]
    #[fuchsia::test]
    async fn add_route_error(
        mut capabilities: Vec<ftest::Capability2>,
        mut from: fcdecl::Ref,
        mut to: Vec<fcdecl::Ref>,
        expected_err: ftest::RealmBuilderError2,
    ) {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .add_child_or_panic("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await;

        let err = realm_and_builder_task
            .realm_proxy
            .add_route(&mut capabilities.iter_mut(), &mut from, &mut to.iter_mut())
            .await
            .expect("failed to call AddRoute")
            .expect_err("AddRoute succeeded unexpectedly");

        assert_eq!(err, expected_err);
    }

    fn create_valid_capability() -> ftest::Capability2 {
        ftest::Capability2::Protocol(ftest::Protocol {
            name: Some("fuchsia.examples.Hippo".to_owned()),
            as_: None,
            type_: None,
            ..ftest::Protocol::EMPTY
        })
    }

    #[fuchsia::test]
    async fn add_local_child_to_child_realm() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        let (child_realm_proxy, child_realm_server_end) =
            create_proxy::<ftest::RealmMarker>().unwrap();
        realm_and_builder_task
            .realm_proxy
            .add_child_realm("a", ftest::ChildOptions::EMPTY, child_realm_server_end)
            .await
            .expect("failed to call add_child_realm")
            .expect("add_child_realm returned an error");
        child_realm_proxy
            .add_local_child("b", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let b_decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![
                        fdata::DictionaryEntry {
                            key: runner::LOCAL_COMPONENT_ID_KEY.to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("0".to_string()))),
                        },
                        fdata::DictionaryEntry {
                            key: runner::LOCAL_COMPONENT_NAME_KEY.to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("a/b".to_string()))),
                        },
                    ]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            ..cm_rust::ComponentDecl::default()
        };
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl::default(),
            children: vec![(
                "a".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree {
                    decl: cm_rust::ComponentDecl::default(),
                    children: vec![(
                        "b".to_string(),
                        ftest::ChildOptions::EMPTY,
                        ComponentTree { decl: b_decl, children: vec![] },
                    )],
                },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn replace_component_decl_immutable_program() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_local_child("a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .replace_component_decl("a", fcdecl::Component::EMPTY)
            .await
            .expect("failed to call replace_component_decl")
            .expect_err("replace_component_decl did not return an error");
        assert_eq!(err, ftest::RealmBuilderError2::ImmutableProgram);
    }

    #[fuchsia::test]
    async fn replace_component_decl_for_nonexistent_child() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        let err = realm_and_builder_task
            .realm_proxy
            .replace_component_decl("a", fcdecl::Component::EMPTY)
            .await
            .expect("failed to call replace_component_decl")
            .expect_err("replace_component_decl did not return an error");
        assert_eq!(err, ftest::RealmBuilderError2::NoSuchChild);
    }

    #[fuchsia::test]
    async fn replace_component_decl_for_child_behind_child_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .replace_component_decl("a", fcdecl::Component::EMPTY)
            .await
            .expect("failed to call replace_component_decl")
            .expect_err("replace_component_decl did not return an error");
        assert_eq!(err, ftest::RealmBuilderError2::ChildDeclNotVisible);
    }

    // TODO(88429): The following test is impossible to write until sub-realms are supported
    // #[fuchsia::test]
    // async fn replace_component_decl_where_decl_children_conflict_with_mutable_children() {
    // }

    // Everything below this line are tests for the old fuchsia.component.test.RealmBuilder logic,
    // and will eventually be deleted.

    #[fuchsia::test]
    async fn set_component() {
        let mut realm = RealmNode::default();

        let root_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("a".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let mut a_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("b".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };

        realm
            .set_component(
                Moniker::default(),
                ftest::Component::Decl(root_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(a_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                ftest::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();

        a_decl.children.push(cm_rust::ChildDecl {
            name: "b".to_string(),
            url: "fuchsia-pkg://b".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        });

        assert_eq!(
            realm.get_node_mut(&Moniker::default(), GetBehavior::ErrorIfMissing).unwrap().decl,
            root_decl
        );
        assert_eq!(
            realm.get_node_mut(&"a".into(), GetBehavior::ErrorIfMissing).unwrap().decl,
            a_decl
        );
    }

    #[fuchsia::test]
    async fn contains_component() {
        let mut realm = RealmNode::default();

        let root_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("a".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let a_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("b".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            children: vec![cm_rust::ChildDecl {
                name: "b".to_string(),
                url: "fuchsia-pkg://b".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            ..cm_rust::ComponentDecl::default()
        };

        realm
            .set_component(
                Moniker::default(),
                ftest::Component::Decl(root_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(a_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();

        assert_eq!(true, realm.contains(Moniker::default()));
        assert_eq!(true, realm.contains("a".into()));
        assert_eq!(true, realm.contains("a/b".into()));
        assert_eq!(false, realm.contains("a/a".into()));
        assert_eq!(false, realm.contains("b".into()));
    }

    #[fuchsia::test]
    async fn mark_as_eager() {
        let mut realm = RealmNode::default();

        let root_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("a".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let a_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("b".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let b_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("c".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            children: vec![cm_rust::ChildDecl {
                name: "c".to_string(),
                url: "fuchsia-pkg://c".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            ..cm_rust::ComponentDecl::default()
        };

        realm
            .set_component(
                Moniker::default(),
                ftest::Component::Decl(root_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(a_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                ftest::Component::Decl(b_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();

        realm.mark_as_eager("a/b/c".into()).unwrap();
        assert_eq!(
            realm.get_node_mut(&"a".into(), GetBehavior::ErrorIfMissing).unwrap().eager,
            true
        );
        assert_eq!(
            realm.get_node_mut(&"a/b".into(), GetBehavior::ErrorIfMissing).unwrap().decl.children,
            vec![cm_rust::ChildDecl {
                name: "c".to_string(),
                url: "fuchsia-pkg://c".to_string(),
                startup: fsys::StartupMode::Eager,
                environment: None,
                on_terminate: None,
            }]
        );
    }

    fn check_results(
        mut realm: RealmNode,
        expected_results: Vec<(&'static str, cm_rust::ComponentDecl)>,
    ) {
        assert!(!expected_results.is_empty(), "can't build an empty realm");

        for (component, decl) in expected_results {
            assert_eq!(
                realm
                    .get_node_mut(&component.into(), GetBehavior::ErrorIfMissing)
                    .expect("component is missing from realm")
                    .decl,
                decl,
                "decl in realm doesn't match expectations for component  {:?}",
                component
            );
        }
    }

    #[fuchsia::test]
    async fn missing_route_source_error() {
        let mut realm = RealmNode::default();
        realm
            .set_component("a".into(), ftest::Component::Url("fuchsia-pkg://a".to_string()), &None)
            .await
            .unwrap();
        let res = realm.route_capability(ftest::CapabilityRoute {
            capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                name: Some("fidl.examples.routing.echo.Echo".to_string()),
                ..ftest::ProtocolCapability::EMPTY
            })),
            source: Some(ftest::RouteEndpoint::Component("b".to_string())),
            targets: Some(vec![ftest::RouteEndpoint::Component("a".to_string())]),
            ..ftest::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(Error::MissingRouteSource(m)) if m == "b".into() => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn empty_route_targets() {
        let mut realm = RealmNode::default();
        realm
            .set_component("a".into(), ftest::Component::Url("fuchsia-pkg://a".to_string()), &None)
            .await
            .unwrap();
        let res = realm.route_capability(ftest::CapabilityRoute {
            capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                name: Some("fidl.examples.routing.echo.Echo".to_string()),
                ..ftest::ProtocolCapability::EMPTY
            })),
            source: Some(ftest::RouteEndpoint::Component("a".to_string())),
            targets: Some(vec![]),
            ..ftest::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(e) => {
                if let Error::RouteTargetsEmpty = e {
                    ()
                } else {
                    panic!("unexpected error: {:?}", e);
                }
            }
        }
    }

    #[fuchsia::test]
    async fn multiple_offer_same_source() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "1/src".into(),
                ftest::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "2/target_1".into(),
                ftest::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "2/target_2".into(),
                ftest::Component::Url("fuchsia-pkg://c".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("1/src".to_string())),
                targets: Some(vec![
                    ftest::RouteEndpoint::Component("2/target_1".to_string()),
                    ftest::RouteEndpoint::Component("2/target_2".to_string()),
                ]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();
    }

    #[fuchsia::test]
    async fn same_capability_from_different_sources_in_same_node_error() {
        {
            let mut realm = RealmNode::default();
            realm
                .set_component(
                    "1/a".into(),
                    ftest::Component::Url("fuchsia-pkg://a".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "1/b".into(),
                    ftest::Component::Url("fuchsia-pkg://b".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "2/c".into(),
                    ftest::Component::Url("fuchsia-pkg://c".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "2/d".into(),
                    ftest::Component::Url("fuchsia-pkg://d".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .route_capability(ftest::CapabilityRoute {
                    capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..ftest::ProtocolCapability::EMPTY
                    })),
                    source: Some(ftest::RouteEndpoint::Component("1/a".to_string())),
                    targets: Some(vec![ftest::RouteEndpoint::Component("2/c".to_string())]),
                    ..ftest::CapabilityRoute::EMPTY
                })
                .unwrap();
            realm
                .route_capability(ftest::CapabilityRoute {
                    capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..ftest::ProtocolCapability::EMPTY
                    })),
                    source: Some(ftest::RouteEndpoint::Component("1/b".to_string())),
                    targets: Some(vec![ftest::RouteEndpoint::Component("2/d".to_string())]),
                    ..ftest::CapabilityRoute::EMPTY
                })
                .unwrap();
            // get and set this component, to confirm that `set_component` runs `validate`
            let decl = realm.get_component_decl("1".into()).unwrap().native_into_fidl();
            let res = realm.set_component("1".into(), ftest::Component::Decl(decl), &None).await;

            match res {
                Err(Error::ValidationError(_, e)) => {
                    assert_eq!(
                        e,
                        cm_fidl_validator::error::ErrorList {
                            errs: vec![cm_fidl_validator::error::Error::DuplicateField(
                                cm_fidl_validator::error::DeclField {
                                    decl: "ExposeProtocolDecl".to_string(),
                                    field: "target_name".to_string()
                                },
                                "fidl.examples.routing.echo.Echo".to_string()
                            )]
                        }
                    );
                }
                Err(e) => panic!("unexpected error: {:?}", e),
                Ok(_) => panic!("builder commands should have errored"),
            }
        }

        {
            let mut realm = RealmNode::default();
            realm
                .set_component(
                    "1/a".into(),
                    ftest::Component::Url("fuchsia-pkg://a".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "1/b".into(),
                    ftest::Component::Url("fuchsia-pkg://b".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "2/c".into(),
                    ftest::Component::Url("fuchsia-pkg://c".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "2/d".into(),
                    ftest::Component::Url("fuchsia-pkg://d".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .route_capability(ftest::CapabilityRoute {
                    capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..ftest::ProtocolCapability::EMPTY
                    })),
                    source: Some(ftest::RouteEndpoint::Component("1/a".to_string())),
                    targets: Some(vec![ftest::RouteEndpoint::Component("1/b".to_string())]),
                    ..ftest::CapabilityRoute::EMPTY
                })
                .unwrap();
            realm
                .route_capability(ftest::CapabilityRoute {
                    capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..ftest::ProtocolCapability::EMPTY
                    })),
                    source: Some(ftest::RouteEndpoint::Component("2/c".to_string())),
                    targets: Some(vec![ftest::RouteEndpoint::Component("2/d".to_string())]),
                    ..ftest::CapabilityRoute::EMPTY
                })
                .unwrap();
        }
    }

    #[fuchsia::test]
    async fn missing_route_target_error() {
        let mut realm = RealmNode::default();
        realm
            .set_component("a".into(), ftest::Component::Url("fuchsia-pkg://a".to_string()), &None)
            .await
            .unwrap();
        let res = realm.route_capability(ftest::CapabilityRoute {
            capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                name: Some("fidl.examples.routing.echo.Echo".to_string()),
                ..ftest::ProtocolCapability::EMPTY
            })),
            source: Some(ftest::RouteEndpoint::Component("a".to_string())),
            targets: Some(vec![ftest::RouteEndpoint::Component("b".to_string())]),
            ..ftest::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(Error::MissingRouteTarget(m)) => {
                assert_eq!(m, "b".into());
            }
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[test]
    fn route_source_and_target_both_above_root_error() {
        let mut realm = RealmNode::default();
        let res = realm.route_capability(ftest::CapabilityRoute {
            capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                name: Some("fidl.examples.routing.echo.Echo".to_string()),
                ..ftest::ProtocolCapability::EMPTY
            })),
            source: Some(ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {})),
            targets: Some(vec![ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {})]),
            ..ftest::CapabilityRoute::EMPTY
        });

        match res {
            Err(Error::RouteSourceAndTargetMatch(ftest::RouteEndpoint::AboveRoot(
                ftest::AboveRoot {},
            ))) => (),
            Ok(_) => panic!("builder commands should have errored"),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn expose_storage_from_child_error() {
        let mut realm = RealmNode::default();
        realm
            .set_component("a".into(), ftest::Component::Url("fuchsia-pkg://a".to_string()), &None)
            .await
            .unwrap();
        let res = realm.route_capability(ftest::CapabilityRoute {
            capability: Some(ftest::Capability::Storage(ftest::StorageCapability {
                name: Some("foo".to_string()),
                path: Some("foo".to_string()),
                ..ftest::StorageCapability::EMPTY
            })),
            source: Some(ftest::RouteEndpoint::Component("a".to_string())),
            targets: Some(vec![ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {})]),
            ..ftest::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(Error::UnableToExpose("storage")) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn offer_storage_from_child_error() {
        let mut realm = RealmNode::default();
        realm
            .set_component("a".into(), ftest::Component::Url("fuchsia-pkg://a".to_string()), &None)
            .await
            .unwrap();
        realm
            .set_component("b".into(), ftest::Component::Url("fuchsia-pkg://b".to_string()), &None)
            .await
            .unwrap();
        let res = realm.route_capability(ftest::CapabilityRoute {
            capability: Some(ftest::Capability::Storage(ftest::StorageCapability {
                name: Some("foo".to_string()),
                path: Some("/foo".to_string()),
                ..ftest::StorageCapability::EMPTY
            })),
            source: Some(ftest::RouteEndpoint::Component("a".to_string())),
            targets: Some(vec![ftest::RouteEndpoint::Component("b".to_string())]),
            ..ftest::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(Error::UnableToExpose("storage")) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fuchsia::test]
    async fn verify_storage_routing() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Storage(ftest::StorageCapability {
                    name: Some("foo".to_string()),
                    path: Some("/bar".to_string()),
                    ..ftest::StorageCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {})),
                targets: Some(vec![ftest::RouteEndpoint::Component("a".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                            source: cm_rust::OfferSource::Parent,
                            source_name: "foo".into(),
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
                            target_name: "foo".into(),
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Storage(cm_rust::UseStorageDecl {
                            source_name: "foo".into(),
                            target_path: "/bar".try_into().unwrap(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn two_sibling_realm_no_mocks() {
        let mut realm = RealmNode::default();
        realm
            .set_component("a".into(), ftest::Component::Url("fuchsia-pkg://a".to_string()), &None)
            .await
            .unwrap();
        realm
            .set_component("b".into(), ftest::Component::Url("fuchsia-pkg://b".to_string()), &None)
            .await
            .unwrap();
        realm.mark_as_eager("b".into()).unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("b".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![(
                "",
                cm_rust::ComponentDecl {
                    offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::static_child("a".to_string()),
                        source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        target: cm_rust::OfferTarget::static_child("b".to_string()),
                        target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        dependency_type: cm_rust::DependencyType::Strong,
                    })],
                    children: vec![
                        cm_rust::ChildDecl {
                            name: "a".to_string(),
                            url: "fuchsia-pkg://a".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        },
                        cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        },
                    ],
                    ..cm_rust::ComponentDecl::default()
                },
            )],
        );
    }

    #[fuchsia::test]
    async fn two_sibling_realm_both_mocks() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "b".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("b".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::static_child("a".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("b".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                source_path: Some(
                                    "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                ),
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "b",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Parent,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn mock_with_child() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                ftest::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("a/b".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                source_path: Some(
                                    "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                ),
                            },
                        )],
                        offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("b".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        }],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn three_sibling_realm_one_mock() {
        let mut realm = RealmNode::default();
        realm
            .set_component("a".into(), ftest::Component::Url("fuchsia-pkg://a".to_string()), &None)
            .await
            .unwrap();
        realm
            .set_component(
                "b".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component("c".into(), ftest::Component::Url("fuchsia-pkg://c".to_string()), &None)
            .await
            .unwrap();
        realm.mark_as_eager("c".into()).unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("b".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Directory(ftest::DirectoryCapability {
                    name: Some("example-dir".to_string()),
                    path: Some("/example".to_string()),
                    rights: Some(fio2::RW_STAR_DIR),
                    ..ftest::DirectoryCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("b".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("c".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![
                            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("b".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                                source: cm_rust::OfferSource::static_child("b".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("c".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                            cm_rust::ChildDecl {
                                name: "a".to_string(),
                                url: "fuchsia-pkg://a".to_string(),
                                startup: fsys::StartupMode::Lazy,
                                environment: None,
                                on_terminate: None,
                            },
                            cm_rust::ChildDecl {
                                name: "c".to_string(),
                                url: "fuchsia-pkg://c".to_string(),
                                startup: fsys::StartupMode::Eager,
                                environment: None,
                                on_terminate: None,
                            },
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "b",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Parent,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        capabilities: vec![cm_rust::CapabilityDecl::Directory(
                            cm_rust::DirectoryDecl {
                                name: "example-dir".try_into().unwrap(),
                                source_path: Some("/example".try_into().unwrap()),
                                rights: fio2::RW_STAR_DIR,
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Directory(
                            cm_rust::ExposeDirectoryDecl {
                                source: cm_rust::ExposeSource::Self_,
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::ExposeTarget::Parent,
                                target_name: "example-dir".try_into().unwrap(),
                                rights: None,
                                subdir: None,
                            },
                        )],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn three_siblings_two_targets() {
        let mut realm = RealmNode::default();
        realm
            .set_component("a".into(), ftest::Component::Url("fuchsia-pkg://a".to_string()), &None)
            .await
            .unwrap();
        realm
            .set_component("b".into(), ftest::Component::Url("fuchsia-pkg://b".to_string()), &None)
            .await
            .unwrap();
        realm
            .set_component("c".into(), ftest::Component::Url("fuchsia-pkg://c".to_string()), &None)
            .await
            .unwrap();
        realm.mark_as_eager("a".into()).unwrap();
        realm.mark_as_eager("c".into()).unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("b".to_string())),
                targets: Some(vec![
                    ftest::RouteEndpoint::Component("a".to_string()),
                    ftest::RouteEndpoint::Component("c".to_string()),
                ]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Directory(ftest::DirectoryCapability {
                    name: Some("example-dir".to_string()),
                    path: Some("/example".to_string()),
                    rights: Some(fio2::RW_STAR_DIR),
                    ..ftest::DirectoryCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("b".to_string())),
                targets: Some(vec![
                    ftest::RouteEndpoint::Component("a".to_string()),
                    ftest::RouteEndpoint::Component("c".to_string()),
                ]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![(
                "",
                cm_rust::ComponentDecl {
                    offers: vec![
                        cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        }),
                        cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("c".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        }),
                        cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "example-dir".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
                            target_name: "example-dir".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                            rights: None,
                            subdir: None,
                        }),
                        cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "example-dir".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("c".to_string()),
                            target_name: "example-dir".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                            rights: None,
                            subdir: None,
                        }),
                    ],
                    children: vec![
                        cm_rust::ChildDecl {
                            name: "a".to_string(),
                            url: "fuchsia-pkg://a".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        },
                        cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        },
                        cm_rust::ChildDecl {
                            name: "c".to_string(),
                            url: "fuchsia-pkg://c".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        },
                    ],
                    ..cm_rust::ComponentDecl::default()
                },
            )],
        );
    }

    #[fuchsia::test]
    async fn two_cousins_realm_one_mock() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a/b".into(),
                ftest::Component::Url("fuchsia-pkg://a-b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "c/d".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a/b".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("c/d".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Directory(ftest::DirectoryCapability {
                    name: Some("example-dir".to_string()),
                    path: Some("/example".to_string()),
                    rights: Some(fio2::RW_STAR_DIR),
                    ..ftest::DirectoryCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a/b".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("c/d".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![
                            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("c".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("c".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        children: vec![
                            // Generated children aren't inserted into the decls at this point, as
                            // their URLs are unknown until registration with the realm builder
                            // server, and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        exposes: vec![
                            cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                                source: cm_rust::ExposeSource::Child("b".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: cm_rust::ExposeTarget::Parent,
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            }),
                            cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                                source: cm_rust::ExposeSource::Child("b".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::ExposeTarget::Parent,
                                target_name: "example-dir".try_into().unwrap(),
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        children: vec![cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://a-b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        }],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "c",
                    cm_rust::ComponentDecl {
                        offers: vec![
                            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("d".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("d".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "c/d",
                    cm_rust::ComponentDecl {
                        uses: vec![
                            cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                                source: cm_rust::UseSource::Parent,
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target_path: "/svc/fidl.examples.routing.echo.Echo"
                                    .try_into()
                                    .unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                            cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
                                source: cm_rust::UseSource::Parent,
                                source_name: "example-dir".try_into().unwrap(),
                                target_path: "/example".try_into().unwrap(),
                                rights: fio2::RW_STAR_DIR,
                                subdir: None,
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn parent_use_from_url_child() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                ftest::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("a/b".into()).unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a/b".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("a".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        }],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn parent_use_from_mock_child() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("a/b".into()).unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a/b".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("a".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a/b",
                    cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                source_path: Some(
                                    "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                ),
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn grandparent_use_from_mock_child() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a/b/c".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("a/b/c".into()).unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Protocol(ftest::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..ftest::ProtocolCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a/b/c".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::Component("a".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a/b",
                    cm_rust::ComponentDecl {
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Child("c".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the realm builder server,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a/b/c",
                    cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                source_path: Some(
                                    "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                ),
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn use_service_from_parent() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Service(ftest::ServiceCapability {
                    name: Some("fuchsia.examples.services.BankAccount".to_string()),
                    ..ftest::ServiceCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {})),
                targets: Some(vec![ftest::RouteEndpoint::Component("a".to_string())]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                            source: cm_rust::OfferSource::Parent,
                            source_name: "fuchsia.examples.services.BankAccount".into(),
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
                            target_name: "fuchsia.examples.services.BankAccount".into(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Service(cm_rust::UseServiceDecl {
                            source: cm_rust::UseSource::Parent,
                            source_name: "fuchsia.examples.services.BankAccount".into(),
                            target_path: "/svc/fuchsia.examples.services.BankAccount"
                                .try_into()
                                .unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fuchsia::test]
    async fn expose_service_to_parent() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "b".into(),
                ftest::Component::Decl(cm_rust::ComponentDecl::default().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Service(ftest::ServiceCapability {
                    name: Some("fuchsia.examples.services.BankAccount".to_string()),
                    ..ftest::ServiceCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {})]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();
        realm
            .route_capability(ftest::CapabilityRoute {
                capability: Some(ftest::Capability::Service(ftest::ServiceCapability {
                    name: Some("fuchsia.examples.services.BankAccount".to_string()),
                    ..ftest::ServiceCapability::EMPTY
                })),
                source: Some(ftest::RouteEndpoint::Component("b".to_string())),
                targets: Some(vec![ftest::RouteEndpoint::AboveRoot(ftest::AboveRoot {})]),
                ..ftest::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![(
                "",
                cm_rust::ComponentDecl {
                    exposes: vec![
                        cm_rust::ExposeDecl::Service(cm_rust::ExposeServiceDecl {
                            source: cm_rust::ExposeSource::Child("a".to_string()),
                            source_name: "fuchsia.examples.services.BankAccount".into(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fuchsia.examples.services.BankAccount".into(),
                        }),
                        cm_rust::ExposeDecl::Service(cm_rust::ExposeServiceDecl {
                            source: cm_rust::ExposeSource::Child("b".to_string()),
                            source_name: "fuchsia.examples.services.BankAccount".into(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fuchsia.examples.services.BankAccount".into(),
                        }),
                    ],
                    ..cm_rust::ComponentDecl::default()
                },
            )],
        );
    }
}
