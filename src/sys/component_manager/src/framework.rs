// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.cti

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        model::{
            component::{ComponentInstance, StartReason, WeakComponentInstance},
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
        },
    },
    ::routing::{
        capability_source::InternalCapability, config::RuntimeConfig, error::ComponentInstanceError,
    },
    anyhow::Error,
    async_trait::async_trait,
    cm_fidl_validator,
    cm_rust::{CapabilityName, FidlIntoNative},
    cm_task_scope::TaskScope,
    cm_util::channel,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    moniker::{AbsoluteMoniker, ChildMoniker, ChildMonikerBase},
    std::{
        cmp,
        path::PathBuf,
        sync::{Arc, Weak},
    },
    tracing::{debug, error, warn},
};

lazy_static! {
    // Path for SDK clients.
    pub static ref SDK_REALM_SERVICE: CapabilityName = "fuchsia.component.Realm".into();
}

// The `fuchsia.sys2.Realm` is currently being migrated to the `fuchsia.component`
// namespace. Until all clients of this protocol have been moved to the latter
// namespace, the following CapabilityProvider will support both paths.
// Tracking bug: https://fxbug.dev/85183.
pub struct RealmCapabilityProvider {
    scope_moniker: AbsoluteMoniker,
    host: Arc<RealmCapabilityHost>,
}

impl RealmCapabilityProvider {
    pub fn new(scope_moniker: AbsoluteMoniker, host: Arc<RealmCapabilityHost>) -> Self {
        Self { scope_moniker, host }
    }
}

#[async_trait]
impl CapabilityProvider for RealmCapabilityProvider {
    async fn open(
        self: Box<Self>,
        task_scope: TaskScope,
        _flags: fio::OpenFlags,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let server_end = channel::take_channel(server_end);
        let host = self.host.clone();
        // We only need to look up the component matching this scope.
        // These operations should all work, even if the component is not running.
        let model = host.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
        let component = WeakComponentInstance::from(&model.look_up(&self.scope_moniker).await?);
        task_scope
            .add_task(async move {
                let serve_result = host
                    .serve(
                        component,
                        ServerEnd::<fcomponent::RealmMarker>::new(server_end)
                            .into_stream()
                            .expect("could not convert channel into stream"),
                    )
                    .await;
                if let Err(error) = serve_result {
                    // TODO: Set an epitaph to indicate this was an unexpected error.
                    warn!(%error, "serve failed");
                }
            })
            .await;
        Ok(())
    }
}

#[derive(Clone)]
pub struct RealmCapabilityHost {
    model: Weak<Model>,
    config: Arc<RuntimeConfig>,
}

