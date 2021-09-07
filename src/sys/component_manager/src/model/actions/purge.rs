// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{
            Action, ActionKey, ActionSet, DestroyChildAction, DiscoverAction, PurgeChildAction,
            ResolveAction, StartAction,
        },
        component::{BindReason, ComponentInstance, InstanceState},
        error::ModelError,
    },
    async_trait::async_trait,
    futures::{
        future::{join_all, BoxFuture},
        Future,
    },
    moniker::ChildMonikerBase,
    std::sync::Arc,
};

/// Destroy this component instance, including all instances nested in its component.
pub struct PurgeAction {}

impl PurgeAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for PurgeAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_purge(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Purge
    }
}

async fn do_purge(component: &Arc<ComponentInstance>) -> Result<(), ModelError> {
    // It is always expected that the component shut down first.
    {
        let execution = component.lock_execution().await;
        assert!(
            execution.is_shut_down(),
            "Component was not shut down before being purged? {}",
            component.abs_moniker
        );
    }

    // Require the component to be discovered before purging it so a Purged event is always
    // preceded by a Discovered.
    ActionSet::register(component.clone(), DiscoverAction::new()).await?;

    let nfs = {
        match *component.lock_state().await {
            InstanceState::Resolved(ref s) => {
                let mut nfs = vec![];
                for (m, _) in s.all_children().iter() {
                    let component = component.clone();
                    let m = m.clone();
                    let nf = async move {
                        ActionSet::register(
                            component.clone(),
                            DestroyChildAction::new(m.to_partial()),
                        )
                        .await?;
                        ActionSet::register(component, PurgeChildAction::new(m)).await
                    };
                    nfs.push(nf);
                }
                nfs
            }
            InstanceState::New | InstanceState::Discovered | InstanceState::Purged => {
                // Component was never resolved. No explicit cleanup is required for children.
                vec![]
            }
        }
    };
    let results = join_all(nfs).await;
    ok_or_first_error(results)?;

    // Now that all children have been purged, purge the parent.
    component.destroy_instance().await?;

    // Only consider the component fully purged once it's no longer executing any lifecycle
    // transitions.
    {
        let mut state = component.lock_state().await;
        state.set(InstanceState::Purged);
    }
    fn wait(nf: Option<impl Future + Send + 'static>) -> BoxFuture<'static, ()> {
        Box::pin(async {
            if let Some(nf) = nf {
                let _ = nf.await;
            }
        })
    }
    let nfs = {
        let actions = component.lock_actions().await;
        vec![
            wait(actions.wait(ResolveAction::new())),
            wait(actions.wait(StartAction::new(BindReason::Unsupported))),
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
                test_utils::{is_child_deleted, is_destroyed, is_executing, is_purged},
                ActionNotifier, ShutdownAction,
            },
            binding::Binder,
            component::BindReason,
            events::{registry::EventSubscription, stream::EventStream},
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            testing::{
                test_helpers::{
                    component_decl_with_test_runner, execution_is_shut_down, has_child, ActionsTest,
                },
                test_hook::Lifecycle,
            },
        },
        cm_rust::EventMode,
        cm_rust_testing::ComponentDeclBuilder,
        fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::{join, FutureExt},
        matches::assert_matches,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, PartialChildMoniker},
        std::sync::atomic::Ordering,
        std::sync::Weak,
    };

    #[fuchsia::test]
    async fn purge_one_component() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Bind to the component, causing it to start. This should cause the component to have an
        // `Execution`.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&component_a.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);

        // Register shutdown first because PurgeChild requires the component to be shut down.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        // Register delete child action, and wait for it. Component should be purged.
        ActionSet::register(component_root.clone(), PurgeChildAction::new("a:0".into()))
            .await
            .expect("purge failed");
        // PurgeChild should not mark the instance non-live. That's done by Destroy which we
        // don't call here.
        assert!(!is_destroyed(&component_root, &"a:0".into()).await);
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
                vec![Lifecycle::Stop(vec!["a:0"].into()), Lifecycle::Destroy(vec!["a:0"].into())],
            );
        }

        // Trying to bind to the component should fail because it's shut down.
        test.model
            .bind(&component_a.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Purge the component again. This succeeds, but has no additional effect.
        ActionSet::register(component_root.clone(), PurgeChildAction::new("a:0".into()))
            .await
            .expect("purge failed");
        assert!(!is_destroyed(&component_root, &"a:0".into()).await);
        assert!(is_child_deleted(&component_root, &component_a).await);
    }

    #[fuchsia::test]
    async fn purge_collection() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("container").build()),
            ("container", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container:0"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Bind to the components, causing them to start. This should cause them to have an
        // `Execution`.
        let component_root = test.look_up(vec![].into()).await;
        let component_container = test.look_up(vec!["container:0"].into()).await;
        let component_a = test.look_up(vec!["container:0", "coll:a:1"].into()).await;
        let component_b = test.look_up(vec!["container:0", "coll:b:2"].into()).await;
        test.model
            .bind(&component_container.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to container");
        test.model
            .bind(&component_a.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to coll:a");
        test.model
            .bind(&component_b.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to coll:b");
        assert!(is_executing(&component_container).await);
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);

        // Register delete child action, and wait for it. Components should be purged.
        let component_container = test.look_up(vec!["container:0"].into()).await;
        ActionSet::register(component_container.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        ActionSet::register(component_root.clone(), PurgeChildAction::new("container:0".into()))
            .await
            .expect("purge failed");
        assert!(is_child_deleted(&component_root, &component_container).await);
        assert!(is_purged(&component_container).await);
        assert!(is_purged(&component_a).await);
        assert!(is_purged(&component_b).await);
    }

    #[fuchsia::test]
    async fn purge_already_shut_down() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&component_a.clone()).await);
        assert!(execution_is_shut_down(&component_b.clone()).await);

        // Now delete child "a". This should cause all components to be purged.
        ActionSet::register(component_root.clone(), PurgeChildAction::new("a:0".into()))
            .await
            .expect("purge failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_purged(&component_a).await);

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
                    Lifecycle::Destroy(vec!["a:0", "b:0"].into()),
                    Lifecycle::Destroy(vec!["a:0"].into()),
                ]
            );
        }
    }

    async fn setup_purge_blocks_test(event_types: Vec<EventType>) -> (ActionsTest, EventStream) {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let event_stream = setup_purge_blocks_test_event_stream(&test, event_types).await;
        (test, event_stream)
    }

    async fn setup_purge_blocks_test_event_stream(
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
        {
            let _ = event_source.take_static_event_stream("StartComponentTree".to_string()).await;
        }
        let model = test.model.clone();
        fasync::Task::spawn(async move { model.start().await }).detach();
        event_stream
    }

    async fn run_purge_blocks_test<A>(
        test: &ActionsTest,
        event_stream: &mut EventStream,
        event_type: EventType,
        action: A,
        expected_ref_count: usize,
    ) where
        A: Action,
    {
        let event = event_stream.wait_until(event_type, vec!["a:0"].into()).await.unwrap();

        // Register delete child action, while `action` is stalled.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = match *component_root.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_live_child(&PartialChildMoniker::from("a")).expect("child a not found")
            }
            _ => panic!("not resolved"),
        };
        let (f, delete_handle) = {
            let component_root = component_root.clone();
            let component_a = component_a.clone();
            async move {
                ActionSet::register(component_a, ShutdownAction::new())
                    .await
                    .expect("shutdown failed");
                ActionSet::register(component_root, PurgeChildAction::new("a:0".into()))
                    .await
                    .expect("purge failed");
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
                assert!(actions.contains(&ActionKey::Purge));
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
    async fn purge_blocks_on_discover() {
        let (test, mut event_stream) = setup_purge_blocks_test(vec![EventType::Discovered]).await;
        run_purge_blocks_test(
            &test,
            &mut event_stream,
            EventType::Discovered,
            DiscoverAction::new(),
            // expected_ref_count:
            // - 1 for the ActionSet
            // - 1 for PurgeAction to wait on the action
            // (the task that registers the action does not wait on it
            2,
        )
        .await;
    }

    #[fuchsia::test]
    async fn purge_registers_discover() {
        let components = vec![("root", ComponentDeclBuilder::new().build())];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        // This setup circumvents the registration of the Discover action on component_a.
        {
            let mut resolved_state = component_root.lock_resolved_state().await.unwrap();
            let child = cm_rust::ChildDecl {
                name: format!("a"),
                url: format!("test:///a"),
                startup: fsys::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            };
            resolved_state
                .add_child_for_test(
                    &component_root,
                    &child,
                    None,
                    false, /* !register_discover */
                )
                .await
                .unwrap()
                .await
                .unwrap();
        }
        let mut event_stream = setup_purge_blocks_test_event_stream(
            &test,
            vec![EventType::Discovered, EventType::Purged],
        )
        .await;

        // Shut down component so we can purge it.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = match *component_root.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_live_child(&PartialChildMoniker::from("a")).expect("child a not found")
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

        // Register PurgeChild.
        let nf = {
            let mut actions = component_root.lock_actions().await;
            actions.register_no_wait(&component_root, PurgeChildAction::new("a:0".into()))
        };

        // Wait for Discover action, which should be registered by Purge, followed by
        // Purged.
        let event =
            event_stream.wait_until(EventType::Discovered, vec!["a:0"].into()).await.unwrap();
        event.resume();
        let event = event_stream.wait_until(EventType::Purged, vec!["a:0"].into()).await.unwrap();
        event.resume();
        nf.await.unwrap();
        assert!(is_child_deleted(&component_root, &component_a).await);
    }

    #[fuchsia::test]
    async fn purge_blocks_on_resolve() {
        let (test, mut event_stream) = setup_purge_blocks_test(vec![EventType::Resolved]).await;
        let event = event_stream.wait_until(EventType::Resolved, vec![].into()).await.unwrap();
        event.resume();
        // Cause `a` to resolve.
        let look_up_a = async {
            // This could fail if it races with deletion.
            let _ = test.model.look_up(&vec!["a"].into()).await;
        };
        join!(
            look_up_a,
            run_purge_blocks_test(
                &test,
                &mut event_stream,
                EventType::Resolved,
                ResolveAction::new(),
                // expected_ref_count:
                // - 1 for the ActionSet
                // - 1 for the task that registers the action
                // - 1 for PurgeAction to wait on the action
                3,
            ),
        );
    }

    #[fuchsia::test]
    async fn purge_blocks_on_start() {
        let (test, mut event_stream) = setup_purge_blocks_test(vec![EventType::Started]).await;
        let event = event_stream.wait_until(EventType::Started, vec![].into()).await.unwrap();
        event.resume();
        // Cause `a` to start.
        let bind_a = async {
            // This could fail if it races with deletion.
            let _ = test.model.bind(&vec!["a"].into(), &BindReason::Eager).await;
        };
        join!(
            bind_a,
            run_purge_blocks_test(
                &test,
                &mut event_stream,
                EventType::Started,
                StartAction::new(BindReason::Eager),
                // expected_ref_count:
                // - 1 for the ActionSet
                // - 1 for the task that registers the action
                // - 1 for PurgeAction to wait on the action
                3,
            ),
        );
    }

    #[fuchsia::test]
    async fn purge_not_resolved() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("c").build()),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&component_a.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        // Get component_b without resolving it.
        let component_b = match *component_a.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_live_child(&PartialChildMoniker::from("b")).expect("child b not found")
            }
            _ => panic!("not resolved"),
        };

        // Register purge action on "a", and wait for it.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        ActionSet::register(component_root.clone(), PurgeChildAction::new("a:0".into()))
            .await
            .expect("purge failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_purged(&component_b).await);

        // Now "a" is purged. Expect purge events for "a" and "b".
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
                    Lifecycle::Stop(vec!["a:0"].into()),
                    Lifecycle::Destroy(vec!["a:0", "b:0"].into()),
                    Lifecycle::Destroy(vec!["a:0"].into())
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
    async fn purge_hierarchy() {
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
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let component_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;
        let component_x = test.look_up(vec!["x:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to a");
        test.model
            .bind(&component_x.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to x");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_x).await);

        // Register purge action on "a", and wait for it. This should cause all components
        // in "a"'s component to be shut down and purged, in bottom-up order, but "x" is still
        // running.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        ActionSet::register(component_root.clone(), PurgeChildAction::new("a:0".into()))
            .await
            .expect("delete child failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_purged(&component_a).await);
        assert!(is_purged(&component_b).await);
        assert!(is_purged(&component_c).await);
        assert!(is_purged(&component_d).await);
        assert!(is_executing(&component_x).await);
        {
            // Expect only "x" as child of root.
            let state = component_root.lock_state().await;
            let children: Vec<_> = match *state {
                InstanceState::Resolved(ref s) => {
                    s.all_children().keys().map(|m| m.clone()).collect()
                }
                _ => {
                    panic!("not resolved");
                }
            };
            assert_eq!(children, vec!["x:0".into()]);
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
                    Lifecycle::Stop(vec!["a:0", "b:0", "c:0"].into()),
                    Lifecycle::Stop(vec!["a:0", "b:0", "d:0"].into())
                ]
            );
            let next: Vec<_> = events.drain(0..2).collect();
            assert_eq!(
                next,
                vec![
                    Lifecycle::Stop(vec!["a:0", "b:0"].into()),
                    Lifecycle::Stop(vec!["a:0"].into())
                ]
            );

            // The leaves could be purged in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            assert_eq!(
                first,
                vec![
                    Lifecycle::Destroy(vec!["a:0", "b:0", "c:0"].into()),
                    Lifecycle::Destroy(vec!["a:0", "b:0", "d:0"].into())
                ]
            );
            assert_eq!(
                events,
                vec![
                    Lifecycle::Destroy(vec!["a:0", "b:0"].into()),
                    Lifecycle::Destroy(vec!["a:0"].into())
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
    async fn purge_self_referential() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_b2 = test.look_up(vec!["a:0", "b:0", "b:0"].into()).await;

        // Bind to second `b`.
        test.model
            .bind(&component_a.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&component_b.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&component_b2.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_b2).await);

        // Register purge action on "a", and wait for it. This should cause all components
        // that were started to be purged, in bottom-up order.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        ActionSet::register(component_root.clone(), PurgeChildAction::new("a:0".into()))
            .await
            .expect("delete child failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_purged(&component_a).await);
        assert!(is_purged(&component_b).await);
        assert!(is_purged(&component_b2).await);
        {
            let state = component_root.lock_state().await;
            let children: Vec<_> = match *state {
                InstanceState::Resolved(ref s) => {
                    s.all_children().keys().map(|m| m.clone()).collect()
                }
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
                    Lifecycle::Stop(vec!["a:0", "b:0", "b:0"].into()),
                    Lifecycle::Stop(vec!["a:0", "b:0"].into()),
                    Lifecycle::Stop(vec!["a:0"].into()),
                    // This component instance is never resolved but we still invoke the Destroy
                    // hook on it.
                    Lifecycle::Destroy(vec!["a:0", "b:0", "b:0", "b:0"].into()),
                    Lifecycle::Destroy(vec!["a:0", "b:0", "b:0"].into()),
                    Lifecycle::Destroy(vec!["a:0", "b:0"].into()),
                    Lifecycle::Destroy(vec!["a:0"].into())
                ]
            );
        }
    }

    /// Purge `a`:
    ///
    ///    a*
    ///     \
    ///      b
    ///     / \
    ///    c   d
    ///
    /// `a` fails to purge the first time, but succeeds the second time.
    #[fuchsia::test]
    async fn purge_error() {
        struct PurgeErrorHook {
            moniker: AbsoluteMoniker,
        }

        impl PurgeErrorHook {
            fn new(moniker: AbsoluteMoniker) -> Self {
                Self { moniker }
            }

            fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
                vec![HooksRegistration::new(
                    "PurgeErrorHook",
                    vec![EventType::Purged],
                    Arc::downgrade(self) as Weak<dyn Hook>,
                )]
            }

            async fn on_purged_async(
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
        impl Hook for PurgeErrorHook {
            async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
                let target_moniker = event
                    .target_moniker
                    .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
                if let Ok(EventPayload::Purged) = event.result {
                    self.on_purged_async(target_moniker).await?;
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
        // The purge hook is invoked just after the component instance is removed from the
        // list of children. Therefore, to cause destruction of `a` to fail, fail removal of
        // `/a/b`.
        let error_hook = Arc::new(PurgeErrorHook::new(vec!["a:0", "b:0"].into()));
        let test = ActionsTest::new_with_hooks("root", components, None, error_hook.hooks()).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let component_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker.to_partial(), &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);

        // Register delete action on "a", and wait for it. "b"'s component is deleted, but "b"
        // returns an error so the delete action on "a" does not succeed.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        ActionSet::register(component_root.clone(), PurgeChildAction::new("a:0".into()))
            .await
            .expect_err("purge succeeded unexpectedly");
        assert!(has_child(&component_root, "a:0").await);
        assert!(!has_child(&component_a, "b:0").await);
        assert!(!is_purged(&component_a).await);
        assert!(is_purged(&component_b).await);
        assert!(is_purged(&component_c).await);
        assert!(is_purged(&component_d).await);
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
                Lifecycle::Destroy(vec!["a:0", "b:0", "c:0"].into()),
                Lifecycle::Destroy(vec!["a:0", "b:0", "d:0"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(events, vec![Lifecycle::Destroy(vec!["a:0", "b:0"].into())]);
        }

        // Register purge action on "a:0" again. "b:0"'s delete succeeds, and "a:0" is deleted this
        // time.
        ActionSet::register(component_root.clone(), PurgeChildAction::new("a:0".into()))
            .await
            .expect("purge failed");
        assert!(!has_child(&component_root, "a:0").await);
        assert!(is_purged(&component_a).await);
        assert!(is_purged(&component_b).await);
        assert!(is_purged(&component_c).await);
        assert!(is_purged(&component_d).await);
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
                Lifecycle::Destroy(vec!["a:0", "b:0", "c:0"].into()),
                Lifecycle::Destroy(vec!["a:0", "b:0", "d:0"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(
                events,
                vec![
                    Lifecycle::Destroy(vec!["a:0", "b:0"].into()),
                    Lifecycle::Destroy(vec!["a:0"].into())
                ]
            );
        }
    }
}
