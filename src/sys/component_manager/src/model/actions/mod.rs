// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The "Action" concept represents an asynchronous activity on a realm that should eventually
//! complete.
//!
//! Actions decouple the "what" of what needs to happen to a component from the "how". Several
//! client APIs may induce operations on a realm's state that complete asynchronously. These
//! operations could depend on each other in various ways.
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

mod shutdown;
pub mod start;
mod stop;

use {
    crate::model::{
        error::ModelError,
        hooks::{Event, EventPayload},
        moniker::ChildMoniker,
        realm::{BindReason, Realm},
    },
    futures::future::{join_all, BoxFuture, FutureExt, Shared},
    std::collections::HashMap,
    std::hash::{Hash, Hasher},
    std::sync::Arc,
};

/// A action on a realm that must eventually be fulfilled.
#[derive(Debug, Clone)]
pub enum Action {
    /// This single component instance should be started.
    Start(BindReason),
    /// This single component instance should be stopped.
    Stop,
    /// This realm's component instances should be shut down (stopped and never started again).
    Shutdown,
    /// The given child of this realm should be marked deleting.
    MarkDeleting(ChildMoniker),
    /// The given child of this realm should be deleted.
    DeleteChild(ChildMoniker),
    /// This realm and all its component instance should be destroyed.
    Destroy,
}

/// Two Actions remain equivalent even if their BindReasons differ. The first
/// BindReason is the reason for starting this component. Subsequent BindReasons
/// are ignored.
impl PartialEq for Action {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Action::Start(_), Action::Start(_)) => true,
            (Action::Stop, Action::Stop) => true,
            (Action::Shutdown, Action::Shutdown) => true,
            (Action::MarkDeleting(l), Action::MarkDeleting(r)) => l == r,
            (Action::DeleteChild(l), Action::DeleteChild(r)) => l == r,
            (Action::Destroy, Action::Destroy) => true,
            _ => false,
        }
    }
}

impl Eq for Action {}

