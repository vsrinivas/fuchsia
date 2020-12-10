// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{start, Action, ActionSet},
        error::ModelError,
        model::Model,
        realm::{BindReason, Realm},
    },
    async_trait::async_trait,
    fidl_fuchsia_sys2 as fsys,
    futures::future::{join_all, BoxFuture},
    moniker::AbsoluteMoniker,
    std::sync::{Arc, Weak},
};

/// A trait to enable support for different `bind()` implementations. This is used,
/// for example, for testing code that depends on `bind()`, but no other `Model`
/// functionality.
#[async_trait]
pub trait Binder: Send + Sync {
    async fn bind<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
        reason: &'a BindReason,
    ) -> Result<Arc<Realm>, ModelError>;
}

#[async_trait]
impl Binder for Arc<Model> {
    /// Binds to the component instance with the specified moniker. This has the following effects:
    /// - Binds to the parent instance.
    /// - Starts the component instance, if it is not already running and not shut down.
    /// - Binds to any descendant component instances that need to be eagerly started.
    // TODO: This function starts the parent component, but doesn't track the bindings anywhere.
    // This means that when the child stops and the parent has no other reason to run, we won't
    // stop the parent. To solve this, we need to track the bindings.
    async fn bind<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
        reason: &'a BindReason,
    ) -> Result<Arc<Realm>, ModelError> {
        bind_at_moniker(self, abs_moniker, reason).await
    }
}

#[async_trait]
impl Binder for Weak<Model> {
    async fn bind<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
        reason: &'a BindReason,
    ) -> Result<Arc<Realm>, ModelError> {
        if let Some(model) = self.upgrade() {
            model.bind(abs_moniker, reason).await
        } else {
            Err(ModelError::ModelNotAvailable)
        }
    }
}

/// Binds to the component instance in the given realm, starting it if it's not already running.
/// Returns the realm that was bound to.
pub async fn bind_at_moniker<'a>(
    model: &'a Arc<Model>,
    abs_moniker: &'a AbsoluteMoniker,
    reason: &BindReason,
) -> Result<Arc<Realm>, ModelError> {
    let mut cur_moniker = AbsoluteMoniker::root();
    let mut realm = model.root_realm.clone();
    bind_at(realm.clone(), reason).await?;
    for m in abs_moniker.path().iter() {
        cur_moniker = cur_moniker.child(m.clone());
        realm = model.look_up_realm(&cur_moniker).await?;
        bind_at(realm.clone(), reason).await?;
    }
    Ok(realm)
}

/// Binds to the component instance in the given realm, starting it if it's not already
/// running.
pub async fn bind_at(realm: Arc<Realm>, reason: &BindReason) -> Result<(), ModelError> {
    // Skip starting a component instance that was already started.
    // Eager binding can cause `bind_at` to be re-entrant. It's important to bail out here so
    // we don't end up in an infinite loop of binding to the same eager child.
    {
        let execution = realm.lock_execution().await;
        if let Some(res) = start::should_return_early(&execution, &realm.abs_moniker) {
            return res;
        }
    }
    ActionSet::register(realm.clone(), Action::Start(reason.clone())).await?;

    let eager_children: Vec<_> = {
        let mut state = realm.lock_state().await;
        let state = state.as_mut().expect("bind_single_instance: not resolved");
        state
            .live_child_realms()
            .filter_map(|(_, r)| match r.startup {
                fsys::StartupMode::Eager => Some(r.clone()),
                fsys::StartupMode::Lazy => None,
            })
            .collect()
    };
    bind_eager_children_recursive(eager_children).await.or_else(|e| match e {
        ModelError::InstanceShutDown { .. } => Ok(()),
        _ => Err(e),
    })?;
    Ok(())
}

