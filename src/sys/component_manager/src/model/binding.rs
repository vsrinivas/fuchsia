// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        exposed_dir::ExposedDir,
        hooks::{Event, EventPayload},
        model::Model,
        moniker::AbsoluteMoniker,
        namespace::IncomingNamespace,
        realm::{ExecutionState, Realm, Runtime},
        resolver::Resolver,
        routing_facade::RoutingFacade,
    },
    cm_rust::data,
    fidl::endpoints::{create_endpoints, Proxy, ServerEnd},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        future::{join_all, BoxFuture},
        lock::Mutex,
        FutureExt,
    },
    std::sync::Arc,
};

#[derive(Clone)]
pub struct ComponentDescriptor {
    pub abs_moniker: AbsoluteMoniker,
    pub url: String,
}

impl Model {
    /// Binds to the component instance in the given realm, starting it if it's not already
    /// running. Returns the list of child realms whose instances need to be eagerly started after
    /// this function returns. The caller is responsible for calling
    /// `bind_eager_children_recursive` to ensure eager children are recursively binded.
    pub async fn bind_single_instance(
        &self,
        realm: Arc<Realm>,
    ) -> Result<Vec<Arc<Realm>>, ModelError> {
        // Resolve the component and find the runner to use.
        let component = realm.resolver_registry.resolve(&realm.component_url).await?;
        let (decl, live_child_descriptors) = {
            // The `ComponentDecl` must always be resolved by `resolve_decl` as that
            // can trigger a `ResolveInstance` event.
            Realm::resolve_decl(&realm).await?;
            let state = realm.lock_state().await;
            let state = state.as_ref().unwrap();
            let decl = state.decl().clone();
            let live_child_descriptors: Vec<_> = state
                .live_child_realms()
                .map(|(_, r)| ComponentDescriptor {
                    abs_moniker: r.abs_moniker.clone(),
                    url: r.component_url.clone(),
                })
                .collect();
            (decl, live_child_descriptors)
        };
        let runner = Realm::resolve_runner(&realm, self).await?;

        // Pre-flight check: if the component is already started, return now (and don't invoke the
        // hook).
        let maybe_return_early =
            |execution: &ExecutionState| -> Option<Result<Vec<Arc<Realm>>, ModelError>> {
                if execution.is_shut_down() {
                    Some(Err(ModelError::instance_shut_down(realm.abs_moniker.clone())))
                } else if execution.runtime.is_some() {
                    // TODO: Add binding to the execution once we track bindings.
                    Some(Ok(vec![]))
                } else {
                    None
                }
            };
        {
            let execution = realm.lock_execution().await;
            if let Some(res) = maybe_return_early(&execution) {
                return res;
            }
        }

        // Generate the Runtime which will be set in the Execution.
        let (pending_runtime, start_info, controller_server) = self
            .make_execution_runtime(
                &realm.abs_moniker,
                component.resolved_url.ok_or(ModelError::ComponentInvalid)?,
                component.package,
                &decl,
            )
            .await?;

        // Invoke the BeforeStart hook outside of lock, passing it a Runtime reference. Note that
        // this could race with the component being started first in some other task. In that case,
        // the hook will be invoked with a Runtime; from the client's perspective, this is like
        // getting an invocation for a component that's started and immediately stopped, except it
        // won't see a Stop event.
        {
            let routing_facade = RoutingFacade::new(self.clone());
            let event = Event::new(
                realm.abs_moniker.clone(),
                EventPayload::BeforeStartInstance {
                    runtime: pending_runtime.clone(),
                    component_decl: decl.clone(),
                    live_children: live_child_descriptors,
                    routing_facade,
                },
            );
            realm.hooks.dispatch(&event).await?;
        }

        // Set the Runtime in the Execution. From component manager's perspective, this indicates
        // that the component has started.
        {
            let mut execution = realm.lock_execution().await;
            if let Some(res) = maybe_return_early(&execution) {
                // This task raced with another task that started the component. Return.
                return res;
            }
            execution.runtime = Some(pending_runtime);
        }

        // We call `Start` outside of lock, after the Runtime is populated. When the runtime was
        // populated, we checked if it was already set, so if there were two concurrent bind
        // calls at most one of them will get here.
        //
        // It is also possible that the component is stopped before this. If so, that's fine: the
        // runner will start the component, but its stop or kill signal will be immediately set on
        // the component controller.
        runner.start(start_info, controller_server).await?;

        // Return eager children that need to be bound to.
        {
            let mut state = realm.lock_state().await;
            let state = state.as_mut().expect("bind_single_instance: not resolved");
            let eager_child_realms: Vec<_> = state
                .live_child_realms()
                .filter_map(|(_, r)| match r.startup {
                    fsys::StartupMode::Eager => Some(r.clone()),
                    fsys::StartupMode::Lazy => None,
                })
                .collect();
            Ok(eager_child_realms)
        }
    }

