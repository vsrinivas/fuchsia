// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::capability::{CapabilityProvider, CapabilitySource, PERMITTED_FLAGS},
    crate::framework::realm::RealmCapabilityHost,
    crate::model::{
        component::{ComponentInstance, StartReason, WeakComponentInstance},
        error::ModelError,
        hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
        model::Model,
        storage::admin_protocol::StorageAdmin,
    },
    ::routing::error::ComponentInstanceError,
    async_trait::async_trait,
    cm_rust::CapabilityName,
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::endpoints::{DiscoverableProtocolMarker, ServerEnd},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::prelude::*,
    lazy_static::lazy_static,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker},
    routing::component_instance::ComponentInstanceInterface,
    std::convert::TryFrom,
    std::path::PathBuf,
    std::sync::{Arc, Weak},
    tracing::warn,
};

lazy_static! {
    pub static ref LIFECYCLE_CONTROLLER_CAPABILITY_NAME: CapabilityName =
        fsys::LifecycleControllerMarker::PROTOCOL_NAME.into();
}

#[derive(Clone)]
pub struct LifecycleController {
    model: Arc<Model>,
}

impl LifecycleController {
    pub fn new(model: Arc<Model>) -> Self {
        Self { model }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "LifecycleController",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn resolve(
        &self,
        scope_moniker: &AbsoluteMoniker,
        moniker: String,
    ) -> Result<Arc<ComponentInstance>, fcomponent::Error> {
        let moniker = join_monikers(scope_moniker, &moniker)?;
        self.model.look_up(&moniker).await.map_err(|e| match e {
            error @ ModelError::ResolverError { .. }
            | error @ ModelError::ComponentInstanceError {
                err: ComponentInstanceError::ResolveFailed { .. },
            } => {
                warn!(%moniker, ?error, "LifecycleController could not resolve");
                fcomponent::Error::InstanceCannotResolve
            }
            error @ ModelError::ComponentInstanceError {
                err: ComponentInstanceError::InstanceNotFound { .. },
            } => {
                warn!(%moniker, ?error, "LifecycleController could not find");
                fcomponent::Error::InstanceNotFound
            }
            error => {
                warn!(
                    %moniker, ?error,
                    "LifecycleController encountered unknown error",
                );
                fcomponent::Error::Internal
            }
        })
    }

    // If the component exists and is resolved, unresolve it.
    async fn unresolve(
        &self,
        scope_moniker: &AbsoluteMoniker,
        moniker: String,
    ) -> Result<(), fcomponent::Error> {
        let moniker = join_monikers(scope_moniker, &moniker)?;
        let component =
            self.model.find(&moniker).await.ok_or(fcomponent::Error::InstanceNotFound)?;
        component.unresolve().await.map_err(|error| {
            warn!(%moniker, ?error, "LifecycleController could not unresolve");
            fcomponent::Error::InstanceCannotUnresolve
        })
    }

    async fn start(
        &self,
        scope_moniker: &AbsoluteMoniker,
        moniker: String,
    ) -> Result<fsys::StartResult, fcomponent::Error> {
        let moniker = join_monikers(scope_moniker, &moniker)?;
        let component =
            self.model.find(&moniker).await.ok_or(fcomponent::Error::InstanceNotFound)?;
        let res = component.start(&StartReason::Debug).await.map_err(|e: ModelError| {
            warn!("LifecycleController could not start {}: {:?}", moniker, e);
            fcomponent::Error::InstanceCannotStart
        })?;
        Ok(res)
    }

    async fn stop(
        &self,
        scope_moniker: &AbsoluteMoniker,
        moniker: String,
        is_recursive: bool,
    ) -> Result<(), fcomponent::Error> {
        let moniker = join_monikers(scope_moniker, &moniker)?;
        let component =
            self.model.find(&moniker).await.ok_or(fcomponent::Error::InstanceNotFound)?;
        component.stop_instance(false, is_recursive).await.map_err(|error| {
            warn!(%moniker, ?error, "LifecycleController could not stop");
            fcomponent::Error::Internal
        })
    }

    async fn create_child(
        &self,
        scope_moniker: &AbsoluteMoniker,
        parent_moniker: String,
        collection: fdecl::CollectionRef,
        child_decl: fdecl::Child,
        child_args: fcomponent::CreateChildArgs,
    ) -> Result<(), fcomponent::Error> {
        let parent_moniker = join_monikers(scope_moniker, &parent_moniker)?;
        let parent_component =
            self.model.find(&parent_moniker).await.ok_or(fcomponent::Error::InstanceNotFound)?;
        let parent_component = WeakComponentInstance::new(&parent_component);
        RealmCapabilityHost::create_child(&parent_component, collection, child_decl, child_args)
            .await
    }

    async fn destroy_child(
        &self,
        scope_moniker: &AbsoluteMoniker,
        parent_moniker: String,
        child: fdecl::ChildRef,
    ) -> Result<(), fcomponent::Error> {
        let parent_moniker = join_monikers(scope_moniker, &parent_moniker)?;
        let parent_component =
            self.model.find(&parent_moniker).await.ok_or(fcomponent::Error::InstanceNotFound)?;
        let parent_component = WeakComponentInstance::new(&parent_component);
        RealmCapabilityHost::destroy_child(&parent_component, child).await
    }

    async fn open_storage_admin(
        &self,
        scope_moniker: &AbsoluteMoniker,
        moniker: String,
        capability: String,
        admin_server: ServerEnd<fsys::StorageAdminMarker>,
    ) -> Result<(), fcomponent::Error> {
        let moniker = join_monikers(scope_moniker, &moniker)?;
        let component =
            self.model.find(&moniker).await.ok_or(fcomponent::Error::InstanceNotFound)?;
        let storage_admin = StorageAdmin::new(Arc::downgrade(&self.model));
        let task_scope = component.task_scope();

        let storage_decl = {
            let locked_component = component.lock_resolved_state().await.map_err(|error| {
                warn!(%moniker, ?error, "LifecycleController could not get resolved state");
                fcomponent::Error::InstanceCannotResolve
            })?;

            locked_component
                .decl()
                .find_storage_source(&CapabilityName::from(capability.as_str()))
                .ok_or_else(|| {
                    warn!(%capability, provider=%moniker, "LifecycleController could not find the storage source");
                    fcomponent::Error::ResourceNotFound
                })?
                .clone()
        };

        task_scope
            .add_task(async move {
                if let Err(e) = Arc::new(storage_admin)
                    .serve(storage_decl, component.as_weak(), admin_server.into_channel().into())
                    .await
                {
                    warn!(
                        "LifecycleController failed to serve StorageAdmin for {}: {:?}",
                        moniker, e
                    );
                };
            })
            .await;
        Ok(())
    }

    pub async fn serve(
        &self,
        scope_moniker: AbsoluteMoniker,
        mut stream: fsys::LifecycleControllerRequestStream,
    ) {
        while let Ok(Some(operation)) = stream.try_next().await {
            match operation {
                fsys::LifecycleControllerRequest::Resolve { moniker, responder } => {
                    let mut res = self.resolve(&scope_moniker, moniker).await.map(|_| ());
                    responder.send(&mut res).unwrap_or_else(
                        |error| warn!(%error, "LifecycleController.Resolve failed to send"),
                    );
                }
                fsys::LifecycleControllerRequest::Unresolve { moniker, responder } => {
                    let mut res = self.unresolve(&scope_moniker, moniker).await;
                    responder.send(&mut res).unwrap_or_else(
                        |error| warn!(%error, "LifecycleController.Unresolve failed to send"),
                    );
                }
                fsys::LifecycleControllerRequest::Start { moniker, responder } => {
                    let mut res = self.start(&scope_moniker, moniker).await;
                    responder.send(&mut res).unwrap_or_else(
                        |error| warn!(%error, "LifecycleController.Start failed to send"),
                    );
                }
                fsys::LifecycleControllerRequest::Stop { moniker, is_recursive, responder } => {
                    let mut res = self.stop(&scope_moniker, moniker, is_recursive).await;
                    responder.send(&mut res).unwrap_or_else(
                        |error| warn!(%error, "LifecycleController.Stop failed to send"),
                    );
                }
                fsys::LifecycleControllerRequest::CreateChild {
                    parent_moniker,
                    collection,
                    decl,
                    args,
                    responder,
                } => {
                    let mut res = self
                        .create_child(&scope_moniker, parent_moniker, collection, decl, args)
                        .await;
                    responder.send(&mut res).unwrap_or_else(
                        |error| warn!(%error, "LifecycleController.CreateChild failed to send"),
                    );
                }
                fsys::LifecycleControllerRequest::DestroyChild {
                    parent_moniker,
                    child,
                    responder,
                } => {
                    let mut res = self.destroy_child(&scope_moniker, parent_moniker, child).await;
                    responder.send(&mut res).unwrap_or_else(
                        |error| warn!(%error, "LifecycleController.DestroyChild failed to send"),
                    );
                }
                fsys::LifecycleControllerRequest::GetStorageAdmin {
                    moniker,
                    capability,
                    admin_server,
                    responder,
                } => {
                    let mut res = self
                        .open_storage_admin(&scope_moniker, moniker, capability, admin_server)
                        .await;
                    responder.send(&mut res).unwrap_or_else(
                        |error| warn!(%error, "LifecycleController.GetStorageAdmin failed to send"),
                    );
                }
            }
        }
    }

    /// Given a `CapabilitySource`, determine if it is a framework-provided
    /// LifecycleController capability. If so, serve the capability.
    async fn on_capability_routed_async(
        self: Arc<Self>,
        source: CapabilitySource,
        capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
    ) -> Result<(), ModelError> {
        // If this is a scoped framework directory capability, then check the source path
        if let CapabilitySource::Framework { capability, component } = source {
            if capability.matches_protocol(&LIFECYCLE_CONTROLLER_CAPABILITY_NAME) {
                // Set the capability provider, if not already set.
                let mut capability_provider = capability_provider.lock().await;
                if capability_provider.is_none() {
                    *capability_provider =
                        Some(Box::new(LifecycleControllerCapabilityProvider::new(
                            self,
                            component.abs_moniker.clone(),
                        )));
                }
            }
        }
        Ok(())
    }
}

#[async_trait]
impl Hook for LifecycleController {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::CapabilityRouted { source, capability_provider }) => {
                self.on_capability_routed_async(source.clone(), capability_provider.clone())
                    .await?;
            }
            _ => {}
        }
        Ok(())
    }
}