// `RealmCapabilityHost` is a `Hook` that serves the `Realm` FIDL protocol.
impl RealmCapabilityHost {
    pub fn new(model: Weak<Model>, config: Arc<RuntimeConfig>) -> Self {
        Self { model, config }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "RealmCapabilityHost",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    pub async fn serve(
        &self,
        component: WeakComponentInstance,
        stream: fcomponent::RealmRequestStream,
    ) -> Result<(), fidl::Error> {
        stream
            .try_for_each(|request| async {
                let method_name = request.method_name();
                let res = self.handle_request(request, &component).await;
                if let Err(error) = &res {
                    warn!(%method_name, %error, "Couldn't send Realm response");
                }
                res
            })
            .await
    }

    async fn handle_request(
        &self,
        request: fcomponent::RealmRequest,
        component: &WeakComponentInstance,
    ) -> Result<(), fidl::Error> {
        match request {
            fcomponent::RealmRequest::CreateChild { responder, collection, decl, args } => {
                let mut res =
                    async { Self::create_child(component, collection, decl, args).await }.await;
                responder.send(&mut res)?;
            }
            fcomponent::RealmRequest::DestroyChild { responder, child } => {
                let mut res = Self::destroy_child(component, child).await;
                responder.send(&mut res)?;
            }
            fcomponent::RealmRequest::ListChildren { responder, collection, iter } => {
                let mut res = Self::list_children(
                    component,
                    self.config.list_children_batch_size,
                    collection,
                    iter,
                )
                .await;
                responder.send(&mut res)?;
            }
            fcomponent::RealmRequest::OpenExposedDir { responder, child, exposed_dir } => {
                let mut res = Self::open_exposed_dir(component, child, exposed_dir).await;
                responder.send(&mut res)?;
            }
        }
        Ok(())
    }

    pub async fn create_child(
        component: &WeakComponentInstance,
        collection: fdecl::CollectionRef,
        child_decl: fdecl::Child,
        child_args: fcomponent::CreateChildArgs,
    ) -> Result<(), fcomponent::Error> {
        let component = component.upgrade().map_err(|_| fcomponent::Error::InstanceDied)?;
        cm_fidl_validator::validate_dynamic_child(&child_decl).map_err(|error| {
            debug!(%error, "validate_dynamic_child() failed");
            fcomponent::Error::InvalidArguments
        })?;
        if child_decl.environment.is_some() {
            return Err(fcomponent::Error::InvalidArguments);
        }
        let child_decl = child_decl.fidl_into_native();
        match component.add_dynamic_child(collection.name.clone(), &child_decl, child_args).await {
            Ok(fdecl::Durability::SingleRun) => {
                // Creating a child in a `SingleRun` collection automatically starts it, so
                // start the component.
                let child_ref =
                    fdecl::ChildRef { name: child_decl.name, collection: Some(collection.name) };
                let weak_component = WeakComponentInstance::new(&component);
                RealmCapabilityHost::start_child(&weak_component, child_ref, StartReason::SingleRun)
                    .await
            }
            Ok(_) => Ok(()),
            Err(e) => {
                warn!(
                    "Failed to create child \"{}\" in collection \"{}\" of component \"{}\": {}",
                    child_decl.name, collection.name, component.abs_moniker, e
                );
                match e {
                    ModelError::InstanceAlreadyExists { .. } => {
                        Err(fcomponent::Error::InstanceAlreadyExists)
                    }
                    ModelError::CollectionNotFound { .. } => {
                        Err(fcomponent::Error::CollectionNotFound)
                    }
                    ModelError::DynamicOffersNotAllowed { .. }
                    | ModelError::DynamicOfferInvalid { .. }
                    | ModelError::DynamicOfferSourceNotFound { .. }
                    | ModelError::NameTooLong { .. } => Err(fcomponent::Error::InvalidArguments),
                    ModelError::Unsupported { .. } => Err(fcomponent::Error::Unsupported),
                    _ => Err(fcomponent::Error::Internal),
                }
            }
        }
    }

    async fn start_child(
        component: &WeakComponentInstance,
        child: fdecl::ChildRef,
        start_reason: StartReason,
    ) -> Result<(), fcomponent::Error> {
        match Self::get_child(component, child.clone()).await? {
            Some(child) => {
                child.start(&start_reason).await.map_err(|e| match e {
                    ModelError::ResolverError { err: error, .. } => {
                        debug!(%error, "failed to resolve child");
                        fcomponent::Error::InstanceCannotResolve
                    }
                    ModelError::RunnerError { err: error } => {
                        debug!(%error, "failed to start child");
                        fcomponent::Error::InstanceCannotStart
                    }
                    error => {
                        error!(%error, "start() failed");
                        fcomponent::Error::Internal
                    }
                })?;
            }
            None => {
                debug!(?child, "start_child() failed: instance not found");
                return Err(fcomponent::Error::InstanceNotFound);
            }
        }
        Ok(())
    }

    async fn open_exposed_dir(
        component: &WeakComponentInstance,
        child: fdecl::ChildRef,
        exposed_dir: ServerEnd<fio::DirectoryMarker>,
    ) -> Result<(), fcomponent::Error> {
        match Self::get_child(component, child.clone()).await? {
            Some(child) => {
                // Resolve child in order to instantiate exposed_dir.
                child.resolve().await.map_err(|e| {
                    warn!(
                        "resolve failed for child {:?} of component {}: {}",
                        child, component.abs_moniker, e
                    );
                    return fcomponent::Error::InstanceCannotResolve;
                })?;
                let mut exposed_dir = exposed_dir.into_channel();
                let () =
                    child.open_exposed(&mut exposed_dir).await.map_err(|error| match error {
                        ModelError::InstanceShutDown { .. } => fcomponent::Error::InstanceDied,
                        _ => {
                            debug!(%error, "open_exposed() failed");
                            fcomponent::Error::Internal
                        }
                    })?;
            }
            None => {
                debug!(?child, "open_exposed_dir() failed: instance not found");
                return Err(fcomponent::Error::InstanceNotFound);
            }
        }
        Ok(())
    }

    pub async fn destroy_child(
        component: &WeakComponentInstance,
        child: fdecl::ChildRef,
    ) -> Result<(), fcomponent::Error> {
        let component = component.upgrade().map_err(|_| fcomponent::Error::InstanceDied)?;
        child.collection.as_ref().ok_or(fcomponent::Error::InvalidArguments)?;
        let child_moniker = ChildMoniker::try_new(&child.name, child.collection.as_ref())
            .map_err(|_| fcomponent::Error::InvalidArguments)?;
        component.remove_dynamic_child(&child_moniker).await.map_err(|e| match e {
            ModelError::InstanceNotFoundInRealm { .. } => fcomponent::Error::InstanceNotFound,
            ModelError::Unsupported { .. } => fcomponent::Error::Unsupported,
            error => {
                error!(%error, "remove_dynamic_child() failed");
                fcomponent::Error::Internal
            }
        })?;
        Ok(())
    }

    async fn on_scoped_framework_capability_routed_async<'a>(
        self: Arc<Self>,
        scope_moniker: AbsoluteMoniker,
        capability: &'a InternalCapability,
        capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        // If some other capability has already been installed, then there's nothing to
        // do here.
        if capability_provider.is_none() && capability.matches_protocol(&SDK_REALM_SERVICE) {
            return Ok(Some(Box::new(RealmCapabilityProvider::new(scope_moniker, self.clone()))
                as Box<dyn CapabilityProvider>));
        }

        Ok(capability_provider)
    }