    /// Binds to a list of instances, and any eager children they may return.
    async fn bind_eager_children_recursive<'a>(
        &'a self,
        mut instances_to_bind: Vec<Arc<Realm>>,
    ) -> Result<(), ModelError> {
        loop {
            if instances_to_bind.is_empty() {
                break;
            }
            let futures: Vec<_> = instances_to_bind
                .iter()
                .map(|realm| {
                    Box::pin(async move { self.bind_single_instance(realm.clone()).await })
                })
                .collect();
            let res = join_all(futures).await;
            instances_to_bind.clear();
            for e in res {
                instances_to_bind.append(&mut e?);
            }
        }
        Ok(())
    }

    /// Returns a configured Runtime for a component and the start info (without actually starting
    /// the component).
    async fn make_execution_runtime(
        &self,
        abs_moniker: &AbsoluteMoniker,
        url: String,
        package: Option<fsys::Package>,
        decl: &cm_rust::ComponentDecl,
    ) -> Result<
        (Arc<Mutex<Runtime>>, fsys::ComponentStartInfo, ServerEnd<fsys::ComponentControllerMarker>),
        ModelError,
    > {
        // Create incoming/outgoing directories, and populate them.
        let exposed_dir = ExposedDir::new(self, abs_moniker, decl.clone())?;
        let (outgoing_dir_client, outgoing_dir_server) =
            zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
        let (runtime_dir_client, runtime_dir_server) =
            zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
        let mut namespace = IncomingNamespace::new(package)?;
        let ns = namespace.populate(self.clone(), abs_moniker, decl).await?;

        let (controller_client, controller_server) =
            create_endpoints::<fsys::ComponentControllerMarker>()
                .expect("could not create component controller endpoints");
        let controller =
            controller_client.into_proxy().expect("failed to create ComponentControllerProxy");
        // Set up channels into/out of the new component.
        let runtime = Arc::new(Mutex::new(Runtime::start_from(
            url.clone(),
            Some(namespace),
            Some(DirectoryProxy::from_channel(
                fasync::Channel::from_channel(outgoing_dir_client).unwrap(),
            )),
            Some(DirectoryProxy::from_channel(
                fasync::Channel::from_channel(runtime_dir_client).unwrap(),
            )),
            exposed_dir,
            Some(controller),
        )?));
        let start_info = fsys::ComponentStartInfo {
            resolved_url: Some(url),
            program: data::clone_option_dictionary(&decl.program),
            ns: Some(ns),
            outgoing_dir: Some(ServerEnd::new(outgoing_dir_server)),
            runtime_dir: Some(ServerEnd::new(runtime_dir_server)),
        };

        Ok((runtime, start_info, controller_server))
    }
}

/// A trait to enable support for different `bind()` implementations. This is used,
/// for example, for testing code that depends on `bind()`, but no other `Model`
/// functionality.
pub trait Binder: Send + Sync {
    fn bind<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
    ) -> BoxFuture<Result<Arc<Realm>, ModelError>>;
}

