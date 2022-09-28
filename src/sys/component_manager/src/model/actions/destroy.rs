// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{
            Action, ActionKey, ActionSet, DestroyChildAction, DiscoverAction, ResolveAction,
            ShutdownAction, StartAction,
        },
        component::{ComponentInstance, InstanceState, StartReason},
        error::ModelError,
    },
    async_trait::async_trait,
    futures::{
        future::{join_all, BoxFuture},
        Future,
    },
    std::sync::Arc,
};

/// Destroy this component instance, including all instances nested in its component.
pub struct DestroyAction {}

impl DestroyAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for DestroyAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_destroy(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Destroy
    }
}

async fn do_destroy(component: &Arc<ComponentInstance>) -> Result<(), ModelError> {
    // Do nothing if already destroyed.
    {
        if let InstanceState::Destroyed = *component.lock_state().await {
            return Ok(());
        }
    }

    // Require the component to be discovered before deleting it so a Destroyed event is
    // always preceded by a Discovered.
    ActionSet::register(component.clone(), DiscoverAction::new()).await?;

    // For destruction to behave correctly, the component has to be shut down first.
    // NOTE: This will recursively shut down the whole subtree. If this component has children,
    // we'll call DestroyChild on them which in turn will call Shutdown on the child. Because
    // the parent's subtree was shutdown, this shutdown is a no-op.
    ActionSet::register(component.clone(), ShutdownAction::new()).await?;

    let nfs = {
        match *component.lock_state().await {
            InstanceState::Resolved(ref s) => {
                let mut nfs = vec![];
                for (m, c) in s.children() {
                    let component = component.clone();
                    let m = m.clone();
                    let incarnation = c.incarnation_id();
                    let nf = async move {
                        ActionSet::register(component, DestroyChildAction::new(m, incarnation))
                            .await
                    };
                    nfs.push(nf);
                }
                nfs
            }
            InstanceState::New | InstanceState::Unresolved | InstanceState::Destroyed => {
                // Component was never resolved. No explicit cleanup is required for children.
                vec![]
            }
        }
    };
    let results = join_all(nfs).await;
    ok_or_first_error(results)?;

    // Now that all children have been destroyed, destroy the parent.
    component.destroy_instance().await?;

    // Only consider the component fully destroyed once it's no longer executing any lifecycle
    // transitions.
    {
        let mut state = component.lock_state().await;
        state.set(InstanceState::Destroyed);
    }
    fn wait(nf: Option<impl Future + Send + 'static>) -> BoxFuture<'static, ()> {
        Box::pin(async {
            if let Some(nf) = nf {
                nf.await;
            }
        })
    }
    let nfs = {
        let actions = component.lock_actions().await;
        vec![
            wait(actions.wait(ResolveAction::new())),
            wait(actions.wait(StartAction::new(StartReason::Debug))),
        ]
        .into_iter()
    };
    join_all(nfs).await;

    Ok(())
}

fn ok_or_first_error(results: Vec<Result<(), ModelError>>) -> Result<(), ModelError> {
    results.into_iter().fold(Ok(()), |acc, r| acc.and_then(|_| r))
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::model::{
            actions::{
                test_utils::{is_child_deleted, is_destroyed, is_executing},
                ActionNotifier, DiscoverAction, ShutdownAction,
            },
            component::StartReason,
            events::{registry::EventSubscription, stream::EventStream},
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            starter::Starter,
            testing::{
                test_helpers::{
                    component_decl_with_test_runner, execution_is_shut_down, get_incarnation_id,
                    has_child, ActionsTest,
                },
                test_hook::Lifecycle,
            },
        },
        assert_matches::assert_matches,
        cm_rust::EventMode,
        cm_rust_testing::ComponentDeclBuilder,
        fidl_fuchsia_component_decl as fdecl, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{join, FutureExt},
        moniker::{AbsoluteMoniker, ChildMoniker},
        std::sync::atomic::Ordering,
        std::sync::Weak,
    };

    #[fuchsia::test]
    async fn destroy_one_component() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Start the component. This should cause the component to have an `Execution`.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);

        // Register shutdown first because DestroyChild requires the component to be shut down.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        // Register destroy child action, and wait for it. Component should be destroyed.
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a".into(), 0))
            .await
            .expect("destroy failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![Lifecycle::Stop(vec!["a"].into()), Lifecycle::Destroy(vec!["a"].into())],
            );
        }

        // Trying to start the component should fail because it's shut down.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Destroy the component again. This succeeds, but has no additional effect.
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a".into(), 0))
            .await
            .expect("destroy failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
    }

    #[fuchsia::test]
    async fn destroy_collection() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("container").build()),
            ("container", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Start the components. This should cause them to have an `Execution`.
        let component_root = test.look_up(vec![].into()).await;
        let component_container = test.look_up(vec!["container"].into()).await;
        let component_a = test.look_up(vec!["container", "coll:a"].into()).await;
        let component_b = test.look_up(vec!["container", "coll:b"].into()).await;
        test.model
            .start_instance(&component_container.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start container");
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start coll:a");
        test.model
            .start_instance(&component_b.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start coll:b");
        assert!(is_executing(&component_container).await);
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);

        // Register destroy child action, and wait for it. Components should be destroyed.
        let component_container = test.look_up(vec!["container"].into()).await;
        ActionSet::register(component_root.clone(), DestroyChildAction::new("container".into(), 0))
            .await
            .expect("destroy failed");
        assert!(is_child_deleted(&component_root, &component_container).await);
        assert!(is_destroyed(&component_container).await);
        assert!(is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
    }

    #[fuchsia::test]
    async fn destroy_already_shut_down() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&component_a.clone()).await);
        assert!(execution_is_shut_down(&component_b.clone()).await);

        // Now delete child "a". This should cause all components to be destroyed.
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a".into(), 0))
            .await
            .expect("destroy failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_destroyed(&component_a).await);

        // Check order of events.
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Destroy(vec!["a", "b"].into()),
                    Lifecycle::Destroy(vec!["a"].into()),
                ]
            );
        }
    }

    async fn setup_destroy_waits_test(event_types: Vec<EventType>) -> (ActionsTest, EventStream) {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let event_stream = setup_destroy_waits_test_event_stream(&test, event_types).await;
        (test, event_stream)
    }

    async fn setup_destroy_waits_test_event_stream(
        test: &ActionsTest,
        event_types: Vec<EventType>,
    ) -> EventStream {
        let events: Vec<_> = event_types.into_iter().map(|e| e.into()).collect();
        let mut event_source = test
            .builtin_environment
            .lock()
            .await
            .event_source_factory
            .create_for_debug()
            .await
            .expect("create event source");
        let event_stream = event_source
            .subscribe(
                events
                    .into_iter()
                    .map(|event| EventSubscription::new(event, EventMode::Sync))
                    .collect(),
            )
            .await
            .expect("subscribe to event stream");
        let model = test.model.clone();
        fasync::Task::spawn(async move { model.start().await }).detach();
        event_stream
    }

    async fn run_destroy_waits_test<A>(
        test: &ActionsTest,
        event_stream: &mut EventStream,
        event_type: EventType,
        action: A,
        expected_ref_count: usize,
    ) where
        A: Action,
    {
        let event = event_stream.wait_until(event_type, vec!["a"].into()).await.unwrap();

        // Register destroy child action, while `action` is stalled.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = match *component_root.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_child(&ChildMoniker::from("a")).expect("child a not found").clone()
            }
            _ => panic!("not resolved"),
        };
        let (f, delete_handle) = {
            let component_root = component_root.clone();
            async move {
                ActionSet::register(component_root, DestroyChildAction::new("a".into(), 0))
                    .await
                    .expect("destroy failed");
            }
            .remote_handle()
        };
        fasync::Task::spawn(f).detach();

        // Check that `action` is being waited on.
        let action_key = action.key();
        loop {
            let actions = component_a.lock_actions().await;
            assert!(actions.contains(&action_key));
            let rx = &actions.rep[&action_key];
            let rx = rx
                .downcast_ref::<ActionNotifier<A::Output>>()
                .expect("action notifier has unexpected type");
            let refcount = rx.refcount.load(Ordering::Relaxed);
            if refcount == expected_ref_count {
                assert!(actions.contains(&ActionKey::Destroy));
                break;
            }
            drop(actions);
            fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(100))).await;
        }

        // Resuming the action should allow deletion to proceed.
        event.resume();
        delete_handle.await;
        assert!(is_child_deleted(&component_root, &component_a).await);
    }

    #[fuchsia::test]
    async fn destroy_waits_on_discover() {
        let (test, mut event_stream) = setup_destroy_waits_test(vec![EventType::Discovered]).await;
        run_destroy_waits_test(
            &test,
            &mut event_stream,
            EventType::Discovered,
            DiscoverAction::new(),
            // expected_ref_count:
            // - 1 for the ActionSet
            // - 1 for DestroyAction to wait on the action
            // (the task that registers the action does not wait on it
            2,
        )
        .await;
    }

    #[fuchsia::test]
    async fn destroy_registers_discover() {
        let components = vec![("root", ComponentDeclBuilder::new().build())];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        // This setup circumvents the registration of the Discover action on component_a.
        {
            let mut resolved_state = component_root.lock_resolved_state().await.unwrap();
            let child = cm_rust::ChildDecl {
                name: format!("a"),
                url: format!("test:///a"),
                startup: fdecl::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            };
            assert!(resolved_state.add_child_no_discover(&component_root, &child, None).is_ok());
        }
        let mut event_stream = setup_destroy_waits_test_event_stream(
            &test,
            vec![EventType::Discovered, EventType::Destroyed],
        )
        .await;

        // Shut down component so we can destroy it.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = match *component_root.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_child(&ChildMoniker::from("a")).expect("child a not found").clone()
            }
            _ => panic!("not resolved"),
        };
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");

        // Confirm component is still in New state.
        {
            let state = &*component_a.lock_state().await;
            assert_matches!(state, InstanceState::New);
        };

        // Register DestroyChild.
        let nf = {
            let mut actions = component_root.lock_actions().await;
            actions.register_no_wait(&component_root, DestroyChildAction::new("a".into(), 0))
        };

        // Wait for Discover action, which should be registered by Destroy, followed by
        // Destroyed.
        let event = event_stream.wait_until(EventType::Discovered, vec!["a"].into()).await.unwrap();
        event.resume();
        let event = event_stream.wait_until(EventType::Destroyed, vec!["a"].into()).await.unwrap();
        event.resume();
        nf.await.unwrap();
        assert!(is_child_deleted(&component_root, &component_a).await);
    }

    #[fuchsia::test]
    async fn destroy_waits_on_resolve() {
        let (test, mut event_stream) = setup_destroy_waits_test(vec![EventType::Resolved]).await;
        let event = event_stream.wait_until(EventType::Resolved, vec![].into()).await.unwrap();
        event.resume();
        // Cause `a` to resolve.
        let look_up_a = async {
            // This could fail if it races with deletion.
            let _: Result<Arc<ComponentInstance>, ModelError> =
                test.model.look_up(&vec!["a"].into()).await;
        };
        join!(
            look_up_a,
            run_destroy_waits_test(
                &test,
                &mut event_stream,
                EventType::Resolved,
                ResolveAction::new(),
                // expected_ref_count:
                // - 1 for the ActionSet
                // - 1 for the task that registers the action
                // - 1 for DestroyAction to wait on the action
                3,
            ),
        );
    }

    #[fuchsia::test]
    async fn destroy_waits_on_start() {
        let (test, mut event_stream) = setup_destroy_waits_test(vec![EventType::Started]).await;
        let event = event_stream.wait_until(EventType::Started, vec![].into()).await.unwrap();
        event.resume();
        // Cause `a` to start.
        let bind_a = async {
            // This could fail if it races with deletion.
            let _: Result<Arc<ComponentInstance>, ModelError> =
                test.model.start_instance(&vec!["a"].into(), &StartReason::Eager).await;
        };
        join!(
            bind_a,
            run_destroy_waits_test(
                &test,
                &mut event_stream,
                EventType::Started,
                StartAction::new(StartReason::Eager),
                // expected_ref_count:
                // - 1 for the ActionSet
                // - 1 for the task that registers the action
                // - 1 for DestroyAction to wait on the action
                3,
            ),
        );
    }

    #[fuchsia::test]
    async fn destroy_not_resolved() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("c").build()),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);
        // Get component_b without resolving it.
        let component_b = match *component_a.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_child(&ChildMoniker::from("b")).expect("child b not found").clone()
            }
            _ => panic!("not resolved"),
        };

        // Register destroy action on "a", and wait for it.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a".into(), 0))
            .await
            .expect("destroy failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_destroyed(&component_b).await);

        // Now "a" is destroyed. Expect destroy events for "a" and "b".
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a"].into()),
                    Lifecycle::Destroy(vec!["a", "b"].into()),
                    Lifecycle::Destroy(vec!["a"].into())
                ]
            );
        }
    }

    ///  Delete "a" as child of root:
    ///
    ///  /\
    /// x  a*
    ///     \
    ///      b
    ///     / \
    ///    c   d
    #[fuchsia::test]
    async fn destroy_hierarchy() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").add_lazy_child("x").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
            ("x", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;
        let component_x = test.look_up(vec!["x"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        test.model
            .start_instance(&component_x.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start x");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_x).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // in "a"'s component to be shut down and destroyed, in bottom-up order, but "x" is still
        // running.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a".into(), 0))
            .await
            .expect("delete child failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
        assert!(is_destroyed(&component_c).await);
        assert!(is_destroyed(&component_d).await);
        assert!(is_executing(&component_x).await);
        {
            // Expect only "x" as child of root.
            let state = component_root.lock_state().await;
            let children: Vec<_> = match *state {
                InstanceState::Resolved(ref s) => s.children().map(|(k, _)| k.clone()).collect(),
                _ => {
                    panic!("not resolved");
                }
            };
            assert_eq!(children, vec!["x".into()]);
        }
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();

            // The leaves could be stopped in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            assert_eq!(
                first,
                vec![
                    Lifecycle::Stop(vec!["a", "b", "c"].into()),
                    Lifecycle::Stop(vec!["a", "b", "d"].into())
                ]
            );
            let next: Vec<_> = events.drain(0..2).collect();
            assert_eq!(
                next,
                vec![Lifecycle::Stop(vec!["a", "b"].into()), Lifecycle::Stop(vec!["a"].into())]
            );

            // The leaves could be destroyed in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            assert_eq!(
                first,
                vec![
                    Lifecycle::Destroy(vec!["a", "b", "c"].into()),
                    Lifecycle::Destroy(vec!["a", "b", "d"].into())
                ]
            );
            assert_eq!(
                events,
                vec![
                    Lifecycle::Destroy(vec!["a", "b"].into()),
                    Lifecycle::Destroy(vec!["a"].into())
                ]
            );
        }
    }

    /// Destroy `b`:
    ///  a
    ///   \
    ///    b
    ///     \
    ///      b
    ///       \
    ///      ...
    ///
    /// `b` is a child of itself, but destruction should still be able to complete.
    #[fuchsia::test]
    async fn destroy_self_referential() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_b2 = test.look_up(vec!["a", "b", "b"].into()).await;

        // Start the second `b`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start b2");
        test.model
            .start_instance(&component_b.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start b2");
        test.model
            .start_instance(&component_b2.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start b2");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_b2).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // that were started to be destroyed, in bottom-up order.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a".into(), 0))
            .await
            .expect("delete child failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
        assert!(is_destroyed(&component_b2).await);
        {
            let state = component_root.lock_state().await;
            let children: Vec<_> = match *state {
                InstanceState::Resolved(ref s) => s.children().map(|(k, _)| k.clone()).collect(),
                _ => panic!("not resolved"),
            };
            assert_eq!(children, Vec::<ChildMoniker>::new());
        }
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a", "b", "b"].into()),
                    Lifecycle::Stop(vec!["a", "b"].into()),
                    Lifecycle::Stop(vec!["a"].into()),
                    // This component instance is never resolved but we still invoke the Destroy
                    // hook on it.
                    Lifecycle::Destroy(vec!["a", "b", "b", "b"].into()),
                    Lifecycle::Destroy(vec!["a", "b", "b"].into()),
                    Lifecycle::Destroy(vec!["a", "b"].into()),
                    Lifecycle::Destroy(vec!["a"].into())
                ]
            );
        }
    }

    /// Destroy `a`:
    ///
    ///    a*
    ///     \
    ///      b
    ///     / \
    ///    c   d
    ///
    /// `a` fails to destroy the first time, but succeeds the second time.
    #[fuchsia::test]
    async fn destroy_error() {
        struct DestroyErrorHook {
            moniker: AbsoluteMoniker,
        }

        impl DestroyErrorHook {
            fn new(moniker: AbsoluteMoniker) -> Self {
                Self { moniker }
            }

            fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
                vec![HooksRegistration::new(
                    "DestroyErrorHook",
                    vec![EventType::Destroyed],
                    Arc::downgrade(self) as Weak<dyn Hook>,
                )]
            }

            async fn on_destroyed_async(
                &self,
                target_moniker: &AbsoluteMoniker,
            ) -> Result<(), ModelError> {
                if *target_moniker == self.moniker {
                    return Err(ModelError::unsupported("ouch"));
                }
                Ok(())
            }
        }

        #[async_trait]
        impl Hook for DestroyErrorHook {
            async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
                let target_moniker = event
                    .target_moniker
                    .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
                if let Ok(EventPayload::Destroyed) = event.result {
                    self.on_destroyed_async(target_moniker).await?;
                }
                Ok(())
            }
        }

        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        // The destroy hook is invoked just after the component instance is removed from the
        // list of children. Therefore, to cause destruction of `a` to fail, fail removal of
        // `/a/b`.
        let error_hook = Arc::new(DestroyErrorHook::new(vec!["a", "b"].into()));
        let test = ActionsTest::new_with_hooks("root", components, None, error_hook.hooks()).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);

        // Register delete action on "a", and wait for it. "b"'s component is deleted, but "b"
        // returns an error so the delete action on "a" does not succeed.
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a".into(), 0))
            .await
            .expect_err("destroy succeeded unexpectedly");
        assert!(has_child(&component_root, "a").await);
        assert!(!has_child(&component_a, "b").await);
        assert!(!is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
        assert!(is_destroyed(&component_c).await);
        assert!(is_destroyed(&component_d).await);
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            // The leaves could be stopped in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Destroy(vec!["a", "b", "c"].into()),
                Lifecycle::Destroy(vec!["a", "b", "d"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(events, vec![Lifecycle::Destroy(vec!["a", "b"].into())]);
        }

        // Register destroy action on "a" again. "b"'s delete succeeds, and "a" is deleted
        // this time.
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a".into(), 0))
            .await
            .expect("destroy failed");
        assert!(!has_child(&component_root, "a").await);
        assert!(is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
        assert!(is_destroyed(&component_c).await);
        assert!(is_destroyed(&component_d).await);
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            // The leaves could be stopped in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Destroy(vec!["a", "b", "c"].into()),
                Lifecycle::Destroy(vec!["a", "b", "d"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(
                events,
                vec![
                    Lifecycle::Destroy(vec!["a", "b"].into()),
                    Lifecycle::Destroy(vec!["a"].into())
                ]
            );
        }
    }

    #[fuchsia::test]
    async fn destroy_runs_after_new_instance_created() {
        // We want to demonstrate that running two destroy child actions for the same child
        // instance, which should be idempotent, works correctly if a new instance of the child
        // under the same name is created between them.
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec![].into())).await;

        // Create dynamic instance in "coll".
        test.create_dynamic_child("coll", "a").await;

        // Start the component so we can witness it getting stopped.
        test.start(vec!["coll:a"].into()).await;

        // We're going to run the destroy action for `a` twice. One after the other finishes, so
        // the actions semantics don't dedup them to the same work item.
        let component_root = test.look_up(vec![].into()).await;
        let destroy_fut_1 = ActionSet::register(
            component_root.clone(),
            DestroyChildAction::new("coll:a".into(), 1),
        );
        let destroy_fut_2 = ActionSet::register(
            component_root.clone(),
            DestroyChildAction::new("coll:a".into(), 1),
        );

        let component_a = test.look_up(vec!["coll:a"].into()).await;
        assert!(!is_child_deleted(&component_root, &component_a).await);

        destroy_fut_1.await.expect("destroy failed");
        assert!(is_child_deleted(&component_root, &component_a).await);

        // Now recreate `a`
        test.create_dynamic_child("coll", "a").await;
        test.start(vec!["coll:a"].into()).await;

        // Run the second destroy fut, it should leave the newly created `a` alone
        destroy_fut_2.await.expect("destroy failed");
        let component_a = test.look_up(vec!["coll:a"].into()).await;
        assert_eq!(get_incarnation_id(&component_root, "coll:a").await, 2);
        assert!(!is_child_deleted(&component_root, &component_a).await);

        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["coll:a"].into()),
                    Lifecycle::Destroy(vec!["coll:a"].into()),
                ],
            );
        }
    }
}