    async fn get_child(
        parent: &WeakComponentInstance,
        child: fdecl::ChildRef,
    ) -> Result<Option<Arc<ComponentInstance>>, fcomponent::Error> {
        let parent = parent.upgrade().map_err(|_| fcomponent::Error::InstanceDied)?;
        let state = parent.lock_resolved_state().await.map_err(|e| match e {
            ComponentInstanceError::ResolveFailed { moniker, err: error, .. } => {
                debug!(%moniker, %error, "failed to resolve instance");
                return fcomponent::Error::InstanceCannotResolve;
            }
            error => {
                error!(%error, "failed to resolve InstanceState");
                return fcomponent::Error::Internal;
            }
        })?;
        let child_moniker = ChildMoniker::try_new(&child.name, child.collection.as_ref())
            .map_err(|_| fcomponent::Error::InvalidArguments)?;
        Ok(state.get_child(&child_moniker).map(|r| r.clone()))
    }

    async fn list_children(
        component: &WeakComponentInstance,
        batch_size: usize,
        collection: fdecl::CollectionRef,
        iter: ServerEnd<fcomponent::ChildIteratorMarker>,
    ) -> Result<(), fcomponent::Error> {
        let component = component.upgrade().map_err(|_| fcomponent::Error::InstanceDied)?;
        let state = component.lock_resolved_state().await.map_err(|error| {
            error!(%error, "failed to resolve InstanceState");
            fcomponent::Error::Internal
        })?;
        let decl = state.decl();
        decl.find_collection(&collection.name).ok_or(fcomponent::Error::CollectionNotFound)?;
        let mut children: Vec<_> = state
            .children()
            .filter_map(|(m, _)| match m.collection() {
                Some(c) => {
                    if c == collection.name {
                        Some(fdecl::ChildRef {
                            name: m.name().to_string(),
                            collection: m.collection().map(|s| s.to_string()),
                        })
                    } else {
                        None
                    }
                }
                _ => None,
            })
            .collect();
        children.sort_unstable_by(|a, b| {
            let a = &a.name;
            let b = &b.name;
            if a == b {
                cmp::Ordering::Equal
            } else if a < b {
                cmp::Ordering::Less
            } else {
                cmp::Ordering::Greater
            }
        });
        let stream = iter.into_stream().map_err(|_| fcomponent::Error::AccessDenied)?;
        fasync::Task::spawn(async move {
            if let Err(error) = Self::serve_child_iterator(children, stream, batch_size).await {
                // TODO: Set an epitaph to indicate this was an unexpected error.
                warn!(%error, "serve_child_iterator failed");
            }
        })
        .detach();
        Ok(())
    }

    async fn serve_child_iterator(
        mut children: Vec<fdecl::ChildRef>,
        mut stream: fcomponent::ChildIteratorRequestStream,
        batch_size: usize,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                fcomponent::ChildIteratorRequest::Next { responder } => {
                    let n_to_send = std::cmp::min(children.len(), batch_size);
                    let mut res: Vec<_> = children.drain(..n_to_send).collect();
                    responder.send(&mut res.iter_mut())?;
                }
            }
        }
        Ok(())
    }
}