impl Hash for Action {
    fn hash<H: Hasher>(&self, state: &mut H) {
        match self {
            Action::Start(_) => state.write_u8(0),
            Action::Stop => state.write_u8(1),
            Action::Shutdown => state.write_u8(2),
            Action::MarkDeleting(child_moniker) => {
                state.write_u8(3);
                child_moniker.hash(state);
            }
            Action::DeleteChild(child_moniker) => {
                state.write_u8(4);
                child_moniker.hash(state);
            }
            Action::Destroy => state.write_u8(5),
        }
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
pub struct ActionSet {
    rep: HashMap<Action, ActionFuture>,
}

/// Type of future that runs an action.
pub type ActionFuture = Shared<BoxFuture<'static, Result<(), ModelError>>>;

impl ActionSet {
    pub fn new() -> Self {
        ActionSet { rep: HashMap::new() }
    }

    pub fn contains(&self, action: &Action) -> bool {
        self.rep.contains_key(action)
    }

    /// Registers an action in the set, returning when the action is finished (which may represent
    /// a task that's already running for this action).
    pub async fn register(realm: Arc<Realm>, action: Action) -> Result<(), ModelError> {
        let fut;
        {
            let mut actions = realm.lock_actions().await;
            fut = actions.register_inner(&realm, action.clone());
        }
        fut.await
    }

    /// Removes an action from the set, completing it.
    async fn finish<'a>(realm: &Arc<Realm>, action: &'a Action) {
        let mut action_set = realm.lock_actions().await;
        action_set.rep.remove(action);
    }

    /// Registers, but does not execute, an action.
    ///
    /// Returns a future that completes when the action is finished. If a matching action was
    /// already registered, this may be a clone of that action's future.
    #[must_use]
    pub(crate) fn register_inner(&mut self, realm: &Arc<Realm>, action: Action) -> ActionFuture {
        if let Some(fut) = self.rep.get_mut(&action) {
            fut.clone()
        } else {
            let fut = action.handle(realm.clone());
            let blocking_action = match action {
                Action::Shutdown => self.rep.get_mut(&Action::Stop),
                Action::Stop => self.rep.get_mut(&Action::Shutdown),
                _ => None,
            };
            let fut = if let Some(blocking_action) = blocking_action {
                let blocking_action = blocking_action.clone();
                async move {
                    let _ = blocking_action.await;
                    fut.await
                }
                .boxed()
                .shared()
            } else {
                fut
            };
            self.rep.insert(action, fut.clone());
            fut
        }
    }
}

impl Action {
    /// Returns a future for the new action on the given realm. Should be called when a new action
    /// is registered.
    pub fn handle(&self, realm: Arc<Realm>) -> ActionFuture {
        let action = self.clone();
        async move {
            let action_res = match &action {
                Action::Start(bind_reason) => start::do_start(&realm, bind_reason).await,
                Action::Stop => stop::do_stop(&realm).await,
                Action::MarkDeleting(moniker) => {
                    do_mark_deleting(realm.clone(), moniker.clone()).await
                }
                Action::DeleteChild(moniker) => {
                    do_delete_child(realm.clone(), moniker.clone()).await
                }
                Action::Destroy => do_destroy(realm.clone()).await,
                Action::Shutdown => shutdown::do_shutdown(realm.clone()).await,
            };
            ActionSet::finish(&realm, &action).await;
            action_res
        }
        .boxed()
        .shared()
    }
}

async fn do_mark_deleting(realm: Arc<Realm>, moniker: ChildMoniker) -> Result<(), ModelError> {
    let partial_moniker = moniker.to_partial();
    let child_realm = {
        let state = realm.lock_state().await;
        let state = state.as_ref().expect("do_mark_deleting: not resolved");
        state.get_live_child_realm(&partial_moniker).map(|r| r.clone())
    };
    if let Some(child_realm) = child_realm {
        let event = Event::new(&child_realm, Ok(EventPayload::MarkedForDestruction));
        child_realm.hooks.dispatch(&event).await?;
        let mut state = realm.lock_state().await;
        let state = state.as_mut().expect("do_mark_deleting: not resolved");
        state.mark_child_realm_deleting(&partial_moniker);
    } else {
        // Child already marked deleting. Nothing to do.
    }
    Ok(())
}

async fn do_delete_child(realm: Arc<Realm>, moniker: ChildMoniker) -> Result<(), ModelError> {
    // Some paths may have already marked the child deleting before scheduling the DeleteChild
    // action, in which case this is a no-op.
    ActionSet::register(realm.clone(), Action::MarkDeleting(moniker.clone())).await?;

    // The child may not exist or may already be deleted by a previous DeleteChild action.
    let child_realm = {
        let state = realm.lock_state().await;
        let state = state.as_ref().expect("do_delete_child: not resolved");
        state.all_child_realms().get(&moniker).map(|r| r.clone())
    };
    if let Some(child_realm) = child_realm {
        ActionSet::register(child_realm.clone(), Action::Destroy).await?;
        {
            let mut state = realm.lock_state().await;
            state.as_mut().expect("do_delete_child: not resolved").remove_child_realm(&moniker);
        }
        let event = Event::new(&child_realm, Ok(EventPayload::Destroyed));
        child_realm.hooks.dispatch(&event).await?;
    }

    Ok(())
}

async fn do_destroy(realm: Arc<Realm>) -> Result<(), ModelError> {
    // For destruction to behave correctly, the component has to be shut down first.
    ActionSet::register(realm.clone(), Action::Shutdown).await?;

    let nfs = if let Some(state) = realm.lock_state().await.as_ref() {
        let mut nfs = vec![];
        for (m, _) in state.all_child_realms().iter() {
            let realm = realm.clone();
            let nf = ActionSet::register(realm, Action::DeleteChild(m.clone()));
            nfs.push(nf);
        }
        nfs
    } else {
        // Component was never resolved. No explicit cleanup is required for children.
        vec![]
    };
    let results = join_all(nfs).await;
    ok_or_first_error(results)?;

    // Now that all children have been destroyed, destroy the parent.
    realm.destroy_instance().await?;
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
            realm::BindReason,
            testing::{
                test_helpers::{
                    component_decl_with_test_runner, execution_is_shut_down, has_child,
                    ActionsTest, ComponentDeclBuilder, ComponentInfo,
                },
                test_hook::Lifecycle,
            },
        },
        async_trait::async_trait,
        cm_rust::{
            CapabilityPath, DependencyType, ExposeDecl, ExposeProtocolDecl, ExposeSource,
            ExposeTarget, OfferDecl, OfferProtocolDecl, OfferServiceSource, OfferTarget,
            ProtocolDecl, UseDecl, UseProtocolDecl, UseSource,
        },
        fuchsia_async as fasync,
        futures::{channel::mpsc, SinkExt, StreamExt},
        std::{convert::TryFrom, sync::Weak},
    };

    async fn register_action_in_new_task(
        action: Action,
        realm: Arc<Realm>,
        mut responder: mpsc::Sender<Result<(), ModelError>>,
    ) {
        let (mut starter_tx, mut starter_rx) = mpsc::channel(0);
        fasync::Task::spawn(async move {
            let mut action_set = realm.lock_actions().await;

            // Register action, and get the future. Use `register_inner` so that we can control
            // when to run the future.
            let fut = action_set.register_inner(&realm, action);

            // Signal to test that action is registered.
            starter_tx.send(()).await.unwrap();

            // Drop `action_set` to release the lock.
            drop(action_set);

            let res = fut.await;

            // If the future completed successfully then we will get to this point.
            responder.send(res).await.expect("failed to send response");
        })
        .detach();
        starter_rx.next().await.expect("Unable to receive start signal");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_set() {
        let test = ActionsTest::new("root", vec![], None).await;
        let realm = test.model.root_realm.clone();

        let (tx, mut rx) = mpsc::channel(0);

        register_action_in_new_task(Action::Destroy, realm.clone(), tx.clone()).await;
        register_action_in_new_task(Action::Shutdown, realm.clone(), tx.clone()).await;
        register_action_in_new_task(Action::Destroy, realm.clone(), tx.clone()).await;

        // Complete actions, while checking notifications.
        ActionSet::finish(&realm, &Action::Destroy).await;
        rx.next().await.expect("Unable to receive result of Notification").unwrap();
        rx.next().await.expect("Unable to receive result of Notification").unwrap();

        ActionSet::finish(&realm, &Action::Shutdown).await;
        ActionSet::finish(&realm, &Action::Shutdown).await;
        rx.next().await.expect("Unable to receive result of Notification").unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_shutdown_blocks_stop() {
        let test = ActionsTest::new("root", vec![], None).await;
        let realm = test.model.root_realm.clone();
        let mut action_set = realm.lock_actions().await;

        // Register some actions, and get notifications. Use `register_inner` so we can register
        // the action without immediately running it.
        let nf1 = action_set.register_inner(&realm, Action::Shutdown);
        let nf2 = action_set.register_inner(&realm, Action::Stop);

        drop(action_set);

        // Complete actions, while checking futures.
        ActionSet::finish(&realm, &Action::Shutdown).await;

        // When nf2 finishes, nf1 should be done because nf2 was blocked on it.
        assert!(nf1.peek().is_none());
        assert!(nf2.peek().is_none());
        nf2.await.unwrap();
        assert!(nf1.peek().is_some());
        nf1.await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_shutdown_stop_stop() {
        let test = ActionsTest::new("root", vec![], None).await;
        let realm = test.model.root_realm.clone();
        let mut action_set = realm.lock_actions().await;

        // Register some actions, and get notifications. Use `register_inner` so we can register
        // the action without immediately running it.
        let nf1 = action_set.register_inner(&realm, Action::Shutdown);
        let nf2 = action_set.register_inner(&realm, Action::Stop);
        let nf3 = action_set.register_inner(&realm, Action::Stop);

        drop(action_set);

        // Complete actions, while checking notifications.
        ActionSet::finish(&realm, &Action::Shutdown).await;

        // When nf2 finishes, nf1 should be done because nf2 was blocked on it.
        assert!(nf1.peek().is_none());
        assert!(nf2.peek().is_none());
        nf2.await.unwrap();
        assert!(nf1.peek().is_some());
        assert!(nf3.peek().is_some());
        nf1.await.unwrap();
        nf3.await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_one_component() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Bind to the component, causing it to start. This should cause the realm to have an
        // `Execution`.
        let realm = test.look_up(vec!["a:0"].into()).await;
        test.model.bind(&realm.abs_moniker, &BindReason::Eager).await.expect("could not bind to a");
        assert!(is_executing(&realm).await);
        let a_info = ComponentInfo::new(realm.clone()).await;

        // Register shutdown action, and wait for it. Component should shut down (no more
        // `Execution`).
        ActionSet::register(a_info.realm.clone(), Action::Shutdown).await.expect("shutdown failed");
        a_info.check_is_shut_down(&test.runner).await;

        // Trying to bind to the component should fail because it's shut down.
        test.model
            .bind(&a_info.realm.abs_moniker, &BindReason::Eager)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Shut down the component again. This succeeds, but has no additional effect.
        ActionSet::register(a_info.realm.clone(), Action::Shutdown).await.expect("shutdown failed");
        &a_info.check_is_shut_down(&test.runner).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_collection() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("container").build()),
            (
                "container",
                ComponentDeclBuilder::new()
                    .add_transient_collection("coll")
                    .add_lazy_child("c")
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
        test.model
            .bind(&realm_container.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to container");
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:a");
        test.model
            .bind(&realm_b.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:b");
        test.model
            .bind(&realm_c.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:b");
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
        ActionSet::register(realm_container_info.realm.clone(), Action::Shutdown)
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        assert!(!is_executing(&realm_a).await);
        assert!(!is_executing(&realm_b).await);

        // Register shutdown action on "a", and wait for it.
        ActionSet::register(realm_a.clone(), Action::Shutdown).await.expect("shutdown failed");
        assert!(execution_is_shut_down(&realm_a).await);
        assert!(execution_is_shut_down(&realm_b).await);

        // Now "a" is shut down. There should be no events though because the component was
        // never started.
        ActionSet::register(realm_a.clone(), Action::Shutdown).await.expect("shutdown failed");
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("c").build()),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&realm_a).await);

        // Register shutdown action on "a", and wait for it.
        ActionSet::register(realm_a.clone(), Action::Shutdown).await.expect("shutdown failed");
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
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
        ActionSet::register(realm_a_info.realm.clone(), Action::Shutdown)
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceD".into(),
                        source_path: "/svc/serviceD".parse().unwrap(),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
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
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
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
        ActionSet::register(realm_a_info.realm.clone(), Action::Shutdown)
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .add_eager_child("f")
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("e".to_string()),
                        source_name: "serviceE".into(),
                        target_name: "serviceE".into(),
                        target: OfferTarget::Child("f".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceD".into(),
                        source_path: "/svc/serviceD".parse().unwrap(),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceE".into(),
                        source_path: "/svc/serviceE".parse().unwrap(),
                    })
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceE".into(),
                        target_name: "serviceE".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "f",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceE".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                    }))
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
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
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
        ActionSet::register(realm_a_info.realm.clone(), Action::Shutdown)
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .add_eager_child("e")
                    .add_eager_child("f")
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("f".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("e".to_string()),
                        source_name: "serviceE".into(),
                        target_name: "serviceE".into(),
                        target: OfferTarget::Child("f".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceD".into(),
                        source_path: "/svc/serviceD".parse().unwrap(),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceE".into(),
                        source_path: "/svc/serviceE".parse().unwrap(),
                    })
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceE".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceE".into(),
                        target_name: "serviceE".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "f",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceE".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceE").unwrap(),
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceD".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceD").unwrap(),
                    }))
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
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
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
        ActionSet::register(realm_a_info.realm.clone(), Action::Shutdown)
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_eager_child("c")
                    .add_eager_child("d")
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferServiceSource::Child("c".to_string()),
                        source_name: "serviceC".into(),
                        target_name: "serviceC".into(),
                        target: OfferTarget::Child("d".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDecl {
                        name: "serviceC".into(),
                        source_path: "/svc/serviceC".parse().unwrap(),
                    })
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "serviceC".into(),
                        target_name: "serviceC".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "serviceC".into(),
                        target_path: CapabilityPath::try_from("/svc/serviceC").unwrap(),
                    }))
                    .build(),
            ),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_c_info = ComponentInfo::new(realm_c).await;
        let realm_d_info = ComponentInfo::new(realm_d).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up and dependency order.
        ActionSet::register(realm_a_info.realm.clone(), Action::Shutdown)
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_b2 = test.look_up(vec!["a:0", "b:0", "b:0"].into()).await;

        // Bind to second `b`.
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&realm_b.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&realm_b2.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_b2).await);

        let realm_a_info = ComponentInfo::new(realm_a).await;
        let realm_b_info = ComponentInfo::new(realm_b).await;
        let realm_b2_info = ComponentInfo::new(realm_b2).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up and dependency order.
        ActionSet::register(realm_a_info.realm.clone(), Action::Shutdown)
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
            moniker: AbsoluteMoniker,
        }

        impl StopErrorHook {
            fn new(moniker: AbsoluteMoniker) -> Self {
                Self { moniker }
            }

            fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
                vec![HooksRegistration::new(
                    "StopErrorHook",
                    vec![EventType::Stopped],
                    Arc::downgrade(self) as Weak<dyn Hook>,
                )]
            }

            async fn on_shutdown_instance_async(
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
        impl Hook for StopErrorHook {
            async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
                if let Ok(EventPayload::Stopped { .. }) = event.result {
                    self.on_shutdown_instance_async(&event.target_moniker).await?;
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
        let error_hook = Arc::new(StopErrorHook::new(vec!["a:0", "b:0"].into()));
        let test = ActionsTest::new_with_hooks("root", components, None, error_hook.hooks()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
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
        ActionSet::register(realm_a_info.realm.clone(), Action::Shutdown)
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
        ActionSet::register(realm_a_info.realm.clone(), Action::Shutdown)
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
    async fn mark_deleting() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // Register `mark_deleting` action, and wait for it. Component should be marked deleted.
        let realm_root = test.look_up(vec![].into()).await;
        ActionSet::register(realm_root.clone(), Action::MarkDeleting("a:0".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&realm_root, "a:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, vec![Lifecycle::PreDestroy(vec!["a:0"].into())],);
        }

        // Execute action again, same state and no new events.
        ActionSet::register(realm_root.clone(), Action::MarkDeleting("a:0".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&realm_root, "a:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, vec![Lifecycle::PreDestroy(vec!["a:0"].into())],);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn mark_deleting_in_collection() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec![].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Register `mark_deleting` action for "a" only.
        let realm_root = test.look_up(vec![].into()).await;
        ActionSet::register(realm_root.clone(), Action::MarkDeleting("coll:a:1".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&realm_root, "coll:a:1".into()).await);
        assert!(!is_deleting(&realm_root, "coll:b:2".into()).await);

        // Register `mark_deleting` action for "b".
        ActionSet::register(realm_root.clone(), Action::MarkDeleting("coll:b:1".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&realm_root, "coll:a:1".into()).await);
        assert!(is_deleting(&realm_root, "coll:b:2".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::PreDestroy(vec!["coll:a:1"].into()),
                    Lifecycle::PreDestroy(vec!["coll:b:2"].into())
                ],
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_one_component() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Bind to the component, causing it to start. This should cause the realm to have an
        // `Execution`.
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&realm_a).await);

        // Register delete child action, and wait for it. Component should be destroyed.
        ActionSet::register(realm_root.clone(), Action::DeleteChild("a:0".into()))
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
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Destroy the component again. This succeeds, but has no additional effect.
        ActionSet::register(realm_root.clone(), Action::DeleteChild("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm_root, &realm_a).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_collection() {
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
        let realm_root = test.look_up(vec![].into()).await;
        let realm_container = test.look_up(vec!["container:0"].into()).await;
        let realm_a = test.look_up(vec!["container:0", "coll:a:1"].into()).await;
        let realm_b = test.look_up(vec!["container:0", "coll:b:2"].into()).await;
        test.model
            .bind(&realm_container.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to container");
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:a");
        test.model
            .bind(&realm_b.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:b");
        assert!(is_executing(&realm_container).await);
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);

        // Register delete child action, and wait for it. Components should be destroyed.
        let realm_container = test.look_up(vec!["container:0"].into()).await;
        ActionSet::register(realm_root.clone(), Action::DeleteChild("container:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm_root, &realm_container).await);
        assert!(is_destroyed(&realm_container, &realm_a).await);
        assert!(is_destroyed(&realm_container, &realm_b).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_already_shut_down() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(realm_a.clone(), Action::Shutdown).await.expect("shutdown failed");
        assert!(execution_is_shut_down(&realm_a.clone()).await);
        assert!(execution_is_shut_down(&realm_b.clone()).await);

        // Now delete child "a". This should cause all components to be destroyed.
        ActionSet::register(realm_root.clone(), Action::DeleteChild("a:0".into()))
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("c").build()),
            ("c", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        // Get realm_b without resolving it.
        let realm_b = {
            let state = realm_a.lock_state().await;
            let state = state.as_ref().unwrap();
            state.get_live_child_realm(&PartialMoniker::from("b")).expect("child b not found")
        };

        // Register delete action on "a", and wait for it.
        ActionSet::register(realm_root.clone(), Action::DeleteChild("a:0".into()))
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").add_lazy_child("x").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
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
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        test.model
            .bind(&realm_x.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to x");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);
        assert!(is_executing(&realm_x).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // in "a"'s realm to be shut down and destroyed, in bottom-up order, but "x" is still
        // running.
        ActionSet::register(realm_root.clone(), Action::DeleteChild("a:0".into()))
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
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_b2 = test.look_up(vec!["a:0", "b:0", "b:0"].into()).await;

        // Bind to second `b`.
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&realm_b.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&realm_b2.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_b2).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // that were started to be destroyed, in bottom-up order.
        ActionSet::register(realm_root.clone(), Action::DeleteChild("a:0".into()))
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
                if let Ok(EventPayload::Destroyed) = event.result {
                    self.on_destroyed_async(&event.target_moniker).await?;
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
        let error_hook = Arc::new(DestroyErrorHook::new(vec!["a:0", "b:0"].into()));
        let test = ActionsTest::new_with_hooks("root", components, None, error_hook.hooks()).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a:0"].into()).await;
        let realm_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let realm_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let realm_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&realm_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);

        // Register delete action on "a", and wait for it. "b"'s realm is deleted, but "b"
        // returns an error so the delete action on "a" does not succeed.
        ActionSet::register(realm_root.clone(), Action::DeleteChild("a:0".into()))
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
        ActionSet::register(realm_root.clone(), Action::DeleteChild("a:0".into()))
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

    async fn is_executing(realm: &Realm) -> bool {
        realm.lock_execution().await.runtime.is_some()
    }

    async fn is_deleting(realm: &Realm, moniker: ChildMoniker) -> bool {
        let partial = moniker.to_partial();
        let state = realm.lock_state().await;
        let state = state.as_ref().unwrap();
        state.get_live_child_realm(&partial).is_none()
            && state.get_child_instance(&moniker).is_some()
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
}
