// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The "Action" concept represents a long-running activity on a realm that should eventually
/// complete.
///
/// Actions decouple the "what" of what needs to happen to a component from the "how". Several
/// client APIs may induce long-running operations on a realm's state that complete asynchronously.
/// These operations could depend on each other in various ways.
///
/// A key property of actions is idempotency. If two equal actions are registered on a realm, the
/// work for that action is performed only once. This means that two distinct call sites can
/// register the same action, and be ensured the work is not repeated.
///
/// Here are a couple examples:
/// - A `Shutdown()` FIDL call must shut down every component instance in the tree, in
///   dependency order. For this to happen every component must shut down, but not before its
///   downstream dependencies have shut down.
/// - A `Realm.DestroyChild()` FIDL call returns right after a child component is marked deleted.
///   However, in order to actually delete the child, a sequence of events must happen:
///     * All instances in the realm must be shut down (see above)
///     * The component instance's persistent storage must be erased, if any.
///     * The component's parent must remove it as a child.
///
/// Note the interdependencies here -- destroying a component also requires shutdown, for example.
///
/// These processes could be implemented through a chain of futures in the vicinity of the API
/// call. However, this doesn't scale well, because it requires distributed state handling and is
/// prone to races. Actions solve this problem by allowing client code to just specify the actions
/// that need to eventually be fulfilled. The actual business logic to perform the actions can be
/// implemented by the realm itself in a coordinated manner.
///
/// `DestroyChild()` is an example of how this can work. For simplicity, suppose it's called on a
/// component with no children of its own. This might cause a chain of events like the following:
///
/// - Before it returns, the `DestroyChild` FIDL handler registers the `DeleteChild` action on the
///   containing realm for child being destroyed.
/// - This results in a call on the realm's `RealmState::handle_action()`. In response to
///   `DestroyChild`, `handle_action()` spawns a future that sets a `Destroy` action on the child.
///   Note that `handle_action()` should never block, so it always spawns any work that might block
///   in a future.
/// - `handle_action()` is called on the child. In response to `Destroy`, it sets a `Shutdown`
///   action
///   (the component instance must be stopped before it is destroyed).
/// - `handle_action()` is called on the child again, in response to `Shutdown`. It turns out the
///   instance is still running, so the `Shutdown` future tells the instance to stop. When this
///   completes, the `Shutdown` action is finished.
/// - The future that was spawned for `Destroy` is notified that `Shutdown` completes, so it cleans
///   up the instance's resources and finishes the `Destroy` action.
/// - When the work for `Destroy` completes, the future spawned for `DestroyChild` deletes the
///   child and marks `DestroyChild` finished, which will notify the client that the action is
///   complete.
use {
    crate::model::*,
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
/// as a result of the call. If so, the caller should invoke `RealmState::handle_action()` to
/// ensure the change in actions is acted upon.
pub struct ActionSet {
    rep: HashMap<Action, Arc<ActionStatus>>,
}

/// Type of notification returned by `ActionSet::register()`.
pub type Notification = BoxFuture<'static, Result<(), ModelError>>;

impl ActionSet {
    pub fn new() -> Self {
        ActionSet { rep: HashMap::new() }
    }

    /// Returns true if `action` is registered.
    pub fn has(&self, action: &Action) -> bool {
        self.rep.contains_key(action)
    }

    /// Registers an action in the set, returning a notification that completes when the action
    /// is finished.
    ///
    /// The return value is as follows:
    /// - A notification, i.e. a Future that completes when the action is finished. This can be
    ///   chained with other futures to perform additional work when the action completes.
    /// - A boolean that indicates whether `handle_action()` should be called to reify the action
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

/// Register and wait for an action on a realm.
pub async fn execute_action(
    model: Model,
    realm: Arc<Realm>,
    action: Action,
) -> Result<(), ModelError> {
    let nf = register_action(model, realm, action).await?;
    nf.await
}

/// Register an action on a realm.
pub async fn register_action(
    model: Model,
    realm: Arc<Realm>,
    action: Action,
) -> Result<Notification, ModelError> {
    // `RealmState::register_action()` assumes component is resolved.
    realm.resolve_decl().await?;
    let mut state = realm.lock_state().await;
    let state = state.get_mut();
    state.register_action(model, realm.clone(), action).await
}

/// Finish an action on a realm.
pub async fn finish_action(realm: Arc<Realm>, action: &Action, res: Result<(), ModelError>) {
    let mut state = realm.lock_state().await;
    let state = state.get_mut();
    state.finish_action(action, res).await
}

impl RealmState {
    /// Take any necessary actions for the new action on the given realm Should be called after a
    /// new action was registered.
    ///
    /// The work to fulfill these actions should be scheduled asynchronously, so this method should
    /// not use `await` for long-running work.
    ///
    /// REQUIRES: Component has been resolved.
    pub async fn handle_action<'a>(
        &'a mut self,
        model: Model,
        realm: Arc<Realm>,
        action: &'a Action,
    ) -> Result<(), ModelError> {
        match action {
            Action::DeleteChild(child_moniker) => {
                self.handle_delete_child(model, realm, action.clone(), child_moniker.clone())
                    .await?;
            }
            Action::Destroy => {
                self.handle_destroy(model, realm).await?;
            }
            Action::Shutdown => {
                self.handle_shutdown(model, realm).await?;
            }
        }
        Ok(())
    }

    async fn handle_shutdown(&mut self, model: Model, realm: Arc<Realm>) -> Result<(), ModelError> {
        if self.is_shut_down() && self.execution().is_none() {
            // I was shutdown, which implies my dependents have already been shut down. Nothing
            // to do.
            self.finish_action(&Action::Shutdown, Ok(())).await;
        } else {
            fasync::spawn(async move {
                let res = do_shutdown(model, realm.clone()).await;
                finish_action(realm, &Action::Shutdown, res).await;
            });
        }
        Ok(())
    }

    async fn handle_delete_child(
        &mut self,
        model: Model,
        realm: Arc<Realm>,
        action: Action,
        child_moniker: ChildMoniker,
    ) -> Result<(), ModelError> {
        if let Some(child_realm) = self.all_child_realms().get(&child_moniker) {
            // Child exists (live or deleting), schedule work.
            let child_realm = child_realm.clone();
            fasync::spawn(async move {
                let res = do_delete_child(model, realm.clone(), child_realm, child_moniker).await;
                finish_action(realm, &action, res).await;
            });
        } else {
            // The child was already deleted, so immediately complete the action.
            self.finish_action(&action, Ok(())).await;
        }
        Ok(())
    }

    async fn handle_destroy(&mut self, model: Model, realm: Arc<Realm>) -> Result<(), ModelError> {
        fasync::spawn(async move {
            let res = do_destroy(model, realm.clone()).await;
            finish_action(realm, &Action::Destroy, res).await;
        });
        Ok(())
    }
}

async fn do_shutdown(model: Model, realm: Arc<Realm>) -> Result<(), ModelError> {
    let child_realms = {
        let state = realm.lock_state().await;
        state.get().live_child_realms().clone()
    };
    // Stop children before stopping the parent.
    let mut futures = vec![];
    for (_, child_realm) in child_realms {
        let nf = register_action(model.clone(), child_realm, Action::Shutdown).await?;
        futures.push(nf);
    }
    let results = join_all(futures).await;
    ok_or_first_error(results)?;

    // Now that all children have shut down, shut down the parent.
    Realm::shut_down_instance(realm, &model.hooks).await
}

async fn do_delete_child(
    model: Model,
    realm: Arc<Realm>,
    child_realm: Arc<Realm>,
    child_moniker: ChildMoniker,
) -> Result<(), ModelError> {
    {
        let mut state = realm.lock_state().await;
        state.get_mut().mark_child_realm_deleting(&child_moniker);
    }
    execute_action(model, child_realm, Action::Destroy).await?;
    {
        let mut state = realm.lock_state().await;
        state.get_mut().remove_child_realm(&child_moniker);
    }
    Ok(())
}

async fn do_destroy(model: Model, realm: Arc<Realm>) -> Result<(), ModelError> {
    // For destruction to behave correctly, the component has to be shut down first.
    execute_action(model.clone(), realm.clone(), Action::Shutdown).await?;

    // Shutdown is complete, now destroy the component. This involves marking all children
    // "deleting", destroying them all, and finishing the action.
    let child_realms = {
        let mut state = realm.lock_state().await;
        let state = state.get_mut();
        let live_monikers = {
            let res: Vec<_> = state.live_child_realms().keys().map(|m| m.clone()).collect();
            res
        };
        for m in live_monikers {
            state.mark_child_realm_deleting(&m);
        }
        state.all_child_realms().clone()
    };
    let mut futures = vec![];
    for (child_moniker, child_realm) in child_realms {
        let realm = realm.clone();
        let nf = register_action(model.clone(), child_realm, Action::Destroy).await?;
        let fut = async move {
            nf.await?;
            let mut state = realm.lock_state().await;
            state.get_mut().remove_child_realm(&child_moniker);
            Ok(())
        };
        futures.push(fut);
    }
    let results = join_all(futures).await;
    ok_or_first_error(results)?;

    // Now that all children have been destroyed, destroy the containing realm.
    Realm::destroy_instance(realm, &model.hooks).await
}

fn ok_or_first_error(results: Vec<Result<(), ModelError>>) -> Result<(), ModelError> {
    results.into_iter().fold(Ok(()), |acc, r| acc.and_then(|_| r))
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        crate::framework::RealFrameworkServiceHost,
        crate::klog,
        crate::model::testing::{mocks::*, test_helpers::*, test_hook::*},
        cm_rust::{ChildDecl, CollectionDecl, ComponentDecl, NativeIntoFidl},
        fidl::endpoints,
        fidl_fuchsia_sys2 as fsys,
        futures::task::Context,
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

    struct ActionsTest {
        model: Model,
        hook: Arc<TestHook>,
        realm_proxy: Option<fsys::RealmProxy>,
    }

    impl ActionsTest {
        pub async fn new(
            root_component: &'static str,
            components: Vec<(&'static str, ComponentDecl)>,
            realm_moniker: Option<AbsoluteMoniker>,
        ) -> Self {
            // Ensure that kernel logging has been set up
            let _ = klog::KernelLogger::init();

            let mut resolver = ResolverRegistry::new();
            let runner = MockRunner::new();

            let mut mock_resolver = MockResolver::new();
            for (name, decl) in &components {
                mock_resolver.add_component(name, decl.clone());
            }
            resolver.register("test".to_string(), Box::new(mock_resolver));

            let hook = Arc::new(TestHook::new());
            let framework_services = Arc::new(RealFrameworkServiceHost::new());
            let model = Model::new(ModelParams {
                framework_services: framework_services.clone(),
                root_component_url: format!("test:///{}", root_component),
                root_resolver_registry: resolver,
                root_default_runner: Arc::new(runner),
                hooks: vec![hook.clone()],
                config: ModelConfig::default(),
            });

            // Host framework service for root realm, if requested.
            let realm_proxy = if let Some(realm_moniker) = realm_moniker {
                let (realm_proxy, stream) =
                    endpoints::create_proxy_and_stream::<fsys::RealmMarker>().unwrap();
                let realm = model
                    .look_up_realm(&realm_moniker)
                    .await
                    .expect(&format!("could not look up {}", realm_moniker));
                let model = model.clone();
                fasync::spawn(async move {
                    framework_services
                        .serve_realm_service(model.clone(), realm, stream)
                        .await
                        .expect("failed serving realm service");
                });
                Some(realm_proxy)
            } else {
                None
            };

            Self { model, hook, realm_proxy }
        }

        async fn look_up(&self, moniker: AbsoluteMoniker) -> Arc<Realm> {
            self.model
                .look_up_realm(&moniker)
                .await
                .expect(&format!("could not look up {}", moniker))
        }

        async fn create_dynamic_child(&self, coll: &str, name: &str) {
            let mut collection_ref = fsys::CollectionRef { name: coll.to_string() };
            let child_decl = ChildDecl {
                name: name.to_string(),
                url: format!("test:///{}", name),
                startup: fsys::StartupMode::Lazy,
            }
            .native_into_fidl();
            let res = self
                .realm_proxy
                .as_ref()
                .expect("realm service not started")
                .create_child(&mut collection_ref, child_decl)
                .await;
            res.expect("failed to create child").expect("failed to create child");
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_one_component() {
        let components = vec![
            (
                "root",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            ("a", ComponentDecl { ..default_component_decl() }),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Bind to the component, causing it to start. This should cause the realm to have an
        // `Execution`.
        let realm = test.look_up(vec!["a"].into()).await;
        test.model.bind_instance(realm.clone()).await.expect("could not bind to a");
        assert!(is_executing(&realm).await);

        // Register shutdown action, and wait for it. Component should shut down (no more
        // `Execution`).
        execute_action(test.model.clone(), realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(is_shut_down(&realm).await);

        // Trying to bind to the component should fail because it's shut down.
        test.model
            .bind_instance(realm.clone())
            .await
            .expect_err("successfully bound to a after shutdown");

        // Shut down the component again. This succeeds, but has no additional effect.
        execute_action(test.model.clone(), realm.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(is_shut_down(&realm).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_collection() {
        let components = vec![
            (
                "root",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "container".to_string(),
                        url: "test:///container".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "container",
                ComponentDecl {
                    collections: vec![CollectionDecl {
                        name: "coll".to_string(),
                        durability: fsys::Durability::Transient,
                    }],
                    ..default_component_decl()
                },
            ),
            ("a", default_component_decl()),
            ("b", default_component_decl()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Bind to the components, causing them to start. This should cause them to have an
        // `Execution`.
        let realm_a = test.look_up(vec!["container", "coll:a"].into()).await;
        let realm_b = test.look_up(vec!["container", "coll:b"].into()).await;
        test.model.bind_instance(realm_a.clone()).await.expect("could not bind to coll:a");
        test.model.bind_instance(realm_b.clone()).await.expect("could not bind to coll:b");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);

        // Register shutdown action, and wait for it. Components should shut down (no more
        // `Execution`).
        let realm_container = test.look_up(vec!["container"].into()).await;
        execute_action(test.model.clone(), realm_container.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(is_shut_down(&realm_container).await);
        assert!(is_shut_down(&realm_a).await);
        assert!(is_shut_down(&realm_b).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn shutdown_not_started() {
        let components = vec![
            (
                "root",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "a",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            ("b", ComponentDecl { ..default_component_decl() }),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a"].into()).await;
        let realm_b = test.look_up(vec!["a", "b"].into()).await;
        assert!(!is_executing(&realm_a).await);
        assert!(!is_executing(&realm_b).await);

        // Register shutdown action on "a", and wait for it.
        execute_action(test.model.clone(), realm_a.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(is_shut_down(&realm_a).await);
        assert!(is_shut_down(&realm_b).await);

        // Now "a" is shut down. Shutting it down again should short-circuit, so there is a
        // duplicate `Shutdown` for "a" but not for "b".
        execute_action(test.model.clone(), realm_a.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(is_shut_down(&realm_a).await);
        assert!(is_shut_down(&realm_b).await);
        {
            let events: Vec<_> = test
                .hook
                .lifecycle()
                .into_iter()
                .filter_map(|e| match e {
                    Lifecycle::Stop(_) => Some(e),
                    _ => None,
                })
                .collect();
            assert_eq!(
                events,
                vec![Lifecycle::Stop(vec!["a", "b"].into()), Lifecycle::Stop(vec!["a"].into()),]
            );
        }
    }

    ///  Shut down "a":
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
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "a",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Eager,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "b",
                ComponentDecl {
                    children: vec![
                        ChildDecl {
                            name: "c".to_string(),
                            url: "test:///c".to_string(),
                            startup: fsys::StartupMode::Eager,
                        },
                        ChildDecl {
                            name: "d".to_string(),
                            url: "test:///d".to_string(),
                            startup: fsys::StartupMode::Eager,
                        },
                    ],
                    ..default_component_decl()
                },
            ),
            ("c", ComponentDecl { ..default_component_decl() }),
            ("d", ComponentDecl { ..default_component_decl() }),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a"].into()).await;
        let realm_b = test.look_up(vec!["a", "b"].into()).await;
        let realm_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let realm_d = test.look_up(vec!["a", "b", "d"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind_instance(realm_a.clone()).await.expect("could not bind to a");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        execute_action(test.model.clone(), realm_a.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(is_shut_down(&realm_a).await);
        assert!(is_shut_down(&realm_b).await);
        assert!(is_shut_down(&realm_c).await);
        assert!(is_shut_down(&realm_d).await);
        {
            let mut events: Vec<_> = test
                .hook
                .lifecycle()
                .into_iter()
                .filter_map(|e| match e {
                    Lifecycle::Stop(_) => Some(e),
                    _ => None,
                })
                .collect();
            // The leaves could be stopped in any order.
            let mut first: Vec<_> = events.drain(0..2).collect();
            first.sort_unstable();
            let expected: Vec<_> = vec![
                Lifecycle::Stop(vec!["a", "b", "c"].into()),
                Lifecycle::Stop(vec!["a", "b", "d"].into()),
            ];
            assert_eq!(first, expected);
            assert_eq!(
                events,
                vec![Lifecycle::Stop(vec!["a", "b"].into()), Lifecycle::Stop(vec!["a"].into())]
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_one_component() {
        let components = vec![
            (
                "root",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            ("a", ComponentDecl { ..default_component_decl() }),
        ];
        let test = ActionsTest::new("root", components, None).await;
        // Bind to the component, causing it to start. This should cause the realm to have an
        // `Execution`.
        let realm = test.look_up(vec!["a"].into()).await;
        test.model.bind_instance(realm.clone()).await.expect("could not bind to a");
        assert!(is_executing(&realm).await);

        // Register destroy action, and wait for it. Component should be destroyed.
        execute_action(test.model.clone(), realm.clone(), Action::Destroy)
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm).await);
        {
            let events: Vec<_> = test
                .hook
                .lifecycle()
                .into_iter()
                .filter_map(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => Some(e),
                    _ => None,
                })
                .collect();
            assert_eq!(
                events,
                vec![Lifecycle::Stop(vec!["a"].into()), Lifecycle::Destroy(vec!["a"].into())],
            );
        }

        // Trying to bind to the component should fail because it's shut down.
        test.model
            .bind_instance(realm.clone())
            .await
            .expect_err("successfully bound to a after shutdown");

        // Destroy the component again. This succeeds, but has no additional effect.
        execute_action(test.model.clone(), realm.clone(), Action::Destroy)
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_collection() {
        let components = vec![
            (
                "root",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "container".to_string(),
                        url: "test:///container".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "container",
                ComponentDecl {
                    collections: vec![CollectionDecl {
                        name: "coll".to_string(),
                        durability: fsys::Durability::Transient,
                    }],
                    ..default_component_decl()
                },
            ),
            ("a", default_component_decl()),
            ("b", default_component_decl()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Bind to the components, causing them to start. This should cause them to have an
        // `Execution`.
        let realm_a = test.look_up(vec!["container", "coll:a"].into()).await;
        let realm_b = test.look_up(vec!["container", "coll:b"].into()).await;
        test.model.bind_instance(realm_a.clone()).await.expect("could not bind to coll:a");
        test.model.bind_instance(realm_b.clone()).await.expect("could not bind to coll:b");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);

        // Register destroy action, and wait for it. Components should be destroyed.
        let realm_container = test.look_up(vec!["container"].into()).await;
        execute_action(test.model.clone(), realm_container.clone(), Action::Destroy)
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm_container).await);
        assert!(is_destroyed(&realm_a).await);
        assert!(is_destroyed(&realm_b).await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn destroy_already_shut_down() {
        let components = vec![
            (
                "root",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "a",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    }],
                    ..default_component_decl()
                },
            ),
            ("b", ComponentDecl { ..default_component_decl() }),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_a = test.look_up(vec!["a"].into()).await;
        let realm_b = test.look_up(vec!["a", "b"].into()).await;

        // Register shutdown action on "a", and wait for it. This should cause all components
        // to shut down, in bottom-up order.
        execute_action(test.model.clone(), realm_a.clone(), Action::Shutdown)
            .await
            .expect("shutdown failed");
        assert!(is_shut_down(&realm_a).await);
        assert!(is_shut_down(&realm_b).await);

        // Now destroy "a". This should cause all components to be destroyed.
        execute_action(test.model.clone(), realm_a.clone(), Action::Destroy)
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&realm_a).await);
        assert!(is_destroyed(&realm_b).await);

        // Check order of events.
        {
            let events: Vec<_> = test
                .hook
                .lifecycle()
                .into_iter()
                .filter_map(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => Some(e),
                    _ => None,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a", "b"].into()),
                    Lifecycle::Stop(vec!["a"].into()),
                    Lifecycle::Destroy(vec!["a", "b"].into()),
                    Lifecycle::Destroy(vec!["a"].into()),
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
    async fn delete_hierarchy() {
        let components = vec![
            (
                "root",
                ComponentDecl {
                    children: vec![
                        ChildDecl {
                            name: "a".to_string(),
                            url: "test:///a".to_string(),
                            startup: fsys::StartupMode::Lazy,
                        },
                        ChildDecl {
                            name: "x".to_string(),
                            url: "test:///x".to_string(),
                            startup: fsys::StartupMode::Lazy,
                        },
                    ],
                    ..default_component_decl()
                },
            ),
            (
                "a",
                ComponentDecl {
                    children: vec![ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Eager,
                    }],
                    ..default_component_decl()
                },
            ),
            (
                "b",
                ComponentDecl {
                    children: vec![
                        ChildDecl {
                            name: "c".to_string(),
                            url: "test:///c".to_string(),
                            startup: fsys::StartupMode::Eager,
                        },
                        ChildDecl {
                            name: "d".to_string(),
                            url: "test:///d".to_string(),
                            startup: fsys::StartupMode::Eager,
                        },
                    ],
                    ..default_component_decl()
                },
            ),
            ("c", ComponentDecl { ..default_component_decl() }),
            ("d", ComponentDecl { ..default_component_decl() }),
            ("x", ComponentDecl { ..default_component_decl() }),
        ];
        let test = ActionsTest::new("root", components, None).await;
        let realm_root = test.look_up(vec![].into()).await;
        let realm_a = test.look_up(vec!["a"].into()).await;
        let realm_b = test.look_up(vec!["a", "b"].into()).await;
        let realm_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let realm_d = test.look_up(vec!["a", "b", "d"].into()).await;
        let realm_x = test.look_up(vec!["x"].into()).await;

        // Component startup was eager, so they should all have an `Execution`.
        test.model.bind_instance(realm_a.clone()).await.expect("could not bind to a");
        test.model.bind_instance(realm_x.clone()).await.expect("could not bind to x");
        assert!(is_executing(&realm_a).await);
        assert!(is_executing(&realm_b).await);
        assert!(is_executing(&realm_c).await);
        assert!(is_executing(&realm_d).await);
        assert!(is_executing(&realm_x).await);

        // Register destroy action on "a", and wait for it. This should cause all components
        // in "a"'s realm to be shut down and destroyed, in bottom-up order, but "x" is still
        // running.
        execute_action(test.model.clone(), realm_root.clone(), Action::DeleteChild("a".into()))
            .await
            .expect("delete child failed");
        assert!(is_destroyed(&realm_a).await);
        assert!(is_destroyed(&realm_b).await);
        assert!(is_destroyed(&realm_c).await);
        assert!(is_destroyed(&realm_d).await);
        assert!(is_executing(&realm_x).await);
        {
            // Expect only "x" as child of root.
            let state = realm_root.lock_state().await;
            let children: Vec<_> =
                state.get().all_child_realms().keys().map(|m| m.clone()).collect();
            assert_eq!(children, vec!["x".into()]);
        }
        {
            let mut events: Vec<_> = test
                .hook
                .lifecycle()
                .into_iter()
                .filter_map(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::Destroy(_) => Some(e),
                    _ => None,
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

    async fn is_executing(realm: &Realm) -> bool {
        let state = realm.lock_state().await;
        state.get().execution().is_some()
    }

    async fn is_shut_down(realm: &Realm) -> bool {
        let state = realm.lock_state().await;
        let state = state.get();
        state.execution().is_none() && state.is_shut_down()
    }

    async fn is_destroyed(realm: &Realm) -> bool {
        let state = realm.lock_state().await;
        let state = state.get();
        state.execution().is_none() && state.is_shut_down() && state.all_child_realms().is_empty()
    }

    fn is_pending(p: Poll<Result<(), ModelError>>) -> bool {
        match p {
            Poll::Pending => true,
            _ => false,
        }
    }
}
