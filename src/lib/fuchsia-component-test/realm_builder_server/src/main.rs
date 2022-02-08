// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    cm_rust::{FidlIntoNative, NativeIntoFidl},
    fidl::endpoints::{ProtocolMarker, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fcdecl,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_component_test as ftest,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fuchsia_component::server as fserver,
    fuchsia_zircon_status as zx_status,
    futures::{future::BoxFuture, join, lock::Mutex, FutureExt, StreamExt, TryStreamExt},
    io_util,
    lazy_static::lazy_static,
    std::{
        collections::HashMap,
        convert::TryInto,
        ops::{Deref, DerefMut},
        path::PathBuf,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
    thiserror::{self, Error},
    tracing::*,
    url::Url,
    vfs::execution_scope::ExecutionScope,
};

mod builtin;
mod resolver;
mod runner;

lazy_static! {
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

    let execution_scope_clone = execution_scope.clone();
    fs.dir("svc").add_fidl_service(move |stream| {
        let factory = RealmBuilderFactory::new(
            registry.clone(),
            runner.clone(),
            execution_scope_clone.clone(),
        );
        execution_scope_clone.spawn(async move {
            if let Err(e) = factory.handle_stream(stream).await {
                error!("error encountered while running realm builder service: {:?}", e);
            }
        });
    });

    fs.take_and_serve_directory_handle().expect("did not receive directory handle");

    join!(execution_scope.wait(), fs.collect::<()>());
}

struct RealmBuilderFactory {
    registry: Arc<resolver::Registry>,
    runner: Arc<runner::Runner>,
    execution_scope: ExecutionScope,
}

impl RealmBuilderFactory {
    fn new(
        registry: Arc<resolver::Registry>,
        runner: Arc<runner::Runner>,
        execution_scope: ExecutionScope,
    ) -> Self {
        Self { registry, runner, execution_scope }
    }

    async fn handle_stream(
        self,
        mut stream: ftest::RealmBuilderFactoryRequestStream,
    ) -> Result<(), anyhow::Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                ftest::RealmBuilderFactoryRequest::CreateFromRelativeUrl {
                    pkg_dir_handle,
                    relative_url,
                    realm_server_end,
                    builder_server_end,
                    responder,
                } => {
                    if !is_relative_url(&relative_url) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::UrlIsNotRelative))?;
                        continue;
                    }
                    let pkg_dir = match pkg_dir_handle
                        .into_proxy()
                        .context("failed to convert pkg_dir ClientEnd to proxy")
                    {
                        Ok(pkg_dir) => pkg_dir,
                        Err(e) => {
                            responder
                                .send(&mut Err(ftest::RealmBuilderError2::InvalidPkgDirHandle))?;
                            return Err(e);
                        }
                    };
                    let realm_node = match RealmNode2::load_from_pkg(
                        relative_url.clone(),
                        Clone::clone(&pkg_dir),
                    )
                    .await
                    {
                        Ok(realm_node) => realm_node,
                        Err(e) => {
                            warn!("unable to load manifest at {:?}: {:?}", relative_url, e);
                            responder.send(&mut Err(e.into()))?;
                            continue;
                        }
                    };
                    self.create_realm_and_builder(
                        realm_node,
                        pkg_dir,
                        realm_server_end,
                        builder_server_end,
                    )?;
                    responder.send(&mut Ok(()))?;
                }
                ftest::RealmBuilderFactoryRequest::Create {
                    pkg_dir_handle,
                    realm_server_end,
                    builder_server_end,
                    responder,
                } => {
                    let pkg_dir = pkg_dir_handle
                        .into_proxy()
                        .context("failed to convert pkg_dir ClientEnd to proxy")?;
                    self.create_realm_and_builder(
                        RealmNode2::new(),
                        pkg_dir,
                        realm_server_end,
                        builder_server_end,
                    )?;
                    responder.send()?;
                }
            }
        }
        Ok(())
    }

    fn create_realm_and_builder(
        &self,
        realm_node: RealmNode2,
        pkg_dir: fio::DirectoryProxy,
        realm_server_end: ServerEnd<ftest::RealmMarker>,
        builder_server_end: ServerEnd<ftest::BuilderMarker>,
    ) -> Result<(), anyhow::Error> {
        let runner_proxy_placeholder = Arc::new(Mutex::new(None));

        let realm_stream = realm_server_end
            .into_stream()
            .context("failed to convert realm_server_end to stream")?;

        let realm_has_been_built = Arc::new(AtomicBool::new(false));

        let realm = Realm {
            pkg_dir: Clone::clone(&pkg_dir),
            realm_node: realm_node.clone(),
            registry: self.registry.clone(),
            runner: self.runner.clone(),
            runner_proxy_placeholder: runner_proxy_placeholder.clone(),
            realm_path: vec![],
            execution_scope: self.execution_scope.clone(),
            realm_has_been_built: realm_has_been_built.clone(),
        };

        self.execution_scope.spawn(async move {
            if let Err(e) = realm.handle_stream(realm_stream).await {
                error!("error encountered while handling Realm requests: {:?}", e);
            }
        });

        let builder_stream = builder_server_end
            .into_stream()
            .context("failed to convert builder_server_end to stream")?;

        let builder = Builder {
            pkg_dir: Clone::clone(&pkg_dir),
            realm_node,
            registry: self.registry.clone(),
            runner_proxy_placeholder: runner_proxy_placeholder.clone(),
            realm_has_been_built: realm_has_been_built,
        };
        self.execution_scope.spawn(async move {
            if let Err(e) = builder.handle_stream(builder_stream).await {
                error!("error encountered while handling Builder requests: {:?}", e);
            }
        });
        Ok(())
    }
}