pub struct LifecycleControllerCapabilityProvider {
    control: Arc<LifecycleController>,
    scope_moniker: AbsoluteMoniker,
}

impl LifecycleControllerCapabilityProvider {
    pub fn new(control: Arc<LifecycleController>, scope_moniker: AbsoluteMoniker) -> Self {
        Self { control, scope_moniker }
    }
}

#[async_trait]
impl CapabilityProvider for LifecycleControllerCapabilityProvider {
    async fn open(
        self: Box<Self>,
        task_scope: TaskScope,
        flags: fio::OpenFlags,
        _open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let forbidden = flags - PERMITTED_FLAGS;
        if !forbidden.is_empty() {
            warn!(?forbidden, "LifecycleController capability");
            return Ok(());
        }

        if relative_path.components().count() != 0 {
            warn!(
                path=%relative_path.display(),
                "LifecycleController capability got open request with non-empty",
            );
            return Ok(());
        }

        let server_end = channel::take_channel(server_end);

        let server_end = ServerEnd::<fsys::LifecycleControllerMarker>::new(server_end);
        let stream: fsys::LifecycleControllerRequestStream =
            server_end.into_stream().map_err(ModelError::stream_creation_error)?;
        task_scope
            .add_task(async move {
                self.control.serve(self.scope_moniker, stream).await;
            })
            .await;

        Ok(())
    }
}

