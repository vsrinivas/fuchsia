// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.cti

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability, OptionalTask},
        channel,
        config::RuntimeConfig,
        convert::{
            child_args_to_fsys, child_decl_to_fsys, child_ref_to_fsys, collection_ref_to_fsys,
        },
        model::{
            component::{BindReason, ComponentInstance, WeakComponentInstance},
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            model::Model,
            routing::error::RoutingError,
        },
    },
    ::routing::error::ComponentInstanceError,
    anyhow::Error,
    async_trait::async_trait,
    cm_fidl_validator,
    cm_rust::{CapabilityName, FidlIntoNative},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    log::*,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase, PartialChildMoniker},
    std::{
        cmp,
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

lazy_static! {
    // Path for in-tree clients.
    pub static ref INTERNAL_REALM_SERVICE: CapabilityName = "fuchsia.sys2.Realm".into();

    // Path for SDK clients.
    pub static ref SDK_REALM_SERVICE: CapabilityName = "fuchsia.component.Realm".into();
}

// Path in which to serve the implementation of the protocol.
pub enum RequestPath {
    // Serve protocol for in-tree clients, fuchsia.sys2.Realm.
    Internal,

    // Serve protocol for SDK clients, fuchsia.sys2.Component.
    Sdk,
}

// The `fuchsia.sys2.Realm` is currently being migrated to the `fuchsia.component`
// namespace. Until all clients of this protocol have been moved to the latter
// namespace, the following CapabilityProvider will support both paths.
// Tracking bug: https://fxbug.dev/85183.
pub struct RealmCapabilityProvider {
    scope_moniker: AbsoluteMoniker,
    host: Arc<RealmCapabilityHost>,
    path: RequestPath,
}

impl RealmCapabilityProvider {
    pub fn new(
        scope_moniker: AbsoluteMoniker,
        host: Arc<RealmCapabilityHost>,
        path: RequestPath,
    ) -> Self {
        Self { scope_moniker, host, path }
    }
}

#[async_trait]
impl CapabilityProvider for RealmCapabilityProvider {
    async fn open(
        self: Box<Self>,
        _flags: u32,
        _open_mode: u32,
        _relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<OptionalTask, ModelError> {
        let server_end = channel::take_channel(server_end);
        let scope_moniker = self.scope_moniker.to_partial();
        let host = self.host.clone();
        // We only need to look up the component matching this scope.
        // These operations should all work, even if the component is not running.
        let model = host.model.upgrade().ok_or(ModelError::ModelNotAvailable)?;
        let component = WeakComponentInstance::from(&model.look_up(&scope_moniker).await?);
        Ok(fasync::Task::spawn(async move {
            let serve_result = match &self.path {
                RequestPath::Internal => {
                    host.serve_for_internal_namespace(
                        component,
                        ServerEnd::<fsys::RealmMarker>::new(server_end)
                            .into_stream()
                            .expect("could not convert channel into stream"),
                    )
                    .await
                }
                RequestPath::Sdk => {
                    host.serve_for_sdk_namespace(
                        component,
                        ServerEnd::<fcomponent::RealmMarker>::new(server_end)
                            .into_stream()
                            .expect("could not convert channel into stream"),
                    )
                    .await
                }
            };
            if let Err(e) = serve_result {
                // TODO: Set an epitaph to indicate this was an unexpected error.
                warn!("serve failed: {}", e);
            }
        })
        .into())
    }
}

// Generate method to serve request stream for particular FIDL namespace.
// `label` is used to generate method name in the format `serve_for_$label_namespace`.
// `namespace` is a Rust crate for the FIDL bindings of the target namespace.
macro_rules! serve_request_stream_fn {
    ($label:ident, $namespace:ident) => {
        paste::paste! {
            pub async fn [<serve _ for _ $label _ namespace>](
                &self,
                component: WeakComponentInstance,
                stream: $namespace::RealmRequestStream,
            ) -> Result<(), fidl::Error> {
                stream
                    .try_for_each(|request| async {
                        let method_name = request.method_name();
                        let res = self.[<handle _ $label _ request>](request, &component).await;
                        if let Err(e) = &res {
                            warn!("Error occurred sending Realm response for {}: {}", method_name, e);
                        }
                        res
                    })
                    .await
            }
        }
    }
}

// Generate method to implement `Realm.ListChildren` for server. This method
// is modulated on a namespace because it implements a ServerEnd<ChildIterator>.
// `label` is used to generate method name in the format `list_$label_children`.
// `namespace` is a Rust crate for the FIDL bindings of the target namespace.
macro_rules! list_children_fn {
    ($label:ident, $namespace:ident, $decl:ident) => {
        paste::paste! {
            async fn [<list _ $label _ children>](
                component: &WeakComponentInstance,
                batch_size: usize,
                collection: $decl::CollectionRef,
                iter: ServerEnd<$namespace::ChildIteratorMarker>,
            ) -> Result<(), fcomponent::Error> {
                let component = component.upgrade().map_err(|_| fcomponent::Error::InstanceDied)?;
                let state = component.lock_resolved_state().await.map_err(|e| {
                    error!("failed to resolve InstanceState: {}", e);
                    fcomponent::Error::Internal
                })?;
                let decl = state.decl();
                let _ = decl
                    .find_collection(&collection.name)
                    .ok_or_else(|| fcomponent::Error::CollectionNotFound)?;
                let mut children: Vec<_> = state
                    .live_children()
                    .filter_map(|(m, _)| match m.collection() {
                        Some(c) => {
                            if c == collection.name {
                                Some($decl::ChildRef {
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
                    if let Err(e) = Self::[<serve _ $label _ child _ iterator>](children, stream, batch_size).await {
                        // TODO: Set an epitaph to indicate this was an unexpected error.
                        warn!("serve_child_iterator failed: {}", e);
                    }
                })
                .detach();
                Ok(())
            }

            async fn [<serve _ $label _ child _ iterator>](
                mut children: Vec<$decl::ChildRef>,
                mut stream: $namespace::ChildIteratorRequestStream,
                batch_size: usize,
            ) -> Result<(), Error> {
                while let Some(request) = stream.try_next().await? {
                    match request {
                        $namespace::ChildIteratorRequest::Next { responder } => {
                            let n_to_send = std::cmp::min(children.len(), batch_size);
                            let mut res: Vec<_> = children.drain(..n_to_send).collect();
                            responder.send(&mut res.iter_mut())?;
                        }
                    }
                }
                Ok(())
            }
        }
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

    serve_request_stream_fn!(internal, fsys);
    serve_request_stream_fn!(sdk, fcomponent);

    list_children_fn!(internal, fsys, fsys);
    list_children_fn!(sdk, fcomponent, fdecl);

    async fn handle_sdk_request(
        &self,
        request: fcomponent::RealmRequest,
        component: &WeakComponentInstance,
    ) -> Result<(), fidl::Error> {
        match request {
            fcomponent::RealmRequest::CreateChild { responder, collection, decl, args } => {
                let mut res = async {
                    let child_args = child_args_to_fsys(args).map_err(|err| {
                        log::warn!("Received invalid CreateChildArgs: {:?}", err);
                        fcomponent::Error::InvalidArguments
                    })?;

                    Self::create_child(
                        component,
                        collection_ref_to_fsys(collection),
                        child_decl_to_fsys(decl),
                        child_args,
                    )
                    .await
                }
                .await;
                responder.send(&mut res)?;
            }
            fcomponent::RealmRequest::DestroyChild { responder, child } => {
                let mut res = Self::destroy_child(component, child_ref_to_fsys(child)).await;
                responder.send(&mut res)?;
            }
            fcomponent::RealmRequest::ListChildren { responder, collection, iter } => {
                let mut res = Self::list_sdk_children(
                    component,
                    self.config.list_children_batch_size,
                    collection,
                    iter,
                )
                .await;
                responder.send(&mut res)?;
            }
            fcomponent::RealmRequest::OpenExposedDir { responder, child, exposed_dir } => {
                let mut res =
                    Self::open_exposed_dir(component, child_ref_to_fsys(child), exposed_dir).await;
                responder.send(&mut res)?;
            }
        }
        Ok(())
    }

    async fn handle_internal_request(
        &self,
        request: fsys::RealmRequest,
        component: &WeakComponentInstance,
    ) -> Result<(), fidl::Error> {
        match request {
            fsys::RealmRequest::CreateChild { responder, collection, decl, args } => {
                let mut res = Self::create_child(component, collection, decl, args).await;
                responder.send(&mut res)?;
            }
            fsys::RealmRequest::DestroyChild { responder, child } => {
                let mut res = Self::destroy_child(component, child).await;
                responder.send(&mut res)?;
            }
            fsys::RealmRequest::ListChildren { responder, collection, iter } => {
                let mut res = Self::list_internal_children(
                    component,
                    self.config.list_children_batch_size,
                    collection,
                    iter,
                )
                .await;
                responder.send(&mut res)?;
            }
            fsys::RealmRequest::OpenExposedDir { responder, child, exposed_dir } => {
                let mut res = Self::open_exposed_dir(component, child, exposed_dir).await;
                responder.send(&mut res)?;
            }
        }
        Ok(())
    }

    pub async fn create_child(
        component: &WeakComponentInstance,
        collection: fsys::CollectionRef,
        child_decl: fsys::ChildDecl,
        child_args: fsys::CreateChildArgs,
    ) -> Result<(), fcomponent::Error> {
        let component = component.upgrade().map_err(|_| fcomponent::Error::InstanceDied)?;
        cm_fidl_validator::validate_child(&child_decl).map_err(|e| {
            debug!("validate_child() failed: {}", e);
            fcomponent::Error::InvalidArguments
        })?;
        if child_decl.environment.is_some() {
            return Err(fcomponent::Error::InvalidArguments);
        }
        let child_decl = child_decl.fidl_into_native();
        match component.add_dynamic_child(collection.name.clone(), &child_decl, child_args).await {
            Ok(fsys::Durability::SingleRun) => {
                // Creating a child in a `SingleRun` collection automatically starts it, so
                // start the component.
                let child_ref =
                    fsys::ChildRef { name: child_decl.name, collection: Some(collection.name) };
                let (_client_end, server_end) =
                    fidl::endpoints::create_endpoints::<DirectoryMarker>().unwrap();
                let weak_component = WeakComponentInstance::new(&component);
                RealmCapabilityHost::bind_child(&weak_component, child_ref, server_end).await
            }
            Ok(_) => Ok(()),
            Err(e) => match e {
                ModelError::InstanceAlreadyExists { .. } => {
                    Err(fcomponent::Error::InstanceAlreadyExists)
                }
                ModelError::CollectionNotFound { .. } => Err(fcomponent::Error::CollectionNotFound),
                ModelError::DynamicOffersNotAllowed { .. } => {
                    Err(fcomponent::Error::InvalidArguments)
                }
                ModelError::Unsupported { .. } => Err(fcomponent::Error::Unsupported),
                _ => Err(fcomponent::Error::Internal),
            },
        }
    }

    async fn bind_child(
        component: &WeakComponentInstance,
        child: fsys::ChildRef,
        exposed_dir: ServerEnd<DirectoryMarker>,
    ) -> Result<(), fcomponent::Error> {
        match Self::get_child(component, child.clone()).await? {
            Some(child) => {
                let mut exposed_dir = exposed_dir.into_channel();
                let res = child
                    .bind(&BindReason::BindChild { parent: component.moniker.clone() })
                    .await
                    .map_err(|e| match e {
                        ModelError::ResolverError { err, .. } => {
                            debug!("failed to resolve child: {}", err);
                            fcomponent::Error::InstanceCannotResolve
                        }
                        ModelError::RunnerError { err } => {
                            debug!("failed to start child: {}", err);
                            fcomponent::Error::InstanceCannotStart
                        }
                        e => {
                            error!("bind() failed: {}", e);
                            fcomponent::Error::Internal
                        }
                    })?
                    .open_exposed(&mut exposed_dir)
                    .await;
                match res {
                    Ok(()) => (),
                    Err(ModelError::RoutingError {
                        err: RoutingError::SourceInstanceStopped { .. },
                    }) => {
                        // TODO(fxbug.dev/54109): The runner may have decided to not run the component. Perhaps a
                        // security policy prevented it, or maybe there was some other issue.
                        // Unfortunately these failed runs may or may not have occurred by this point,
                        // but we want to be consistent about how bind_child responds to these errors.
                        // Since this call succeeds if the runner hasn't yet decided to not run the
                        // component, we need to also succeed if the runner has already decided to not
                        // run the component, because otherwise the result of this call will be
                        // inconsistent.
                        ()
                    }
                    Err(e) => {
                        debug!("open_exposed() failed: {}", e);
                        return Err(fcomponent::Error::Internal);
                    }
                }
            }
            None => {
                debug!("bind_child() failed: instance not found {:?}", child);
                return Err(fcomponent::Error::InstanceNotFound);
            }
        }
        Ok(())
    }

    async fn open_exposed_dir(
        component: &WeakComponentInstance,
        child: fsys::ChildRef,
        exposed_dir: ServerEnd<DirectoryMarker>,
    ) -> Result<(), fcomponent::Error> {
        match Self::get_child(component, child.clone()).await? {
            Some(child) => {
                // Resolve child in order to instantiate exposed_dir.
                let _ = child.resolve().await.map_err(|_| {
                    return fcomponent::Error::InstanceCannotResolve;
                })?;
                let mut exposed_dir = exposed_dir.into_channel();
                let () = child.open_exposed(&mut exposed_dir).await.map_err(|e| match e {
                    ModelError::InstanceShutDown { .. } => fcomponent::Error::InstanceDied,
                    _ => {
                        debug!("open_exposed() failed: {}", e);
                        fcomponent::Error::Internal
                    }
                })?;
            }
            None => {
                debug!("open_exposed_dir() failed: instance not found {:?}", child);
                return Err(fcomponent::Error::InstanceNotFound);
            }
        }
        Ok(())
    }

    pub async fn destroy_child(
        component: &WeakComponentInstance,
        child: fsys::ChildRef,
    ) -> Result<(), fcomponent::Error> {
        let component = component.upgrade().map_err(|_| fcomponent::Error::InstanceDied)?;
        child.collection.as_ref().ok_or(fcomponent::Error::InvalidArguments)?;
        let partial_moniker = PartialChildMoniker::new(child.name, child.collection);
        let destroy_fut =
            component.remove_dynamic_child(&partial_moniker).await.map_err(|e| match e {
                ModelError::InstanceNotFoundInRealm { .. } => fcomponent::Error::InstanceNotFound,
                ModelError::Unsupported { .. } => fcomponent::Error::Unsupported,
                e => {
                    error!("remove_dynamic_child() failed: {}", e);
                    fcomponent::Error::Internal
                }
            })?;
        // This function returns as soon as the child is marked deleted, while actual destruction
        // proceeds in the background.
        fasync::Task::spawn(async move {
            let _ = destroy_fut.await;
        })
        .detach();
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
        if capability_provider.is_none() && capability.matches_protocol(&INTERNAL_REALM_SERVICE) {
            Ok(Some(Box::new(RealmCapabilityProvider::new(
                scope_moniker,
                self.clone(),
                RequestPath::Internal,
            )) as Box<dyn CapabilityProvider>))
        } else if capability_provider.is_none() && capability.matches_protocol(&SDK_REALM_SERVICE) {
            Ok(Some(Box::new(RealmCapabilityProvider::new(
                scope_moniker,
                self.clone(),
                RequestPath::Sdk,
            )) as Box<dyn CapabilityProvider>))
        } else {
            Ok(capability_provider)
        }
    }

    async fn get_child(
        parent: &WeakComponentInstance,
        child: fsys::ChildRef,
    ) -> Result<Option<Arc<ComponentInstance>>, fcomponent::Error> {
        let parent = parent.upgrade().map_err(|_| fcomponent::Error::InstanceDied)?;
        let state = parent.lock_resolved_state().await.map_err(|e| match e {
            ComponentInstanceError::ResolveFailed { moniker, err, .. } => {
                debug!("failed to resolve instance with moniker {}: {}", moniker, err);
                return fcomponent::Error::InstanceCannotResolve;
            }
            e => {
                error!("failed to resolve InstanceState: {}", e);
                return fcomponent::Error::Internal;
            }
        })?;
        let partial_moniker = PartialChildMoniker::new(child.name, child.collection);
        Ok(state.get_live_child(&partial_moniker).map(|r| r.clone()))
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
                    component.moniker.clone(),
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
            convert::decl as fdecl,
            model::{
                binding::Binder,
                component::{BindReason, ComponentInstance},
                events::{source::EventSource, stream::EventStream},
                testing::{mocks::*, out_dir::OutDir, test_helpers::*, test_hook::*},
            },
        },
        cm_rust::{
            self, CapabilityName, CapabilityPath, ComponentDecl, EventMode, ExposeDecl,
            ExposeProtocolDecl, ExposeSource, ExposeTarget,
        },
        cm_rust_testing::*,
        fidl::endpoints::{self, Proxy},
        fidl_fidl_examples_echo as echo, fidl_fuchsia_component as fcomponent,
        fidl_fuchsia_io::MODE_TYPE_SERVICE,
        fuchsia_async as fasync,
        fuchsia_component::client,
        futures::{lock::Mutex, poll, task::Poll},
        io_util::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
        matches::assert_matches,
        moniker::PartialAbsoluteMoniker,
        routing_test_helpers::component_decl_with_exposed_binder,
        std::collections::HashSet,
        std::convert::TryFrom,
        std::path::PathBuf,
    };

    // Generate test suite for each supported FIDL namespace, `fuchsia.sys2`
    // and `fuchsia.component`. These macros are needed only temporarily.
    // All clients of `fuchsia.sys2.Realm` will be migrated to
    // `fuchsia.component.Realm` (https://fxbug.dev/85183). Until the migration
    // is complete, this test suite will maintain test coverage parity for both
    // namespaces.
    macro_rules! realm_test_suite {
    ($label:ident, $namespace:ident, $decl:ident) => {
    paste::paste! {
        struct [<$label:camel RealmCapabilityTest>] {
            builtin_environment: Option<Arc<Mutex<BuiltinEnvironment>>>,
            mock_runner: Arc<MockRunner>,
            component: Option<Arc<ComponentInstance>>,
            realm_proxy: $namespace::RealmProxy,
            hook: Arc<TestHook>,
        }

        impl [<$label:camel RealmCapabilityTest>] {
            async fn new(
                components: Vec<(&'static str, ComponentDecl)>,
                component_moniker: PartialAbsoluteMoniker,
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
                model.root().hooks.install(hooks).await;

                // Look up and bind to component.
                let component = model
                    .bind(&component_moniker, &BindReason::Eager)
                    .await
                    .expect("failed to bind to component");

                // Host framework service.
                let (realm_proxy, stream) =
                    endpoints::create_proxy_and_stream::<$namespace::RealmMarker>().unwrap();
                {
                    let component = WeakComponentInstance::from(&component);
                    let realm_capability_host =
                        builtin_environment.lock().await.realm_capability_host.clone();
                    fasync::Task::spawn(async move {
                        realm_capability_host
                            .[<serve _ for _ $label _ namespace>](component, stream)
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

        fn [<$label _ child _ decl>](name: &str) -> $decl::ChildDecl {
            $decl::ChildDecl {
                name: Some(name.to_owned()),
                url: Some(format!("test:///{}", name)),
                startup: Some($decl::StartupMode::Lazy),
                environment: None,
                on_terminate: None,
                ..$decl::ChildDecl::EMPTY
            }
        }

        #[fuchsia::test]
        async fn [<$label _ create _ dynamic _ child>]() {
            // Set up model and realm service.
            let test = [<$label:camel RealmCapabilityTest>]::new(
                vec![
                    ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                    ("system", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
                ],
                vec!["system"].into(),
            )
            .await;

            let (_event_source, mut event_stream) =
                test.new_event_stream(vec![EventType::Discovered.into()], EventMode::Sync).await;

            // Create children "a" and "b" in collection. Expect a Discovered event for each.
            let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
            for (name, moniker) in [("a", "coll:a:1"), ("b", "coll:b:2")] {
                let mut create = fasync::Task::spawn(test.realm_proxy.create_child(
                    &mut collection_ref,
                    [<$label _ child _ decl>](name),
                    $namespace::CreateChildArgs::EMPTY,
                ));
                let event = event_stream
                    .wait_until(EventType::Discovered, vec!["system:0", moniker].into())
                    .await
                    .unwrap();

                // Give create requests time to be processed. Ensure they don't return before
                // Discover action completes.
                fasync::Timer::new(fasync::Time::after(zx::Duration::from_seconds(5))).await;
                assert_matches!(poll!(&mut create), Poll::Pending);

                // Unblock Discovered and wait for request to complete.
                event.resume();
                let _ = create.await.unwrap().unwrap();
            }

            // Verify that the component topology matches expectations.
            let actual_children = get_live_children(test.component()).await;
            let mut expected_children: HashSet<PartialChildMoniker> = HashSet::new();
            expected_children.insert("coll:a".into());
            expected_children.insert("coll:b".into());
            assert_eq!(actual_children, expected_children);
            assert_eq!("(system(coll:a,coll:b))", test.hook.print());
        }

        #[fuchsia::test]
        async fn [<$label _ create _ dynamic _ child _ errors>]() {
            let mut test = [<$label:camel RealmCapabilityTest>]::new(
                vec![
                    ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                    (
                        "system",
                        ComponentDeclBuilder::new()
                            .add_transient_collection("coll")
                            .add_collection(
                                CollectionDeclBuilder::new()
                                    .name("pcoll")
                                    .durability(fsys::Durability::Persistent)
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
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
                let child_decl = $decl::ChildDecl {
                    name: Some("a".to_string()),
                    url: None,
                    startup: Some($decl::StartupMode::Lazy),
                    environment: None,
                    ..$decl::ChildDecl::EMPTY
                };
                let err = test
                    .realm_proxy
                    .create_child(&mut collection_ref, child_decl, $namespace::CreateChildArgs::EMPTY)
                    .await
                    .expect("fidl call failed")
                    .expect_err("unexpected success");
                assert_eq!(err, fcomponent::Error::InvalidArguments);
            }
            {
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
                let child_decl = $decl::ChildDecl {
                    name: Some("a".to_string()),
                    url: Some("test:///a".to_string()),
                    startup: Some($decl::StartupMode::Lazy),
                    environment: Some("env".to_string()),
                    ..$decl::ChildDecl::EMPTY
                };
                let err = test
                    .realm_proxy
                    .create_child(&mut collection_ref, child_decl, $namespace::CreateChildArgs::EMPTY)
                    .await
                    .expect("fidl call failed")
                    .expect_err("unexpected success");
                assert_eq!(err, fcomponent::Error::InvalidArguments);
            }

            // Instance already exists.
            {
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
                let res = test
                    .realm_proxy
                    .create_child(&mut collection_ref, [<$label _ child _ decl>]("a"), $namespace::CreateChildArgs::EMPTY)
                    .await;
                let _ = res.expect("failed to create child a");
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
                let err = test
                    .realm_proxy
                    .create_child(&mut collection_ref, [<$label _ child _ decl>]("a"), $namespace::CreateChildArgs::EMPTY)
                    .await
                    .expect("fidl call failed")
                    .expect_err("unexpected success");
                assert_eq!(err, fcomponent::Error::InstanceAlreadyExists);
            }

            // Collection not found.
            {
                let mut collection_ref = $decl::CollectionRef { name: "nonexistent".to_string() };
                let err = test
                    .realm_proxy
                    .create_child(&mut collection_ref, [<$label _ child _ decl>]("a"), $namespace::CreateChildArgs::EMPTY)
                    .await
                    .expect("fidl call failed")
                    .expect_err("unexpected success");
                assert_eq!(err, fcomponent::Error::CollectionNotFound);
            }

            // Unsupported.
            {
                let mut collection_ref = $decl::CollectionRef { name: "pcoll".to_string() };
                let err = test
                    .realm_proxy
                    .create_child(&mut collection_ref, [<$label _ child _ decl>]("a"), $namespace::CreateChildArgs::EMPTY)
                    .await
                    .expect("fidl call failed")
                    .expect_err("unexpected success");
                assert_eq!(err, fcomponent::Error::Unsupported);
            }
            {
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
                let child_decl = $decl::ChildDecl {
                    name: Some("b".to_string()),
                    url: Some("test:///b".to_string()),
                    startup: Some($decl::StartupMode::Eager),
                    environment: None,
                    ..$decl::ChildDecl::EMPTY
                };
                let err = test
                    .realm_proxy
                    .create_child(&mut collection_ref, child_decl, $namespace::CreateChildArgs::EMPTY)
                    .await
                    .expect("fidl call failed")
                    .expect_err("unexpected success");
                assert_eq!(err, fcomponent::Error::Unsupported);
            }

            // Disallowed dynamic offers specified.
            {
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
                let child_decl = $decl::ChildDecl {
                    name: Some("b".to_string()),
                    url: Some("test:///b".to_string()),
                    startup: Some($decl::StartupMode::Lazy),
                    environment: None,
                    ..$decl::ChildDecl::EMPTY
                };
                let err = test
                    .realm_proxy
                    .create_child(
                        &mut collection_ref,
                        child_decl,
                        $namespace::CreateChildArgs {
                            dynamic_offers: Some(vec![$decl::OfferDecl::Protocol(
                                $decl::OfferProtocolDecl {
                                    source: Some($decl::Ref::Parent($decl::ParentRef {})),
                                    source_name: Some("foo".to_string()),
                                    target_name: Some("foo".to_string()),
                                    ..$decl::OfferProtocolDecl::EMPTY
                                },
                            )]),
                            ..$namespace::CreateChildArgs::EMPTY
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
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
                let child_decl = $decl::ChildDecl {
                    name: Some("b".to_string()),
                    url: Some("test:///b".to_string()),
                    startup: Some($decl::StartupMode::Lazy),
                    environment: None,
                    ..$decl::ChildDecl::EMPTY
                };
                let err = test
                    .realm_proxy
                    .create_child(&mut collection_ref, child_decl, $namespace::CreateChildArgs::EMPTY)
                    .await
                    .expect("fidl call failed")
                    .expect_err("unexpected success");
                assert_eq!(err, fcomponent::Error::InstanceDied);
            }
        }

        #[fuchsia::test]
        async fn [<$label _ destroy _ dynamic _ child>]() {
            // Set up model and realm service.
            let test = [<$label:camel RealmCapabilityTest>]::new(
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
                    vec![
                        EventType::Stopped.into(),
                        EventType::Destroyed.into(),
                        EventType::Purged.into(),
                    ],
                    EventMode::Sync,
                )
                .await;

            // Create children "a" and "b" in collection, and bind to them.
            for name in &["a", "b"] {
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
                let res = test
                    .realm_proxy
                    .create_child(&mut collection_ref, [<$label _ child _ decl>](name), $namespace::CreateChildArgs::EMPTY)
                    .await;
                let _ = res
                    .unwrap_or_else(|_| panic!("failed to create child {}", name))
                    .unwrap_or_else(|_| panic!("failed to create child {}", name));
                let mut child_ref =
                    $decl::ChildRef { name: name.to_string(), collection: Some("coll".to_string()) };
                let (exposed_dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
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
            let instance_id = get_instance_id(test.component(), "coll:a").await;
            assert_eq!("(system(coll:a,coll:b))", test.hook.print());
            assert_eq!(child.component_url, "test:///a".to_string());
            assert_eq!(instance_id, 1);

            // Destroy "a". "a" is no longer live from the client's perspective, although it's still
            // being destroyed.
            let mut child_ref =
                $decl::ChildRef { name: "a".to_string(), collection: Some("coll".to_string()) };
            let (f, destroy_handle) = test.realm_proxy.destroy_child(&mut child_ref).remote_handle();
            fasync::Task::spawn(f).detach();

            // The component should be stopped (shut down) before it is marked deleted.
            let event = event_stream
                .wait_until(EventType::Stopped, vec!["system:0", "coll:a:1"].into())
                .await
                .unwrap();
            event.resume();
            let event = event_stream
                .wait_until(EventType::Destroyed, vec!["system:0", "coll:a:1"].into())
                .await
                .unwrap();

            // Child is not marked deleted yet, but should be shut down.
            {
                let actual_children = get_live_children(test.component()).await;
                let mut expected_children: HashSet<PartialChildMoniker> = HashSet::new();
                expected_children.insert("coll:a".into());
                expected_children.insert("coll:b".into());
                assert_eq!(actual_children, expected_children);
                let child_a = get_live_child(test.component(), "coll:a").await;
                let child_b = get_live_child(test.component(), "coll:b").await;
                assert!(execution_is_shut_down(&child_a).await);
                assert!(!execution_is_shut_down(&child_b).await);
            }

            // The destruction of "a" was arrested during `PreDestroy`. The old "a" should still exist,
            // although it's not live.
            assert!(has_child(test.component(), "coll:a:1").await);

            // Move past the 'PreDestroy' event for "a", and wait for destroy_child to return.
            event.resume();
            let res = destroy_handle.await;
            let _ = res.expect("failed to destroy child a").expect("failed to destroy child a");

            // Child is marked deleted now.
            {
                let actual_children = get_live_children(test.component()).await;
                let mut expected_children: HashSet<PartialChildMoniker> = HashSet::new();
                expected_children.insert("coll:b".into());
                assert_eq!(actual_children, expected_children);
                assert_eq!("(system(coll:b))", test.hook.print());
            }

            // Wait until 'PostDestroy' event for "a"
            let event = event_stream
                .wait_until(EventType::Purged, vec!["system:0", "coll:a:1"].into())
                .await
                .unwrap();
            event.resume();

            assert!(!has_child(test.component(), "coll:a:1").await);

            // Recreate "a" and verify "a" is back (but it's a different "a"). The old "a" is gone
            // from the client's point of view, but it hasn't been cleaned up yet.
            let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
            let child_decl = $decl::ChildDecl {
                name: Some("a".to_string()),
                url: Some("test:///a_alt".to_string()),
                startup: Some($decl::StartupMode::Lazy),
                environment: None,
                ..$decl::ChildDecl::EMPTY
            };
            let res = test
                .realm_proxy
                .create_child(&mut collection_ref, child_decl, $namespace::CreateChildArgs::EMPTY)
                .await;
            let _ = res.expect("failed to recreate child a").expect("failed to recreate child a");

            assert_eq!("(system(coll:a,coll:b))", test.hook.print());
            let child = get_live_child(test.component(), "coll:a").await;
            let instance_id = get_instance_id(test.component(), "coll:a").await;
            assert_eq!(child.component_url, "test:///a_alt".to_string());
            assert_eq!(instance_id, 3);
        }

        #[fuchsia::test]
        async fn [<$label _ destroy _ dynamic _ child _ errors>]() {
            let mut test = [<$label:camel RealmCapabilityTest>]::new(
                vec![
                    ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                    ("system", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
                ],
                vec!["system"].into(),
            )
            .await;

            // Create child "a" in collection.
            let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
            let res = test
                .realm_proxy
                .create_child(&mut collection_ref, [<$label _ child _ decl>]("a"), $namespace::CreateChildArgs::EMPTY)
                .await;
            let _ = res.expect("failed to create child a").expect("failed to create child a");

            // Invalid arguments.
            {
                let mut child_ref = $decl::ChildRef { name: "a".to_string(), collection: None };
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
                    $decl::ChildRef { name: "b".to_string(), collection: Some("coll".to_string()) };
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
                    $decl::ChildRef { name: "a".to_string(), collection: Some("coll".to_string()) };
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
        async fn [<$label _ dynamic _ single _ run _ child>]() {
            // Set up model and realm service.
            let test = [<$label:camel RealmCapabilityTest>]::new(
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
                    vec![EventType::Started.into(), EventType::Purged.into()],
                    EventMode::Sync,
                )
                .await;

            // Create child "a" in collection. Expect a Started event.
            let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
            let create_a = fasync::Task::spawn(test.realm_proxy.create_child(
                &mut collection_ref,
                [<$label _ child _ decl>]("a"),
                $namespace::CreateChildArgs::EMPTY,
            ));
            let event_a = event_stream
                .wait_until(EventType::Started, vec!["system:0", "coll:a:1"].into())
                .await
                .unwrap();

            // Started action completes.
            // Unblock Started and wait for requests to complete.
            event_a.resume();
            let _ = create_a.await.unwrap().unwrap();

            let child = {
                let state = test.component().lock_resolved_state().await.unwrap();
                let child = state.all_children().iter().next().unwrap();
                assert_eq!("a", child.0.name());
                child.1.clone()
            };

            // The stop should trigger a delete/purge.
            child.stop_instance(false, false).await.unwrap();

            let event_a = event_stream
                .wait_until(EventType::Purged, vec!["system:0", "coll:a:1"].into())
                .await
                .unwrap();
            event_a.resume();

            // Verify that the component topology matches expectations.
            let actual_children = get_live_children(test.component()).await;
            let expected_children: HashSet<PartialChildMoniker> = HashSet::new();
            assert_eq!(actual_children, expected_children);
        }

        #[fuchsia::test]
        async fn [<$label _ list _ children _ errors>]() {
            // Create a root component with a collection.
            let mut test = [<$label:camel RealmCapabilityTest>]::new(
                vec![("root", ComponentDeclBuilder::new().add_transient_collection("coll").build())],
                vec![].into(),
            )
            .await;

            // Collection not found.
            {
                let mut collection_ref = $decl::CollectionRef { name: "nonexistent".to_string() };
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
                let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
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
        async fn [<$label _ open _ exposed _ dir>]() {
            let test = [<$label:camel RealmCapabilityTest>]::new(
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
            let mut child_ref = $decl::ChildRef { name: "system".to_string(), collection: None };
            let (dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
            let res = test.realm_proxy.open_exposed_dir(&mut child_ref, server_end).await;
            let _ = res.expect("open_exposed_dir() failed").expect("open_exposed_dir() failed");

            // Assert that child was resolved.
            let event = event_stream.wait_until(EventType::Resolved, vec!["system:0"].into()).await;
            assert!(event.is_some());

            // Assert that event stream doesn't have any outstanding messages.
            // This ensures that EventType::Started for "system:0" has not been
            // registered.
            let event =
                event_stream.wait_until(EventType::Started, vec!["system:0"].into()).now_or_never();
            assert!(event.is_none());

            // Now that it was asserted that "system:0" has yet to start,
            // assert that it starts after making connection below.
            let node_proxy = io_util::open_node(
                &dir_proxy,
                &PathBuf::from("hippo"),
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_SERVICE,
            )
            .expect("failed to open hippo service");
            let event = event_stream.wait_until(EventType::Started, vec!["system:0"].into()).await;
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
        async fn [<$label _ open _ exposed _ dir _ dynamic _ child>]() {
            let test = [<$label:camel RealmCapabilityTest>]::new(
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
            let mut collection_ref = $decl::CollectionRef { name: "coll".to_string() };
            let res = test
                .realm_proxy
                .create_child(&mut collection_ref, [<$label _ child _ decl>]("system"), $namespace::CreateChildArgs::EMPTY)
                .await;
            let _ = res.expect("failed to create child system").expect("failed to create child system");

            // Open exposed directory of child.
            let mut child_ref =
                $decl::ChildRef { name: "system".to_string(), collection: Some("coll".to_owned()) };
            let (dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
            let res = test.realm_proxy.open_exposed_dir(&mut child_ref, server_end).await;
            let _ = res.expect("open_exposed_dir() failed").expect("open_exposed_dir() failed");

            // Assert that child was resolved.
            let event =
                event_stream.wait_until(EventType::Resolved, vec!["coll:system:1"].into()).await;
            assert!(event.is_some());

            // Assert that event stream doesn't have any outstanding messages.
            // This ensures that EventType::Started for "system:0" has not been
            // registered.
            let event = event_stream
                .wait_until(EventType::Started, vec!["coll:system:1"].into())
                .now_or_never();
            assert!(event.is_none());

            // Now that it was asserted that "system:0" has yet to start,
            // assert that it starts after making connection below.
            let node_proxy = io_util::open_node(
                &dir_proxy,
                &PathBuf::from("hippo"),
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_SERVICE,
            )
            .expect("failed to open hippo service");
            let event = event_stream.wait_until(EventType::Started, vec!["coll:system:1"].into()).await;
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
        async fn [<$label _ open _ exposed _ dir _ errors>]() {
            let mut test = [<$label:camel RealmCapabilityTest>]::new(
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
                let mut child_ref = $decl::ChildRef { name: "missing".to_string(), collection: None };
                let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
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
                    $decl::ChildRef { name: "unresolvable".to_string(), collection: None };
                let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
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
                let mut child_ref = $decl::ChildRef { name: "unrunnable".to_string(), collection: None };
                let (dir_proxy, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
                let res = test.realm_proxy.open_exposed_dir(&mut child_ref, server_end).await;
                let _ = res.expect("open_exposed_dir() failed").expect("open_exposed_dir() failed");
                let node_proxy = io_util::open_node(
                    &dir_proxy,
                    &PathBuf::from("hippo"),
                    OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    MODE_TYPE_SERVICE,
                )
                .expect("failed to open hippo service");
                let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
                let res = echo_proxy.echo_string(Some("hippos")).await;
                assert!(res.is_err());
            }

            // Instance died.
            {
                test.drop_component();
                let mut child_ref = $decl::ChildRef { name: "system".to_string(), collection: None };
                let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
                let err = test
                    .realm_proxy
                    .open_exposed_dir(&mut child_ref, server_end)
                    .await
                    .expect("fidl call failed")
                    .expect_err("unexpected success");
                assert_eq!(err, fcomponent::Error::InstanceDied);
            }
        }

    }}}

    realm_test_suite!(internal, fsys, fsys);
    realm_test_suite!(sdk, fcomponent, fdecl);
}
