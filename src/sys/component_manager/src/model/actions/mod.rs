// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The "Action" concept represents an asynchronous activity on a component that should eventually
//! complete.
//!
//! Actions decouple the "what" of what needs to happen to a component from the "how". Several
//! client APIs may induce operations on a component's state that complete asynchronously. These
//! operations could depend on each other in various ways.
//!
//! A key property of actions is idempotency. If two equal actions are registered on a component,
//! the work for that action is performed only once. This means that two distinct call sites can
//! register the same action, and be guaranteed the work is not repeated.
//!
//! Here are a couple examples:
//! - A `Shutdown` FIDL call must shut down every component instance in the tree, in
//!   dependency order. For this to happen every component must shut down, but not before its
//!   downstream dependencies have shut down.
//! - A `Realm.DestroyChild` FIDL call returns right after a child component is marked deleted.
//!   However, in order to actually delete the child, a sequence of events must happen:
//!     * All instances in the component must be shut down (see above)
//!     * The component instance's persistent storage must be erased, if any.
//!     * The component's parent must remove it as a child.
//!
//! Note the interdependencies here -- destroying a component also requires shutdown, for example.
//!
//! These processes could be implemented through a chain of futures in the vicinity of the API
//! call. However, this doesn't scale well, because it requires distributed state handling and is
//! prone to races. Actions solve this problem by allowing client code to just specify the actions
//! that need to eventually be fulfilled. The actual business logic to perform the actions can be
//! implemented by the component itself in a coordinated manner.
//!
//! `DestroyChild()` is an example of how this can work. For simplicity, suppose it's called on a
//! component with no children of its own. This might cause a chain of events like the following:
//!
//! - Before it returns, the `DestroyChild` FIDL handler registers the `DeleteChild` action on the
//!   parent component for child being destroyed.
//! - This results in a call to `Action::handle` for the component. In response to
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

mod resolve;
mod shutdown;
pub mod start;
mod stop;

use {
    crate::model::{
        component::{BindReason, Component, ComponentInstance, InstanceState},
        error::ModelError,
        hooks::{Event, EventPayload},
    },
    async_trait::async_trait,
    fuchsia_async as fasync,
    futures::{
        channel::oneshot,
        future::{join_all, pending, BoxFuture, FutureExt, Shared},
        task::{Context, Poll},
        Future,
    },
    moniker::ChildMoniker,
    std::any::Any,
    std::collections::HashMap,
    std::fmt::Debug,
    std::hash::Hash,
    std::pin::Pin,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
};

/// A action on a component that must eventually be fulfilled.
#[async_trait]
pub trait Action: Send + Sync + 'static {
    type Output: Send + Sync + Clone + Debug;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output;
    fn key(&self) -> ActionKey;
}

/// Dispatches a `Discovered` event for a component instance. This action should be registered
/// when a component instance is created.
pub struct DiscoverAction {}

impl DiscoverAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for DiscoverAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_discover(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Discover
    }
}

/// Resolves a component instance's declaration and initializes its state.
pub struct ResolveAction {}

impl ResolveAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for ResolveAction {
    type Output = Result<Component, ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        resolve::do_resolve(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Resolve
    }
}

/// Starts a component instance.
pub struct StartAction {
    bind_reason: BindReason,
}

impl StartAction {
    pub fn new(bind_reason: BindReason) -> Self {
        Self { bind_reason }
    }
}

#[async_trait]
impl Action for StartAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        start::do_start(component, &self.bind_reason).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Start
    }
}

/// Stops a component instance.
pub struct StopAction {}

impl StopAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for StopAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        stop::do_stop(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Stop
    }
}

/// Marks a child of a component as deleting.
pub struct MarkDeletingAction {
    moniker: ChildMoniker,
}

impl MarkDeletingAction {
    pub fn new(moniker: ChildMoniker) -> Self {
        Self { moniker }
    }
}

#[async_trait]
impl Action for MarkDeletingAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_mark_deleting(component, self.moniker.clone()).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::MarkDeleting(self.moniker.clone())
    }
}

/// Completely deletes the given child of a component.
pub struct DeleteChildAction {
    moniker: ChildMoniker,
}

impl DeleteChildAction {
    pub fn new(moniker: ChildMoniker) -> Self {
        Self { moniker }
    }
}

#[async_trait]
impl Action for DeleteChildAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_delete_child(component, self.moniker.clone()).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::DeleteChild(self.moniker.clone())
    }
}

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

/// Shuts down all component instances in this component (stops them and guarantees they will never
/// be started again).
pub struct ShutdownAction {}

