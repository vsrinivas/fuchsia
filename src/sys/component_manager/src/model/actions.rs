// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The "Action" concept represents a long-running activity on a realm that should eventually
//! complete.
//!
//! Actions decouple the "what" of what needs to happen to a component from the "how". Several
//! client APIs may induce long-running operations on a realm's state that complete asynchronously.
//! These operations could depend on each other in various ways.
//!
//! A key property of actions is idempotency. If two equal actions are registered on a realm, the
//! work for that action is performed only once. This means that two distinct call sites can
//! register the same action, and be guaranteed the work is not repeated.
//!
//! Here are a couple examples:
//! - A `Shutdown` FIDL call must shut down every component instance in the tree, in
//!   dependency order. For this to happen every component must shut down, but not before its
//!   downstream dependencies have shut down.
//! - A `Realm.DestroyChild` FIDL call returns right after a child component is marked deleted.
//!   However, in order to actually delete the child, a sequence of events must happen:
//!     * All instances in the realm must be shut down (see above)
//!     * The component instance's persistent storage must be erased, if any.
//!     * The component's parent must remove it as a child.
//!
//! Note the interdependencies here -- destroying a component also requires shutdown, for example.
//!
//! These processes could be implemented through a chain of futures in the vicinity of the API
//! call. However, this doesn't scale well, because it requires distributed state handling and is
//! prone to races. Actions solve this problem by allowing client code to just specify the actions
//! that need to eventually be fulfilled. The actual business logic to perform the actions can be
//! implemented by the realm itself in a coordinated manner.
//!
//! `DestroyChild()` is an example of how this can work. For simplicity, suppose it's called on a
//! component with no children of its own. This might cause a chain of events like the following:
//!
//! - Before it returns, the `DestroyChild` FIDL handler registers the `DeleteChild` action on the
//!   containing realm for child being destroyed.
//! - This results in a call to `Action::handle` for the realm. In response to
//!   `DestroyChild`, `Action::handle()` spawns a future that sets a `Destroy` action on the child.
//!   Note that `Action::handle()` is not async, it always spawns any work that might block
//!   in a future.
//! - `Action::handle()` is called on the child. In response to `Destroy`, it sets a `Shutdown`
//!   action on itself (the component instance must be stopped before it is destroyed).
//! - `Action::handle()` is called on the child again, in response to `Shutdown`. It turns out the
//!   instance is still running, so the `Shutdown` future tells the instance to stop. When this
//!   completes, the `Shutdown` action is finished.
//! - The future that was spawned for `Destroy` is notified that `Shutdown` completes, so it cleans
//!   up the instance's resources and finishes the `Destroy` action.
//! - When the work for `Destroy` completes, the future spawned for `DestroyChild` deletes the
//!   child and marks `DestroyChild` finished, which will notify the client that the action is
//!   complete.

use {
    crate::model::{
        error::ModelError,
        hooks::{Event, EventPayload},
        model::Model,
        moniker::ChildMoniker,
        realm::Realm,
        shutdown,
    },
    fuchsia_async as fasync,
    futures::future::{join_all, poll_fn, BoxFuture},
    futures::lock::Mutex,
    futures::pending,
    futures::task::{Poll, Waker},
    std::collections::HashMap,
    std::sync::Arc,
};

/// A action on a realm that must eventually be fulfilled.
#[derive(Debug, PartialEq, Eq, Hash, Clone)]
pub enum Action {
    /// This realm's component instances should be shut down (stopped and never started again).
    Shutdown,
    /// The given child of this realm should be deleted.
    DeleteChild(ChildMoniker),
    /// This realm and all its component instance should be destroyed.
    Destroy,
}

struct ActionStatus {
    inner: Mutex<ActionStatusInner>,
}

struct ActionStatusInner {
    result: Option<Result<(), ModelError>>,
    waker: Option<Waker>,
}

impl ActionStatus {
    fn new() -> Self {
        ActionStatus { inner: Mutex::new(ActionStatusInner { result: None, waker: None }) }
    }
}

/// A set of actions on a realm that must be completed.
///
/// Each action is mapped to a status that is populated when the action is complete. This also
/// causes the notifications returned by `register()` to be woken.
///
/// `register()` and `finish()` return a boolean that indicates whether the set of actions changed
/// as a result of the call. If so, the caller should invoke `Action::handle()` to
/// ensure the change in actions is acted upon.
pub(super) struct ActionSet {
    rep: HashMap<Action, Arc<ActionStatus>>,
}

/// Type of notification returned by `ActionSet::register()`.
pub(super) type Notification = BoxFuture<'static, Result<(), ModelError>>;

impl ActionSet {
    pub fn new() -> Self {
        ActionSet { rep: HashMap::new() }
    }