impl Binder for Model {
    /// Binds to the component instance with the specified moniker. This has the following effects:
    /// - Binds to the parent instance.
    /// - Starts the component instance, if it is not already running and not shut down.
    /// - Binds to any descendant component instances that need to be eagerly started.
    // TODO: This function starts the parent component, but doesn't track the bindings anywhere.
    // This means that when the child stops and the parent has no other reason to run, we won't
    // stop the parent. To solve this, we need to track the bindings.
    fn bind<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
    ) -> BoxFuture<Result<Arc<Realm>, ModelError>> {
        async move {
            async fn bind_one(model: &Model, m: AbsoluteMoniker) -> Result<Arc<Realm>, ModelError> {
                let realm = model.look_up_realm(&m).await?;
                let eager_children = model.bind_single_instance(realm.clone()).await?;
                // If the bind to this realm's instance succeeded but the child is shut down, allow
                // the call to succeed. If the child is shut down, that shouldn't cause the client
                // to believe the bind to `abs_moniker` failed.
                //
                // TODO: Have a more general strategy for dealing with errors from eager binding.
                // Should we ever pass along the error?
                model.bind_eager_children_recursive(eager_children).await.or_else(|e| match e {
                    ModelError::InstanceShutDown { .. } => Ok(()),
                    _ => Err(e),
                })?;
                Ok(realm)
            }
            let mut cur_moniker = AbsoluteMoniker::root();
            let mut realm = bind_one(self, cur_moniker.clone()).await?;
            for m in abs_moniker.path().iter() {
                cur_moniker = cur_moniker.child(m.clone());
                realm = bind_one(self, cur_moniker.clone()).await?;
            }
            Ok(realm)
        }
        .boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::{
            builtin_environment::BuiltinEnvironment,
            model::testing::{mocks::*, test_helpers::*, test_hook::TestHook},
            model::{
                hooks::HooksRegistration,
                model::{ComponentManagerConfig, ModelParams},
                moniker::PartialMoniker,
                resolver::ResolverRegistry,
            },
            startup,
        },
        std::collections::HashSet,
    };

    async fn new_model(
        mock_resolver: MockResolver,
        mock_runner: Arc<MockRunner>,
    ) -> (Arc<Model>, BuiltinEnvironment) {
        new_model_with(mock_resolver, mock_runner.clone(), vec![]).await
    }

    async fn new_model_with(
        mock_resolver: MockResolver,
        mock_runner: Arc<MockRunner>,
        additional_hooks: Vec<HooksRegistration>,
    ) -> (Arc<Model>, BuiltinEnvironment) {
        let mut resolver = ResolverRegistry::new();
        resolver.register("test".to_string(), Box::new(mock_resolver));
        let startup_args = startup::Arguments {
            use_builtin_process_launcher: false,
            use_builtin_vmex: false,
            root_component_url: "".to_string(),
            debug: false,
        };
        let model = Arc::new(Model::new(ModelParams {
            root_component_url: "test:///root".to_string(),
            root_resolver_registry: resolver,
            elf_runner: mock_runner.clone(),
            builtin_runners: vec![(TEST_RUNNER_NAME.into(), mock_runner.clone() as _)]
                .into_iter()
                .collect(),
        }));
        let builtin_environment =
            BuiltinEnvironment::new(&startup_args, &model, ComponentManagerConfig::default())
                .await
                .expect("builtin environment setup failed");
        model.root_realm.hooks.install(additional_hooks).await;
        (model, builtin_environment)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_root() {
        let mock_runner = Arc::new(MockRunner::new());
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component("root", component_decl_with_test_runner());
        let (model, _builtin_environment) = new_model(mock_resolver, mock_runner.clone()).await;
        let m: AbsoluteMoniker = AbsoluteMoniker::root();
        let res = model.bind(&m).await;
        assert!(res.is_ok());
        let actual_urls = mock_runner.urls_run();
        let expected_urls = vec!["test:///root_resolved".to_string()];
        assert_eq!(actual_urls, expected_urls);
        let actual_children = get_live_children(&model.root_realm).await;
        assert!(actual_children.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_root_non_existent() {
        let mock_runner = Arc::new(MockRunner::new());
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component("root", component_decl_with_test_runner());
        let (model, _builtin_environment) = new_model(mock_resolver, mock_runner.clone()).await;
        let m: AbsoluteMoniker = vec!["no-such-instance:0"].into();
        let res = model.bind(&m).await;
        let expected_res: Result<Arc<Realm>, ModelError> =
            Err(ModelError::instance_not_found(vec!["no-such-instance:0"].into()));
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_urls = mock_runner.urls_run();
        let expected_urls: Vec<String> = vec!["test:///root_resolved".to_string()];
        assert_eq!(actual_urls, expected_urls);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_parent_then_child() {
        let mock_runner = Arc::new(MockRunner::new());
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDeclBuilder::new()
                .add_lazy_child("system")
                .add_lazy_child("echo")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component("system", component_decl_with_test_runner());
        mock_resolver.add_component("echo", component_decl_with_test_runner());
        let hook = Arc::new(TestHook::new());
        let (model, _builtin_environment) =
            new_model_with(mock_resolver, mock_runner.clone(), hook.hooks()).await;
        // bind to system
        let m: AbsoluteMoniker = vec!["system:0"].into();
        assert!(model.bind(&m).await.is_ok());
        let expected_urls =
            vec!["test:///root_resolved".to_string(), "test:///system_resolved".to_string()];
        assert_eq!(mock_runner.urls_run(), expected_urls);

        // Validate children. system is resolved, but not echo.
        let actual_children = get_live_children(&*model.root_realm).await;
        let mut expected_children: HashSet<PartialMoniker> = HashSet::new();
        expected_children.insert("system".into());
        expected_children.insert("echo".into());
        assert_eq!(actual_children, expected_children);

        let system_realm = get_live_child(&*model.root_realm, "system").await;
        let echo_realm = get_live_child(&*model.root_realm, "echo").await;
        let actual_children = get_live_children(&*system_realm).await;
        assert!(actual_children.is_empty());
        assert!(echo_realm.lock_state().await.is_none());
        // bind to echo
        let m: AbsoluteMoniker = vec!["echo:0"].into();
        assert!(model.bind(&m).await.is_ok());
        let expected_urls = vec![
            "test:///root_resolved".to_string(),
            "test:///system_resolved".to_string(),
            "test:///echo_resolved".to_string(),
        ];
        assert_eq!(mock_runner.urls_run(), expected_urls);

        // Validate children. Now echo is resolved.
        let echo_realm = get_live_child(&*model.root_realm, "echo").await;
        let actual_children = get_live_children(&*echo_realm).await;
        assert!(actual_children.is_empty());

        // Verify that the component topology matches expectations.
        assert_eq!("(echo,system)", hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_child_binds_parent() {
        let mock_runner = Arc::new(MockRunner::new());
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDeclBuilder::new()
                .add_lazy_child("system")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component(
            "system",
            ComponentDeclBuilder::new()
                .add_lazy_child("logger")
                .add_lazy_child("netstack")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component("logger", component_decl_with_test_runner());
        mock_resolver.add_component("netstack", component_decl_with_test_runner());
        let hook = Arc::new(TestHook::new());
        let (model, _builtin_environment) =
            new_model_with(mock_resolver, mock_runner.clone(), hook.hooks()).await;

        // Bind to logger (before ever binding to system). Ancestors are bound first.
        let m: AbsoluteMoniker = vec!["system:0", "logger:0"].into();
        assert!(model.bind(&m).await.is_ok());
        let expected_urls = vec![
            "test:///root_resolved".to_string(),
            "test:///system_resolved".to_string(),
            "test:///logger_resolved".to_string(),
        ];
        assert_eq!(mock_runner.urls_run(), expected_urls);

        // Bind to netstack.
        let m: AbsoluteMoniker = vec!["system:0", "netstack:0"].into();
        assert!(model.bind(&m).await.is_ok());
        let expected_urls = vec![
            "test:///root_resolved".to_string(),
            "test:///system_resolved".to_string(),
            "test:///logger_resolved".to_string(),
            "test:///netstack_resolved".to_string(),
        ];
        assert_eq!(mock_runner.urls_run(), expected_urls);

        // finally, bind to system. Was already bound, so no new results.
        let m: AbsoluteMoniker = vec!["system:0"].into();
        assert!(model.bind(&m).await.is_ok());
        let expected_urls = vec![
            "test:///root_resolved".to_string(),
            "test:///system_resolved".to_string(),
            "test:///logger_resolved".to_string(),
            "test:///netstack_resolved".to_string(),
        ];
        assert_eq!(mock_runner.urls_run(), expected_urls);

        // validate the component topology.
        assert_eq!("(system(logger,netstack))", hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_child_non_existent() {
        let mock_runner = Arc::new(MockRunner::new());
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDeclBuilder::new()
                .add_lazy_child("system")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component("system", component_decl_with_test_runner());
        let (model, _builtin_environment) = new_model(mock_resolver, mock_runner.clone()).await;
        // bind to system
        let m: AbsoluteMoniker = vec!["system:0"].into();
        assert!(model.bind(&m).await.is_ok());
        let expected_urls =
            vec!["test:///root_resolved".to_string(), "test:///system_resolved".to_string()];
        assert_eq!(mock_runner.urls_run(), expected_urls);

        // can't bind to logger: it does not exist
        let m: AbsoluteMoniker = vec!["system:0", "logger:0"].into();
        let res = model.bind(&m).await;
        let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(m));
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_urls = mock_runner.urls_run();
        let expected_urls =
            vec!["test:///root_resolved".to_string(), "test:///system_resolved".to_string()];
        assert_eq!(actual_urls, expected_urls);
    }

    /// Create a hierarchy of children:
    ///
    ///   a
    ///  / \
    /// b   c
    ///      \
    ///       d
    ///        \
    ///         e
    ///
    /// `b`, `c`, and `d` are started eagerly. `a` and `e` are lazy.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_eager_children() {
        let mock_runner = Arc::new(MockRunner::new());
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDeclBuilder::new()
                .add_lazy_child("a")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component(
            "a",
            ComponentDeclBuilder::new()
                .add_eager_child("b")
                .add_eager_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component("b", component_decl_with_test_runner());
        mock_resolver.add_component(
            "c",
            ComponentDeclBuilder::new()
                .add_eager_child("d")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component(
            "d",
            ComponentDeclBuilder::new()
                .add_lazy_child("e")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component("e", component_decl_with_test_runner());
        let hook = Arc::new(TestHook::new());
        let (model, _builtin_environment) =
            new_model_with(mock_resolver, mock_runner.clone(), hook.hooks()).await;

        // Bind to the top component, and check that it and the eager components were started.
        {
            let m = AbsoluteMoniker::new(vec!["a:0".into()]);
            let res = model.bind(&m).await;
            assert!(res.is_ok());
            let actual_urls = mock_runner.urls_run();
            // Execution order of `b` and `c` is non-deterministic.
            let expected_urls1 = vec![
                "test:///root_resolved".to_string(),
                "test:///a_resolved".to_string(),
                "test:///b_resolved".to_string(),
                "test:///c_resolved".to_string(),
                "test:///d_resolved".to_string(),
            ];
            let expected_urls2 = vec![
                "test:///root_resolved".to_string(),
                "test:///a_resolved".to_string(),
                "test:///c_resolved".to_string(),
                "test:///b_resolved".to_string(),
                "test:///d_resolved".to_string(),
            ];
            assert!(
                actual_urls == expected_urls1 || actual_urls == expected_urls2,
                "urls_run failed to match: {:?}",
                actual_urls
            );
        }
        // Verify that the component topology matches expectations.
        assert_eq!("(a(b,c(d(e))))", hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_no_execute() {
        // Create a non-executable component with an eagerly-started child.
        let mock_runner = Arc::new(MockRunner::new());
        let mut mock_resolver = MockResolver::new();
        mock_resolver.add_component(
            "root",
            ComponentDeclBuilder::new()
                .add_lazy_child("a")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component(
            "a",
            ComponentDeclBuilder::new_empty_component()
                .add_eager_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        );
        mock_resolver.add_component("b", component_decl_with_test_runner());
        let (model, _builtin_environment) = new_model(mock_resolver, mock_runner.clone()).await;

        // Bind to the parent component. The child should be started. However, the parent component
        // is non-executable so it is not run.
        let m: AbsoluteMoniker = vec!["a:0"].into();
        assert!(model.bind(&m).await.is_ok());
        let actual_urls = mock_runner.urls_run();
        let expected_urls =
            vec!["test:///root_resolved".to_string(), "test:///b_resolved".to_string()];
        assert_eq!(actual_urls, expected_urls);
    }
}