impl ShutdownAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for ShutdownAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        shutdown::do_shutdown(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Shutdown
    }
}

/// A key that uniquely identifies an action.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum ActionKey {
    Discover,
    Resolve,
    Start,
    Stop,
    Shutdown,
    MarkDeleting(ChildMoniker),
    DeleteChild(ChildMoniker),
    Destroy,
}

/// A set of actions on a component that must be completed.
///
/// Each action is mapped to a future that returns when the action is complete.
pub struct ActionSet {
    rep: HashMap<ActionKey, Box<dyn Any + Send + Sync>>,
}

/// A future bound to a particular action that completes when that action completes.
///
/// Cloning this type will not duplicate the action, but generate another future that waits on the
/// same action.
#[derive(Debug)]
pub struct ActionNotifier<Output: Send + Sync + Clone + Debug> {
    /// The inner future.
    fut: Shared<BoxFuture<'static, Output>>,
    /// How many clones of this ActionNotifer are live, useful for testing.
    refcount: Arc<AtomicUsize>,
}

impl<Output: Send + Sync + Clone + Debug> ActionNotifier<Output> {
    /// Instantiate an `ActionNotifier` wrapping `fut`.
    pub fn new(fut: BoxFuture<'static, Output>) -> Self {
        Self { fut: fut.shared(), refcount: Arc::new(AtomicUsize::new(1)) }
    }
}

impl<Output: Send + Sync + Clone + Debug> Clone for ActionNotifier<Output> {
    fn clone(&self) -> Self {
        let _ = self.refcount.fetch_add(1, Ordering::Relaxed);
        Self { fut: self.fut.clone(), refcount: self.refcount.clone() }
    }
}

impl<Output: Send + Sync + Clone + Debug> Drop for ActionNotifier<Output> {
    fn drop(&mut self) {
        let _ = self.refcount.fetch_sub(1, Ordering::Relaxed);
    }
}

impl<Output: Send + Sync + Clone + Debug> Future for ActionNotifier<Output> {
    type Output = Output;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let fut = Pin::new(&mut self.fut);
        fut.poll(cx)
    }
}

/// Represents a task that implements an action.
pub(crate) struct ActionTask<A>
where
    A: Action,
{
    tx: oneshot::Sender<A::Output>,
    fut: BoxFuture<'static, A::Output>,
}

impl<A> ActionTask<A>
where
    A: Action,
{
    fn new(tx: oneshot::Sender<A::Output>, fut: BoxFuture<'static, A::Output>) -> Self {
        Self { tx, fut }
    }

    /// Runs the action in a separate task and signals the `ActionNotifier` when it completes.
    pub fn spawn(self) {
        fasync::Task::spawn(async move {
            let _ = self.tx.send(self.fut.await);
        })
        .detach();
    }
}

impl ActionSet {
    pub fn new() -> Self {
        ActionSet { rep: HashMap::new() }
    }

    pub fn contains(&self, key: &ActionKey) -> bool {
        self.rep.contains_key(key)
    }

    /// Registers an action in the set, returning when the action is finished (which may represent
    /// a task that's already running for this action).
    pub async fn register<A>(component: Arc<ComponentInstance>, action: A) -> A::Output
    where
        A: Action,
    {
        let rx = {
            let mut actions = component.lock_actions().await;
            actions.register_no_wait(&component, action)
        };
        rx.await
    }

    /// Registers an action in the set, but does not wait for it to complete, instead returning a
    /// future that can be used to wait on the task. This function is a no-op if the task is
    /// already registered.
    ///
    /// REQUIRES: `self` is the `ActionSet` contained in `component`.
    pub fn register_no_wait<A>(
        &mut self,
        component: &Arc<ComponentInstance>,
        action: A,
    ) -> impl Future<Output = A::Output>
    where
        A: Action,
    {
        let (task, rx) = self.register_inner(component, action);
        if let Some(task) = task {
            task.spawn();
        }
        rx
    }

    /// Returns a future that waits for the given action to complete, if one exists.
    pub fn wait<A>(&self, action: A) -> Option<impl Future<Output = A::Output>>
    where
        A: Action,
    {
        let key = action.key();
        if let Some(rx) = self.rep.get(&key) {
            let rx = rx
                .downcast_ref::<ActionNotifier<A::Output>>()
                .expect("action notifier has unexpected type");
            let rx = rx.clone();
            Some(rx)
        } else {
            None
        }
    }

