// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    cm_rust::{FidlIntoNative, NativeIntoFidl},
    fidl::endpoints::{ProtocolMarker, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_config as fconfig,
    fidl_fuchsia_component_decl as fcdecl, fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_component_test as ftest, fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fuchsia_component::server as fserver,
    fuchsia_fs, fuchsia_zircon_status as zx_status,
    futures::{future::BoxFuture, join, lock::Mutex, FutureExt, StreamExt, TryStreamExt},
    lazy_static::lazy_static,
    std::{
        collections::HashMap,
        convert::TryInto,
        fmt,
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

// The program section of a component decl is not allowed to be mutated
// for local and legacy components except for one instance. In the event,
// that an `args` key is added for a legacy component, replacement of the
// component decl is allowed. This is to enable passing argv to the
// legacy component runner.
const ALLOWLISTED_PROGRAM_ARGS_KEY: &'static str = "args";

#[fuchsia::main]
async fn main() {
    info!("Started.");

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
            if let Err(err) = factory.handle_stream(stream).await {
                error!(%err, "Encountered unexpected error.");
            }
        });
    });

    fs.take_and_serve_directory_handle().expect("Did not receive directory handle.");

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
                        responder.send(&mut Err(ftest::RealmBuilderError::UrlIsNotRelative))?;
                        continue;
                    }
                    let pkg_dir = match pkg_dir_handle
                        .into_proxy()
                        .context("Failed to convert `pkg_dir` ClientEnd to proxy.")
                    {
                        Ok(pkg_dir) => pkg_dir,
                        Err(err) => {
                            responder
                                .send(&mut Err(ftest::RealmBuilderError::InvalidPkgDirHandle))?;
                            return Err(err);
                        }
                    };
                    if let Err(e) = pkg_dir.query().await.context(
                        "Invoking `fuchsia.unknown/Queryable.query` on provided `pkg_dir` failed.",
                    ) {
                        responder.send(&mut Err(ftest::RealmBuilderError::InvalidPkgDirHandle))?;
                        return Err(e);
                    }
                    let realm_node = match RealmNode2::load_from_pkg(
                        relative_url.clone(),
                        Clone::clone(&pkg_dir),
                    )
                    .await
                    {
                        Ok(realm_node) => realm_node,
                        Err(err) => {
                            warn!(method = "RealmBuilderFactory.CreateFromRelativeUrl", message = %err);
                            responder.send(&mut Err(err.into()))?;
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
                        .context("Failed to convert `pkg_dir` ClientEnd to proxy.")?;
                    if let Err(err) = pkg_dir.query().await.context(
                        "Invoking `fuchsia.unknown/Queryable.query` on provided `pkg_dir` failed.",
                    ) {
                        warn!(method = "RealmBuilderFactory.Create", message = %err);
                        responder.send(&mut Err(ftest::RealmBuilderError::InvalidPkgDirHandle))?;
                        continue;
                    }

                    self.create_realm_and_builder(
                        RealmNode2::new(),
                        pkg_dir,
                        realm_server_end,
                        builder_server_end,
                    )?;
                    responder.send(&mut Ok(()))?;
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
            .context("Failed to convert `realm_server_end` to stream.")?;

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
            if let Err(err) = realm.handle_stream(realm_stream).await {
                error!(%err, "`Realm` server unexpectedly failed.");
            }
        });

        let builder_stream = builder_server_end
            .into_stream()
            .context("Failed to convert `builder_server_end` to stream.")?;

        let builder = Builder {
            pkg_dir: Clone::clone(&pkg_dir),
            realm_node,
            registry: self.registry.clone(),
            runner_proxy_placeholder: runner_proxy_placeholder.clone(),
            realm_has_been_built: realm_has_been_built,
        };
        self.execution_scope.spawn(async move {
            if let Err(err) = builder.handle_stream(builder_stream).await {
                error!(%err, "`Builder` server unexpectedly failed.");
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
                        warn!(method = "Builder.Build", message = %RealmBuilderError::BuildAlreadyCalled);
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
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
                        Err(err) => {
                            warn!(method = "Builder.Build", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

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
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.add_child(name.clone(), url.clone(), options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(err) => {
                            warn!(method = "Realm.AddChild", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddLegacyChild { name, legacy_url, options, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.add_legacy_child(name.clone(), legacy_url.clone(), options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(err) => {
                            warn!(method = "Realm.AddLegacyChild", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddChildFromDecl { name, decl, options, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.add_child_from_decl(name.clone(), decl, options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(err) => {
                            warn!(method = "Realm.AddChildFromDecl", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddLocalChild { name, options, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.add_local_child(name.clone(), options).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(err) => {
                            warn!(method = "Realm.AddLocalChild", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddChildRealm { name, options, child_realm, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.add_child_realm(name.clone(), options, child_realm).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(err) => {
                            warn!(method = "Realm.AddChildRealm", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::GetComponentDecl { name, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.get_component_decl(name.clone()).await {
                        Ok(decl) => responder.send(&mut Ok(decl))?,
                        Err(err) => {
                            warn!(method = "Realm.GetComponentDecl", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::ReplaceComponentDecl { name, component_decl, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.replace_component_decl(name.clone(), component_decl).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(err) => {
                            warn!(method = "Realm.ReplaceComponentDecl", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::GetRealmDecl { responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    responder.send(&mut Ok(self.get_realm_decl().await))?;
                }
                ftest::RealmRequest::ReplaceRealmDecl { component_decl, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.replace_realm_decl(component_decl).await {
                        Ok(()) => responder.send(&mut Ok(()))?,
                        Err(err) => {
                            warn!(method = "Realm.ReplaceRealmDecl", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::AddRoute { capabilities, from, to, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.realm_node.route_capabilities(capabilities, from, to).await {
                        Ok(()) => {
                            responder.send(&mut Ok(()))?;
                        }
                        Err(err) => {
                            warn!(method = "Realm.AddRoute", message = %err);
                            responder.send(&mut Err(err.into()))?;
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
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }
                    match self.read_only_directory(name, to, directory_contents).await {
                        Ok(()) => {
                            responder.send(&mut Ok(()))?;
                        }
                        Err(err) => {
                            warn!(method = "Realm.ReadOnlyDirectory", message = %err);
                            responder.send(&mut Err(err.into()))?;
                        }
                    }
                }
                ftest::RealmRequest::InitMutableConfigFromPackage { name, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }

                    self.realm_node
                        .get_sub_realm(&name)
                        .await?
                        .state
                        .lock()
                        .await
                        .config_override_policy = ConfigOverridePolicy::LoadPackagedValuesFirst;

                    responder.send(&mut Ok(()))?;
                }
                ftest::RealmRequest::InitMutableConfigToEmpty { name, responder } => {
                    if self.realm_has_been_built.load(Ordering::Relaxed) {
                        responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
                        continue;
                    }

                    self.realm_node
                        .get_sub_realm(&name)
                        .await?
                        .state
                        .lock()
                        .await
                        .config_override_policy = ConfigOverridePolicy::RequireAllValuesFromBuilder;

                    responder.send(&mut Ok(()))?;
                }
                // TODO(https://fxbug.dev/103951) delete this
                ftest::RealmRequest::ReplaceConfigValue { name, key, value, responder } => {
                    self.handle_replace_config_value_soft_transition(name, key, value, responder)
                        .await?;
                }
                ftest::RealmRequest::SetConfigValue { name, key, value, responder } => {
                    self.handle_replace_config_value_soft_transition(name, key, value, responder)
                        .await?;
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
            return Err(RealmBuilderError::InvalidManifestExtension {
                name,
                extension: ManifestExtension::Modern,
                suggestion: "AddLegacyChild".to_owned(),
            });
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
            return Err(RealmBuilderError::InvalidManifestExtension {
                name,
                extension: ManifestExtension::Legacy,
                suggestion: "AddChild".to_owned(),
            });
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
            return Err(RealmBuilderError::InvalidComponentDeclWithName(
                name,
                to_tabulated_string(e),
            ));
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
                .map_err(|e| RealmBuilderError::InvalidChildRealmHandle(name.clone(), e))?;
            self_realm_node.add_child(name, options, child_realm_node).await?;

            self_execution_scope.spawn(async move {
                if let Err(e) = child_realm.handle_stream(child_realm_stream).await {
                    error!(
                        "|Realm| server for child \"{}\" unexpectedly failed: {}",
                        child_path.join("/"),
                        e
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

    async fn set_config_value(
        &self,
        name: String,
        key: String,
        value_spec: fconfig::ValueSpec,
    ) -> Result<(), RealmBuilderError> {
        let child_node = self.realm_node.get_sub_realm(&name).await?;

        let override_policy = child_node.state.lock().await.config_override_policy;
        if matches!(override_policy, ConfigOverridePolicy::DisallowValuesFromBuilder) {
            return Err(RealmBuilderError::ConfigOverrideUnsupported { name });
        }

        let decl = child_node.get_decl().await;
        let config = decl.config.ok_or(RealmBuilderError::NoConfigSchema(name.clone()))?;
        cm_fidl_validator::validate_value_spec(&value_spec)
            .map_err(|e| RealmBuilderError::ConfigValueInvalid(key.clone(), anyhow::anyhow!(e)))?;
        let value_spec = value_spec.fidl_into_native();
        for (index, field) in config.fields.iter().enumerate() {
            if field.key == key {
                config_encoder::ConfigField::resolve(value_spec.clone(), &field).map_err(|e| {
                    RealmBuilderError::ConfigValueInvalid(name.clone(), anyhow::anyhow!(e))
                })?;
                let mut state_guard = child_node.state.lock().await;
                state_guard.config_value_replacements.insert(index, value_spec);
                return Ok(());
            }
        }

        Err(RealmBuilderError::NoSuchConfigField {
            name,
            key,
            present: config.fields.iter().map(|f| f.key.clone()).collect::<Vec<_>>(),
        })
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
                vec![ftest::Capability::Directory(ftest::Directory {
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

    // TODO(https://fxbug.dev/103951) inline this method
    async fn handle_replace_config_value_soft_transition(
        &self,
        name: String,
        key: String,
        value: fconfig::ValueSpec,
        responder: impl RealmBuilderNullResponder,
    ) -> Result<(), fidl::Error> {
        if self.realm_has_been_built.load(Ordering::Relaxed) {
            responder.send(&mut Err(ftest::RealmBuilderError::BuildAlreadyCalled))?;
        } else {
            match self.set_config_value(name, key, value).await {
                Ok(()) => {
                    responder.send(&mut Ok(()))?;
                }
                Err(err) => {
                    warn!(method = "Realm.SetConfigValue", message = %err);
                    responder.send(&mut Err(err.into()))?;
                }
            }
        }
        Ok(())
    }
}

// TODO(https://fxbug.dev/103951) delete this trait
trait RealmBuilderNullResponder {
    fn send(self, response: &mut Result<(), ftest::RealmBuilderError>) -> Result<(), fidl::Error>;
}
impl RealmBuilderNullResponder for ftest::RealmReplaceConfigValueResponder {
    fn send(self, response: &mut Result<(), ftest::RealmBuilderError>) -> Result<(), fidl::Error> {
        self.send(response)
    }
}
impl RealmBuilderNullResponder for ftest::RealmSetConfigValueResponder {
    fn send(self, response: &mut Result<(), ftest::RealmBuilderError>) -> Result<(), fidl::Error> {
        self.send(response)
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

    /// Stores indices to configuration values that must be replaced when the config value file
    /// of a component is read in from the package directory during resolve.
    config_value_replacements: HashMap<usize, cm_rust::ValueSpec>,

    /// Policy for allowing values from SetConfigValue and whether to also load a component's
    /// packaged/default values.
    config_override_policy: ConfigOverridePolicy,

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
            return Err(RealmBuilderError::InvalidComponentDecl(to_tabulated_string(e)));
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, Default)]
pub enum ConfigOverridePolicy {
    #[default]
    DisallowValuesFromBuilder,
    LoadPackagedValuesFirst,
    RequireAllValuesFromBuilder,
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
        let () = validate_program_modifications(&state_guard.decl, &new_decl)?;
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

            let file_proxy_res = fuchsia_fs::directory::open_file(
                &test_pkg_dir,
                &path,
                fuchsia_fs::OpenFlags::RIGHT_READABLE,
            )
            .await;
            let file_proxy = match file_proxy_res {
                Ok(file_proxy) => file_proxy,
                Err(fuchsia_fs::node::OpenError::OpenError(zx_status::Status::NOT_FOUND)) => {
                    return Err(RealmBuilderError::DeclNotFound(relative_url.clone()))
                }
                Err(e) => {
                    return Err(RealmBuilderError::DeclReadError(relative_url.clone(), e.into()))
                }
            };

            let fidl_decl = fuchsia_fs::read_file_fidl::<fcdecl::Component>(&file_proxy)
                .await
                .map_err(|e| RealmBuilderError::DeclReadError(relative_url.clone(), e))?;
            cm_fidl_validator::validate(&fidl_decl).map_err(|e| {
                RealmBuilderError::InvalidComponentDeclWithName(
                    relative_url,
                    to_tabulated_string(e),
                )
            })?;

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
        capabilities: Vec<ftest::Capability>,
        from: fcdecl::Ref,
        to: Vec<fcdecl::Ref>,
    ) -> Result<(), RealmBuilderError> {
        if capabilities.is_empty() {
            return Err(RealmBuilderError::CapabilitiesEmpty);
        }

        let mut state_guard = self.state.lock().await;
        if !contains_child(state_guard.deref(), &from) {
            return Err(RealmBuilderError::NoSuchSource(ref_to_string(&from)));
        }

        for capability in capabilities {
            for target in &to {
                if &from == target {
                    return Err(RealmBuilderError::SourceAndTargetMatch(ref_to_string(&from)));
                }

                if !contains_child(state_guard.deref(), target) {
                    return Err(RealmBuilderError::NoSuchTarget(ref_to_string(&target)));
                }

                if is_parent_ref(&target) {
                    match &capability {
                        ftest::Capability::Protocol(ftest::Protocol { availability, .. })
                        | ftest::Capability::Directory(ftest::Directory { availability, .. })
                        | ftest::Capability::Storage(ftest::Storage { availability, .. })
                        | ftest::Capability::Service(ftest::Service { availability, .. })
                        | ftest::Capability::Event(ftest::Event { availability, .. }) => {
                            match availability {
                                Some(fcdecl::Availability::Required) | None => (),
                                _ => {
                                    return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                                        "capability availability cannot be \"SameAsTarget\" or \"Optional\" when the target is the parent",
                                    )));
                                }
                            }
                        }
                        _ => {
                            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                                "unknown capability type",
                            )))
                        }
                    }
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
            let config_value_replacements = state_guard.config_value_replacements.clone();
            match registry
                .validate_and_register(
                    &decl,
                    name.clone(),
                    Some(Clone::clone(&package_dir)),
                    config_value_replacements,
                    state_guard.config_override_policy,
                )
                .await
            {
                Ok(url) => Ok(url),
                Err(e) => Err(RealmBuilderError::InvalidComponentDeclWithName(
                    name,
                    to_tabulated_string(e),
                )),
            }
        }
        .boxed()
    }
}

async fn add_use_decl_if_needed(
    realm: &mut RealmNodeState,
    ref_: fcdecl::Ref,
    capability: ftest::Capability,
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
    capability: ftest::Capability,
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
                "Required field `name` is empty."
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
        RealmBuilderError::CapabilityInvalid(anyhow::format_err!("Required field `name` is empty."))
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
            RealmBuilderError::CapabilityInvalid(anyhow::format_err!("The `path` field is not set. This field is required when routing to or from a local component. For more information on the `path` field, see https://fuchsia.dev/go/components/realm-builder-reference#Directory."))
        })?
        .as_str()
        .try_into()
        .map_err(|e| {
            RealmBuilderError::CapabilityInvalid(anyhow::format_err!("The `path` field is invalid: {:?}. All paths must be `/` delimited strings with a leading slash.", e))
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
        RealmBuilderError::CapabilityInvalid(anyhow::format_err!("Required field `name` is empty. This field must be provided. For more information on the `name` field, see https://fuchsia.dev/go/components/realm-builder-reference#Protocol."))
    })?;
    let path = path.as_ref().cloned().unwrap_or_else(|| format!("/svc/{}", name));
    path.as_str().try_into().map_err(|e| {
        RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
            "Could not create path for protocol {}. Encountered unexpected error. {:?}",
            name,
            e
        ))
    })
}

fn create_capability_decl(
    capability: ftest::Capability,
) -> Result<cm_rust::CapabilityDecl, RealmBuilderError> {
    Ok(match capability {
        ftest::Capability::Protocol(protocol) => {
            let name = try_into_source_name(&protocol.name)?;
            let source_path = Some(try_into_service_path(&protocol.name, &protocol.path)?);
            cm_rust::CapabilityDecl::Protocol(cm_rust::ProtocolDecl { name, source_path })
        }
        ftest::Capability::Directory(directory) => {
            let name = try_into_source_name(&directory.name)?;
            let source_path = Some(try_into_capability_path(&directory.path)?);
            let rights = directory.rights.ok_or_else(|| RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!(
                    "The `rights` field is not set. This field is required when routing directory capabilities to or from a local component. Required fields are defined at https://fuchsia.dev/go/components/realm-builder-reference#Directory.",
                ),
            ))?;
            cm_rust::CapabilityDecl::Directory(cm_rust::DirectoryDecl { name, source_path, rights })
        }
        ftest::Capability::Storage(_) => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "Storage capabilities with a source of `self` can not be routed. Please use `GetComponentDecl` and `ReplaceComponentDecl` to declare and route this capability."
            )))?;
        }
        ftest::Capability::Service(service) => {
            let name = try_into_source_name(&service.name)?;
            let source_path = Some(try_into_service_path(&service.name, &service.path)?);
            cm_rust::CapabilityDecl::Service(cm_rust::ServiceDecl { name, source_path })
        }
        ftest::Capability::Event(event) => {
            let name = try_into_source_name(&event.name)?;
            cm_rust::CapabilityDecl::Event(cm_rust::EventDecl { name })
        }
        ftest::Capability::EventStream(event) => {
            let name = try_into_source_name(&event.name)?;
            cm_rust::CapabilityDecl::EventStream(cm_rust::EventStreamDecl { name })
        }
        _ => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "Encountered unsupported capability variant: {:?}.",
                capability.clone()
            )));
        }
    })
}

fn get_offer_availability(availability: &Option<fcdecl::Availability>) -> cm_rust::Availability {
    match availability {
        Some(fcdecl::Availability::Optional) => cm_rust::Availability::Optional,
        Some(fcdecl::Availability::SameAsTarget) => cm_rust::Availability::SameAsTarget,
        _ => cm_rust::Availability::Required,
    }
}

fn create_offer_decl(
    capability: ftest::Capability,
    source: fcdecl::Ref,
    target: fcdecl::Ref,
) -> Result<cm_rust::OfferDecl, RealmBuilderError> {
    let source: cm_rust::OfferSource = source.fidl_into_native();
    let target: cm_rust::OfferTarget = target.fidl_into_native();

    Ok(match capability {
        ftest::Capability::Protocol(protocol) => {
            let source_name = try_into_source_name(&protocol.name)?;
            let target_name = try_into_target_name(&protocol.name, &protocol.as_)?;
            let dependency_type = into_dependency_type(&protocol.type_);
            let availability = get_offer_availability(&protocol.availability);
            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source,
                source_name,
                target,
                target_name,
                dependency_type,
                availability,
            })
        }
        ftest::Capability::Directory(directory) => {
            let source_name = try_into_source_name(&directory.name)?;
            let target_name = try_into_target_name(&directory.name, &directory.as_)?;
            let dependency_type = into_dependency_type(&directory.type_);
            let availability = get_offer_availability(&directory.availability);
            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                source,
                source_name,
                target,
                target_name,
                rights: directory.rights,
                subdir: directory.subdir.map(PathBuf::from),
                dependency_type,
                availability,
            })
        }
        ftest::Capability::Storage(storage) => {
            let source_name = try_into_source_name(&storage.name)?;
            let target_name = try_into_target_name(&storage.name, &storage.as_)?;
            let availability = get_offer_availability(&storage.availability);
            cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                source,
                source_name,
                target,
                target_name,
                availability,
            })
        }
        ftest::Capability::Service(service) => {
            let source_name = try_into_source_name(&service.name)?;
            let target_name = try_into_target_name(&service.name, &service.as_)?;
            let availability = get_offer_availability(&service.availability);
            cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                source,
                source_name,
                target,
                target_name,
                source_instance_filter: None,
                renamed_instances: None,
                availability,
            })
        }
        ftest::Capability::Event(event) => {
            let source_name = try_into_source_name(&event.name)?;
            let target_name = try_into_target_name(&event.name, &event.as_)?;
            let filter = event.filter.as_ref().cloned().map(FidlIntoNative::fidl_into_native);
            let availability = get_offer_availability(&event.availability);
            cm_rust::OfferDecl::Event(cm_rust::OfferEventDecl {
                source,
                source_name,
                target,
                target_name,
                filter,
                availability,
            })
        }
        ftest::Capability::EventStream(event_stream) => {
            let source_name = try_into_source_name(&event_stream.name)?;
            let target_name = try_into_target_name(&event_stream.name, &event_stream.as_)?;
            let filter =
                event_stream.filter.as_ref().cloned().map(FidlIntoNative::fidl_into_native);
            cm_rust::OfferDecl::EventStream(cm_rust::OfferEventStreamDecl {
                source,
                source_name,
                target,
                target_name,
                filter,
                scope: event_stream.scope.as_ref().cloned().map(FidlIntoNative::fidl_into_native),
                availability: cm_rust::Availability::Required,
            })
        }
        _ => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "Encountered unsupported capability variant: {:?}.",
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
    capability: ftest::Capability,
    source: fcdecl::Ref,
    exposing_in: ExposingIn,
) -> Result<cm_rust::ExposeDecl, RealmBuilderError> {
    let source: cm_rust::ExposeSource = source.fidl_into_native();

    Ok(match capability {
        ftest::Capability::Protocol(protocol) => {
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
        ftest::Capability::Directory(directory) => {
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
        ftest::Capability::Storage(storage) => {
            let source_name = try_into_source_name(&storage.name)?;
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "Capability \"{}\" can not be exposed because it's not possible to expose storage capabilities. This is most likely a bug from the Realm Builder library. Please file one at https://bugs.fuchsia.dev under the ComponentFramework>SDK component.", source_name
            )));
        }
        ftest::Capability::Service(service) => {
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
        ftest::Capability::Event(event) => {
            let source_name = try_into_source_name(&event.name)?;
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "Capability \"{}\" can not be exposed because it's not possible to expose event capabilities. This is most likely a bug from the Realm Builder library. Please file one at https://bugs.fuchsia.dev under the ComponentFramework>SDK component.", source_name
            )));
        }
        _ => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "Encountered unsupported capability variant: {:?}.",
                capability.clone()
            )));
        }
    })
}

fn check_and_unwrap_use_availability(
    availability: Option<fcdecl::Availability>,
) -> Result<cm_rust::Availability, RealmBuilderError> {
    match availability {
        None => Ok(cm_rust::Availability::Required),
        Some(fcdecl::Availability::SameAsTarget) => {
            Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "availability can not be \"same_as_target\" if the target is a local component"
            )))
        }
        Some(availability) => Ok(availability.fidl_into_native()),
    }
}

fn create_use_decl(capability: ftest::Capability) -> Result<cm_rust::UseDecl, RealmBuilderError> {
    Ok(match capability {
        ftest::Capability::Protocol(protocol) => {
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
                availability: check_and_unwrap_use_availability(protocol.availability)?,
            })
        }
        ftest::Capability::Directory(directory) => {
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&directory.name, &directory.as_)?;
            let target_path = try_into_capability_path(&directory.path)?;
            let rights = directory.rights.ok_or_else(|| RealmBuilderError::CapabilityInvalid(
                anyhow::format_err!(
                    "The `rights` field is not set. This field is required when routing directory capabilities to or from a local component.",
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
                availability: check_and_unwrap_use_availability(directory.availability)?,
            })
        }
        ftest::Capability::Storage(storage) => {
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&storage.name, &storage.as_)?;
            let target_path = try_into_capability_path(&storage.path)?;
            cm_rust::UseDecl::Storage(cm_rust::UseStorageDecl {
                source_name,
                target_path,
                availability: check_and_unwrap_use_availability(storage.availability)?,
            })
        }
        ftest::Capability::Service(service) => {
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
                availability: check_and_unwrap_use_availability(service.availability)?,
            })
        }
        ftest::Capability::Event(event) => {
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&event.name, &event.as_)?;
            let filter = event.filter.as_ref().cloned().map(FidlIntoNative::fidl_into_native);
            cm_rust::UseDecl::Event(cm_rust::UseEventDecl {
                source: cm_rust::UseSource::Parent,
                source_name: source_name.clone(),
                target_name: source_name,
                filter,
                dependency_type: cm_rust::DependencyType::Strong,
                availability: check_and_unwrap_use_availability(event.availability)?,
            })
        }
        ftest::Capability::EventStream(event) => {
            // If the capability was renamed in the parent's offer declaration, we want to use the
            // post-rename version of it here.
            let source_name = try_into_target_name(&event.name, &event.as_)?;
            let filter = event.filter.as_ref().cloned().map(FidlIntoNative::fidl_into_native);
            let target_path = try_into_capability_path(&event.path)?;
            cm_rust::UseDecl::EventStream(cm_rust::UseEventStreamDecl {
                source: cm_rust::UseSource::Parent,
                source_name: source_name.clone(),
                target_path,
                filter,
                scope: event.scope.as_ref().cloned().map(FidlIntoNative::fidl_into_native),
                availability: cm_rust::Availability::Required,
            })
        }
        _ => {
            return Err(RealmBuilderError::CapabilityInvalid(anyhow::format_err!(
                "Encountered unsupported capability variant: {:?}.",
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

// If this realm node is going have its decl replaced, we need to ensure
// that the program section isn't corrupted. This is only necessary for
// legacy components, whose program section is generated by
// realm builder. In that case, we only allow specific modifications to
// the program section. Specifically, we only allow adding an `args` entry for
// legacy components.
fn validate_program_modifications(
    old_decl: &cm_rust::ComponentDecl,
    new_decl: &cm_rust::ComponentDecl,
) -> Result<(), RealmBuilderError> {
    if old_decl.program.as_ref().and_then(|p| p.runner.as_ref())
        == Some(&runner::RUNNER_NAME.into())
    {
        let new_decl_program = match new_decl.program.as_ref() {
            Some(program) => {
                if program_contains_entry(program, runner::LEGACY_URL_KEY)
                    && program_contains_entry(program, ALLOWLISTED_PROGRAM_ARGS_KEY)
                {
                    program_with_entry_removed(program.clone(), ALLOWLISTED_PROGRAM_ARGS_KEY)
                } else {
                    program.clone()
                }
            }
            None => {
                return Err(RealmBuilderError::ImmutableProgram);
            }
        };

        // We know that `old_decl.program` is `Some(_)` because we're inside
        // this `if` clause. Therefore, it's safe to check equality against
        // `Some(new_decl_program)`.
        if old_decl.program != Some(new_decl_program) {
            return Err(RealmBuilderError::ImmutableProgram);
        }
    }

    Ok(())
}

fn program_contains_entry(program: &cm_rust::ProgramDecl, key: &str) -> bool {
    if let Some(entries) = program.info.entries.as_ref() {
        return entries.iter().any(|entry| entry.key == key);
    }

    return false;
}

fn program_with_entry_removed(
    mut program: cm_rust::ProgramDecl,
    key: &str,
) -> cm_rust::ProgramDecl {
    if let Some(entries) = program.info.entries.as_mut() {
        entries.retain(|entry| entry.key != key);
    }

    return program;
}

#[derive(Debug)]
enum ManifestExtension {
    Modern,
    Legacy,
}

impl fmt::Display for ManifestExtension {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let ext = match &self {
            ManifestExtension::Modern => ".cm",
            ManifestExtension::Legacy => ".cmx",
        };

        write!(f, "{}", ext)
    }
}

// Since Rust doesn't allow one to implement out-of-crate traits (fmt::Display
// for out-of-crate types (fcdecl::Ref), a convenience function is provided.
// See Rust E0117 for more info.
fn ref_to_string(ref_: &fcdecl::Ref) -> String {
    match ref_ {
        fcdecl::Ref::Child(c) => c.name.to_owned(),
        fcdecl::Ref::Parent(_) => "<parent>".to_owned(),
        fcdecl::Ref::Self_(_) => "<self>".to_owned(),
        fcdecl::Ref::Collection(c) => c.name.to_owned(),
        fcdecl::Ref::Framework(_) => "<framework>".to_owned(),
        fcdecl::Ref::Capability(c) => c.name.to_owned(),
        fcdecl::Ref::Debug(_) => "<debug>".to_owned(),
        fcdecl::Ref::VoidType(_) => "<void>".to_owned(),
        _ => unreachable!("Encountered unknown `Ref` variant."),
    }
}

#[allow(unused)]
#[derive(Debug, Error)]
enum RealmBuilderError {
    /// Child cannot be added to the realm, as there is already a child in the realm with that
    /// name.
    #[error("Unable to add child because one already exists with the name \"{0}\". Child names within a realm must be unique.")]
    ChildAlreadyExists(String),

    /// A legacy component URL was given to `AddChild`, or a modern component url was given to
    /// `AddLegacyChild`.
    #[error("Unable to add child \"{name}\" because the component URL is invalid. Only URLs ending with the `{extension}` extension are supported for this method. Use the method `Realm.{suggestion}` instead.")]
    InvalidManifestExtension { name: String, extension: ManifestExtension, suggestion: String },

    /// A component declaration failed validation.
    #[error("The constructed component declaration is invalid. Please fix all the listed errors:\n{0}\nFor a reference as to how component declarations are authored, see https://fuchsia.dev/go/components/declaration.")]
    InvalidComponentDecl(String),

    /// A component declaration failed validation.
    #[error("The component declaration for child \"{0}\" is invalid. Please fix all the listed errors:\n{1}\nFor a reference as to how component declarations are authored, see https://fuchsia.dev/go/components/declaration.")]
    InvalidComponentDeclWithName(String, String),

    /// The referenced child does not exist.
    #[error("No child exists with the name \"{0}\". Before fetching or changing its component declaration, a child must be added to the realm with the `AddChild` group of methods.")]
    NoSuchChild(String),

    /// The component declaration for the referenced child cannot be viewed nor manipulated by
    /// RealmBuilder because the child was added to the realm using an URL that was neither a
    /// relative nor a legacy URL.
    #[error("The component declaration for child {0} cannot be replaced. This occurs for components referenced via absolute URL. If you'd like to mutate a component's decl, add it your test package and reference it via a relative URL: https://fuchsia.dev/go/components/url#relative.")]
    ChildDeclNotVisible(String),

    /// The source does not exist.
    #[error("Source component for capability is invalid. No child exists with the name \"{0}\". Before a component can be set as a source for a capability, it must be added to the realm with the `AddChild` group of methods.")]
    NoSuchSource(String),

    /// A target does not exist.
    #[error("Target component for capability is invalid. No child exists with the name '{0}'. Before a component can be set as a source for a capability, it must be added to the realm with the `AddChild` group of methods.")]
    NoSuchTarget(String),

    /// The `capabilities` field is empty.
    #[error("The `capabilities` field can not be omitted. It is used to specify what capabilities will be routed. Provide at least one capability to route: https://fuchsia.dev/go/components/realm-builder-reference#Realm.AddRoute.")]
    CapabilitiesEmpty,

    /// The `targets` field is empty.
    #[error("The `targets` field can not be omitted. It is used to determine what component(s) to route a capability to. Provide at least one component as a target: https://fuchsia.dev/go/components/realm-builder-reference#Realm.AddRoute.")]
    TargetsEmpty,

    /// The `from` value is equal to one of the elements in `to`.
    #[error("One of the targets of this route is equal to the source {0:?}. Routing a capability to itself is not supported.")]
    SourceAndTargetMatch(String),

    /// The test package does not contain the component declaration referenced by a relative URL.
    #[error("Component \"{0}\" not found in package. Only components added to the test's package can be referenced by relative URLs. Ensure that this component is included in the test's package.")]
    DeclNotFound(String),

    /// Encountered an I/O error when attempting to read a component declaration referenced by a
    /// relative URL from the test package.
    #[error("Could not read the manifest for component \"{0}\". {1:?}")]
    DeclReadError(String, anyhow::Error),

    /// The `Build` function has been called multiple times on this channel.
    #[error("Build method was called multiple times. This method can only be called once. After it's called, the realm to be constructed can not be changed.")]
    BuildAlreadyCalled,

    #[error("Failed to route capability. {0:?}")]
    CapabilityInvalid(anyhow::Error),

    /// The handle the client provided is not usable
    #[error("Handle for child realm \"{0}\" is not usable. {1:?}")]
    InvalidChildRealmHandle(String, fidl::Error),

    /// `ReplaceComponentDecl` was called on a legacy or local component with a program declaration
    /// that did not match the one from the old component declaration. This could render a legacy
    /// or local component non-functional, and is disallowed.
    #[error(
        "Attempted to change `program` section of immutable child. The `program` section of a legacy or local component cannot be changed. Migrate the referenced child to modern component framework to use this feature."
    )]
    ImmutableProgram,

    /// The component does not have a config schema defined. Attempting to
    /// set a config value is not allowed.
    #[error("Could not replace config value for child \"{0}\". The component does not have a config schema in its declaration. Only components with a config schema can have their config values modified. For more information about the structured configuration feature, see https://fuchsia.dev/go/components/structured-config.")]
    NoConfigSchema(String),

    /// The component's config schema does not have a field with that name.
    #[error("Could not replace config value for child \"{name}\". No field with the name `{key}` is present in the config schema. The fields present in the schema are: {present:?}.")]
    NoSuchConfigField { name: String, key: String, present: Vec<String> },

    /// A config value is invalid. This may mean a type mismatch or an issue
    /// with constraints like string/vector length.
    #[error(
        "Could not replace config value for child '{0}'. The value provided is invalid: {1:?}"
    )]
    ConfigValueInvalid(String, anyhow::Error),

    /// The caller never told us how to merge their config overrides with the packaged ones.
    #[error("Could not replace config value for child '{name}' because no override strategy has been selected. First call InitMutableConfigFromPackage or InitMutableConfigToEmpty.")]
    ConfigOverrideUnsupported { name: String },
}

impl From<RealmBuilderError> for ftest::RealmBuilderError {
    fn from(err: RealmBuilderError) -> Self {
        match err {
            RealmBuilderError::ChildAlreadyExists(_) => Self::ChildAlreadyExists,
            RealmBuilderError::InvalidManifestExtension { .. } => Self::InvalidManifestExtension,
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
            RealmBuilderError::InvalidChildRealmHandle(_, _) => Self::InvalidChildRealmHandle,
            RealmBuilderError::ImmutableProgram => Self::ImmutableProgram,
            RealmBuilderError::NoConfigSchema(_) => Self::NoConfigSchema,
            RealmBuilderError::NoSuchConfigField { .. } => Self::NoSuchConfigField,
            RealmBuilderError::ConfigValueInvalid(_, _) => Self::ConfigValueInvalid,
            RealmBuilderError::ConfigOverrideUnsupported { .. } => Self::ConfigOverrideUnsupported,
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

// Formats an ErrorList into a tabulated string. This format is used to create
// more readable user error messages.
fn to_tabulated_string(errors: cm_fidl_validator::error::ErrorList) -> String {
    let mut output = String::new();
    for (i, err) in errors.errs.iter().enumerate() {
        let is_last_element = errors.errs.len() - i == 1;
        output.push_str(&format!("  {}. {}", i + 1, err));
        if !is_last_element {
            output.push('\n');
        }
    }

    output
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
    ) -> Result<(String, Arc<resolver::Registry>), ftest::RealmBuilderError> {
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
    ) -> Result<(String, Arc<resolver::Registry>), ftest::RealmBuilderError> {
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
            let pkg_dir = fuchsia_fs::directory::open_in_namespace(
                "/pkg",
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
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

        async fn call_build(&mut self) -> Result<String, ftest::RealmBuilderError> {
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
            mut capabilities: Vec<ftest::Capability>,
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
        assert_eq!(Err(ftest::RealmBuilderError::BuildAlreadyCalled), res);
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
                    availability: cm_rust::Availability::Required,
                })],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![],
        };
        let error = build_tree(&mut tree).await.expect_err("builder didn't notice invalid decl");
        assert_eq!(error, ftest::RealmBuilderError::InvalidComponentDecl);
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
                    availability: cm_rust::Availability::Required,
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
                    availability: cm_rust::Availability::Required,
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
                    availability: cm_rust::Availability::Required,
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
                    availability: cm_rust::Availability::Required,
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
                            availability: cm_rust::Availability::Required,
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
                    availability: cm_rust::Availability::Required,
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
                    availability: cm_rust::Availability::Required,
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
                    availability: cm_rust::Availability::Required,
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
        assert_eq!(err, ftest::RealmBuilderError::InvalidManifestExtension);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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

        let a_decl_file = fuchsia_fs::file::open_in_namespace(
            "/pkg/meta/realm_builder_server_unit_tests.cm",
            fio::OpenFlags::RIGHT_READABLE,
        )
        .expect("failed to open manifest");
        let a_decl = fuchsia_fs::read_file_fidl::<fcdecl::Component>(&a_decl_file)
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

        let realm_with_child_decl_file = fuchsia_fs::file::open_in_namespace(
            "/pkg/meta/realm_with_child.cm",
            fio::OpenFlags::RIGHT_READABLE,
        )
        .expect("failed to open manifest");
        let mut realm_with_child_decl =
            fuchsia_fs::read_file_fidl::<fcdecl::Component>(&realm_with_child_decl_file)
                .await
                .expect("failed to read manifest")
                .fidl_into_native();

        // The "a" child is rewritten by realm builder
        realm_with_child_decl.children =
            realm_with_child_decl.children.into_iter().filter(|c| &c.name != "a").collect();

        let a_decl_file =
            fuchsia_fs::file::open_in_namespace("/pkg/meta/a.cm", fio::OpenFlags::RIGHT_READABLE)
                .expect("failed to open manifest");
        let a_decl = fuchsia_fs::read_file_fidl::<fcdecl::Component>(&a_decl_file)
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
        assert_eq!(err, ftest::RealmBuilderError::InvalidManifestExtension);
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
        assert_eq!(err, ftest::RealmBuilderError::InvalidManifestExtension);
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
                availability: cm_rust::Availability::Required,
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
                availability: cm_rust::Availability::Required,
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
                vec![ftest::Capability::Protocol(ftest::Protocol {
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildAlreadyExists);
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
                    ftest::Capability::Protocol(ftest::Protocol {
                        name: Some("fuchsia.examples.Hippo".to_owned()),
                        as_: Some("fuchsia.examples.Elephant".to_owned()),
                        type_: Some(fcdecl::DependencyType::Strong),
                        ..ftest::Protocol::EMPTY
                    }),
                    ftest::Capability::Directory(ftest::Directory {
                        name: Some("config-data".to_owned()),
                        rights: Some(fio::RW_STAR_DIR),
                        subdir: Some("component".to_owned()),
                        ..ftest::Directory::EMPTY
                    }),
                    ftest::Capability::Storage(ftest::Storage {
                        name: Some("temp".to_string()),
                        as_: Some("data".to_string()),
                        ..ftest::Storage::EMPTY
                    }),
                    ftest::Capability::Service(ftest::Service {
                        name: Some("fuchsia.examples.Whale".to_string()),
                        as_: Some("fuchsia.examples.Orca".to_string()),
                        ..ftest::Service::EMPTY
                    }),
                    ftest::Capability::EventStream(ftest::EventStream {
                        name: Some("started_v2".to_string()),
                        as_: Some("started_event".to_string()),
                        ..ftest::EventStream::EMPTY
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
                vec![ftest::Capability::Protocol(ftest::Protocol {
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
                vec![ftest::Capability::Protocol(ftest::Protocol {
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
                        availability: cm_rust::Availability::Required,
                    }),
                    cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "config-data".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "config-data".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        rights: Some(fio::RW_STAR_DIR),
                        subdir: Some(PathBuf::from("component")),
                        availability: cm_rust::Availability::Required,
                    }),
                    cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "temp".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "data".into(),
                        availability: cm_rust::Availability::Required,
                    }),
                    cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Whale".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "fuchsia.examples.Orca".into(),
                        source_instance_filter: None,
                        renamed_instances: None,
                        availability: cm_rust::Availability::Required,
                    }),
                    cm_rust::OfferDecl::EventStream(cm_rust::OfferEventStreamDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "started_v2".into(),
                        filter: None,
                        scope: None,
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "started_event".into(),
                        availability: cm_rust::Availability::Required,
                    }),
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::static_child("a".to_string()),
                        source_name: "fuchsia.examples.Echo".into(),
                        target: cm_rust::OfferTarget::static_child("b".to_string()),
                        target_name: "fuchsia.examples.Echo".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        availability: cm_rust::Availability::Required,
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
    async fn add_optional_route() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .add_child_or_panic("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await;
        realm_and_builder_task
            .realm_proxy
            .add_local_child("b", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_local_child")
            .expect("add_local_child returned an error");

        // Assert that parent -> child optional capabilities generate proper offer decls.
        realm_and_builder_task
            .add_route_or_panic(
                vec![
                    ftest::Capability::Protocol(ftest::Protocol {
                        name: Some("fuchsia.examples.Hippo".to_owned()),
                        as_: Some("fuchsia.examples.Elephant".to_owned()),
                        type_: Some(fcdecl::DependencyType::Strong),
                        availability: Some(fcdecl::Availability::Optional),
                        ..ftest::Protocol::EMPTY
                    }),
                    ftest::Capability::Directory(ftest::Directory {
                        name: Some("config-data".to_owned()),
                        rights: Some(fio::RW_STAR_DIR),
                        path: Some("/config-data".to_owned()),
                        subdir: Some("component".to_owned()),
                        availability: Some(fcdecl::Availability::Optional),
                        ..ftest::Directory::EMPTY
                    }),
                    ftest::Capability::Storage(ftest::Storage {
                        name: Some("temp".to_string()),
                        as_: Some("data".to_string()),
                        path: Some("/data".to_string()),
                        availability: Some(fcdecl::Availability::Optional),
                        ..ftest::Storage::EMPTY
                    }),
                    ftest::Capability::Service(ftest::Service {
                        name: Some("fuchsia.examples.Whale".to_string()),
                        as_: Some("fuchsia.examples.Orca".to_string()),
                        availability: Some(fcdecl::Availability::Optional),
                        ..ftest::Service::EMPTY
                    }),
                ],
                fcdecl::Ref::Parent(fcdecl::ParentRef {}),
                vec![
                    fcdecl::Ref::Child(fcdecl::ChildRef { name: "a".to_owned(), collection: None }),
                    fcdecl::Ref::Child(fcdecl::ChildRef { name: "b".to_owned(), collection: None }),
                ],
            )
            .await;

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
                            value: Some(Box::new(fdata::DictionaryValue::Str("b".to_string()))),
                        },
                    ]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            uses: vec![
                cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                    source: cm_rust::UseSource::Parent,
                    source_name: "fuchsia.examples.Elephant".into(),
                    target_path: "/svc/fuchsia.examples.Elephant".try_into().unwrap(),
                    dependency_type: cm_rust::DependencyType::Strong,
                    availability: cm_rust::Availability::Optional,
                }),
                cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
                    source: cm_rust::UseSource::Parent,
                    source_name: "config-data".into(),
                    target_path: "/config-data".try_into().unwrap(),
                    rights: fio::RW_STAR_DIR,
                    subdir: None,
                    dependency_type: cm_rust::DependencyType::Strong,
                    availability: cm_rust::Availability::Optional,
                }),
                cm_rust::UseDecl::Storage(cm_rust::UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: cm_rust::Availability::Optional,
                }),
                cm_rust::UseDecl::Service(cm_rust::UseServiceDecl {
                    source: cm_rust::UseSource::Parent,
                    source_name: cm_rust::CapabilityName("fuchsia.examples.Orca".to_owned()),
                    target_path: "/svc/fuchsia.examples.Orca".try_into().unwrap(),
                    dependency_type: cm_rust::DependencyType::Strong,
                    availability: cm_rust::Availability::Optional,
                }),
            ],
            ..cm_rust::ComponentDecl::default()
        };
        let mut expected_tree = ComponentTree {
            decl: cm_rust::ComponentDecl {
                children: vec![cm_rust::ChildDecl {
                    name: "a".to_string(),
                    url: "test:///a".to_string(),
                    startup: fcdecl::StartupMode::Lazy,
                    on_terminate: None,
                    environment: None,
                }],
                offers: vec![
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Hippo".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "fuchsia.examples.Elephant".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        availability: cm_rust::Availability::Optional,
                    }),
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Hippo".into(),
                        target: cm_rust::OfferTarget::static_child("b".to_string()),
                        target_name: "fuchsia.examples.Elephant".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        availability: cm_rust::Availability::Optional,
                    }),
                    cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "config-data".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "config-data".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        rights: Some(fio::RW_STAR_DIR),
                        subdir: Some(PathBuf::from("component")),
                        availability: cm_rust::Availability::Optional,
                    }),
                    cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "config-data".into(),
                        target: cm_rust::OfferTarget::static_child("b".to_string()),
                        target_name: "config-data".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        rights: Some(fio::RW_STAR_DIR),
                        subdir: Some(PathBuf::from("component")),
                        availability: cm_rust::Availability::Optional,
                    }),
                    cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "temp".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "data".into(),
                        availability: cm_rust::Availability::Optional,
                    }),
                    cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "temp".into(),
                        target: cm_rust::OfferTarget::static_child("b".to_string()),
                        target_name: "data".into(),
                        availability: cm_rust::Availability::Optional,
                    }),
                    cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Whale".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "fuchsia.examples.Orca".into(),
                        source_instance_filter: None,
                        renamed_instances: None,
                        availability: cm_rust::Availability::Optional,
                    }),
                    cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Whale".into(),
                        target: cm_rust::OfferTarget::static_child("b".to_string()),
                        target_name: "fuchsia.examples.Orca".into(),
                        source_instance_filter: None,
                        renamed_instances: None,
                        availability: cm_rust::Availability::Optional,
                    }),
                ],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![(
                "b".to_string(),
                ftest::ChildOptions::EMPTY,
                ComponentTree { decl: b_decl, children: vec![] },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn add_same_as_target_route() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .add_child_or_panic("a", "test:///a", ftest::ChildOptions::EMPTY)
            .await;

        // Assert that parent -> child optional capabilities generate proper offer decls.
        realm_and_builder_task
            .add_route_or_panic(
                vec![
                    ftest::Capability::Protocol(ftest::Protocol {
                        name: Some("fuchsia.examples.Hippo".to_owned()),
                        as_: Some("fuchsia.examples.Elephant".to_owned()),
                        type_: Some(fcdecl::DependencyType::Strong),
                        availability: Some(fcdecl::Availability::SameAsTarget),
                        ..ftest::Protocol::EMPTY
                    }),
                    ftest::Capability::Directory(ftest::Directory {
                        name: Some("config-data".to_owned()),
                        rights: Some(fio::RW_STAR_DIR),
                        path: Some("/config-data".to_owned()),
                        subdir: Some("component".to_owned()),
                        availability: Some(fcdecl::Availability::SameAsTarget),
                        ..ftest::Directory::EMPTY
                    }),
                    ftest::Capability::Storage(ftest::Storage {
                        name: Some("temp".to_string()),
                        as_: Some("data".to_string()),
                        path: Some("/data".to_string()),
                        availability: Some(fcdecl::Availability::SameAsTarget),
                        ..ftest::Storage::EMPTY
                    }),
                    ftest::Capability::Service(ftest::Service {
                        name: Some("fuchsia.examples.Whale".to_string()),
                        as_: Some("fuchsia.examples.Orca".to_string()),
                        availability: Some(fcdecl::Availability::SameAsTarget),
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
                offers: vec![
                    cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Hippo".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "fuchsia.examples.Elephant".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        availability: cm_rust::Availability::SameAsTarget,
                    }),
                    cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "config-data".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "config-data".into(),
                        dependency_type: cm_rust::DependencyType::Strong,
                        rights: Some(fio::RW_STAR_DIR),
                        subdir: Some(PathBuf::from("component")),
                        availability: cm_rust::Availability::SameAsTarget,
                    }),
                    cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "temp".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "data".into(),
                        availability: cm_rust::Availability::SameAsTarget,
                    }),
                    cm_rust::OfferDecl::Service(cm_rust::OfferServiceDecl {
                        source: cm_rust::OfferSource::Parent,
                        source_name: "fuchsia.examples.Whale".into(),
                        target: cm_rust::OfferTarget::static_child("a".to_string()),
                        target_name: "fuchsia.examples.Orca".into(),
                        source_instance_filter: None,
                        renamed_instances: None,
                        availability: cm_rust::Availability::SameAsTarget,
                    }),
                ],
                ..cm_rust::ComponentDecl::default()
            },
            children: vec![],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
    }

    #[fuchsia::test]
    async fn same_as_target_route_to_local_component() {
        let realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_local_child("a", ftest::ChildOptions::EMPTY)
            .await
            .expect("failed to call add_local_child")
            .expect("add_local_child returned an error");
        let err = realm_and_builder_task
            .realm_proxy
            .add_route(
                &mut vec![ftest::Capability::Protocol(ftest::Protocol {
                    name: Some("fuchsia.examples.Hippo".to_owned()),
                    as_: Some("fuchsia.examples.Elephant".to_owned()),
                    type_: Some(fcdecl::DependencyType::Strong),
                    availability: Some(fcdecl::Availability::SameAsTarget),
                    ..ftest::Protocol::EMPTY
                })]
                .iter_mut(),
                &mut fcdecl::Ref::Parent(fcdecl::ParentRef {}),
                &mut vec![fcdecl::Ref::Child(fcdecl::ChildRef {
                    name: "a".to_owned(),
                    collection: None,
                })]
                .iter_mut(),
            )
            .await
            .expect("failed to call add_route")
            .expect_err("add_route should have returned an error");
        assert_eq!(err, ftest::RealmBuilderError::CapabilityInvalid);
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
                vec![ftest::Capability::Protocol(ftest::Protocol {
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
                vec![ftest::Capability::Protocol(ftest::Protocol {
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
                        availability: cm_rust::Availability::Required,
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
                        availability: cm_rust::Availability::Required,
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
                vec![ftest::Capability::Protocol(ftest::Protocol {
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
                vec![ftest::Capability::Protocol(ftest::Protocol {
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
                        availability: cm_rust::Availability::Required,
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
                        availability: cm_rust::Availability::Required,
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
                            availability: cm_rust::Availability::Required,
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
        assert_eq!(err, ftest::RealmBuilderError::NoSuchChild);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildDeclNotVisible);
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
            availability: cm_rust::Availability::Required,
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
                &mut vec![ftest::Capability::Event(ftest::Event {
                    name: Some("directory_ready".to_string()),
                    as_: None,
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
                    availability: cm_rust::Availability::Required,
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
        ftest::RealmBuilderError::NoSuchSource ; "no_such_source")]
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
        ftest::RealmBuilderError::NoSuchTarget ; "no_such_target")]
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
        ftest::RealmBuilderError::SourceAndTargetMatch ; "source_and_target_match")]
    #[test_case(vec![],
        fcdecl::Ref::Child(fcdecl::ChildRef {
            name: "a".to_owned(),
            collection: None
        }),
        vec![fcdecl::Ref::Parent(fcdecl::ParentRef {})],
        ftest::RealmBuilderError::CapabilitiesEmpty ; "capabilities_empty")]
    #[test_case(vec![ftest::Capability::unknown(100, vec![])],
        fcdecl::Ref::Child(fcdecl::ChildRef {
            name: "a".to_owned(),
            collection: None
        }),
        vec![fcdecl::Ref::Parent(fcdecl::ParentRef {})],
        ftest::RealmBuilderError::CapabilityInvalid ; "invalid_capability")]
    #[fuchsia::test]
    async fn add_route_error(
        mut capabilities: Vec<ftest::Capability>,
        mut from: fcdecl::Ref,
        mut to: Vec<fcdecl::Ref>,
        expected_err: ftest::RealmBuilderError,
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

    fn create_valid_capability() -> ftest::Capability {
        ftest::Capability::Protocol(ftest::Protocol {
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
        assert_eq!(err, ftest::RealmBuilderError::ImmutableProgram);
    }

    #[fuchsia::test]
    async fn replace_component_decl_immutable_program_not_raised_if_args_changed() {
        let mut realm_and_builder_task = RealmAndBuilderTask::new();
        realm_and_builder_task
            .realm_proxy
            .add_legacy_child("a", EXAMPLE_LEGACY_URL, ftest::ChildOptions::EMPTY)
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
        let program = a_decl.program.as_mut().expect("program section is None");
        let entries = program.info.entries.as_mut().expect("program info is None");
        let args_entry = fdata::DictionaryEntry {
            key: crate::ALLOWLISTED_PROGRAM_ARGS_KEY.to_string(),
            value: Some(Box::new(fdata::DictionaryValue::Str("test".to_string()))),
        };
        entries.push(args_entry.clone());
        realm_and_builder_task
            .realm_proxy
            .replace_component_decl("a", a_decl.clone().native_into_fidl())
            .await
            .expect("failed to call replace_component_decl")
            .expect("replace_component_decl returned an error");
        let tree_from_resolver = realm_and_builder_task.call_build_and_get_tree().await;
        let expected_a_decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![
                        fdata::DictionaryEntry {
                            key: runner::LEGACY_URL_KEY.to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str(
                                EXAMPLE_LEGACY_URL.to_string(),
                            ))),
                        },
                        args_entry,
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
                ComponentTree { decl: expected_a_decl, children: vec![] },
            )],
        };
        expected_tree.add_binder_expose();
        assert_eq!(expected_tree, tree_from_resolver);
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
        assert_eq!(err, ftest::RealmBuilderError::NoSuchChild);
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
        assert_eq!(err, ftest::RealmBuilderError::ChildDeclNotVisible);
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
        assert_eq!(err, ftest::RealmBuilderError::InvalidComponentDecl);
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
            fut: impl futures::Future<Output = Result<Result<V, ftest::RealmBuilderError>, fidl::Error>>,
        ) {
            assert_eq!(
                ftest::RealmBuilderError::BuildAlreadyCalled,
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
                    availability: cm_rust::Availability::Required,
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