struct Builder {
    pkg_dir: fio::DirectoryProxy,
    realm_node: RealmNode2,
    registry: Arc<resolver::Registry>,
    runner_proxy_placeholder: Arc<Mutex<Option<fcrunner::ComponentRunnerProxy>>>,
    realm_has_been_built: Arc<AtomicBool>,
}

impl Builder {
    async fn handle_stream(
        &self,
        mut stream: ftest::BuilderRequestStream,
    ) -> Result<(), anyhow::Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                ftest::BuilderRequest::Build { runner, responder } => {
                    if self.realm_has_been_built.swap(true, Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
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
    realm_has_been_built: Arc<AtomicBool>,
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
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
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
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
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
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.add_child_from_decl(name.clone(), decl, options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to add child {:?} from decl to realm: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddLocalChild { name, options, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.add_local_child(name.clone(), options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to add local child {:?} to realm: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddChildRealm { name, options, child_realm, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.add_child_realm(name.clone(), options, child_realm).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to add child realm {:?}: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::GetComponentDecl { name, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.get_component_decl(name.clone()).await {
                        Ok(decl) => responder.send(&mut Ok(decl))?,
                        Err(e) => {
                            warn!("unable to get component decl for child {:?}: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::ReplaceComponentDecl { name, component_decl, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.replace_component_decl(name.clone(), component_decl).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to replace component decl for child {:?}: {:?}", name, e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::GetRealmDecl { responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
                    responder.send(&mut Ok(self.get_realm_decl().await))?;
                }
                ftest::RealmRequest::ReplaceRealmDecl { component_decl, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.replace_realm_decl(component_decl).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(e) => {
                            warn!("unable to replace realm decl: {:?}", e);
                            responder.send(&mut Err(e.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddRoute { capabilities, from, to, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
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
                ftest::RealmRequest::ReadOnlyDirectory {
                    name,
                    to,
                    directory_contents,
                    responder,
                } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError2::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.read_only_directory(name, to, directory_contents).await {
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
        if let Err(e) = cm_fidl_validator::validate(&component_decl) {
            return Err(RealmBuilderError::InvalidComponentDecl(e));
        }
        let child_realm_node = RealmNode2::new_from_decl(component_decl.fidl_into_native(), false);
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
                (ftest::LOCAL_COMPONENT_NAME_KEY.to_string(), child_path.join("/").to_string()),
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
            realm_has_been_built: self.realm_has_been_built.clone(),
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
        let child_node = self.realm_node.get_sub_realm(&name).await?;
        child_node.replace_decl_with_untrusted(component_decl).await
    }

    async fn get_realm_decl(&self) -> fcdecl::Component {
        self.realm_node.get_decl().await.native_into_fidl()
    }

    async fn replace_realm_decl(
        &self,
        component_decl: fcdecl::Component,
    ) -> Result<(), RealmBuilderError> {
        self.realm_node.replace_decl_with_untrusted(component_decl).await
    }

    async fn read_only_directory(
        &self,
        directory_name: String,
        to: Vec<fcdecl::Ref>,
        directory_contents: ftest::DirectoryContents,
    ) -> Result<(), RealmBuilderError> {
        // Add a new component to the realm to serve the directory capability from
        let dir_name = directory_name.clone();
        let directory_contents = Arc::new(directory_contents);
        let local_component_id = self
            .runner
            .register_builtin_component(move |outgoing_dir| {
                builtin::read_only_directory(
                    dir_name.clone(),
                    directory_contents.clone(),
                    outgoing_dir,
                )
                .boxed()
            })
            .await;
        let string_id: String = local_component_id.clone().into();
        let child_name = format!("read-only-directory-{}", string_id);

        let mut child_path = self.realm_path.clone();
        child_path.push(child_name.clone());

        let child_realm_node = RealmNode2::new_from_decl(
            new_decl_with_program_entries(vec![(
                runner::LOCAL_COMPONENT_ID_KEY.to_string(),
                local_component_id.into(),
            )]),
            true,
        );
        self.realm_node
            .add_child(child_name.clone(), ftest::ChildOptions::EMPTY, child_realm_node)
            .await?;
        let path = Some(format!("/{}", directory_name));
        self.realm_node
            .route_capabilities(
                vec![ftest::Capability2::Directory(ftest::Directory {
                    name: Some(directory_name),
                    rights: Some(fio::R_STAR_DIR),
                    path,
                    ..ftest::Directory::EMPTY
                })],
                fcdecl::Ref::Child(fcdecl::ChildRef { name: child_name.clone(), collection: None }),
                to,
            )
            .await
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
                Some(fcdecl::StartupMode::Lazy) => fcdecl::StartupMode::Lazy,
                Some(fcdecl::StartupMode::Eager) => fcdecl::StartupMode::Eager,
                None => fcdecl::StartupMode::Lazy,
            },
            environment: child_options.environment,
            on_terminate: match child_options.on_terminate {
                Some(fcdecl::OnTerminate::None) => Some(fcdecl::OnTerminate::None),
                Some(fcdecl::OnTerminate::Reboot) => Some(fcdecl::OnTerminate::Reboot),
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

    // Whenever this realm node is going to get a new decl we'd like to validate the new
    // hypothetical decl, but the decl likely references children within `self.mutable_children`.
    // Since these children do not (yet) exist in `decl.children`, the decl will fail validation.
    // To get around this, generate hypothetical `fcdecl::Child` structs and add them to
    // `decl.children`, and then run validation.
    fn validate_with_hypothetical_children(
        &self,
        mut decl: fcdecl::Component,
    ) -> Result<(), RealmBuilderError> {
        let child_decls =
            self.mutable_children.iter().map(|(name, _options_and_node)| fcdecl::Child {
                name: Some(name.clone()),
                url: Some("invalid://url".to_string()),
                startup: Some(fcdecl::StartupMode::Lazy),
                ..fcdecl::Child::EMPTY
            });
        decl.children.get_or_insert(vec![]).extend(child_decls);
        if let Err(e) = cm_fidl_validator::validate(&decl) {
            return Err(RealmBuilderError::InvalidComponentDecl(e));
        }
        Ok(())
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

    // Validates `new_decl`, confirms that `new_decl` isn't overwriting anything necessary for the
    // realm builder runner to work, and then replaces this realm's decl with `new_decl`.
    async fn replace_decl_with_untrusted(
        &self,
        new_decl: fcdecl::Component,
    ) -> Result<(), RealmBuilderError> {
        let mut state_guard = self.state.lock().await;
        state_guard.validate_with_hypothetical_children(new_decl.clone())?;
        let new_decl = new_decl.fidl_into_native();
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

    // Replaces the decl for this realm with `new_decl`.
    async fn replace_decl(
        &self,
        new_decl: cm_rust::ComponentDecl,
    ) -> Result<(), RealmBuilderError> {
        self.state.lock().await.decl = new_decl;
        Ok(())
    }

    async fn add_child(
        &self,
        child_name: String,
        child_options: ftest::ChildOptions,
        node: RealmNode2,
    ) -> Result<(), RealmBuilderError> {
        let mut state_guard = self.state.lock().await;
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
            cm_fidl_validator::validate(&fidl_decl)
                .map_err(|e| RealmBuilderError::InvalidComponentDeclWithName(relative_url, e))?;

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
                            fcdecl::StartupMode::Lazy => Some(fcdecl::StartupMode::Lazy),
                            fcdecl::StartupMode::Eager => Some(fcdecl::StartupMode::Eager),
                        },
                        environment: child.environment,
                        on_terminate: match child.on_terminate {
                            Some(fcdecl::OnTerminate::None) => Some(fcdecl::OnTerminate::None),
                            Some(fcdecl::OnTerminate::Reboot) => Some(fcdecl::OnTerminate::Reboot),
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
            .ok_or_else(|| RealmBuilderError::NoSuchChild(child_name.clone()))
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
                    let decl =
                        create_expose_decl(capability.clone(), from.clone(), ExposingIn::Realm)?;
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
                    Err(RealmBuilderError::InvalidComponentDeclWithName(walked_path.join("/"), e))
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
                create_expose_decl(
                    capability,
                    fcdecl::Ref::Self_(fcdecl::SelfRef {}),
                    ExposingIn::Child,
                )?,
            );
            let () = child.replace_decl(decl).await?;
        }
    }

    Ok(())
}

fn into_dependency_type(type_: &Option<fcdecl::DependencyType>) -> cm_rust::DependencyType {
    type_
        .as_ref()
        .cloned()
        .map(FidlIntoNative::fidl_into_native)
        .unwrap_or(cm_rust::DependencyType::Strong)
}

/// Attempts to produce the target name from the set of "name" and "as" fields from a capability.
fn try_into_source_name(
    name: &Option<String>,
) -> Result<cm_rust::CapabilityName, RealmBuilderError> {
    Ok(name
        .as_ref()
        .ok_or_else(|| {
            RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "capability `name` received was empty"
            ))
        })?
        .as_str()
        .into())
}

/// Attempts to produce the target name from the set of "name" and "as" fields from a capability.
fn try_into_target_name(
    name: &Option<String>,
    as_: &Option<String>,
) -> Result<cm_rust::CapabilityName, RealmBuilderError> {
    let name = name.as_ref().ok_or_else(|| {
        RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
            "capability `name` received was empty"
        ))
    })?;
    Ok(as_.as_ref().unwrap_or(name).clone().into())
}

/// Attempts to produce a valid CapabilityPath from the "path" field from a capability
fn try_into_capability_path(
    input: &Option<String>,
) -> Result<cm_rust::CapabilityPath, RealmBuilderError> {
    input
        .as_ref()
        .ok_or_else(|| {
            RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "`path` field is required on capability when routing to or from a local component",
            ))
        })?
        .as_str()
        .try_into()
        .map_err(|e| {
            RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "`path` field in capability is invalid: {:?}",
                e
            ))
        })
}

/// Attempts to produce a valid CapabilityPath from the "path" field from a capability, and if that
/// fails then attempts to produce a valid CapabilityPath from the "name" field following
/// "/svc/{name}"
fn try_into_service_path(
    name: &Option<String>,
    path: &Option<String>,
) -> Result<cm_rust::CapabilityPath, RealmBuilderError> {
    let name = name.as_ref().ok_or_else(|| {
        RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
            "capability `name` received was empty"
        ))
    })?;
    let path = path.as_ref().cloned().unwrap_or_else(|| format!("/svc/{}", name));
    path.as_str().try_into().map_err(|e| {
        RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
            "unable to create service path: {:?}",
            e
        ))
    })
}

fn create_capability_decl(
    capability: ftest::Capability2,
) -> Result<cm_rust::CapabilityDecl, RealmBuilderError> {
    Ok(match capability {
        ftest::Capability2::Protocol(protocol) => {
            let name = try_into_source_name(&protocol.name)?;
            let source_path = Some(try_into_service_path(&protocol.name, &protocol.path)?);
            cm_rust::CapabilityDecl::Protocol(cm_rust::ProtocolDecl { name, source_path })
        }
        ftest::Capability2::Directory(directory) => {
            let name = try_into_source_name(&directory.name)?;
            let source_path = Some(try_into_capability_path(&directory.path)?);
            let rights = directory.rights.ok_or_else(|| RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!(
                    "`rights` field is required on directory capabilities when routing to or from a local component",
                ),
            ))?;
            cm_rust::CapabilityDecl::Directory(cm_rust::DirectoryDecl { name, source_path, rights })
        }
        ftest::Capability2::Storage(_) => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "declaring storage capabilities with add_route is unsupported"
            )))?;
        }
        ftest::Capability2::Service(service) => {
            let name = try_into_source_name(&service.name)?;
            let source_path = Some(try_into_service_path(&service.name, &service.path)?);
            cm_rust::CapabilityDecl::Service(cm_rust::ServiceDecl { name, source_path })
        }
        ftest::Capability2::Event(event) => {
            let name = try_into_source_name(&event.name)?;
            cm_rust::CapabilityDecl::Event(cm_rust::EventDecl { name })
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
            let source_name = try_into_source_name(&protocol.name)?;
            let target_name = try_into_target_name(&protocol.name, &protocol.as_)?;
            let dependency_type = into_dependency_type(&protocol.type_);
            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source,
                source_name,
                target,
                target_name,
                dependency_type,
            })
        }
        ftest::Capability2::Directory(directory) => {
            let source_name = try_into_source_name(&directory.name)?;
            let target_name = try_into_target_name(&directory.name, &directory.as_)?;
            let dependency_type = into_dependency_type(&directory.type_);
            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                source,
                source_name,
                target,
                target_name,
                rights: directory.rights,
                subdir: directory.subdir.map(PathBuf::from),
                dependency_type,
            })
        }
        ftest::Capability2::Storage(storage) => {
            let source_name = try_into_source_name(&storage.name)?;
            let target_name = try_into_target_name(&storage.name, &storage.as_)?;
            cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                source,
                source_name,
                target,
                target_name,
            })
        }
        ftest::Capability2::Service(service) => {
            let source_name = try_into_source_name(&service.name)?;
            let target_name = try_into_target_name(&service.name, &service.as_)?;
            cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                source,
                source_name,
                target,
                target_name,
            })
        }
        ftest::Capability2::Event(event) => {
            let source_name = try_into_source_name(&event.name)?;
            let target_name = try_into_target_name(&event.name, &event.as_)?;
            let mode = event
                .mode
                .as_ref()
                .ok_or_else(|| {
                    RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                        "capability `mode` received was empty: {:?}",
                        event.clone()
                    ))
                })?
                .clone()
                .fidl_into_native();
            let filter = event.filter.as_ref().cloned().map(FidlIntoNative::fidl_into_native);
            cm_rust::OfferDecl::Event(cm_rust::OfferEventDecl {
                source,
                source_name,
                target,
                target_name,
                filter,
                mode,
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

// We only want to apply the rename for a capability once. If we're handling a route from a local
// component child to the parent, we want to use the source name in the child for the source and
// target names, and apply the rename (where the source_name and target_name fields don't match) in
// the parent. This field is used to track when an expose declaration is being generated for a
// child versus the parent realm.
enum ExposingIn {
    Realm,
    Child,
}

fn create_expose_decl(
    capability: ftest::Capability2,
    source: fcdecl::Ref,
    exposing_in: ExposingIn,
) -> Result<cm_rust::ExposeDecl, RealmBuilderError> {
    let source: cm_rust::ExposeSource = source.fidl_into_native();

    Ok(match capability {
        ftest::Capability2::Protocol(protocol) => {
            let source_name = try_into_source_name(&protocol.name)?;
            let target_name = match exposing_in {
                ExposingIn::Child => try_into_source_name(&protocol.name)?,
                ExposingIn::Realm => try_into_target_name(&protocol.name, &protocol.as_)?,
            };
            cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                source: source.clone(),
                source_name,
                target: cm_rust::ExposeTarget::Parent,
                target_name,
            })
        }
        ftest::Capability2::Directory(directory) => {
            let source_name = try_into_source_name(&directory.name)?;
            let target_name = match exposing_in {
                ExposingIn::Child => try_into_source_name(&directory.name)?,
                ExposingIn::Realm => try_into_target_name(&directory.name, &directory.as_)?,
            };
            // Much like capability renames, we want to only apply the subdir field once. Use the
            // exposing_in field to ensure that we apply the subdir field in the parent, and not in
            // a local child's manifest.
            let subdir = match exposing_in {
                ExposingIn::Child => None,
                ExposingIn::Realm => directory.subdir.map(PathBuf::from),
            };
            cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                source,
                source_name,
                target: cm_rust::ExposeTarget::Parent,
                target_name,
                rights: directory.rights,
                subdir,
            })
        }
        ftest::Capability2::Storage(_) => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "storage capabilities cannot be exposed: {:?}",
                capability.clone()
            )));
        }
        ftest::Capability2::Service(service) => {
            let source_name = try_into_source_name(&service.name)?;
            let target_name = match exposing_in {
                ExposingIn::Child => try_into_source_name(&service.name)?,
                ExposingIn::Realm => try_into_target_name(&service.name, &service.as_)?,
            };
            cm_rust::ExposeDecl::Service(cm_rust::ExposeServiceDecl {
                source,
                source_name,
                target: cm_rust::ExposeTarget::Parent,
                target_name,
            })
        }
        ftest::Capability2::Event(_) => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "event capabilities cannot be exposed: {:?}",
                capability.clone()
            )));
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
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&protocol.name, &protocol.as_)?;
            let target_path = try_into_service_path(
                &Some(source_name.clone().native_into_fidl()),
                &protocol.path,
            )?;
            let dependency_type = protocol
                .type_
                .map(FidlIntoNative::fidl_into_native)
                .unwrap_or(cm_rust::DependencyType::Strong);
            cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                source: cm_rust::UseSource::Parent,
                source_name,
                target_path,
                dependency_type,
            })
        }
        ftest::Capability2::Directory(directory) => {
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&directory.name, &directory.as_)?;
            let target_path = try_into_capability_path(&directory.path)?;
            let rights = directory.rights.ok_or_else(|| RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!(
                    "`rights` field is required on directory capabilities when routing to or from a local component",
                ),
            ))?;
            let dependency_type = directory
                .type_
                .map(FidlIntoNative::fidl_into_native)
                .unwrap_or(cm_rust::DependencyType::Strong);
            cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
                source: cm_rust::UseSource::Parent,
                source_name,
                target_path,
                rights,
                // We only want to set the sub-directory field once, and if we're generating a use
                // declaration then we've already generated an offer declaration in the parent and
                // we'll set the sub-directory field there.
                subdir: None,
                dependency_type,
            })
        }
        ftest::Capability2::Storage(storage) => {
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&storage.name, &storage.as_)?;
            let target_path = try_into_capability_path(&storage.path)?;
            cm_rust::UseDecl::Storage(cm_rust::UseStorageDecl { source_name, target_path })
        }
        ftest::Capability2::Service(service) => {
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&service.name, &service.as_)?;
            let target_path = try_into_service_path(
                &Some(source_name.clone().native_into_fidl()),
                &service.path,
            )?;
            cm_rust::UseDecl::Service(cm_rust::UseServiceDecl {
                source: cm_rust::UseSource::Parent,
                source_name,
                target_path,
                dependency_type: cm_rust::DependencyType::Strong,
            })
        }
        ftest::Capability2::Event(event) => {
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&event.name, &event.as_)?;
            let mode = event.mode.as_ref().ok_or_else(|| {
                RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                    "capability `mode` received was empty: {:?}",
                    event.clone()
                ))
            })?;
            let filter = event.filter.as_ref().cloned().map(FidlIntoNative::fidl_into_native);
            cm_rust::UseDecl::Event(cm_rust::UseEventDecl {
                source: cm_rust::UseSource::Parent,
                source_name: source_name.clone(),
                target_name: source_name,
                filter,
                mode: mode.clone().fidl_into_native(),
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
    #[error("a component manifest failed validation: {0:?}")]
    InvalidComponentDecl(cm_fidl_validator::error::ErrorList),

    /// A component declaration failed validation.
    #[error("the manifest for component {0:?} failed validation: {1:?}")]
    InvalidComponentDeclWithName(String, cm_fidl_validator::error::ErrorList),

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
            RealmBuilderError::InvalidComponentDecl(_) => Self::InvalidComponentDecl,
            RealmBuilderError::InvalidComponentDeclWithName(_, _) => Self::InvalidComponentDecl,
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl::endpoints::{
            create_endpoints, create_proxy, create_proxy_and_stream, create_request_stream,
            ClientEnd,
        },
        fidl_fuchsia_io as fio, fidl_fuchsia_mem as fmem, fuchsia_async as fasync,
        fuchsia_zircon as zx,
        maplit::hashmap,
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
                                    fcdecl::StartupMode::Eager => Some(fcdecl::StartupMode::Eager),
                                    fcdecl::StartupMode::Lazy => None,
                                },
                                environment: child.environment,
                                on_terminate: match child.on_terminate {
                                    Some(fcdecl::OnTerminate::None) => {
                                        Some(fcdecl::OnTerminate::None)
                                    }
                                    Some(fcdecl::OnTerminate::Reboot) => {
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
        realm_has_been_built: Arc<AtomicBool>,
    ) -> (ftest::BuilderProxy, fasync::Task<()>) {
        let (pkg_dir, pkg_dir_stream) = create_proxy_and_stream::<fio::DirectoryMarker>().unwrap();
        drop(pkg_dir_stream);

        let builder = Builder {
            pkg_dir,
            realm_node,
            registry,
            runner_proxy_placeholder,
            realm_has_been_built,
        };

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
        let (builder_proxy, _builder_stream_task) = launch_builder_task(
            realm_node,
            registry.clone(),
            Arc::new(Mutex::new(None)),
            Arc::new(AtomicBool::new(false)),
        );

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

            let realm_has_been_built = Arc::new(AtomicBool::new(false));

            let (builder_proxy, builder_task) = launch_builder_task(
                realm_root.clone(),
                registry.clone(),
                runner_proxy_placeholder.clone(),
                realm_has_been_built.clone(),
            );

            let realm = Realm {
                pkg_dir,
                realm_node: realm_root,
                registry: registry.clone(),
                runner: runner.clone(),
                runner_proxy_placeholder,
                realm_path: vec![],
                execution_scope: ExecutionScope::new(),
                realm_has_been_built,
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

        let (builder_proxy, _builder_stream_task) = launch_builder_task(
            realm_node,
            resolver::Registry::new(),
            Arc::new(Mutex::new(None)),
            Arc::new(AtomicBool::new(false)),
        );

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
                    startup: fcdecl::StartupMode::Lazy,
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
                    startup: fcdecl::StartupMode::Lazy,
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
                    extends: fcdecl::EnvironmentExtends::None,
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
                Some(runner::ComponentImplementer::RunnerProxy(rp)) => rp,
                Some(_) => {
                    panic!("unexpected component implementer")
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
                    startup: fcdecl::StartupMode::Lazy,
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

        let realm_with_child_decl_file = io_util::open_file_in_namespace(
            "/pkg/meta/realm_with_child.cm",
            fio::OPEN_RIGHT_READABLE,
        )
        .expect("failed to open manifest");
        let mut realm_with_child_decl =
            io_util::read_file_fidl::<fcdecl::Component>(&realm_with_child_decl_file)
                .await
                .expect("failed to read manifest")
                .fidl_into_native();

        // The "a" child is rewritten by realm builder
        realm_with_child_decl.children =
            realm_with_child_decl.children.into_iter().filter(|c| &c.name != "a").collect();

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
                    decl: realm_with_child_decl,
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
    async fn add_route_does_not_mutate_children_added_from_decl() {
        let a_decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some("hippo".try_into().unwrap()),
                info: fdata::Dictionary::EMPTY,
            }),
            uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                source: cm_rust::UseSource::Parent,
                source_name: "example.Hippo".into(),
                target_path: "/svc/non-default-path".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        }
        .native_into_fidl();

        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child_from_decl("a", a_decl.clone(), ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child_from_decl returned an error");
        realm_and_builder_task
            .add_route_or_panic(
                vec![ftest::Capability2::Protocol(ftest::Protocol {
                    name: Some("example.Hippo".to_owned()),
                    type_: Some(fcdecl::DependencyType::Strong),
                    ..ftest::Protocol::EMPTY
                })],
                fcdecl::Ref::Parent(fcdecl::ParentRef {}),
                vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "a".to_owned(),
                    collection: None,
                })],
            )
            .await;
        let resulting_a_decl = realm_and_builder_task
            .realm_proxy
            .get_component_decl("a")
            .await
            .expect("failed to call get_component_decl")
            .expect("get_component_decl returned an error");
        assert_eq!(a_decl, resulting_a_decl);
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
                            key: ftest::LOCAL_COMPONENT_NAME_KEY.to_string(),
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
                        rights: Some(fio::RW_STAR_DIR),
                        subdir: Some("component".to_owned()),
                        ..ftest::Directory::EMPTY
                    }),
                    ftest::Capability2::Storage(ftest::Storage {
                        name: Some("temp".to_string()),
                        as_: Some("data".to_string()),
                        ..ftest::Storage::EMPTY
                    }),
                    ftest::Capability2::Service(ftest::Service {
                        name: Some("fuchsia.examples.Whale".to_string()),
                        as_: Some("fuchsia.examples.Orca".to_string()),
                        ..ftest::Service::EMPTY
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
                        startup: fcdecl::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                    cm_rust::ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fcdecl::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                ],
                offers: vec![
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Hippo".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "fuchsia.examples.Elephant".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                    }),
                    cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "config-data".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "config-data".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        rights: Some(fio::RW_STAR_DIR),
                        subdir: Some(PathBuf::from("component")),
                    }),
                    cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "temp".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "data".into(),
                    }),
                    cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Whale".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "fuchsia.examples.Orca".into(),
                    }),
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::static_child("a".to_string()),
                        source_name: "fuchsia.examples.Echo".into(),
                        target: cm_rust::OfferTarget::static_child("b".to_string()),
                        target_name: "fuchsia.examples.Echo".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                    }),
                ],
                exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                    source: cm_rust::ExposeSource::Child("a".to_owned()),
                    source_name: "fuchsia.examples.Echo".into(),
                    target: cm_rust::ExposeTarget::Parent,
                    target_name: "fuchsia.examples.Echo".into(),
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
            .add_local_child("a", ftest::ChildOptions::EMPTY)
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
                        startup: fcdecl::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                    cm_rust::ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fcdecl::StartupMode::Lazy,
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
                        program: Some(cm_rust::ProgramDecl {
                            runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![
                                    fdata::DictionaryEntry {
                                        key: runner::LOCAL_COMPONENT_ID_KEY.to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str(
                                            "0".to_string(),
                                        ))),
                                    },
                                    fdata::DictionaryEntry {
                                        key: ftest::LOCAL_COMPONENT_NAME_KEY.to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str(
                                            "a".to_string(),
                                        ))),
                                    },
                                ]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
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
            .add_local_child("a", ftest::ChildOptions::EMPTY)
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
                        startup: fcdecl::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                    cm_rust::ChildDecl {
                        name: "c".to_owned(),
                        url: "test:///c".to_owned(),
                        startup: fcdecl::StartupMode::Lazy,
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
                        program: Some(cm_rust::ProgramDecl {
                            runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                            info: fdata::Dictionary {
                                entries: Some(vec![
                                    fdata::DictionaryEntry {
                                        key: runner::LOCAL_COMPONENT_ID_KEY.to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str(
                                            "0".to_string(),
                                        ))),
                                    },
                                    fdata::DictionaryEntry {
                                        key: ftest::LOCAL_COMPONENT_NAME_KEY.to_string(),
                                        value: Some(Box::new(fdata::DictionaryValue::Str(
                                            "a".to_string(),
                                        ))),
                                    },
                                ]),
                                ..fdata::Dictionary::EMPTY
                            },
                        }),
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
                            startup: fcdecl::StartupMode::Lazy,
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
                                key: ftest::LOCAL_COMPONENT_NAME_KEY.to_string(),
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

    #[fuchsia::test]
    async fn route_event_to_child_component() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test://a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        realm_and_builder_task
            .realm_proxy
            .add_route(
                &mut vec![ftest::Capability2::Event(ftest::Event {
                    name: Some("directory_ready".to_string()),
                    as_: None,
                    mode: Some(fcdecl::EventMode::Sync),
                    filter: Some(fdata::Dictionary {
                        entries: Some(vec![fdata::DictionaryEntry {
                            key: "name".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str(
                                "hippos".to_string(),
                            ))),
                        }]),
                        ..fdata::Dictionary::EMPTY
                    }),
                    ..ftest::Event::EMPTY
                })]
                .iter_mut(),
                &mut fcdecl::Ref::Parent(fcdecl::ParentRef {}),
                &mut vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "a".to_string(),
                    collection: None,
                })]
                .iter_mut(),
            )
            .await
            .expect("failed to call add_child")
            .expect("add_route returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                children: vec![cm_rust::ChildDecl {
                    name: "a".to_string(),
                    url: "test://a".to_string(),
                    startup: fcdecl::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                }],
                offers: vec![cm_rust::OfferDecl::Event(cm_rust::OfferEventDecl {
                    source: cm_rust::OfferSource::Parent,
                    source_name: "directory_ready".into(),
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    target_name: "directory_ready".into(),
                    filter: Some(hashmap!(
                        "name".to_string() => cm_rust::DictionaryValue::Str("hippos".to_string()),
                    )),
                    mode: cm_rust::EventMode::Sync,
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![],
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
                            key: ftest::LOCAL_COMPONENT_NAME_KEY.to_string(),
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

    #[fuchsia::test]
    async fn get_and_replace_realm_decl() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        let mut realm_decl = realm_and_builder_task
            .realm_proxy
            .get_realm_decl()
            .await
            .expect("failed to call get_realm_decl")
            .expect("get_realm_decl returned an error");
        realm_decl.children = Some(vec![fcdecl::Child {
            name: Some("example-child".to_string()),
            url: Some("example://url".to_string()),
            startup: Some(fcdecl::StartupMode::Eager),
            ..fcdecl::Child::EMPTY
        }]);
        realm_and_builder_task
            .realm_proxy
            .replace_realm_decl(realm_decl.clone())
            .await
            .expect("failed to call replace_realm_decl")
            .expect("replace_realm_decl returned an error");
        assert_eq!(
            realm_decl,
            realm_and_builder_task
                .realm_proxy
                .get_realm_decl()
                .await
                .expect("failed to call get_realm_decl")
                .expect("get_realm_decl returned an error"),
        );
    }

    #[fuchsia::test]
    async fn replace_decl_enforces_validation() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        let realm_decl = fcdecl::Component {
            children: Some(vec![fcdecl::Child {
                name: Some("example-child".to_string()),
                url: Some("example://url".to_string()),
                startup: Some(fcdecl::StartupMode::Eager),
                environment: Some("i-dont-exist".to_string()),
                ..fcdecl::Child::EMPTY
            }]),
            ..fcdecl::Component::EMPTY
        };
        let err = realm_and_builder_task
            .realm_proxy
            .replace_realm_decl(realm_decl)
            .await
            .expect("failed to call replace_realm_decl")
            .expect_err("replace_realm_decl did not return an error");
        assert_eq!(err, ftest::RealmBuilderError2::InvalidComponentDecl);
    }

    #[fuchsia::test]
    async fn all_functions_error_after_build() {
        let mut rabt = RealmAndBuilderTask::new();
        let (child_realm_proxy, child_realm_server_end) =
            create_proxy::<ftest::RealmMarker>().unwrap();
        rabt.realm_proxy
            .add_child_realm("a", ftest::ChildOptions::EMPTY, child_realm_server_end)
            .await
            .expect("failed to call add_child_realm")
            .expect("add_child_realm returned an error");
        child_realm_proxy
            .add_local_child("b", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        let _tree_from_resolver = rabt.call_build_and_get_tree().await;

        async fn assert_err<V: std::fmt::Debug>(
            fut: impl futures::Future<
                Output = Result<Result<V, ftest::RealmBuilderError2>, fidl::Error>,
            >,
        ) {
            assert_eq!(
                ftest::RealmBuilderError2::BuildAlreadyCalled,
                fut.await.expect("failed to call function").expect_err("expected an error"),
            );
        }
        let empty_opts = || ftest::ChildOptions::EMPTY;
        let empty_decl = || fcdecl::Component::EMPTY;

        assert_err(rabt.realm_proxy.add_child("a", "test:///a", empty_opts())).await;
        assert_err(rabt.realm_proxy.add_legacy_child("a", "test:///a.cmx", empty_opts())).await;
        assert_err(rabt.realm_proxy.add_child_from_decl("a", empty_decl(), empty_opts())).await;
        assert_err(rabt.realm_proxy.add_local_child("a", empty_opts())).await;
        let (_child_realm_proxy, server_end) = create_proxy::<ftest::RealmMarker>().unwrap();
        assert_err(rabt.realm_proxy.add_child_realm("a", empty_opts(), server_end)).await;
        assert_err(rabt.realm_proxy.get_component_decl("b")).await;
        assert_err(rabt.realm_proxy.replace_component_decl("b", empty_decl())).await;
        assert_err(rabt.realm_proxy.replace_realm_decl(empty_decl())).await;
        assert_err(rabt.realm_proxy.add_route(
            &mut vec![].iter_mut(),
            &mut fcdecl::Ref::Self_(fcdecl::SelfRef {}),
            &mut vec![].iter_mut(),
        ))
        .await;

        assert_err(child_realm_proxy.add_child("a", "test:///a", empty_opts())).await;
        assert_err(child_realm_proxy.add_legacy_child("a", "test:///a.cmx", empty_opts())).await;
        assert_err(child_realm_proxy.add_child_from_decl("a", empty_decl(), empty_opts())).await;
        assert_err(child_realm_proxy.add_local_child("a", empty_opts())).await;
        let (_child_realm_proxy, server_end) = create_proxy::<ftest::RealmMarker>().unwrap();
        assert_err(child_realm_proxy.add_child_realm("a", empty_opts(), server_end)).await;
        assert_err(child_realm_proxy.get_component_decl("b")).await;
        assert_err(child_realm_proxy.replace_component_decl("b", empty_decl())).await;
        assert_err(child_realm_proxy.replace_realm_decl(empty_decl())).await;
        assert_err(child_realm_proxy.add_route(
            &mut vec![].iter_mut(),
            &mut fcdecl::Ref::Self_(fcdecl::SelfRef {}),
            &mut vec![].iter_mut(),
        ))
        .await;
    }

    #[fuchsia::test]
    async fn read_only_directory() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_child("a", "test://a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_child")
            .expect("add_child returned an error");
        realm_and_builder_task
            .realm_proxy
            .read_only_directory(
                "data",
                &mut vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "a".to_string(),
                    collection: None,
                })]
                .iter_mut(),
                &mut ftest::DirectoryContents {
                    entries: vec![ftest::DirectoryEntry {
                        file_path: "hippos".to_string(),
                        file_contents: {
                            let value = "rule!";
                            let vmo =
                                zx::Vmo::create(value.len() as u64).expect("failed to create vmo");
                            vmo.write(value.as_bytes(), 0).expect("failed to write to vmo");
                            fmem::Buffer { vmo, size: value.len() as u64 }
                        },
                    }],
                },
            )
            .await
            .expect("failed to call read_only_directory")
            .expect("read_only_directory returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let read_only_dir_decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: runner::LOCAL_COMPONENT_ID_KEY.to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str("0".to_string()))),
                    }]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            capabilities: vec![cm_rust::CapabilityDecl::Directory(cm_rust::DirectoryDecl {
                name: "data".into(),
                source_path: Some("/data".try_into().unwrap()),
                rights: fio::R_STAR_DIR,
            })],
            exposes: vec![cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                source: cm_rust::ExposeSource::Self_,
                source_name: "data".into(),
                target: cm_rust::ExposeTarget::Parent,
                target_name: "data".into(),
                rights: Some(fio::R_STAR_DIR),
                subdir: None,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                children: vec![cm_rust::ChildDecl {
                    name: "a".to_string(),
                    url: "test://a".to_string(),
                    startup: fcdecl::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                }],
                offers: vec![cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                    source: cm_rust::OfferSource::Child(cm_rust::ChildRef {
                        name: "read-only-directory-0".to_string(),
                        collection: None,
                    }),
                    source_name: "data".into(),
                    target: cm_rust::OfferTarget::Child(cm_rust::ChildRef {
                        name: "a".to_string(),
                        collection: None,
                    }),
                    target_name: "data".into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                    rights: Some(fio::R_STAR_DIR),
                    subdir: None,
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "read-only-directory-0".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: read_only_dir_decl, children: vec![] },
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

    // TODO(88429): The following test is impossible to write until sub-realms are supported
    // #[fuchsia::test]
    // async fn replace_component_decl_where_decl_children_conflict_with_mutable_children() {
    // }
}