    /// Removes an action from the set, completing it.
    async fn finish<'a>(component: &Arc<ComponentInstance>, key: &'a ActionKey) {
        let mut action_set = component.lock_actions().await;
        action_set.rep.remove(key);
    }

    /// Registers, but does not execute, an action.
    ///
    /// Returns:
    /// - An object that implements the action if it was scheduled for the first time. The caller
    ///   should call spawn() on it.
    /// - A future to listen on the completion of the action.
    #[must_use]
    pub(crate) fn register_inner<'a, A>(
        &'a mut self,
        component: &Arc<ComponentInstance>,
        action: A,
    ) -> (Option<ActionTask<A>>, ActionNotifier<A::Output>)
    where
        A: Action,
    {
        let key = action.key();
        if let Some(rx) = self.rep.get(&key) {
            let rx = rx
                .downcast_ref::<ActionNotifier<A::Output>>()
                .expect("action notifier has unexpected type");
            let rx = rx.clone();
            (None, rx)
        } else {
            let blocking_action = match key {
                ActionKey::Shutdown => self.rep.get(&ActionKey::Stop),
                ActionKey::Stop => self.rep.get(&ActionKey::Shutdown),
                _ => None,
            };
            let component = component.clone();
            let handle_action = async move {
                let res = action.handle(&component).await;
                Self::finish(&component, &action.key()).await;
                res
            };
            let fut = if let Some(blocking_action) = blocking_action {
                let blocking_action = blocking_action
                    .downcast_ref::<ActionNotifier<A::Output>>()
                    .expect("action notifier has unexpected type")
                    .clone();
                async move {
                    let _ = blocking_action.await;
                    handle_action.await
                }
                .boxed()
            } else {
                handle_action.boxed()
            };
            let (tx, rx) = oneshot::channel();
            let task = ActionTask::new(tx, fut);
            let rx = ActionNotifier::new(
                async move {
                    match rx.await {
                        Ok(res) => res,
                        Err(_) => {
                            // Normally we won't get here but this can happen if the sender's task
                            // is cancelled because, for example, component manager exited and the
                            // executor was torn down.
                            let () = pending().await;
                            unreachable!();
                        }
                    }
                }
                .boxed(),
            );
            self.rep.insert(key, Box::new(rx.clone()));
            (Some(task), rx)
        }
    }
}

async fn do_discover(component: &Arc<ComponentInstance>) -> Result<(), ModelError> {
    let is_discovered = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::New => false,
            InstanceState::Discovered => true,
            InstanceState::Resolved(_) => true,
            InstanceState::Destroyed => {
                return Err(ModelError::instance_not_found(component.abs_moniker.clone()));
            }
        }
    };
    if is_discovered {
        return Ok(());
    }
    let event = Event::new(&component, Ok(EventPayload::Discovered));
    component.hooks.dispatch(&event).await?;
    {
        let mut state = component.lock_state().await;
        assert!(
            matches!(*state, InstanceState::New | InstanceState::Destroyed),
            "Component in unexpected state after discover"
        );
        match *state {
            InstanceState::Destroyed => {
                // Nothing to do.
            }
            InstanceState::Discovered | InstanceState::Resolved(_) => {
                panic!(
                    "Component was marked {:?} during Discover action, which shouldn't be possible",
                    *state
                );
            }
            InstanceState::New => {
                state.set(InstanceState::Discovered);
            }
        }
    }
    Ok(())
}

async fn do_mark_deleting(
    component: &Arc<ComponentInstance>,
    moniker: ChildMoniker,
) -> Result<(), ModelError> {
    let partial_moniker = moniker.to_partial();
    let child = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => s.get_live_child(&partial_moniker).map(|r| r.clone()),
            InstanceState::Destroyed => None,
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_mark_deleting: not resolved");
            }
        }
    };
    if let Some(child) = child {
        let event = Event::new(&child, Ok(EventPayload::MarkedForDestruction));
        child.hooks.dispatch(&event).await?;
        let mut state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref mut s) => {
                s.mark_child_deleting(&partial_moniker);
            }
            InstanceState::Destroyed => {}
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_mark_deleting: not resolved");
            }
        }
    } else {
        // Child already marked deleting. Nothing to do.
    }
    Ok(())
}