#[async_trait]
impl Hook for RealmCapabilityHost {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source: CapabilitySource::Framework { capability, component },
            capability_provider,
        }) = &event.result
        {
            let mut capability_provider = capability_provider.lock().await;
            *capability_provider = self
                .on_scoped_framework_capability_routed_async(
                    component.abs_moniker.clone(),
                    &capability,
                    capability_provider.take(),
                )
                .await?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            builtin_environment::BuiltinEnvironment,
            model::{
                component::{ComponentInstance, StartReason},
                events::{source::EventSource, stream::EventStream},
                starter::Starter,
                testing::{mocks::*, out_dir::OutDir, test_helpers::*, test_hook::*},
            },
        },
        assert_matches::assert_matches,
        cm_rust::{
            self, CapabilityName, CapabilityPath, ComponentDecl, EventMode, ExposeDecl,
            ExposeProtocolDecl, ExposeSource, ExposeTarget,
        },
        cm_rust_testing::*,
        fidl::endpoints::{self, Proxy},
        fidl_fidl_examples_routing_echo as echo, fidl_fuchsia_component as fcomponent,
        fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_component::client,
        fuchsia_fs::OpenFlags,
        futures::{lock::Mutex, poll, task::Poll},
        moniker::AbsoluteMoniker,
        routing_test_helpers::component_decl_with_exposed_binder,
        std::collections::HashSet,
        std::convert::TryFrom,
        std::path::PathBuf,
    };

    struct RealmCapabilityTest {
        builtin_environment: Option<Arc<Mutex<BuiltinEnvironment>>>,
        mock_runner: Arc<MockRunner>,
        component: Option<Arc<ComponentInstance>>,
        realm_proxy: fcomponent::RealmProxy,
        hook: Arc<TestHook>,
    }

    impl RealmCapabilityTest {
        async fn new(
            components: Vec<(&'static str, ComponentDecl)>,
            component_moniker: AbsoluteMoniker,
        ) -> Self {
            // Init model.
            let config = RuntimeConfig { list_children_batch_size: 2, ..Default::default() };
            let TestModelResult { model, builtin_environment, mock_runner, .. } =
                TestEnvironmentBuilder::new()
                    .set_components(components)
                    .set_runtime_config(config)
                    .build()
                    .await;

            let hook = Arc::new(TestHook::new());
            let hooks = hook.hooks();
            // Install TestHook at the front so that when we receive an event the hook has already
            // run so the result is reflected in its printout
            model.root().hooks.install_front(hooks).await;

            // Look up and start component.
            let component = model
                .start_instance(&component_moniker, &StartReason::Eager)
                .await
                .expect("failed to start component");

            // Host framework service.
            let (realm_proxy, stream) =
                endpoints::create_proxy_and_stream::<fcomponent::RealmMarker>().unwrap();
            {
                let component = WeakComponentInstance::from(&component);
                let realm_capability_host =
                    builtin_environment.lock().await.realm_capability_host.clone();
                fasync::Task::spawn(async move {
                    realm_capability_host
                        .serve(component, stream)
                        .await
                        .expect("failed serving realm service");
                })
                .detach();
            }
            Self {
                builtin_environment: Some(builtin_environment),
                mock_runner,
                component: Some(component),
                realm_proxy,
                hook,
            }
        }

        fn component(&self) -> &Arc<ComponentInstance> {
            self.component.as_ref().unwrap()
        }

        fn drop_component(&mut self) {
            self.component = None;
            self.builtin_environment = None;
        }

        async fn new_event_stream(
            &self,
            events: Vec<CapabilityName>,
            mode: EventMode,
        ) -> (EventSource, EventStream) {
            new_event_stream(
                self.builtin_environment.as_ref().expect("builtin_environment is none").clone(),
                events,
                mode,
            )
            .await
        }
    }

    fn child_decl(name: &str) -> fdecl::Child {
        fdecl::Child {
            name: Some(name.to_owned()),
            url: Some(format!("test:///{}", name)),
            startup: Some(fdecl::StartupMode::Lazy),
            environment: None,
            on_terminate: None,
            ..fdecl::Child::EMPTY
        }
    }

    #[fuchsia::test]
    async fn create_dynamic_child() {
        // Set up model and realm service.
        let test = RealmCapabilityTest::new(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                (
                    "system",
                    ComponentDeclBuilder::new()
                        .add_collection(
                            CollectionDeclBuilder::new().name("coll").allow_long_names(true),
                        )
                        .build(),
                ),
            ],
            vec!["system"].into(),
        )
        .await;

        let (_event_source, mut event_stream) =
            test.new_event_stream(vec![EventType::Discovered.into()], EventMode::Sync).await;

        // Test that a dynamic child with a long name can also be created.
        let long_name = &"c".repeat(cm_types::MAX_LONG_NAME_LENGTH);

        // Create children "a", "b", and "<long_name>" in collection. Expect a Discovered event for each.
        let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
        for (name, moniker) in
            [("a", "coll:a"), ("b", "coll:b"), (long_name, &format!("coll:{}", long_name))]
        {
            let mut create = fasync::Task::spawn(test.realm_proxy.create_child(
                &mut collection_ref,
                child_decl(name),
                fcomponent::CreateChildArgs::EMPTY,
            ));
            let event = event_stream
                .wait_until(EventType::Discovered, vec!["system", moniker].into())
                .await
                .unwrap();

            // Give create requests time to be processed. Ensure they don't return before
            // Discover action completes.
            fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(5))).await;
            assert_matches!(poll!(&mut create), Poll::Pending);

            // Unblock Discovered and wait for request to complete.
            event.resume();
            create.await.unwrap().unwrap();
        }

        // Verify that the component topology matches expectations.
        let actual_children = get_live_children(test.component()).await;
        let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
        expected_children.insert("coll:a".into());
        expected_children.insert("coll:b".into());
        expected_children.insert(format!("coll:{}", long_name).as_str().into());
        assert_eq!(actual_children, expected_children);
        assert_eq!(format!("(system(coll:a,coll:b,coll:{}))", long_name), test.hook.print());
    }

    #[fuchsia::test]
    async fn create_dynamic_child_errors() {
        let mut test = RealmCapabilityTest::new(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                (
                    "system",
                    ComponentDeclBuilder::new()
                        .add_transient_collection("coll")
                        .add_collection(
                            CollectionDeclBuilder::new()
                                .name("pcoll")
                                .durability(fdecl::Durability::Transient)
                                .allow_long_names(true)
                                .build(),
                        )
                        .add_collection(
                            CollectionDeclBuilder::new()
                                .name("dynoff")
                                .allowed_offers(cm_types::AllowedOffers::StaticAndDynamic)
                                .build(),
                        )
                        .build(),
                ),
            ],
            vec!["system"].into(),
        )
        .await;

        // Invalid arguments.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let child_decl = fdecl::Child {
                name: Some("a".to_string()),
                url: None,
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }
        {
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let child_decl = fdecl::Child {
                name: Some("a".to_string()),
                url: Some("test:///a".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: Some("env".to_string()),
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }
        // Long dynamic child name violations.
        {
            // `allow_long_names` is not set
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let child_decl = fdecl::Child {
                name: Some("a".repeat(cm_types::MAX_NAME_LENGTH + 1).to_string()),
                url: None,
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);

            // Name length exceeds the MAX_LONG_NAME_LENGTH when `allow_long_names` is set.
            let mut collection_ref = fdecl::CollectionRef { name: "pcoll".to_string() };
            let child_decl = fdecl::Child {
                name: Some("a".repeat(cm_types::MAX_LONG_NAME_LENGTH + 1).to_string()),
                url: None,
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }

        // Instance already exists.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let res = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl("a"),
                    fcomponent::CreateChildArgs::EMPTY,
                )
                .await;
            res.expect("fidl call failed").expect("failed to create child a");
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let err = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl("a"),
                    fcomponent::CreateChildArgs::EMPTY,
                )
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InstanceAlreadyExists);
        }

        // Collection not found.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "nonexistent".to_string() };
            let err = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl("a"),
                    fcomponent::CreateChildArgs::EMPTY,
                )
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::CollectionNotFound);
        }

        // Unsupported.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let child_decl = fdecl::Child {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fdecl::StartupMode::Eager),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::Unsupported);
        }

        fn sample_offer_from(source: fdecl::Ref) -> fdecl::Offer {
            fdecl::Offer::Protocol(fdecl::OfferProtocol {
                source: Some(source),
                source_name: Some("foo".to_string()),
                target_name: Some("foo".to_string()),
                dependency_type: Some(fdecl::DependencyType::Strong),
                ..fdecl::OfferProtocol::EMPTY
            })
        }

        // Disallowed dynamic offers specified.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let child_decl = fdecl::Child {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl,
                    fcomponent::CreateChildArgs {
                        dynamic_offers: Some(vec![sample_offer_from(fdecl::Ref::Parent(
                            fdecl::ParentRef {},
                        ))]),
                        ..fcomponent::CreateChildArgs::EMPTY
                    },
                )
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }

        // Malformed dynamic offers specified.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "dynoff".to_string() };
            let child_decl = fdecl::Child {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl,
                    fcomponent::CreateChildArgs {
                        dynamic_offers: Some(vec![fdecl::Offer::Protocol(fdecl::OfferProtocol {
                            source: Some(fdecl::Ref::Parent(fdecl::ParentRef {})),
                            source_name: Some("foo".to_string()),
                            target_name: Some("foo".to_string()),
                            // Note: has no `dependency_type`.
                            ..fdecl::OfferProtocol::EMPTY
                        })]),
                        ..fcomponent::CreateChildArgs::EMPTY
                    },
                )
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }

        // Dynamic offer source is a static component that doesn't exist.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "dynoff".to_string() };
            let child_decl = fdecl::Child {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl,
                    fcomponent::CreateChildArgs {
                        dynamic_offers: Some(vec![sample_offer_from(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "does_not_exist".to_string(),
                                collection: None,
                            },
                        ))]),
                        ..fcomponent::CreateChildArgs::EMPTY
                    },
                )
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }

        // Source is a collection that doesn't exist (and using a Service).
        {
            let mut collection_ref = fdecl::CollectionRef { name: "dynoff".to_string() };
            let child_decl = fdecl::Child {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl,
                    fcomponent::CreateChildArgs {
                        dynamic_offers: Some(vec![fdecl::Offer::Service(fdecl::OfferService {
                            source: Some(fdecl::Ref::Collection(fdecl::CollectionRef {
                                name: "does_not_exist".to_string(),
                            })),
                            source_name: Some("foo".to_string()),
                            target_name: Some("foo".to_string()),
                            ..fdecl::OfferService::EMPTY
                        })]),
                        ..fcomponent::CreateChildArgs::EMPTY
                    },
                )
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }

        // Source is a component in the same collection that doesn't exist.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "dynoff".to_string() };
            let child_decl = fdecl::Child {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl,
                    fcomponent::CreateChildArgs {
                        dynamic_offers: Some(vec![sample_offer_from(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "does_not_exist".to_string(),
                                collection: Some("dynoff".to_string()),
                            },
                        ))]),
                        ..fcomponent::CreateChildArgs::EMPTY
                    },
                )
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }

        // Source is the component itself... which doesn't exist... yet.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "dynoff".to_string() };
            let child_decl = fdecl::Child {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl,
                    fcomponent::CreateChildArgs {
                        dynamic_offers: Some(vec![sample_offer_from(fdecl::Ref::Child(
                            fdecl::ChildRef {
                                name: "b".to_string(),
                                collection: Some("dynoff".to_string()),
                            },
                        ))]),
                        ..fcomponent::CreateChildArgs::EMPTY
                    },
                )
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }

        // Instance died.
        {
            test.drop_component();
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let child_decl = fdecl::Child {
                name: Some("b".to_string()),
                url: Some("test:///b".to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            };
            let err = test
                .realm_proxy
                .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InstanceDied);
        }
    }

    #[fuchsia::test]
    async fn destroy_dynamic_child() {
        // Set up model and realm service.
        let test = RealmCapabilityTest::new(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                ("system", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
                ("a", component_decl_with_exposed_binder()),
                ("b", component_decl_with_exposed_binder()),
            ],
            vec!["system"].into(),
        )
        .await;

        let (_event_source, mut event_stream) = test
            .new_event_stream(
                vec![EventType::Stopped.into(), EventType::Destroyed.into()],
                EventMode::Sync,
            )
            .await;

        // Create children "a" and "b" in collection, and start them.
        for name in &["a", "b"] {
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let res = test
                .realm_proxy
                .create_child(
                    &mut collection_ref,
                    child_decl(name),
                    fcomponent::CreateChildArgs::EMPTY,
                )
                .await;
            res.expect("fidl call failed")
                .unwrap_or_else(|_| panic!("failed to create child {}", name));
            let mut child_ref =
                fdecl::ChildRef { name: name.to_string(), collection: Some("coll".to_string()) };
            let (exposed_dir, server_end) =
                endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            let () = test
                .realm_proxy
                .open_exposed_dir(&mut child_ref, server_end)
                .await
                .expect("OpenExposedDir FIDL")
                .expect("OpenExposedDir Error");
            let _: fcomponent::BinderProxy =
                client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir)
                    .expect("Connection to fuchsia.component.Binder");
        }

        let child = get_live_child(test.component(), "coll:a").await;
        let instance_id = get_incarnation_id(test.component(), "coll:a").await;
        assert_eq!("(system(coll:a,coll:b))", test.hook.print());
        assert_eq!(child.component_url, "test:///a".to_string());
        assert_eq!(instance_id, 1);

        // Destroy "a". "a" is no longer live from the client's perspective, although it's still
        // being destroyed.
        let mut child_ref =
            fdecl::ChildRef { name: "a".to_string(), collection: Some("coll".to_string()) };
        let (f, destroy_handle) = test.realm_proxy.destroy_child(&mut child_ref).remote_handle();
        fasync::Task::spawn(f).detach();

        // The component should be stopped (shut down) before it is destroyed.
        let event = event_stream
            .wait_until(EventType::Stopped, vec!["system", "coll:a"].into())
            .await
            .unwrap();
        event.resume();
        let event = event_stream
            .wait_until(EventType::Destroyed, vec!["system", "coll:a"].into())
            .await
            .unwrap();
        event.resume();

        // "a" is fully deleted now.
        assert!(!has_child(test.component(), "coll:a").await);
        {
            let actual_children = get_live_children(test.component()).await;
            let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
            expected_children.insert("coll:b".into());
            let child_b = get_live_child(test.component(), "coll:b").await;
            assert!(!execution_is_shut_down(&child_b).await);
            assert_eq!(actual_children, expected_children);
            assert_eq!("(system(coll:b))", test.hook.print());
        }

        let res = destroy_handle.await;
        res.expect("fidl call failed").expect("failed to destroy child a");

        // Recreate "a" and verify "a" is back (but it's a different "a"). The old "a" is gone
        // from the client's point of view, but it hasn't been cleaned up yet.
        let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
        let child_decl = fdecl::Child {
            name: Some("a".to_string()),
            url: Some("test:///a_alt".to_string()),
            startup: Some(fdecl::StartupMode::Lazy),
            environment: None,
            ..fdecl::Child::EMPTY
        };
        let res = test
            .realm_proxy
            .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
            .await;
        res.expect("fidl call failed").expect("failed to recreate child a");

        assert_eq!("(system(coll:a,coll:b))", test.hook.print());
        let child = get_live_child(test.component(), "coll:a").await;
        let instance_id = get_incarnation_id(test.component(), "coll:a").await;
        assert_eq!(child.component_url, "test:///a_alt".to_string());
        assert_eq!(instance_id, 3);
    }

    #[fuchsia::test]
    async fn destroy_dynamic_child_errors() {
        let mut test = RealmCapabilityTest::new(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                ("system", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ],
            vec!["system"].into(),
        )
        .await;

        // Create child "a" in collection.
        let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
        let res = test
            .realm_proxy
            .create_child(&mut collection_ref, child_decl("a"), fcomponent::CreateChildArgs::EMPTY)
            .await;
        res.expect("fidl call failed").expect("failed to create child a");

        // Invalid arguments.
        {
            let mut child_ref = fdecl::ChildRef { name: "a".to_string(), collection: None };
            let err = test
                .realm_proxy
                .destroy_child(&mut child_ref)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InvalidArguments);
        }

        // Instance not found.
        {
            let mut child_ref =
                fdecl::ChildRef { name: "b".to_string(), collection: Some("coll".to_string()) };
            let err = test
                .realm_proxy
                .destroy_child(&mut child_ref)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InstanceNotFound);
        }

        // Instance died.
        {
            test.drop_component();
            let mut child_ref =
                fdecl::ChildRef { name: "a".to_string(), collection: Some("coll".to_string()) };
            let err = test
                .realm_proxy
                .destroy_child(&mut child_ref)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InstanceDied);
        }
    }

    #[fuchsia::test]
    async fn dynamic_single_run_child() {
        // Set up model and realm service.
        let test = RealmCapabilityTest::new(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                ("system", ComponentDeclBuilder::new().add_single_run_collection("coll").build()),
                ("a", component_decl_with_test_runner()),
            ],
            vec!["system"].into(),
        )
        .await;

        let (_event_source, mut event_stream) = test
            .new_event_stream(
                vec![EventType::Started.into(), EventType::Destroyed.into()],
                EventMode::Sync,
            )
            .await;

        // Create child "a" in collection. Expect a Started event.
        let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
        let create_a = fasync::Task::spawn(test.realm_proxy.create_child(
            &mut collection_ref,
            child_decl("a"),
            fcomponent::CreateChildArgs::EMPTY,
        ));
        let event_a = event_stream
            .wait_until(EventType::Started, vec!["system", "coll:a"].into())
            .await
            .unwrap();

        // Started action completes.
        // Unblock Started and wait for requests to complete.
        event_a.resume();
        create_a.await.unwrap().unwrap();

        let child = {
            let state = test.component().lock_resolved_state().await.unwrap();
            let child = state.children().next().unwrap();
            assert_eq!("a", child.0.name());
            child.1.clone()
        };

        // The stop should trigger a delete/purge.
        child.stop_instance(false, false).await.unwrap();

        let event_a = event_stream
            .wait_until(EventType::Destroyed, vec!["system", "coll:a"].into())
            .await
            .unwrap();
        event_a.resume();

        // Verify that the component topology matches expectations.
        let actual_children = get_live_children(test.component()).await;
        let expected_children: HashSet<ChildMoniker> = HashSet::new();
        assert_eq!(actual_children, expected_children);
    }

    #[fuchsia::test]
    async fn list_children_errors() {
        // Create a root component with a collection.
        let mut test = RealmCapabilityTest::new(
            vec![("root", ComponentDeclBuilder::new().add_transient_collection("coll").build())],
            vec![].into(),
        )
        .await;

        // Collection not found.
        {
            let mut collection_ref = fdecl::CollectionRef { name: "nonexistent".to_string() };
            let (_, server_end) = endpoints::create_proxy().unwrap();
            let err = test
                .realm_proxy
                .list_children(&mut collection_ref, server_end)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::CollectionNotFound);
        }

        // Instance died.
        {
            test.drop_component();
            let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
            let (_, server_end) = endpoints::create_proxy().unwrap();
            let err = test
                .realm_proxy
                .list_children(&mut collection_ref, server_end)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InstanceDied);
        }
    }

    #[fuchsia::test]
    async fn open_exposed_dir() {
        let test = RealmCapabilityTest::new(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                (
                    "system",
                    ComponentDeclBuilder::new()
                        .protocol(ProtocolDeclBuilder::new("foo").path("/svc/foo").build())
                        .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                            source: ExposeSource::Self_,
                            source_name: "foo".into(),
                            target_name: "hippo".into(),
                            target: ExposeTarget::Parent,
                        }))
                        .build(),
                ),
            ],
            vec![].into(),
        )
        .await;
        let (_event_source, mut event_stream) = test
            .new_event_stream(
                vec![EventType::Resolved.into(), EventType::Started.into()],
                EventMode::Async,
            )
            .await;
        let mut out_dir = OutDir::new();
        out_dir.add_echo_service(CapabilityPath::try_from("/svc/foo").unwrap());
        test.mock_runner.add_host_fn("test:///system_resolved", out_dir.host_fn());

        // Open exposed directory of child.
        let mut child_ref = fdecl::ChildRef { name: "system".to_string(), collection: None };
        let (dir_proxy, server_end) = endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let res = test.realm_proxy.open_exposed_dir(&mut child_ref, server_end).await;
        res.expect("fidl call failed").expect("open_exposed_dir() failed");

        // Assert that child was resolved.
        let event = event_stream.wait_until(EventType::Resolved, vec!["system"].into()).await;
        assert!(event.is_some());

        // Assert that event stream doesn't have any outstanding messages.
        // This ensures that EventType::Started for "system" has not been
        // registered.
        let event =
            event_stream.wait_until(EventType::Started, vec!["system"].into()).now_or_never();
        assert!(event.is_none());

        // Now that it was asserted that "system:0" has yet to start,
        // assert that it starts after making connection below.
        let node_proxy = fuchsia_fs::open_node(
            &dir_proxy,
            &PathBuf::from("hippo"),
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
        )
        .expect("failed to open hippo service");
        let event = event_stream.wait_until(EventType::Started, vec!["system"].into()).await;
        assert!(event.is_some());
        let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = echo_proxy.echo_string(Some("hippos")).await;
        assert_eq!(res.expect("failed to use echo service"), Some("hippos".to_string()));

        // Verify topology matches expectations.
        let expected_urls = &["test:///root_resolved", "test:///system_resolved"];
        test.mock_runner.wait_for_urls(expected_urls).await;
        assert_eq!("(system)", test.hook.print());
    }

    #[fuchsia::test]
    async fn open_exposed_dir_dynamic_child() {
        let test = RealmCapabilityTest::new(
            vec![
                ("root", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
                (
                    "system",
                    ComponentDeclBuilder::new()
                        .protocol(ProtocolDeclBuilder::new("foo").path("/svc/foo").build())
                        .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                            source: ExposeSource::Self_,
                            source_name: "foo".into(),
                            target_name: "hippo".into(),
                            target: ExposeTarget::Parent,
                        }))
                        .build(),
                ),
            ],
            vec![].into(),
        )
        .await;

        let (_event_source, mut event_stream) = test
            .new_event_stream(
                vec![EventType::Resolved.into(), EventType::Started.into()],
                EventMode::Async,
            )
            .await;
        let mut out_dir = OutDir::new();
        out_dir.add_echo_service(CapabilityPath::try_from("/svc/foo").unwrap());
        test.mock_runner.add_host_fn("test:///system_resolved", out_dir.host_fn());

        // Add "system" to collection.
        let mut collection_ref = fdecl::CollectionRef { name: "coll".to_string() };
        let res = test
            .realm_proxy
            .create_child(
                &mut collection_ref,
                child_decl("system"),
                fcomponent::CreateChildArgs::EMPTY,
            )
            .await;
        res.expect("fidl call failed").expect("failed to create child system");

        // Open exposed directory of child.
        let mut child_ref =
            fdecl::ChildRef { name: "system".to_string(), collection: Some("coll".to_owned()) };
        let (dir_proxy, server_end) = endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let res = test.realm_proxy.open_exposed_dir(&mut child_ref, server_end).await;
        res.expect("fidl call failed").expect("open_exposed_dir() failed");

        // Assert that child was resolved.
        let event = event_stream.wait_until(EventType::Resolved, vec!["coll:system"].into()).await;
        assert!(event.is_some());

        // Assert that event stream doesn't have any outstanding messages.
        // This ensures that EventType::Started for "system" has not been
        // registered.
        let event =
            event_stream.wait_until(EventType::Started, vec!["coll:system"].into()).now_or_never();
        assert!(event.is_none());

        // Now that it was asserted that "system" has yet to start,
        // assert that it starts after making connection below.
        let node_proxy = fuchsia_fs::open_node(
            &dir_proxy,
            &PathBuf::from("hippo"),
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
        )
        .expect("failed to open hippo service");
        let event = event_stream.wait_until(EventType::Started, vec!["coll:system"].into()).await;
        assert!(event.is_some());
        let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = echo_proxy.echo_string(Some("hippos")).await;
        assert_eq!(res.expect("failed to use echo service"), Some("hippos".to_string()));

        // Verify topology matches expectations.
        let expected_urls = &["test:///root_resolved", "test:///system_resolved"];
        test.mock_runner.wait_for_urls(expected_urls).await;
        assert_eq!("(coll:system)", test.hook.print());
    }

    #[fuchsia::test]
    async fn open_exposed_dir_errors() {
        let mut test = RealmCapabilityTest::new(
            vec![
                (
                    "root",
                    ComponentDeclBuilder::new()
                        .add_lazy_child("system")
                        .add_lazy_child("unresolvable")
                        .add_lazy_child("unrunnable")
                        .build(),
                ),
                ("system", component_decl_with_test_runner()),
                ("unrunnable", component_decl_with_test_runner()),
            ],
            vec![].into(),
        )
        .await;
        test.mock_runner.cause_failure("unrunnable");

        // Instance not found.
        {
            let mut child_ref = fdecl::ChildRef { name: "missing".to_string(), collection: None };
            let (_, server_end) = endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            let err = test
                .realm_proxy
                .open_exposed_dir(&mut child_ref, server_end)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InstanceNotFound);
        }

        // Instance cannot resolve.
        {
            let mut child_ref =
                fdecl::ChildRef { name: "unresolvable".to_string(), collection: None };
            let (_, server_end) = endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            let err = test
                .realm_proxy
                .open_exposed_dir(&mut child_ref, server_end)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InstanceCannotResolve);
        }

        // Instance can't run.
        {
            let mut child_ref =
                fdecl::ChildRef { name: "unrunnable".to_string(), collection: None };
            let (dir_proxy, server_end) =
                endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            let res = test.realm_proxy.open_exposed_dir(&mut child_ref, server_end).await;
            res.expect("fidl call failed").expect("open_exposed_dir() failed");
            let node_proxy = fuchsia_fs::open_node(
                &dir_proxy,
                &PathBuf::from("hippo"),
                OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_SERVICE,
            )
            .expect("failed to open hippo service");
            let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
            let res = echo_proxy.echo_string(Some("hippos")).await;
            assert!(res.is_err());
        }

        // Instance died.
        {
            test.drop_component();
            let mut child_ref = fdecl::ChildRef { name: "system".to_string(), collection: None };
            let (_, server_end) = endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            let err = test
                .realm_proxy
                .open_exposed_dir(&mut child_ref, server_end)
                .await
                .expect("fidl call failed")
                .expect_err("unexpected success");
            assert_eq!(err, fcomponent::Error::InstanceDied);
        }
    }
}