/// Binds to a list of instances, and any eager children they may return.
// This function recursive calls `bind_at`, so it returns a BoxFutuer,
fn bind_eager_children_recursive<'a>(
    instances_to_bind: Vec<Arc<Realm>>,
) -> BoxFuture<'a, Result<(), ModelError>> {
    let f = async move {
        let futures: Vec<_> = instances_to_bind
            .iter()
            .map(|realm| async move { bind_at(realm.clone(), &BindReason::Eager).await })
            .collect();
        join_all(futures).await.into_iter().fold(Ok(()), |acc, r| acc.and_then(|_| r))?;
        Ok(())
    };
    Box::pin(f)
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::{
            builtin_environment::BuiltinEnvironment,
            config::RuntimeConfig,
            model::{
                actions::{Action, ActionSet},
                events::event::SyncMode,
                hooks::{EventPayload, EventType, HooksRegistration},
                testing::{mocks::*, out_dir::OutDir, test_helpers::*, test_hook::TestHook},
            },
        },
        cm_rust::{
            CapabilityPath, ComponentDecl, RegistrationSource, RunnerDecl, RunnerRegistration,
            RunnerSource,
        },
        fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync,
        futures::prelude::*,
        matches::assert_matches,
        moniker::PartialMoniker,
        std::{collections::HashSet, convert::TryFrom},
    };

    async fn new_model(
        components: Vec<(&'static str, ComponentDecl)>,
    ) -> (Arc<Model>, Arc<BuiltinEnvironment>, Arc<MockRunner>) {
        new_model_with(components, vec![]).await
    }

    async fn new_model_with(
        components: Vec<(&'static str, ComponentDecl)>,
        additional_hooks: Vec<HooksRegistration>,
    ) -> (Arc<Model>, Arc<BuiltinEnvironment>, Arc<MockRunner>) {
        let TestModelResult { model, builtin_environment, mock_runner, .. } =
            new_test_model("root", components, RuntimeConfig::default()).await;
        model.root_realm.hooks.install(additional_hooks).await;
        (model, builtin_environment, mock_runner)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_root() {
        let (model, _builtin_environment, mock_runner) =
            new_model(vec![("root", component_decl_with_test_runner())]).await;
        let m: AbsoluteMoniker = AbsoluteMoniker::root();
        let res = model.bind(&m, &BindReason::Root).await;
        assert!(res.is_ok());
        mock_runner.wait_for_url("test:///root_resolved").await;
        let actual_children = get_live_children(&model.root_realm).await;
        assert!(actual_children.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_root_non_existent() {
        let (model, _builtin_environment, mock_runner) =
            new_model(vec![("root", component_decl_with_test_runner())]).await;
        let m: AbsoluteMoniker = vec!["no-such-instance:0"].into();
        let res = model.bind(&m, &BindReason::Root).await;
        let expected_res: Result<Arc<Realm>, ModelError> =
            Err(ModelError::instance_not_found(vec!["no-such-instance:0"].into()));
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        mock_runner.wait_for_url("test:///root_resolved").await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_concurrent() {
        // Test binding twice concurrently to the same component. The component should only be
        // started once.

        let (model, builtin_environment, mock_runner) = new_model(vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
            ("system", component_decl_with_test_runner()),
        ])
        .await;

        let events = vec![EventType::Started.into()];
        let mut event_source = builtin_environment
            .event_source_factory
            .create_for_debug(SyncMode::Sync)
            .await
            .expect("create event source");

        let mut event_stream =
            event_source.subscribe(events.clone()).await.expect("subscribe to event stream");
        event_source.start_component_tree().await;

        // Bind to "system", pausing before it starts.
        let model_copy = model.clone();
        let (f, bind_handle) = async move {
            let m: AbsoluteMoniker = vec!["system:0"].into();
            model_copy.bind(&m, &BindReason::Root).await.expect("failed to bind 1");
        }
        .remote_handle();
        fasync::Task::spawn(f).detach();
        let event = event_stream.wait_until(EventType::Started, vec![].into()).await.unwrap();
        event.resume();
        let event =
            event_stream.wait_until(EventType::Started, vec!["system:0"].into()).await.unwrap();
        // Verify that the correct BindReason propagates to the event.
        assert_matches!(
            event.event.result,
            Ok(EventPayload::Started { bind_reason: BindReason::Root, .. })
        );
        mock_runner.wait_for_url("test:///root_resolved").await;

        // While the bind() is paused, simulate a second bind by explicitly scheduling a Start
        // action. Allow the original bind to proceed, then check the result of both bindings.
        let m: AbsoluteMoniker = vec!["system:0"].into();
        let realm = model.look_up_realm(&m).await.expect("failed realm lookup");
        let f = ActionSet::register(realm, Action::Start(BindReason::Eager));
        let (f, action_handle) = f.remote_handle();
        fasync::Task::spawn(f).detach();
        event.resume();
        bind_handle.await;
        action_handle.await.expect("failed to bind 2");

        // Verify that the component was started only once.
        mock_runner.wait_for_urls(&["test:///root_resolved", "test:///system_resolved"]).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_parent_then_child() {
        let hook = Arc::new(TestHook::new());
        let (model, _builtin_environment, mock_runner) = new_model_with(
            vec![
                (
                    "root",
                    ComponentDeclBuilder::new()
                        .add_lazy_child("system")
                        .add_lazy_child("echo")
                        .build(),
                ),
                ("system", component_decl_with_test_runner()),
                ("echo", component_decl_with_test_runner()),
            ],
            hook.hooks(),
        )
        .await;
        // bind to system
        let m: AbsoluteMoniker = vec!["system:0"].into();
        assert!(model.bind(&m, &BindReason::Root).await.is_ok());
        mock_runner.wait_for_urls(&["test:///root_resolved", "test:///system_resolved"]).await;

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
        assert!(model.bind(&m, &BindReason::Root).await.is_ok());
        mock_runner
            .wait_for_urls(&[
                "test:///root_resolved",
                "test:///system_resolved",
                "test:///echo_resolved",
            ])
            .await;

        // Validate children. Now echo is resolved.
        let echo_realm = get_live_child(&*model.root_realm, "echo").await;
        let actual_children = get_live_children(&*echo_realm).await;
        assert!(actual_children.is_empty());

        // Verify that the component topology matches expectations.
        assert_eq!("(echo,system)", hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_child_binds_parent() {
        let hook = Arc::new(TestHook::new());
        let (model, _builtin_environment, mock_runner) = new_model_with(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
                (
                    "system",
                    ComponentDeclBuilder::new()
                        .add_lazy_child("logger")
                        .add_lazy_child("netstack")
                        .build(),
                ),
                ("logger", component_decl_with_test_runner()),
                ("netstack", component_decl_with_test_runner()),
            ],
            hook.hooks(),
        )
        .await;

        // Bind to logger (before ever binding to system). Ancestors are bound first.
        let m: AbsoluteMoniker = vec!["system:0", "logger:0"].into();
        assert!(model.bind(&m, &BindReason::Root).await.is_ok());
        mock_runner
            .wait_for_urls(&[
                "test:///root_resolved",
                "test:///system_resolved",
                "test:///logger_resolved",
            ])
            .await;

        // Bind to netstack.
        let m: AbsoluteMoniker = vec!["system:0", "netstack:0"].into();
        assert!(model.bind(&m, &BindReason::Root).await.is_ok());
        mock_runner
            .wait_for_urls(&[
                "test:///root_resolved",
                "test:///system_resolved",
                "test:///logger_resolved",
                "test:///netstack_resolved",
            ])
            .await;

        // finally, bind to system. Was already bound, so no new results.
        let m: AbsoluteMoniker = vec!["system:0"].into();
        assert!(model.bind(&m, &BindReason::Root).await.is_ok());
        mock_runner
            .wait_for_urls(&[
                "test:///root_resolved",
                "test:///system_resolved",
                "test:///logger_resolved",
                "test:///netstack_resolved",
            ])
            .await;

        // validate the component topology.
        assert_eq!("(system(logger,netstack))", hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_child_non_existent() {
        let (model, _builtin_environment, mock_runner) = new_model(vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
            ("system", component_decl_with_test_runner()),
        ])
        .await;
        // bind to system
        let m: AbsoluteMoniker = vec!["system:0"].into();
        assert!(model.bind(&m, &BindReason::Root).await.is_ok());
        mock_runner.wait_for_urls(&["test:///root_resolved", "test:///system_resolved"]).await;

        // can't bind to logger: it does not exist
        let m: AbsoluteMoniker = vec!["system:0", "logger:0"].into();
        let res = model.bind(&m, &BindReason::Root).await;
        let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(m));
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        mock_runner.wait_for_urls(&["test:///root_resolved", "test:///system_resolved"]).await;
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
        let hook = Arc::new(TestHook::new());
        let (model, _builtin_environment, mock_runner) = new_model_with(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
                (
                    "a",
                    ComponentDeclBuilder::new().add_eager_child("b").add_eager_child("c").build(),
                ),
                ("b", component_decl_with_test_runner()),
                ("c", ComponentDeclBuilder::new().add_eager_child("d").build()),
                ("d", ComponentDeclBuilder::new().add_lazy_child("e").build()),
                ("e", component_decl_with_test_runner()),
            ],
            hook.hooks(),
        )
        .await;

        // Bind to the top component, and check that it and the eager components were started.
        {
            let m = AbsoluteMoniker::new(vec!["a:0".into()]);
            let res = model.bind(&m, &BindReason::Root).await;
            assert!(res.is_ok());
            mock_runner
                .wait_for_urls(&[
                    "test:///root_resolved",
                    "test:///a_resolved",
                    "test:///b_resolved",
                    "test:///c_resolved",
                    "test:///d_resolved",
                ])
                .await;
        }
        // Verify that the component topology matches expectations.
        assert_eq!("(a(b,c(d(e))))", hook.print());
    }

    /// `b` is an eager child of `a` that uses a runner provided by `a`. In the process of binding
    /// to `a`, `b` will be eagerly started, which requires re-binding to `a`. This should work
    /// without causing reentrance issues.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_eager_children_reentrant() {
        let hook = Arc::new(TestHook::new());
        let (model, _builtin_environment, mock_runner) = new_model_with(
            vec![
                ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
                (
                    "a",
                    ComponentDeclBuilder::new()
                        .add_child(
                            ChildDeclBuilder::new()
                                .name("b")
                                .url("test:///b")
                                .startup(fsys::StartupMode::Eager)
                                .environment("env")
                                .build(),
                        )
                        .runner(RunnerDecl {
                            name: "foo".into(),
                            source: RunnerSource::Self_,
                            source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                        })
                        .add_environment(
                            EnvironmentDeclBuilder::new()
                                .extends(fsys::EnvironmentExtends::Realm)
                                .name("env")
                                .add_runner(RunnerRegistration {
                                    source_name: "foo".into(),
                                    source: RegistrationSource::Self_,
                                    target_name: "foo".into(),
                                })
                                .build(),
                        )
                        .build(),
                ),
                ("b", ComponentDeclBuilder::new_empty_component().use_runner("foo").build()),
            ],
            hook.hooks(),
        )
        .await;

        // Set up the runner.
        let (runner_service, mut receiver) =
            create_service_directory_entry::<fcrunner::ComponentRunnerMarker>();
        let mut out_dir = OutDir::new();
        out_dir.add_entry(CapabilityPath::try_from("/svc/runner").unwrap(), runner_service);
        mock_runner.add_host_fn("test:///a_resolved", out_dir.host_fn());

        // Bind to the top component, and check that it and the eager components were started.
        {
            let (f, bind_handle) = async move {
                let m = AbsoluteMoniker::new(vec!["a:0".into()]);
                model.bind(&m, &BindReason::Root).await
            }
            .remote_handle();
            fasync::Task::spawn(f).detach();
            // `b` uses the runner offered by `a`.
            assert_eq!(
                wait_for_runner_request(&mut receiver).await.resolved_url,
                Some("test:///b_resolved".to_string())
            );
            bind_handle.await.expect("bind to `a` failed");
            // `root` and `a` use the test runner.
            mock_runner.wait_for_urls(&["test:///root_resolved", "test:///a_resolved"]).await;
        }
        // Verify that the component topology matches expectations.
        assert_eq!("(a(b))", hook.print());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_no_execute() {
        // Create a non-executable component with an eagerly-started child.
        let (model, _builtin_environment, mock_runner) = new_model(vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new_empty_component().add_eager_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ])
        .await;

        // Bind to the parent component. The child should be started. However, the parent component
        // is non-executable so it is not run.
        let m: AbsoluteMoniker = vec!["a:0"].into();
        assert!(model.bind(&m, &BindReason::Root).await.is_ok());
        mock_runner.wait_for_urls(&["test:///root_resolved", "test:///b_resolved"]).await;
    }
}