async fn do_delete_child(
    component: &Arc<ComponentInstance>,
    moniker: ChildMoniker,
) -> Result<(), ModelError> {
    // Some paths may have already marked the child deleting before scheduling the DeleteChild
    // action, in which case this is a no-op.
    ActionSet::register(component.clone(), MarkDeletingAction::new(moniker.clone())).await?;

    // The child may not exist or may already be deleted by a previous DeleteChild action.
    let child = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => s.all_children().get(&moniker).map(|r| r.clone()),
            InstanceState::Destroyed => None,
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_delete_child: not resolved");
            }
        }
    };
    if let Some(child) = child {
        ActionSet::register(child.clone(), DestroyAction::new()).await?;
        {
            let mut state = component.lock_state().await;
            match *state {
                InstanceState::Resolved(ref mut s) => {
                    s.remove_child(&moniker);
                }
                InstanceState::Destroyed => {}
                InstanceState::New | InstanceState::Discovered => {
                    panic!("do_delete_child: not resolved");
                }
            }
        }
        let event = Event::new(&child, Ok(EventPayload::Destroyed));
        child.hooks.dispatch(&event).await?;
    }

    Ok(())
}

async fn do_destroy(component: &Arc<ComponentInstance>) -> Result<(), ModelError> {
    // For destruction to behave correctly, the component has to be shut down first.
    ActionSet::register(component.clone(), ShutdownAction::new()).await?;

    let nfs = {
        match *component.lock_state().await {
            InstanceState::Resolved(ref s) => {
                let mut nfs = vec![];
                for (m, _) in s.all_children().iter() {
                    let component = component.clone();
                    let nf = ActionSet::register(component, DeleteChildAction::new(m.clone()));
                    nfs.push(nf);
                }
                nfs
            }
            InstanceState::New | InstanceState::Discovered | InstanceState::Destroyed => {
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
                let _ = nf.await;
            }
        })
    }
    let nfs = {
        let actions = component.lock_actions().await;
        vec![
            wait(actions.wait(DiscoverAction::new())),
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
    use super::*;
    use {
        crate::model::{
            binding::Binder,
            component::BindReason,
            events::{event::EventMode, registry::EventSubscription, stream::EventStream},
            hooks::{self, EventType, Hook, HooksRegistration},
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
            ExposeTarget, OfferDecl, OfferProtocolDecl, OfferSource, OfferTarget, ProtocolDecl,
            UseDecl, UseProtocolDecl, UseSource,
        },
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::join,
        matches::assert_matches,
        moniker::{AbsoluteMoniker, PartialMoniker},
        std::{convert::TryFrom, sync::Weak},
    };

    async fn register_action_in_new_task<A>(
        action: A,
        component: Arc<ComponentInstance>,
        responder: oneshot::Sender<A::Output>,
        res: A::Output,
    ) where
        A: Action,
    {
        let (starter_tx, starter_rx) = oneshot::channel();
        fasync::Task::spawn(async move {
            let mut action_set = component.lock_actions().await;

            // Register action, and get the future. Use `register_inner` so that we can control
            // when to notify the listener.
            let (task, rx) = action_set.register_inner(&component, action);

            // Signal to test that action is registered.
            starter_tx.send(()).unwrap();

            // Drop `action_set` to release the lock.
            drop(action_set);

            if let Some(task) = task {
                // Notify the listeners, but don't actually run the action since this test tests
                // action registration and not the actions themselves.
                task.tx.send(res).unwrap();
            }
            let res = rx.await;

            // If the future completed successfully then we will get to this point.
            responder.send(res).expect("failed to send response");
        })
        .detach();
        starter_rx.await.expect("Unable to receive start signal");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_set() {
        let test = ActionsTest::new("root", vec![], None).await;
        let component = test.model.root.clone();

        let (tx1, rx1) = oneshot::channel();
        register_action_in_new_task(DestroyAction::new(), component.clone(), tx1, Ok(())).await;
        let (tx2, rx2) = oneshot::channel();
        register_action_in_new_task(
            ShutdownAction::new(),
            component.clone(),
            tx2,
            Err(ModelError::ComponentInvalid),
        )
        .await;
        let (tx3, rx3) = oneshot::channel();
        register_action_in_new_task(DestroyAction::new(), component.clone(), tx3, Ok(())).await;

        // Complete actions, while checking notifications.
        ActionSet::finish(&component, &ActionKey::Destroy).await;
        assert_matches!(rx1.await.expect("Unable to receive result of Notification"), Ok(()));
        assert_matches!(rx3.await.expect("Unable to receive result of Notification"), Ok(()));

        ActionSet::finish(&component, &ActionKey::Shutdown).await;
        assert_matches!(
            rx2.await.expect("Unable to receive result of Notification"),
            Err(ModelError::ComponentInvalid)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_shutdown_blocks_stop() {
        let test = ActionsTest::new("root", vec![], None).await;
        let component = test.model.root.clone();
        let mut action_set = component.lock_actions().await;

        // Register some actions, and get notifications. Use `register_inner` so we can register
        // the action without immediately running it.
        let (task1, nf1) = action_set.register_inner(&component, ShutdownAction::new());
        let (task2, nf2) = action_set.register_inner(&component, StopAction::new());

        drop(action_set);

        // Complete actions, while checking futures.
        ActionSet::finish(&component, &ActionKey::Shutdown).await;

        // nf2 should be blocked on task1 completing.
        assert!(nf1.fut.peek().is_none());
        assert!(nf2.fut.peek().is_none());
        task1.unwrap().tx.send(Ok(())).unwrap();
        task2.unwrap().spawn();
        nf1.await.unwrap();
        nf2.await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_shutdown_stop_stop() {
        let test = ActionsTest::new("root", vec![], None).await;
        let component = test.model.root.clone();
        let mut action_set = component.lock_actions().await;

        // Register some actions, and get notifications. Use `register_inner` so we can register
        // the action without immediately running it.
        let (task1, nf1) = action_set.register_inner(&component, ShutdownAction::new());
        let (task2, nf2) = action_set.register_inner(&component, StopAction::new());
        let (task3, nf3) = action_set.register_inner(&component, StopAction::new());

        drop(action_set);

        // Complete actions, while checking notifications.
        ActionSet::finish(&component, &ActionKey::Shutdown).await;

        // nf2 and nf3 should be blocked on task1 completing.
        assert!(nf1.fut.peek().is_none());
        assert!(nf2.fut.peek().is_none());
        task1.unwrap().tx.send(Ok(())).unwrap();
        task2.unwrap().spawn();
        assert!(task3.is_none());
        nf1.await.unwrap();
        nf2.await.unwrap();
        nf3.await.unwrap();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_one_component() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Bind to the component, causing it to start. This should cause the component to have an
        // `Execution`.
        let component = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&component.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component).await);
        let a_info = ComponentInfo::new(component.clone()).await;

        // Register shutdown action, and wait for it. Component should shut down (no more
        // `Execution`).
        ActionSet::register(a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        a_info.check_is_shut_down(&test.runner).await;

        // Trying to bind to the component should fail because it's shut down.
        test.model
            .bind(&a_info.component.abs_moniker, &BindReason::Eager)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Shut down the component again. This succeeds, but has no additional effect.
        ActionSet::register(a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
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
        let component_container = test.look_up(vec!["container:0"].into()).await;
        let component_a = test.look_up(vec!["container:0", "coll:a:1"].into()).await;
        let component_b = test.look_up(vec!["container:0", "coll:b:2"].into()).await;
        let component_c = test.look_up(vec!["container:0", "c:0"].into()).await;
        test.model
            .bind(&component_container.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to container");
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:a");
        test.model
            .bind(&component_b.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:b");
        test.model
            .bind(&component_c.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:b");
        assert!(is_executing(&component_container).await);
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(has_child(&component_container, "coll:a:1").await);
        assert!(has_child(&component_container, "coll:b:2").await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_container_info = ComponentInfo::new(component_container).await;

        // Register shutdown action, and wait for it. Components should shut down (no more
        // `Execution`). Also, the instances in the collection should have been destroyed because
        // they were transient.
        ActionSet::register(component_container_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_container_info.check_is_shut_down(&test.runner).await;
        assert!(!has_child(&component_container_info.component, "coll:a:1").await);
        assert!(!has_child(&component_container_info.component, "coll:b:2").await);
        assert!(has_child(&component_container_info.component, "c:0").await);
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;

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
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        assert!(!is_executing(&component_a).await);
        assert!(!is_executing(&component_b).await);

        // Register shutdown action on "a", and wait for it.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&component_a).await);
        assert!(execution_is_shut_down(&component_b).await);

        // Now "a" is shut down. There should be no events though because the component was
        // never started.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&component_a).await);
        assert!(execution_is_shut_down(&component_b).await);
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
        let component_a = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);

        // Register shutdown action on "a", and wait for it.
        ActionSet::register(component_a.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(execution_is_shut_down(&component_a).await);
        // Get component without resolving it.
        let component_b = {
            let state = component_a.lock_state().await;
            match *state {
                InstanceState::Resolved(ref s) => {
                    s.get_live_child(&PartialMoniker::from("b")).expect("child b not found")
                }
                _ => panic!("not resolved"),
            }
        };
        assert!(execution_is_shut_down(&component_b).await);
        assert!(is_unresolved(&component_b).await);

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
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let component_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
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
                        source: OfferSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("d".to_string()),
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
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let component_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;
        let component_e = test.look_up(vec!["a:0", "b:0", "e:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_e).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;
        let component_e_info = ComponentInfo::new(component_e).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        component_e_info.check_is_shut_down(&test.runner).await;

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
                        source: OfferSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("e".to_string()),
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
        let component_a = test.look_up(moniker_a.clone()).await;
        let component_b = test.look_up(moniker_b.clone()).await;
        let component_c = test.look_up(moniker_c.clone()).await;
        let component_d = test.look_up(moniker_d.clone()).await;
        let component_e = test.look_up(moniker_e.clone()).await;
        let component_f = test.look_up(moniker_f.clone()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_e).await);
        assert!(is_executing(&component_f).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;
        let component_e_info = ComponentInfo::new(component_e).await;
        let component_f_info = ComponentInfo::new(component_f).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        component_e_info.check_is_shut_down(&test.runner).await;
        component_f_info.check_is_shut_down(&test.runner).await;

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
                        source: OfferSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("d".to_string()),
                        source_name: "serviceD".into(),
                        target_name: "serviceD".into(),
                        target: OfferTarget::Child("f".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("e".to_string()),
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
        let component_a = test.look_up(moniker_a.clone()).await;
        let component_b = test.look_up(moniker_b.clone()).await;
        let component_c = test.look_up(moniker_c.clone()).await;
        let component_d = test.look_up(moniker_d.clone()).await;
        let component_e = test.look_up(moniker_e.clone()).await;
        let component_f = test.look_up(moniker_f.clone()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_e).await);
        assert!(is_executing(&component_f).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;
        let component_e_info = ComponentInfo::new(component_e).await;
        let component_f_info = ComponentInfo::new(component_f).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
        component_e_info.check_is_shut_down(&test.runner).await;
        component_f_info.check_is_shut_down(&test.runner).await;

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
                        source: OfferSource::Child("c".to_string()),
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
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let component_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up and dependency order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;

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
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_b2 = test.look_up(vec!["a:0", "b:0", "b:0"].into()).await;

        // Bind to second `b`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&component_b.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&component_b2.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_b2).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_b2_info = ComponentInfo::new(component_b2).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up and dependency order.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_b2_info.check_is_shut_down(&test.runner).await;
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
            async fn on(self: Arc<Self>, event: &hooks::Event) -> Result<(), ModelError> {
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
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let component_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);

        let component_a_info = ComponentInfo::new(component_a).await;
        let component_b_info = ComponentInfo::new(component_b).await;
        let component_c_info = ComponentInfo::new(component_c).await;
        let component_d_info = ComponentInfo::new(component_d).await;

        // Register shutdown action on "a", and wait for it. "b"'s component shuts down, but "b"
        // returns an error so "a" does not.
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect_err("shutdown succeeded unexpectedly");
        component_a_info.check_not_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
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
        ActionSet::register(component_a_info.component.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        component_a_info.check_is_shut_down(&test.runner).await;
        component_b_info.check_is_shut_down(&test.runner).await;
        component_c_info.check_is_shut_down(&test.runner).await;
        component_d_info.check_is_shut_down(&test.runner).await;
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
        let component_root = test.look_up(vec![].into()).await;
        ActionSet::register(component_root.clone(), MarkDeletingAction::new("a:0".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&component_root, "a:0".into()).await);
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
        ActionSet::register(component_root.clone(), MarkDeletingAction::new("a:0".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&component_root, "a:0".into()).await);
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
        let component_root = test.look_up(vec![].into()).await;
        ActionSet::register(component_root.clone(), MarkDeletingAction::new("coll:a:1".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&component_root, "coll:a:1".into()).await);
        assert!(!is_deleting(&component_root, "coll:b:2".into()).await);

        // Register `mark_deleting` action for "b".
        ActionSet::register(component_root.clone(), MarkDeletingAction::new("coll:b:1".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&component_root, "coll:a:1".into()).await);
        assert!(is_deleting(&component_root, "coll:b:2".into()).await);
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
        // Bind to the component, causing it to start. This should cause the component to have an
        // `Execution`.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);

        // Register delete child action, and wait for it. Component should be destroyed.
        ActionSet::register(component_root.clone(), DeleteChildAction::new("a:0".into()))
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
                vec![Lifecycle::Stop(vec!["a:0"].into()), Lifecycle::Destroy(vec!["a:0"].into())],
            );
        }

        // Trying to bind to the component should fail because it's shut down.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect_err("successfully bound to a after shutdown");

        // Destroy the component again. This succeeds, but has no additional effect.
        ActionSet::register(component_root.clone(), DeleteChildAction::new("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
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
        let component_root = test.look_up(vec![].into()).await;
        let component_container = test.look_up(vec!["container:0"].into()).await;
        let component_a = test.look_up(vec!["container:0", "coll:a:1"].into()).await;
        let component_b = test.look_up(vec!["container:0", "coll:b:2"].into()).await;
        test.model
            .bind(&component_container.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to container");
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:a");
        test.model
            .bind(&component_b.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to coll:b");
        assert!(is_executing(&component_container).await);
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);

        // Register delete child action, and wait for it. Components should be destroyed.
        let component_container = test.look_up(vec!["container:0"].into()).await;
        ActionSet::register(component_root.clone(), DeleteChildAction::new("container:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_child_deleted(&component_root, &component_container).await);
        assert!(is_destroyed(&component_container).await);
        assert!(is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_already_shut_down() {
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

        // Now delete child "a". This should cause all components to be destroyed.
        ActionSet::register(component_root.clone(), DeleteChildAction::new("a:0".into()))
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
                    Lifecycle::Destroy(vec!["a:0", "b:0"].into()),
                    Lifecycle::Destroy(vec!["a:0"].into()),
                ]
            );
        }
    }

    async fn setup_destroy_blocks_test(event_type: EventType) -> (ActionsTest, EventStream) {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let events = vec![event_type.into()];
        let mut event_source = test
            .builtin_environment
            .event_source_factory
            .create_for_debug(EventMode::Sync)
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
        event_source.start_component_tree().await;
        let model = test.model.clone();
        fasync::Task::spawn(async move { model.start().await }).detach();
        (test, event_stream)
    }

    async fn run_destroy_blocks_test<A>(
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
        let (f, delete_handle) = {
            let component_root = component_root.clone();
            async move {
                ActionSet::register(component_root, DeleteChildAction::new("a:0".into()))
                    .await
                    .expect("destroy failed");
            }
            .remote_handle()
        };
        fasync::Task::spawn(f).detach();

        // Check that `action` is being waited on.
        let component_a = match *component_root.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_live_child(&PartialMoniker::from("a")).expect("child a not found")
            }
            _ => panic!("not resolved"),
        };
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_blocks_on_discover() {
        let (test, mut event_stream) = setup_destroy_blocks_test(EventType::Discovered).await;
        run_destroy_blocks_test(
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_blocks_on_resolve() {
        let (test, mut event_stream) = setup_destroy_blocks_test(EventType::Resolved).await;
        let event = event_stream.wait_until(EventType::Resolved, vec![].into()).await.unwrap();
        event.resume();
        // Cause `a` to resolve.
        let look_up_a = async {
            // This could fail if it races with deletion.
            let _ = test.model.look_up(&vec!["a:0"].into()).await;
        };
        join!(
            look_up_a,
            run_destroy_blocks_test(
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_blocks_on_start() {
        let (test, mut event_stream) = setup_destroy_blocks_test(EventType::Started).await;
        let event = event_stream.wait_until(EventType::Started, vec![].into()).await.unwrap();
        event.resume();
        // Cause `a` to start.
        let bind_a = async {
            // This could fail if it races with deletion.
            let _ = test.model.bind(&vec!["a:0"].into(), &BindReason::Eager).await;
        };
        join!(
            bind_a,
            run_destroy_blocks_test(
                &test,
                &mut event_stream,
                EventType::Started,
                StartAction::new(BindReason::Eager),
                // expected_ref_count:
                // - 1 for the ActionSet
                // - 1 for the task that registers the action
                // - 1 for DestroyAction to wait on the action
                3,
            ),
        );
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
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        // Get component_b without resolving it.
        let component_b = match *component_a.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_live_child(&PartialMoniker::from("b")).expect("child b not found")
            }
            _ => panic!("not resolved"),
        };

        // Register delete action on "a", and wait for it.
        ActionSet::register(component_root.clone(), DeleteChildAction::new("a:0".into()))
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
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let component_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;
        let component_x = test.look_up(vec!["x:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        test.model
            .bind(&component_x.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to x");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);
        assert!(is_executing(&component_x).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // in "a"'s component to be shut down and destroyed, in bottom-up order, but "x" is still
        // running.
        ActionSet::register(component_root.clone(), DeleteChildAction::new("a:0".into()))
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
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_b2 = test.look_up(vec!["a:0", "b:0", "b:0"].into()).await;

        // Bind to second `b`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&component_b.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        test.model
            .bind(&component_b2.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to b2");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_b2).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // that were started to be destroyed, in bottom-up order.
        ActionSet::register(component_root.clone(), DeleteChildAction::new("a:0".into()))
            .await
            .expect("delete child failed");
        assert!(is_child_deleted(&component_root, &component_a).await);
        assert!(is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
        assert!(is_destroyed(&component_b2).await);
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
            async fn on(self: Arc<Self>, event: &hooks::Event) -> Result<(), ModelError> {
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
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a:0"].into()).await;
        let component_b = test.look_up(vec!["a:0", "b:0"].into()).await;
        let component_c = test.look_up(vec!["a:0", "b:0", "c:0"].into()).await;
        let component_d = test.look_up(vec!["a:0", "b:0", "d:0"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model
            .bind(&component_a.abs_moniker, &BindReason::Eager)
            .await
            .expect("could not bind to a");
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        assert!(is_executing(&component_c).await);
        assert!(is_executing(&component_d).await);

        // Register delete action on "a", and wait for it. "b"'s component is deleted, but "b"
        // returns an error so the delete action on "a" does not succeed.
        ActionSet::register(component_root.clone(), DeleteChildAction::new("a:0".into()))
            .await
            .expect_err("destroy succeeded unexpectedly");
        assert!(has_child(&component_root, "a:0").await);
        assert!(!has_child(&component_a, "b:0").await);
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
                Lifecycle::Destroy(vec!["a:0", "b:0", "c:0"].into()),
                Lifecycle::Destroy(vec!["a:0", "b:0", "d:0"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(events, vec![Lifecycle::Destroy(vec!["a:0", "b:0"].into())]);
        }

        // Register destroy action on "a:0" again. "b:0"'s delete succeeds, and "a:0" is deleted this
        // time.
        ActionSet::register(component_root.clone(), DeleteChildAction::new("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(!has_child(&component_root, "a:0").await);
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

    async fn is_executing(component: &ComponentInstance) -> bool {
        component.lock_execution().await.runtime.is_some()
    }

    async fn is_deleting(component: &ComponentInstance, moniker: ChildMoniker) -> bool {
        let partial = moniker.to_partial();
        match *component.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_live_child(&partial).is_none() && s.get_child(&moniker).is_some()
            }
            InstanceState::Destroyed => false,
            InstanceState::New | InstanceState::Discovered => {
                panic!("not resolved")
            }
        }
    }

    /// Verifies that a child component is deleted by checking its InstanceState and verifying that it
    /// does not exist in the InstanceState of its parent. Assumes the parent is not destroyed yet.
    async fn is_child_deleted(parent: &ComponentInstance, child: &ComponentInstance) -> bool {
        let child_moniker = child.abs_moniker.leaf().expect("Root component cannot be destroyed");
        let partial_moniker = child_moniker.to_partial();

        // Verify the parent-child relationship
        assert_eq!(parent.abs_moniker.child(child_moniker.clone()), child.abs_moniker);

        let parent_state = parent.lock_state().await;
        let parent_resolved_state = match *parent_state {
            InstanceState::Resolved(ref s) => s,
            _ => panic!("not resolved"),
        };

        let child_state = child.lock_state().await;
        let child_execution = child.lock_execution().await;

        let found_partial_moniker = parent_resolved_state
            .live_children()
            .find(|(curr_partial_moniker, _)| **curr_partial_moniker == partial_moniker);
        let found_child_moniker = parent_resolved_state.all_children().get(child_moniker);

        found_partial_moniker.is_none()
            && found_child_moniker.is_none()
            && matches!(*child_state, InstanceState::Destroyed)
            && child_execution.runtime.is_none()
            && child_execution.is_shut_down()
    }

    async fn is_destroyed(component: &ComponentInstance) -> bool {
        let state = component.lock_state().await;
        let execution = component.lock_execution().await;
        matches!(*state, InstanceState::Destroyed)
            && execution.runtime.is_none()
            && execution.is_shut_down()
    }

    async fn is_unresolved(component: &ComponentInstance) -> bool {
        let state = component.lock_state().await;
        let execution = component.lock_execution().await;
        execution.runtime.is_none()
            && matches!(*state, InstanceState::New | InstanceState::Discovered)
    }
}