/// Takes the scoped component's moniker and a relative moniker string and joins them into an
/// absolute moniker.
fn join_monikers(
    scope_moniker: &AbsoluteMoniker,
    moniker_str: &str,
) -> Result<AbsoluteMoniker, fcomponent::Error> {
    let relative_moniker =
        RelativeMoniker::try_from(moniker_str).map_err(|_| fcomponent::Error::InvalidArguments)?;
    let abs_moniker = scope_moniker.descendant(&relative_moniker);
    Ok(abs_moniker)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            actions::test_utils::{is_discovered, is_resolved},
            testing::test_helpers::TestEnvironmentBuilder,
        },
        cm_rust::{
            CapabilityPath, DirectoryDecl, ExposeDecl, ExposeDirectoryDecl, ExposeSource,
            ExposeTarget, StorageDecl, StorageDirectorySource,
        },
        cm_rust_testing::{CollectionDeclBuilder, ComponentDeclBuilder},
        fidl::endpoints::create_proxy,
        fidl::endpoints::create_proxy_and_stream,
        fidl::handle::Channel,
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
        fidl_fuchsia_component_decl::{ChildRef, CollectionRef},
        fidl_fuchsia_io::Operations,
        fidl_fuchsia_sys2::StorageAdminProxy,
        fuchsia_async as fasync,
    };

    #[fuchsia::test]
    async fn lifecycle_controller_test() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_child(cm_rust::ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fdecl::StartupMode::Eager,
                        environment: None,
                        on_terminate: None,
                    })
                    .add_child(cm_rust::ChildDecl {
                        name: "cant-resolve".to_string(),
                        url: "cant-resolve://cant-resolve".to_string(),
                        startup: fdecl::StartupMode::Eager,
                        environment: None,
                        on_terminate: None,
                    })
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(cm_rust::ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fdecl::StartupMode::Eager,
                        environment: None,
                        on_terminate: None,
                    })
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().build()),
        ];

        let test_model_result =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let lifecycle_controller = {
            let env = test_model_result.builtin_environment.lock().await;
            env.lifecycle_controller.clone().unwrap()
        };

        let (lifecycle_proxy, lifecycle_request_stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();

        // async move {} is used here because we want this to own the lifecycle_controller
        let _lifecycle_server_task = fasync::Task::local(async move {
            lifecycle_controller.serve(AbsoluteMoniker::root(), lifecycle_request_stream).await
        });

        assert_eq!(lifecycle_proxy.resolve(".").await.unwrap(), Ok(()));

        assert_eq!(lifecycle_proxy.resolve("./a").await.unwrap(), Ok(()));

        assert_eq!(
            lifecycle_proxy.resolve(".\\scope-escape-attempt").await.unwrap(),
            Err(fcomponent::Error::InvalidArguments)
        );

        assert_eq!(
            lifecycle_proxy.resolve("./doesnt-exist").await.unwrap(),
            Err(fcomponent::Error::InstanceNotFound)
        );

        assert_eq!(
            lifecycle_proxy.resolve("./cant-resolve").await.unwrap(),
            Err(fcomponent::Error::InstanceCannotResolve)
        );
    }

    #[fuchsia::test]
    async fn lifecycle_controller_unresolve_component_test() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_child(cm_rust::ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fdecl::StartupMode::Eager,
                        environment: None,
                        on_terminate: None,
                    })
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(cm_rust::ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fdecl::StartupMode::Eager,
                        environment: None,
                        on_terminate: None,
                    })
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().build()),
        ];

        let test_model_result =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let lifecycle_controller = {
            let env = test_model_result.builtin_environment.lock().await;
            env.lifecycle_controller.clone().unwrap()
        };

        let (lifecycle_proxy, lifecycle_request_stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();

        // async move {} is used here because we want this to own the lifecycle_controller
        let _lifecycle_server_task = fasync::Task::local(async move {
            lifecycle_controller.serve(AbsoluteMoniker::root(), lifecycle_request_stream).await
        });

        lifecycle_proxy.resolve(".").await.unwrap().unwrap();
        let component_a = test_model_result.model.look_up(&vec!["a"].into()).await.unwrap();
        let component_b = test_model_result.model.look_up(&vec!["a", "b"].into()).await.unwrap();
        assert!(is_resolved(&component_a).await);
        assert!(is_resolved(&component_b).await);

        lifecycle_proxy.unresolve(".").await.unwrap().unwrap();
        assert!(is_discovered(&component_a).await);
        assert!(is_discovered(&component_b).await);

        assert_eq!(
            lifecycle_proxy.unresolve("./nonesuch").await.unwrap(),
            Err(fcomponent::Error::InstanceNotFound)
        );

        // Unresolve again, which is ok because UnresolveAction is idempotent.
        assert_eq!(lifecycle_proxy.unresolve(".").await.unwrap(), Ok(()));
        assert!(is_discovered(&component_a).await);
        assert!(is_discovered(&component_b).await);
    }

    #[fuchsia::test]
    async fn lifecycle_already_started_test() {
        let components = vec![("root", ComponentDeclBuilder::new().build())];

        let test_model_result =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let lifecycle_controller = {
            let env = test_model_result.builtin_environment.lock().await;
            env.lifecycle_controller.clone().unwrap()
        };

        let (lifecycle_proxy, lifecycle_request_stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();

        // async move {} is used here because we want this to own the lifecycle_controller
        let _lifecycle_server_task = fasync::Task::local(async move {
            lifecycle_controller.serve(AbsoluteMoniker::root(), lifecycle_request_stream).await
        });

        assert_eq!(lifecycle_proxy.start(".").await.unwrap(), Ok(fsys::StartResult::Started));

        assert_eq!(
            lifecycle_proxy.start(".").await.unwrap(),
            Ok(fsys::StartResult::AlreadyStarted)
        );

        assert_eq!(lifecycle_proxy.stop(".", false).await.unwrap(), Ok(()));

        assert_eq!(lifecycle_proxy.start(".").await.unwrap(), Ok(fsys::StartResult::Started));
    }

    #[fuchsia::test]
    async fn lifecycle_create_and_destroy_test() {
        let collection = CollectionDeclBuilder::new_transient_collection("coll").build();
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_collection(collection)
                    .add_lazy_child("child")
                    .build(),
            ),
            ("child", ComponentDeclBuilder::new().build()),
        ];

        let test_model_result =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let lifecycle_controller = {
            let env = test_model_result.builtin_environment.lock().await;
            env.lifecycle_controller.clone().unwrap()
        };

        let (lifecycle_proxy, lifecycle_request_stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();

        // async move {} is used here because we want this to own the lifecycle_controller
        let _lifecycle_server_task = fasync::Task::local(async move {
            lifecycle_controller.serve(AbsoluteMoniker::root(), lifecycle_request_stream).await
        });

        assert_eq!(
            lifecycle_proxy
                .create_child(
                    "./",
                    &mut CollectionRef { name: "coll".to_string() },
                    fdecl::Child {
                        name: Some("child".to_string()),
                        url: Some("test:///child".to_string()),
                        startup: Some(fdecl::StartupMode::Lazy),
                        environment: None,
                        on_terminate: None,
                        ..fdecl::Child::EMPTY
                    },
                    fcomponent::CreateChildArgs::EMPTY,
                )
                .await
                .unwrap(),
            Ok(())
        );

        assert_eq!(lifecycle_proxy.resolve("./coll:child").await.unwrap(), Ok(()));

        assert_eq!(
            lifecycle_proxy
                .destroy_child(
                    "./",
                    &mut ChildRef {
                        name: "child".to_string(),
                        collection: Some("coll".to_string()),
                    }
                )
                .await
                .unwrap(),
            Ok(())
        );

        assert_eq!(
            lifecycle_proxy.resolve("./coll:child").await.unwrap(),
            Err(fcomponent::Error::InstanceNotFound)
        );
    }

    #[fuchsia::test]
    async fn lifecycle_get_storage_admin_test() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .storage(StorageDecl {
                        name: CapabilityName("data".to_string()),
                        source: StorageDirectorySource::Child("a".to_string()),
                        backing_dir: CapabilityName("fs".to_string()),
                        subdir: Some("persistent".into()),
                        storage_id:
                            fidl_fuchsia_component_decl::StorageId::StaticInstanceIdOrMoniker,
                    })
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDecl {
                        name: CapabilityName("fs".to_string()),
                        source_path: Some(CapabilityPath {
                            basename: "data".to_string(),
                            dirname: "/fs".to_string(),
                        }),
                        rights: Operations::all(),
                    })
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: CapabilityName("fs".to_string()),
                        target_name: CapabilityName("fs".to_string()),
                        subdir: None,
                        source: ExposeSource::Self_,
                        target: ExposeTarget::Parent,
                        rights: None,
                    }))
                    .build(),
            ),
        ];

        let test_model_result =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let lifecycle_controller = {
            let env = test_model_result.builtin_environment.lock().await;
            env.lifecycle_controller.clone().unwrap()
        };

        let (lifecycle_proxy, lifecycle_request_stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();

        // async move {} is used here because we want this to own the lifecycle_controller
        let _lifecycle_server_task = fasync::Task::local(async move {
            lifecycle_controller.serve(AbsoluteMoniker::root(), lifecycle_request_stream).await
        });

        let (client, server) = Channel::create().unwrap();

        let server_end = ServerEnd::new(server);

        let res = lifecycle_proxy.get_storage_admin("./", "data", server_end).await.unwrap();

        assert_eq!(res, Ok(()));

        let (it_proxy, it_server) =
            create_proxy::<fsys::StorageIteratorMarker>().expect("create iterator");

        let storage_admin =
            StorageAdminProxy::new(fidl::AsyncChannel::from_channel(client).unwrap());

        storage_admin.list_storage_in_realm("./", it_server).await.unwrap().unwrap();

        let res = it_proxy.next().await.unwrap();
        assert!(res.is_empty());
    }
}
