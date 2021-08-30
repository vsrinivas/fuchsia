// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{start, ActionSet, StartAction},
        component::{BindReason, ComponentInstance, InstanceState},
        error::ModelError,
        model::Model,
    },
    async_trait::async_trait,
    fidl_fuchsia_sys2 as fsys,
    futures::future::{join_all, BoxFuture},
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase, PartialAbsoluteMoniker},
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
    ) -> Result<Arc<ComponentInstance>, ModelError>;
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
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        bind_at_moniker(self, abs_moniker, reason).await
    }
}

#[async_trait]
impl Binder for Weak<Model> {
    async fn bind<'a>(
        &'a self,
        abs_moniker: &'a AbsoluteMoniker,
        reason: &'a BindReason,
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        if let Some(model) = self.upgrade() {
            model.bind(abs_moniker, reason).await
        } else {
            Err(ModelError::ModelNotAvailable)
        }
    }
}

/// Binds to the component instance in the given component, starting it if it's not already running.
/// Returns the component that was bound to.
pub async fn bind_at_moniker<'a>(
    model: &'a Arc<Model>,
    abs_moniker: &'a AbsoluteMoniker,
    reason: &BindReason,
) -> Result<Arc<ComponentInstance>, ModelError> {
    let mut cur_moniker = PartialAbsoluteMoniker::root();
    let mut component = model.root().clone();
    bind_at(component.clone(), reason).await?;
    for m in abs_moniker.path().iter() {
        cur_moniker = cur_moniker.child(m.to_partial());
        component = model.look_up(&cur_moniker).await?;
        bind_at(component.clone(), reason).await?;
    }
    Ok(component)
}