    /// Registers an action in the set, returning a notification that completes when the action
    /// is finished.
    ///
    /// The return value is as follows:
    /// - A notification, i.e. a Future that completes when the action is finished. This can be
    ///   chained with other futures to perform additional work when the action completes.
    /// - A boolean that indicates whether `Action::handle()` should be called to reify the action
    ///   update.
    #[must_use]
    pub fn register(&mut self, action: Action) -> (Notification, bool) {
        let needs_handle = !self.rep.contains_key(&action);
        let status = self.rep.entry(action).or_insert(Arc::new(ActionStatus::new())).clone();
        let nf = async move {
            loop {
                {
                    let mut inner = status.inner.lock().await;
                    if let Some(result) = inner.result.as_ref() {
                        return result.clone();
                    } else {
                        // The action is not finished yet. Get a waker for the current task by
                        // returning it from a `PollFn`, which is provided the context.
                        let waker = poll_fn(|cx| Poll::Ready(cx.waker().clone())).await;
                        inner.waker.get_or_insert(waker);
                    }
                }
                // The action is not finished yet, so suspend it. `finish()` will wake up the task.
                pending!();
            }
        };
        (Box::pin(nf), needs_handle)
    }

    /// Removes an action from the set, completing its notifications.
    pub async fn finish<'a>(&'a mut self, action: &'a Action, res: Result<(), ModelError>) {
        if let Some(status) = self.rep.remove(action) {
            let mut inner = status.inner.lock().await;
            inner.result = Some(res);
            if let Some(waker) = inner.waker.take() {
                waker.wake();
            }
        }
    }
}

impl Action {
    /// Schedule a task for the new action on the given realm. Should be called after a new action
    /// was registered.
    ///
    /// The work to fulfill these actions should be scheduled asynchronously, so this method is not
    /// `async`.
    pub fn handle(&self, model: Arc<Model>, realm: Arc<Realm>) {
        let action = self.clone();
        fasync::spawn(async move {
            let res = match &action {
                Action::DeleteChild(moniker) => {
                    do_delete_child(model, realm.clone(), moniker.clone()).await
                }
                Action::Destroy => do_destroy(model, realm.clone()).await,
                Action::Shutdown => shutdown::do_shutdown(model, realm.clone()).await,
            };
            Realm::finish_action(realm, &action, res).await;
        });
    }
}

async fn do_delete_child(
    model: Arc<Model>,
    realm: Arc<Realm>,
    moniker: ChildMoniker,
) -> Result<(), ModelError> {
    let child_realm = {
        let mut state = realm.lock_state().await;
        let state = state.as_mut().expect("do_delete_child: not resolved");
        state.mark_child_realm_deleting(&moniker.to_partial());
        state.all_child_realms().get(&moniker).map(|r| r.clone())
    };

    // The child may not exist or may already be deleted by a previous DeleteChild action.
    if let Some(child_realm) = child_realm {
        let event =
            Event { target_realm: child_realm.clone(), payload: EventPayload::PreDestroyInstance };

        child_realm.hooks.dispatch(&event).await?;

        Realm::register_action(child_realm.clone(), model.clone(), Action::Destroy).await.await?;

        {
            let mut state = realm.lock_state().await;
            state.as_mut().expect("do_delete_child: not resolved").remove_child_realm(&moniker);
        }

        let event =
            Event { target_realm: child_realm.clone(), payload: EventPayload::PostDestroyInstance };
        child_realm.hooks.dispatch(&event).await?;
    }

    Ok(())
}

async fn do_destroy(model: Arc<Model>, realm: Arc<Realm>) -> Result<(), ModelError> {
    // For destruction to behave correctly, the component has to be shut down first.
    Realm::register_action(realm.clone(), model.clone(), Action::Shutdown).await.await?;

    let nfs = if let Some(state) = realm.lock_state().await.as_ref() {
        let mut nfs = vec![];
        for (m, _) in state.all_child_realms().iter() {
            let realm = realm.clone();
            let nf =
                Realm::register_action(realm, model.clone(), Action::DeleteChild(m.clone())).await;
            nfs.push(nf);
        }
        nfs
    } else {
        // Component was never resolved. No explicit cleanup is required for children.
        vec![]
    };
    let results = join_all(nfs).await;
    ok_or_first_error(results)?;

    // Now that all children have been destroyed, destroy the containing realm.
    Realm::destroy_instance(model, realm).await?;
    Ok(())
}

fn ok_or_first_error(results: Vec<Result<(), ModelError>>) -> Result<(), ModelError> {
    results.into_iter().fold(Ok(()), |acc, r| acc.and_then(|_| r))
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use {
        crate::model::{
            binding::Binder,
            hooks::{EventType, Hook, HooksRegistration},
            moniker::{AbsoluteMoniker, PartialMoniker},
            testing::{test_helpers::*, test_hook::*},
        },
        cm_rust::{
            CapabilityPath, ExposeDecl, ExposeServiceProtocolDecl, ExposeSource, ExposeTarget,
            OfferDecl, OfferServiceProtocolDecl, OfferServiceSource, OfferTarget, UseDecl,
            UseServiceProtocolDecl, UseSource,
        },
        fidl_fuchsia_sys2 as fsys,
        std::{convert::TryFrom, sync::Weak, task::Context},
    };

    macro_rules! results_eq {
        ($res:expr, $expected_res:expr) => {
            assert_eq!(format!("{:?}", $res), format!("{:?}", $expected_res));
        };
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_set() {
        let mut action_set = ActionSet::new();

        // Register some actions, and get notifications.
        let (mut nf1, needs_handle) = action_set.register(Action::Destroy);
        assert!(needs_handle);
        let (mut nf2, needs_handle) = action_set.register(Action::Shutdown);
        assert!(needs_handle);
        let (mut nf3, needs_handle) = action_set.register(Action::Destroy);
        assert!(!needs_handle);

        // Notifications have not completed yet, because they have not finished. Note calling
        // `poll()` will cause a waker to be installed.
        let waker = poll_fn(|cx| Poll::Ready(cx.waker().clone())).await;
        let mut cx = Context::from_waker(&waker);
        assert!(is_pending(nf1.as_mut().poll(&mut cx)));
        assert!(is_pending(nf2.as_mut().poll(&mut cx)));
        assert!(is_pending(nf3.as_mut().poll(&mut cx)));

        // Complete actions, while checking notifications.
        action_set.finish(&Action::Destroy, Ok(())).await;
        let ok: Result<(), ModelError> = Ok(());
        let err: Result<(), ModelError> = Err(ModelError::ComponentInvalid);
        results_eq!(nf1.await, ok);
        assert!(is_pending(nf2.as_mut().poll(&mut cx)));
        results_eq!(nf3.await, ok);
        action_set.finish(&Action::Shutdown, Err(ModelError::ComponentInvalid)).await;
        action_set.finish(&Action::Shutdown, Ok(())).await;
        results_eq!(nf2.await, err);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_set_no_wake() {
        let mut action_set = ActionSet::new();

        // Register some actions, and get notifications.
        let (nf1, needs_handle) = action_set.register(Action::Destroy);
        assert!(needs_handle);
        let (nf2, needs_handle) = action_set.register(Action::Shutdown);
        assert!(needs_handle);
        let (nf3, needs_handle) = action_set.register(Action::Destroy);
        assert!(!needs_handle);

        // Complete actions, while checking notifications. Note that no waker was set because
        // `poll()` was never called on the notification before the action completed.
        action_set.finish(&Action::Destroy, Ok(())).await;
        let ok: Result<(), ModelError> = Ok(());
        let err: Result<(), ModelError> = Err(ModelError::ComponentInvalid);
        results_eq!(nf1.await, ok);
        results_eq!(nf3.await, ok);
        action_set.finish(&Action::Shutdown, Err(ModelError::ComponentInvalid)).await;
        action_set.finish(&Action::Shutdown, Ok(())).await;
        results_eq!(nf2.await, err);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_one_component() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Bind to the component, causing it to start. This should cause the realm to have an
        // `Execution`.
        let realm = test.look_up(vec!["a:0"].into()).await;
        test.model.bind(&realm.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm).await);
        let a_info = ComponentInfo::new(realm.clone()).await;

        // Register shutdown action, and wait for it. Component should shut down (no more
        // `Execution`).
        execute_action(test.model.clone(), a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        a_info.check_is_shut_down(&test.runner).await;

        // Trying to bind to the component should fail because it's shut down.
        test.model
            .bind(&a_info.realm.abs_moniker)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Shut down the component again. This succeeds, but has no additional effect.
        execute_action(test.model.clone(), a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        &a_info.check_is_shut_down(&test.runner).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_collection() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("container")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "container",
                ComponentDeclBuilder::new()
                    .add_collection("coll", fsys::Durability::Transient)
                    .add_lazy_child("c")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container:0"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Bind to the components, causing them to start. This should cause them to have an
        // `Execution`.
        let realm_container = test.look_up(vec!["container:0"].into()).await;
        let realm_a = test.look_up(vec!["container:0", "coll:a:1"].into()).await;
        let realm_b = test.look_up(vec!["container:0", "coll:b:2"].into()).await;
        let realm_c = test.look_up(vec!["container:0", "c:0"].into()).await;
        test.model.bind(&realm_container.abs_moniker).await.expect("could not bind to container");
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to coll:a");
        test.model.bind(&realm_b.abs_moniker).await.expect("could not bind to coll:b");
        test.model.bind(&realm_c.abs_moniker).await.expect("could not bind to coll:b");
        assert!(is_executing(&realm_container).await);
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(has_child(&realm_container, "coll:a:1").await);
        assert!(has_child(&realm_container, "coll:b:2").await);

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_container_info = ComponentInfo::new(realm_container).await;

        // Register shutdown action, and wait for it. Components should shut down (no more
        // `Execution`). Also, the instances in the collection should have been destroyed because
        // they were transient.
        execute_action(test.model.clone(), realm_container_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        realm_container_info.check_is_shut_down(&test.runner).await;
        assert!(!has_child(&realm_container_info.realm, "coll:a:1").await);
        assert!(!has_child(&realm_container_info.realm, "coll:b:2").await);
        assert!(has_child(&realm_container_info.realm, "c:0").await);
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;

        // Verify events.
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
            let mut next: Vec<_> = events.drain(0..3).collect();
            next.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["container:0", "c:0"].into()),
                Lifecycle::Stop(vec!["container:0", "coll:a:1"].into()),
                Lifecycle::Stop(vec!["container:0", "coll:b:2"].into()),
            ];
            assert_eq!(next, expected);

            // These components were destroyed because they lived in a transient collection.
            let mut next: Vec<_> = events.drain(0..2).collect();
            next.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Destroy(vec!["container:0", "coll:a:1"].into()),
                Lifecycle::Destroy(vec!["container:0", "coll:b:2"].into()),
            ];
            assert_eq!(next, expected);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_not_started() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_lazy_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        assert!(!is_executing(&realm_a).await);
        assert!(!is_executing(&realm_b).await);

        // Register shutdown action on "a", and wait for it.
        execute_action(test.model.clone(), realm_a.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&realm_a).await);
        assert!(execution_is_shut_down(&realm_b).await);

        // Now "a" is shut down. There should be no events though because the component was
        // never started.
        execute_action(test.model.clone(), realm_a.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&realm_a).await);
        assert!(execution_is_shut_down(&realm_b).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, Vec::<Lifecycle>::new());
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_not_resolved() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_lazy_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_lazy_child("c")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);

        // Register shutdown action on "a", and wait for it.
        execute_action(test.model.clone(), realm_a.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&realm_a).await);
        // Get realm_b without resolving it.
        let realm_b = {
            let state = realm_a.lock_state().await;
            let state = state.as_ref().unwrap();
            state.get_live_child_realm(&PartialMoniker::from("b")).expect("child b not found")
        };
        assert!(execution_is_shut_down(&realm_b).await);
        assert!(is_unresolved(&realm_b).await);

        // Now "a" is shut down. There should be no event for "b" because it was never started
        // (or resolved).
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, vec![Lifecycle::Stop(vec!["a:0"].into())]);
        }
    }

    /// Shut down `a`:
    ///  a
    ///   \
    ///    b
    ///   / \
    ///  c   d
    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_hierarchy() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_c_info = ComponentInfo::new(realm_c).await;
        let realm_d_info = ComponentInfo::new(realm_d).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        execute_action(test.model.clone(), realm_a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a:0", "b:0", "c:0"].into()),
                Lifecycle::Stop(vec!["a:0", "b:0", "d:0"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a:0", "b:0"].into()),
                    Lifecycle::Stop(vec!["a:0"].into())
                ]
            );
        }
    }

    /// Shut down `a`:
    ///   a
    ///    \
    ///     b
    ///   / | \
    ///  c<-d->e
    /// In this case C and E use a service provided by d
    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_with_multiple_deps() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }))
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: OfferTarget::Child("e".to_string()),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: ExposeTarget::Realm,
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;
        let realm_e = test.look_up(vec!["a:0", "b:0", "e:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);
        assert!(is_executing(&realm_e).await);

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_c_info = ComponentInfo::new(realm_c).await;
        let realm_d_info = ComponentInfo::new(realm_d).await;
        let realm_e_info = ComponentInfo::new(realm_e).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        execute_action(test.model.clone(), realm_a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;
        realm_e_info.check_is_shut_down(&test.runner).await;

        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let mut expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a:0", "b:0", "c:0"].into()),
                Lifecycle::Stop(vec!["a:0", "b:0", "e:0"].into()),
            ];
            assert_eq!(first, expected);

            let next: Vec<_> = events.drain(0..1).collect();
            expected = vec![Lifecycle::Stop(vec!["a:0", "b:0", "d:0"].into())];
            assert_eq!(next, expected);

            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a:0", "b:0"].into()),
                    Lifecycle::Stop(vec!["a:0"].into())
                ]
            );
        }
    }

    /// Shut down `a`:
    ///    a
    ///     \
    ///      b
    ///   / / \  \
    ///  c<-d->e->f
    /// In this case C and E use a service provided by D and
    /// F uses a service provided by E, shutdown order should be
    /// {F}, {C, E}, {D}, {B}, {A}
    /// Note that C must stop before D, but may stop before or after
    /// either of F and E.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_with_multiple_out_and_longer_chain() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .add_eager_child("f")
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }))
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: OfferTarget::Child("e".to_string()),
                    }))
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("e".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target: OfferTarget::Child("f".to_string()),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: ExposeTarget::Realm,
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target: ExposeTarget::Realm,
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "f",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
        ];
        let moniker_a: AbsoluteMoniker = vec!["a:0"].into();
        let moniker_b: AbsoluteMoniker = vec!["a:0", "b:0"].into();
        let moniker_c: AbsoluteMoniker = vec!["a:0", "b:0", "c:0"].into();
        let moniker_d: AbsoluteMoniker = vec!["a:0", "b:0", "d:0"].into();
        let moniker_e: AbsoluteMoniker = vec!["a:0", "b:0", "e:0"].into();
        let moniker_f: AbsoluteMoniker = vec!["a:0", "b:0", "f:0"].into();
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(moniker_a.clone()).await;
        let realm_b = test.look_up(moniker_b.clone()).await;
        let realm_c = test.look_up(moniker_c.clone()).await;
        let realm_d = test.look_up(moniker_d.clone()).await;
        let realm_e = test.look_up(moniker_e.clone()).await;
        let realm_f = test.look_up(moniker_f.clone()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);
        assert!(is_executing(&realm_e).await);
        assert!(is_executing(&realm_f).await);

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_c_info = ComponentInfo::new(realm_c).await;
        let realm_d_info = ComponentInfo::new(realm_d).await;
        let realm_e_info = ComponentInfo::new(realm_e).await;
        let realm_f_info = ComponentInfo::new(realm_f).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        execute_action(test.model.clone(), realm_a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;
        realm_e_info.check_is_shut_down(&test.runner).await;
        realm_f_info.check_is_shut_down(&test.runner).await;

        let mut comes_after: HashMap<AbsoluteMoniker, Vec<AbsoluteMoniker>> = HashMap::new();
        comes_after.insert(moniker_a.clone(), vec![moniker_b.clone()]);
        // technically we could just depend on 'D' since it is the last of b's
        // children, but we add all the children for resilence against the
        // future
        comes_after.insert(
            moniker_b.clone(),
            vec![moniker_c.clone(), moniker_d.clone(), moniker_e.clone(), moniker_f.clone()],
        );
        comes_after.insert(moniker_d.clone(), vec![moniker_c.clone(), moniker_e.clone()]);
        comes_after.insert(moniker_c.clone(), vec![]);
        comes_after.insert(moniker_e.clone(), vec![moniker_f.clone()]);
        comes_after.insert(moniker_f.clone(), vec![]);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();

            for e in events {
                match e {
                    Lifecycle::Stop(moniker) => match comes_after.remove(&moniker) {
                        Some(dependents) => {
                            for d in dependents {
                                if comes_after.contains_key(&d) {
                                    panic!("{} stopped before its dependent {}", moniker, d);
                                }
                            }
                        }
                        None => {
                            panic!("{} was unknown or shut down more than once", moniker);
                        }
                    },
                    _ => {
                        panic!("Unexpected lifecycle type");
                    }
                }
            }
        }
    }

    /// Shut down `a`:
    ///           a
    ///
    ///           |
    ///
    ///     +---- b ----+
    ///    /             \
    ///   /     /   \     \
    ///
    ///  c <~~ d ~~> e ~~> f
    ///          \       /
    ///           +~~>~~+
    /// In this case C and E use a service provided by D and
    /// F uses a services provided by E and D, shutdown order should be F must
    /// stop before E and {C,E,F} must stop before D. C may stop before or
    /// after either of {F, E}.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_with_multiple_out_multiple_in() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .add_eager_child("f")
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                    }))
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: OfferTarget::Child("e".to_string()),
                    }))
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: OfferTarget::Child("f".to_string()),
                    }))
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("e".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target: OfferTarget::Child("f".to_string()),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target: ExposeTarget::Realm,
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target: ExposeTarget::Realm,
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "f",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                    }))
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
        ];
        let moniker_a: AbsoluteMoniker = vec!["a:0"].into();
        let moniker_b: AbsoluteMoniker = vec!["a:0", "b:0"].into();
        let moniker_c: AbsoluteMoniker = vec!["a:0", "b:0", "c:0"].into();
        let moniker_d: AbsoluteMoniker = vec!["a:0", "b:0", "d:0"].into();
        let moniker_e: AbsoluteMoniker = vec!["a:0", "b:0", "e:0"].into();
        let moniker_f: AbsoluteMoniker = vec!["a:0", "b:0", "f:0"].into();
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(moniker_a.clone()).await;
        let realm_b = test.look_up(moniker_b.clone()).await;
        let realm_c = test.look_up(moniker_c.clone()).await;
        let realm_d = test.look_up(moniker_d.clone()).await;
        let realm_e = test.look_up(moniker_e.clone()).await;
        let realm_f = test.look_up(moniker_f.clone()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);
        assert!(is_executing(&realm_e).await);
        assert!(is_executing(&realm_f).await);

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_c_info = ComponentInfo::new(realm_c).await;
        let realm_d_info = ComponentInfo::new(realm_d).await;
        let realm_e_info = ComponentInfo::new(realm_e).await;
        let realm_f_info = ComponentInfo::new(realm_f).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        execute_action(test.model.clone(), realm_a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;
        realm_e_info.check_is_shut_down(&test.runner).await;
        realm_f_info.check_is_shut_down(&test.runner).await;

        let mut comes_after: HashMap<AbsoluteMoniker, Vec<AbsoluteMoniker>> = HashMap::new();
        comes_after.insert(moniker_a.clone(), vec![moniker_b.clone()]);
        // technically we could just depend on 'D' since it is the last of b's
        // children, but we add all the children for resilence against the
        // future
        comes_after.insert(
            moniker_b.clone(),
            vec![moniker_c.clone(), moniker_d.clone(), moniker_e.clone(), moniker_f.clone()],
        );
        comes_after.insert(
            moniker_d.clone(),
            vec![moniker_c.clone(), moniker_e.clone(), moniker_f.clone()],
        );
        comes_after.insert(moniker_c.clone(), vec![]);
        comes_after.insert(moniker_e.clone(), vec![moniker_f.clone()]);
        comes_after.insert(moniker_f.clone(), vec![]);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();

            for e in events {
                match e {
                    Lifecycle::Stop(moniker) => {
                        let dependents = comes_after.remove(&moniker).expect(&format!(
                            "{} was unknown or shut down more than once",
                            moniker
                        ));
                        for d in dependents {
                            if comes_after.contains_key(&d) {
                                panic!("{} stopped before its dependent {}", moniker, d);
                            }
                        }
                    }
                    _ => {
                        panic!("Unexpected lifecycle type");
                    }
                }
            }
        }
    }

    /// Shut down `a`:
    ///  a
    ///   \
    ///    b
    ///   / \
    ///  c-->d
    /// In this case D uses a resource exposed by C
    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_with_dependency() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                        source: OfferServiceSource::Child("c".to_string()),
                        source_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        target: OfferTarget::Child("d".to_string()),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                        source: ExposeSource::Self_,
                        source_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        target: ExposeTarget::Realm,
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                        source: UseSource::Realm,
                        source_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                        target_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                    }))
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_c_info = ComponentInfo::new(realm_c).await;
        let realm_d_info = ComponentInfo::new(realm_d).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up and dependency order.
        execute_action(test.model.clone(), realm_a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;

        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a:0", "b:0", "d:0"].into()),
                Lifecycle::Stop(vec!["a:0", "b:0", "c:0"].into()),
                Lifecycle::Stop(vec!["a:0", "b:0"].into()),
                Lifecycle::Stop(vec!["a:0"].into()),
            ];
            assert_eq!(events, expected);
        }
    }

    /// Shut down `b`:
    ///  a
    ///   \
    ///    b
    ///     \
    ///      b
    ///       \
    ///      ...
    ///
    /// `b` is a child of itself, but shutdown should still be able to complete.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_self_referential() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_lazy_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_lazy_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_b2 = test.look_up(vec!["a:0", "b:0", "b:0"].into()).await;

        // Bind to second `b`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to b2");
        test.model.bind(&realm_b.abs_moniker).await.expect("could not bind to b2");
        test.model.bind(&realm_b2.abs_moniker).await.expect("could not bind to b2");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_b2).await);

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_b2_info = ComponentInfo::new(realm_b2).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up and dependency order.
        execute_action(test.model.clone(), realm_a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_b2_info.check_is_shut_down(&test.runner).await;
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a:0", "b:0", "b:0"].into()),
                    Lifecycle::Stop(vec!["a:0", "b:0"].into()),
                    Lifecycle::Stop(vec!["a:0"].into())
                ]
            );
        }
    }

    /// Shut down `a`:
    ///  a
    ///   \
    ///    b
    ///   / \
    ///  c   d
    ///
    /// `b` fails to finish shutdown the first time, but succeeds the second time.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_error() {
        struct StopErrorHook {
            inner: Arc<StopErrorHookInner>,
        }

        impl StopErrorHook {
            fn new(moniker: AbsoluteMoniker) -> Self {
                Self { inner: StopErrorHookInner::new(moniker) }
            }

            fn hooks(&self) -> Vec<HooksRegistration> {
                vec![HooksRegistration {
                    events: vec![EventType::StopInstance],
                    callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
                }]
            }
        }

        struct StopErrorHookInner {
            moniker: AbsoluteMoniker,
        }

        impl StopErrorHookInner {
            fn new(moniker: AbsoluteMoniker) -> Arc<Self> {
                Arc::new(Self { moniker })
            }

            async fn on_shutdown_instance_async(
                &self,
                realm: Arc<Realm>,
            ) -> Result<(), ModelError> {
                if realm.abs_moniker == self.moniker {
                    return Err(ModelError::unsupported("ouch"));
                }
                Ok(())
            }
        }

        impl Hook for StopErrorHookInner {
            fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
                Box::pin(async move {
                    if let EventPayload::StopInstance = event.payload {
                        self.on_shutdown_instance_async(event.target_realm.clone()).await?;
                    }
                    Ok(())
                })
            }
        }

        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let error_hook = StopErrorHook::new(vec!["a:0", "b:0"].into());
        let test = ActionsTest::new_with_hooks("root", components, None, error_hook.hooks()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_c_info = ComponentInfo::new(realm_c).await;
        let realm_d_info = ComponentInfo::new(realm_d).await;

        // Register shutdown action on "a", and wait for it. "b"'s realm shuts down, but "b"
        // returns an error so "a" does not.
        execute_action(test.model.clone(), realm_a_info.realm.clone(), Action::Shutdown)
            .await
            .expect_err("shutdown succeeded unexpectedly");
        realm_a_info.check_not_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            // The leaves could be stopped in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a:0", "b:0", "c:0"].into()),
                Lifecycle::Stop(vec!["a:0", "b:0", "d:0"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(events, vec![Lifecycle::Stop(vec!["a:0", "b:0"].into())],);
        }

        // Register shutdown action on "a" again. "b"'s shutdown succeeds (it's a no-op), and
        // "a" is allowed to shut down this time.
        execute_action(test.model.clone(), realm_a_info.realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        realm_a_info.check_is_shut_down(&test.runner).await;
        realm_b_info.check_is_shut_down(&test.runner).await;
        realm_c_info.check_is_shut_down(&test.runner).await;
        realm_d_info.check_is_shut_down(&test.runner).await;
        {
            let mut events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            // The leaves could be stopped in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a:0", "b:0", "c:0"].into()),
                Lifecycle::Stop(vec!["a:0", "b:0", "d:0"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a:0", "b:0"].into()),
                    Lifecycle::Stop(vec!["a:0"].into())
                ]
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_one_component() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Bind to the component, causing it to start. This should cause the realm to have an
        // `Execution`.
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);

        // Register delete child action, and wait for it. Component should be destroyed.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm_root, &realm_a).await);
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
            .bind(&realm_a.abs_moniker)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Destroy the component again. This succeeds, but has no additional effect.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm_root, &realm_a).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_collection() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("container")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "container",
                ComponentDeclBuilder::new()
                    .add_collection("coll", fsys::Durability::Transient)
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container:0"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Bind to the components, causing them to start. This should cause them to have an
        // `Execution`.
        let realm_root = test.look_up(vec![].into()).await;
        let realm_container = test.look_up(vec!["container:0"].into()).await;
        let realm_a = test.look_up(vec!["container:0", "coll:a:1"].into()).await;
        let realm_b = test.look_up(vec!["container:0", "coll:b:2"].into()).await;
        test.model.bind(&realm_container.abs_moniker).await.expect("could not bind to container");
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to coll:a");
        test.model.bind(&realm_b.abs_moniker).await.expect("could not bind to coll:b");
        assert!(is_executing(&realm_container).await);
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);

        // Register delete child action, and wait for it. Components should be destroyed.
        let realm_container = test.look_up(vec!["container:0"].into()).await;
        execute_action(
            test.model.clone(),
            realm_root.clone(),
            Action::DeleteChild("container:0".into()),
        )
        .await
        .expect("destroy failed");
        assert!(is_destroyed(&realm_root, &realm_container).await);
        assert!(is_destroyed(&realm_container, &realm_a).await);
        assert!(is_destroyed(&realm_container, &realm_b).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_already_shut_down() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_lazy_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        execute_action(test.model.clone(), realm_a.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&realm_a.clone()).await);
        assert!(execution_is_shut_down(&realm_b.clone()).await);

        // Now delete child "a". This should cause all components to be destroyed.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm_root, &realm_a).await);
        assert!(is_destroyed(&realm_a, &realm_b).await);

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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_not_resolved() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_lazy_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_lazy_child("c")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        // Get realm_b without resolving it.
        let realm_b = {
            let state = realm_a.lock_state().await;
            let state = state.as_ref().unwrap();
            state.get_live_child_realm(&PartialMoniker::from("b")).expect("child b not found")
        };

        // Register delete action on "a", and wait for it.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm_root, &realm_a).await);
        assert!(is_unresolved(&realm_b).await);

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
    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_hierarchy() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .add_lazy_child("x")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
            ("x", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;
        let realm_x = test.look_up(vec!["x:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        test.model.bind(&realm_x.abs_moniker).await.expect("could not bind to x");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);
        assert!(is_executing(&realm_x).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // in "a"'s realm to be shut down and destroyed, in bottom-up order, but "x" is still
        // running.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect("delete child failed");
        assert!(is_destroyed(&realm_root, &realm_a).await);
        assert!(is_destroyed(&realm_a, &realm_b).await);
        assert!(is_destroyed(&realm_b, &realm_c).await);
        assert!(is_destroyed(&realm_b, &realm_d).await);
        assert!(is_executing(&realm_x).await);
        {
            // Expect only "x" as child of root.
            let state = realm_root.lock_state().await;
            let children: Vec<_> =
                state.as_ref().unwrap().all_child_realms().keys().map(|m| m.clone()).collect();
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

            // The leaves could be destroyed in any order.
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
    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_self_referential() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_lazy_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_lazy_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_b2 = test.look_up(vec!["a:0", "b:0", "b:0"].into()).await;

        // Bind to second `b`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to b2");
        test.model.bind(&realm_b.abs_moniker).await.expect("could not bind to b2");
        test.model.bind(&realm_b2.abs_moniker).await.expect("could not bind to b2");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_b2).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // that were started to be destroyed, in bottom-up order.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect("delete child failed");
        assert!(is_destroyed(&realm_root, &realm_a).await);
        assert!(is_destroyed(&realm_a, &realm_b).await);
        assert!(is_destroyed(&realm_b, &realm_b2).await);
        {
            let state = realm_root.lock_state().await;
            let children: Vec<_> =
                state.as_ref().unwrap().all_child_realms().keys().map(|m| m.clone()).collect();
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

    /// Destroy `a`:
    ///
    ///    a*
    ///     \
    ///      b
    ///     / \
    ///    c   d
    ///
    /// `a` fails to destroy the first time, but succeeds the second time.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_error() {
        struct DestroyErrorHook {
            inner: Arc<DestroyErrorHookInner>,
        }

        impl DestroyErrorHook {
            fn new(moniker: AbsoluteMoniker) -> Self {
                Self { inner: DestroyErrorHookInner::new(moniker) }
            }

            fn hooks(&self) -> Vec<HooksRegistration> {
                vec![HooksRegistration {
                    events: vec![EventType::PostDestroyInstance],
                    callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
                }]
            }
        }

        struct DestroyErrorHookInner {
            moniker: AbsoluteMoniker,
        }

        impl DestroyErrorHookInner {
            fn new(moniker: AbsoluteMoniker) -> Arc<Self> {
                Arc::new(Self { moniker })
            }

            async fn on_destroy_instance_async(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
                if realm.abs_moniker == self.moniker {
                    return Err(ModelError::unsupported("ouch"));
                }
                Ok(())
            }
        }

        impl Hook for DestroyErrorHookInner {
            fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
                Box::pin(async move {
                    if let EventPayload::PostDestroyInstance = event.payload {
                        self.on_destroy_instance_async(event.target_realm.clone()).await?;
                    }
                    Ok(())
                })
            }
        }

        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_lazy_child("a")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_eager_child("b")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .offer_runner_to_children(TEST_RUNNER_NAME)
                    .build(),
            ),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        // The destroy hook is invoked just after the component instance is removed from the
        // list of children. Therefore, to cause destruction of `a` to fail, fail removal of
        // `/a/b`.
        let error_hook = DestroyErrorHook::new(vec!["a:0", "b:0"].into());
        let test = ActionsTest::new_with_hooks("root", components, None, error_hook.hooks()).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind(&realm_a.abs_moniker).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);

        // Register delete action on "a", and wait for it. "b"'s realm is deleted, but "b"
        // returns an error so the delete action on "a" does not succeed.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect_err("destroy succeeded unexpectedly");
        assert!(has_child(&realm_root, "a:0").await);
        assert!(is_destroyed(&realm_a, &realm_b).await);
        assert!(is_destroyed(&realm_b, &realm_c).await);
        assert!(is_destroyed(&realm_b, &realm_d).await);
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

        // Register destroy action on "a:0" again. "b:0"'s delete succeeds, and "a:0" is deleted this
        // time.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(!has_child(&realm_root, "a:0").await);
        assert!(is_destroyed(&realm_a, &realm_b).await);
        assert!(is_destroyed(&realm_b, &realm_c).await);
        assert!(is_destroyed(&realm_b, &realm_d).await);
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

    async fn execute_action(
        model: Arc<Model>,
        realm: Arc<Realm>,
        action: Action,
    ) -> Result<(), ModelError> {
        Realm::register_action(realm, model, action).await.await
    }

    async fn is_executing(realm: &Realm) -> bool {
        realm.lock_execution().await.runtime.is_some()
    }

    /// Verifies that a child realm is deleted by checking its RealmState
    /// and verifying that it does not exist in the RealmState of its parent
    async fn is_destroyed(parent_realm: &Realm, child_realm: &Realm) -> bool {
        let child_moniker = child_realm.abs_moniker.leaf().expect("Root realm cannot be destroyed");
        let partial_moniker = child_moniker.to_partial();

        // Verify the parent-child relationship
        assert_eq!(parent_realm.abs_moniker.child(child_moniker.clone()), child_realm.abs_moniker);

        let parent_state = parent_realm.lock_state().await;
        let parent_state = parent_state.as_ref().unwrap();

        let child_state = child_realm.lock_state().await;
        let child_state = child_state.as_ref().unwrap();
        let child_execution = child_realm.lock_execution().await;

        let found_partial_moniker = parent_state
            .live_child_realms()
            .find(|(curr_partial_moniker, _)| **curr_partial_moniker == partial_moniker);
        let found_child_moniker = parent_state.all_child_realms().get(child_moniker);

        found_partial_moniker.is_none()
            && found_child_moniker.is_none()
            && child_execution.runtime.is_none()
            && child_execution.is_shut_down()
            && child_state.all_child_realms().is_empty()
    }

    async fn is_unresolved(realm: &Realm) -> bool {
        let state = realm.lock_state().await;
        let execution = realm.lock_execution().await;
        execution.runtime.is_none() && state.is_none()
    }

    fn is_pending(p: Poll<Result<(), ModelError>>) -> bool {
        match p {
            Poll::Pending => true,
            _ => false,
        }
    }
}