/// Binds to the component instance in the given component, starting it if it's not already
/// running.
pub async fn bind_at(
    component: Arc<ComponentInstance>,
    reason: &BindReason,
) -> Result<(), ModelError> {
    // Skip starting a component instance that was already started.
    // Eager binding can cause `bind_at` to be re-entrant. It's important to bail out here so
    // we don't end up in an infinite loop of binding to the same eager child.
    {
        let state = component.lock_state().await;
        let execution = component.lock_execution().await;
        if let Some(res) = start::should_return_early(&state, &execution, &component.abs_moniker) {
            return res;
        }
    }
    ActionSet::register(component.clone(), StartAction::new(reason.clone())).await?;

    let eager_children: Vec<_> = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => s
                .live_children()
                .filter_map(|(_, r)| match r.startup {
                    fsys::StartupMode::Eager => Some(r.clone()),
                    fsys::StartupMode::Lazy => None,
                })
                .collect(),
            InstanceState::Purged => {
                return Err(ModelError::instance_not_found(component.abs_moniker.to_partial()));
            }
            InstanceState::New | InstanceState::Discovered => {
                panic!("bind_at: not resoled")
            }
        }
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
    instances_to_bind: Vec<Arc<ComponentInstance>>,
) -> BoxFuture<'a, Result<(), ModelError>> {
    let f = async move {
        let futures: Vec<_> = instances_to_bind
            .iter()
            .map(|component| async move { bind_at(component.clone(), &BindReason::Eager).await })
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
            model::{
                actions::{ActionKey, ActionSet},
                events::registry::EventSubscription,
                hooks::{EventPayload, EventType, HooksRegistration},
                testing::{mocks::*, out_dir::OutDir, test_helpers::*, test_hook::TestHook},
            },
        },
        cm_rust::{
            CapabilityPath, ComponentDecl, EventMode, RegistrationSource, RunnerDecl,
            RunnerRegistration,
        },
        cm_rust_testing::*,
        fidl_fuchsia_component_runner as fcrunner, fuchsia_async as fasync,
        futures::{join, lock::Mutex, prelude::*},
        matches::assert_matches,
        moniker::PartialChildMoniker,
        std::{collections::HashSet, convert::TryFrom},
    };

    async fn new_model(
        components: Vec<(&'static str, ComponentDecl)>,
    ) -> (Arc<Model>, Arc<Mutex<BuiltinEnvironment>>, Arc<MockRunner>) {
        new_model_with(components, vec![]).await
    }

    async fn new_model_with(
        components: Vec<(&'static str, ComponentDecl)>,
        additional_hooks: Vec<HooksRegistration>,
    ) -> (Arc<Model>, Arc<Mutex<BuiltinEnvironment>>, Arc<MockRunner>) {
        let TestModelResult { model, builtin_environment, mock_runner, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;
        model.root().hooks.install(additional_hooks).await;
        (model, builtin_environment, mock_runner)
    }

    #[fuchsia::test]
    async fn bind_root() {
        let (model, _builtin_environment, mock_runner) =
            new_model(vec![("root", component_decl_with_test_runner())]).await;
        let m: AbsoluteMoniker = AbsoluteMoniker::root();
        let res = model.bind(&m, &BindReason::Root).await;
        assert!(res.is_ok());
        mock_runner.wait_for_url("test:///root_resolved").await;
        let actual_children = get_live_children(&model.root()).await;
        assert!(actual_children.is_empty());
    }

    #[fuchsia::test]
    async fn bind_root_non_existent() {
        let (model, _builtin_environment, mock_runner) =
            new_model(vec![("root", component_decl_with_test_runner())]).await;
        let m: AbsoluteMoniker = vec!["no-such-instance:0"].into();
        let res = model.bind(&m, &BindReason::Root).await;
        let expected_res: Result<Arc<ComponentInstance>, ModelError> =
            Err(ModelError::instance_not_found(vec!["no-such-instance"].into()));
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        mock_runner.wait_for_url("test:///root_resolved").await;
    }

    #[fuchsia::test]
    async fn bind_concurrent() {
        // Test binding twice concurrently to the same component. The component should only be
        // started once.

        let (model, builtin_environment, mock_runner) = new_model(vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
            ("system", component_decl_with_test_runner()),
        ])
        .await;

        let events = vec![EventSubscription::new(EventType::Started.into(), EventMode::Sync)];
        let mut event_source = builtin_environment
            .lock()
            .await
            .event_source_factory
            .create_for_debug()
            .await
            .expect("create event source");

        let mut event_stream =
            event_source.subscribe(events).await.expect("subscribe to event stream");
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
        let m: PartialAbsoluteMoniker = vec!["system"].into();
        let component = model.look_up(&m).await.expect("failed component lookup");
        let f = ActionSet::register(component, StartAction::new(BindReason::Eager));
        let (f, action_handle) = f.remote_handle();
        fasync::Task::spawn(f).detach();
        event.resume();
        bind_handle.await;
        action_handle.await.expect("failed to bind 2");

        // Verify that the component was started only once.
        mock_runner.wait_for_urls(&["test:///root_resolved", "test:///system_resolved"]).await;
    }

    #[fuchsia::test]
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
        let actual_children = get_live_children(&*model.root()).await;
        let mut expected_children: HashSet<PartialChildMoniker> = HashSet::new();
        expected_children.insert("system".into());
        expected_children.insert("echo".into());
        assert_eq!(actual_children, expected_children);

        let system_component = get_live_child(&*model.root(), "system").await;
        let echo_component = get_live_child(&*model.root(), "echo").await;
        let actual_children = get_live_children(&*system_component).await;
        assert!(actual_children.is_empty());
        assert_matches!(
            *echo_component.lock_state().await,
            InstanceState::New | InstanceState::Discovered
        );
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
        let echo_component = get_live_child(&*model.root(), "echo").await;
        let actual_children = get_live_children(&*echo_component).await;
        assert!(actual_children.is_empty());

        // Verify that the component topology matches expectations.
        assert_eq!("(echo,system)", hook.print());
    }

    #[fuchsia::test]
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

    #[fuchsia::test]
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
        let expected_res: Result<(), ModelError> =
            Err(ModelError::instance_not_found(m.to_partial()));
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
    #[fuchsia::test]
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
    #[fuchsia::test]
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
                            source_path: Some(CapabilityPath::try_from("/svc/runner").unwrap()),
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
                ("b", ComponentDeclBuilder::new_empty_component().add_program("foo").build()),
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

    #[fuchsia::test]
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

    #[fuchsia::test]
    async fn bind_action_sequence() {
        // Test that binding registers the expected actions in the expected sequence
        // (Discover -> Resolve -> Start).

        // Set up the tree.
        let (model, builtin_environment, _mock_runner) = new_model(vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("system").build()),
            ("system", component_decl_with_test_runner()),
        ])
        .await;
        let events = vec![
            EventType::Discovered.into(),
            EventType::Resolved.into(),
            EventType::Started.into(),
        ];
        let mut event_source = builtin_environment
            .lock()
            .await
            .event_source_factory
            .create_for_debug()
            .await
            .expect("create event source");
        let mut event_stream = event_source
            .subscribe(
                events
                    .into_iter()
                    .map(|event| EventSubscription::new(event, EventMode::Sync))
                    .collect(),
            )
            .await
            .expect("subscribe to event stream");
        event_source.start_component_tree().await;

        // Child of root should start out discovered but not resolved yet.
        let m = AbsoluteMoniker::new(vec!["system:0".into()]);
        let start_model = model.start();
        let check_events = async {
            let event = event_stream.wait_until(EventType::Discovered, m.clone()).await.unwrap();
            {
                let root_state = model.root().lock_state().await;
                let root_state = match *root_state {
                    InstanceState::Resolved(ref s) => s,
                    _ => panic!("not resolved"),
                };
                let realm = root_state.get_child(&"system:0".into()).unwrap();
                let actions = realm.lock_actions().await;
                assert!(actions.contains(&ActionKey::Discover));
                assert!(!actions.contains(&ActionKey::Resolve));
            }
            event.resume();
            let event = event_stream.wait_until(EventType::Started, vec![].into()).await.unwrap();
            event.resume();
        };
        join!(start_model, check_events);

        // Bind to child and check that it gets resolved, with a Resolve event and action.
        let bind = async {
            model.bind(&m, &BindReason::Root).await.unwrap();
        };
        let check_events = async {
            let event = event_stream.wait_until(EventType::Resolved, m.clone()).await.unwrap();
            // While the Resolved hook is handled, it should be possible to look up the component
            // without deadlocking.
            let component = model.look_up(&m.to_partial()).await.unwrap();
            {
                let actions = component.lock_actions().await;
                assert!(actions.contains(&ActionKey::Resolve));
                assert!(!actions.contains(&ActionKey::Discover));
            }
            event.resume();

            // Check that the child is started, with a Start event and action.
            let event = event_stream.wait_until(EventType::Started, m.clone()).await.unwrap();
            {
                let actions = component.lock_actions().await;
                assert!(actions.contains(&ActionKey::Start));
                assert!(!actions.contains(&ActionKey::Discover));
                assert!(!actions.contains(&ActionKey::Resolve));
            }
            event.resume();
        };
        join!(bind, check_events);
    }
}
