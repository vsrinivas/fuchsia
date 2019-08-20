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
/// call. However, this doesn't scale well, because it requires distributed state handling and
/// is prone to races. Actions solve this problem by allowing client code to just specify the
/// actions that need to eventually be fulfilled. The actual business logic to perform, or "roll
/// forward", the actions can be implemented by the realm itself in a coordinated manner.
///
/// `DestroyChild()` is an example of how this can work. For simplicity, suppose it's called on a
/// component with no children of its own. This might cause a chain of events like the following:
///
/// - Before it returns, the `DestroyChild` FIDL handler registers the `DeleteChild` action on the
///   containing realm for child being destroyed.
/// - This results in a call on the realm's `RealmState::roll_action()`. In response to
///   `DestroyChild`, `roll_action()` spawns a future that sets a `Destroy` action on the child.
///   Note that `roll_action()` should never block, so it always spawns any work that might block
///   in a future.
/// - `roll_action()` is called on the child. In response to `Destroy`, it sets a `Shutdown` action
///   (the component instance must be stopped before it is destroyed).
/// - `roll_action()` is called on the child again, in response to `Shutdown`. It turns out the
///   instance is still running, so the `Shutdown` future tells the instance to stop. When this
///   completes, the `Shutdown` action is finished.
/// - The future that was spawned for `Destroy` is notified that `Shutdown` completes, so it cleans
///   up the instance's resources and finishes the `Destroy` action.
/// - When the work for `Destroy` completes, the future spawned for `DestroyChild` deletes the
///   child and marks `DestroyChild` finished, which will notify the client that the action is
///   complete.
use {
    crate::model::*,
    futures::future::{poll_fn, BoxFuture},
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
/// as a result of the call. If so, the caller should invoke `RealmState::roll_action()` to ensure
/// the change in actions is acted upon.
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
    /// - A boolean that indicates whether `roll_action()` should be called to reify the action
    ///   update.
    #[must_use]
    pub fn register(&mut self, action: Action) -> (Notification, bool) {
        let needs_roll = !self.rep.contains_key(&action);
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
        (Box::pin(nf), needs_roll)
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

#[cfg(test)]
mod tests {
    use super::*;
    use futures::task::Context;

    macro_rules! results_eq {
        ($res:expr, $expected_res:expr) => {
            assert_eq!(format!("{:?}", $res), format!("{:?}", $expected_res));
        };
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn action_set() {
        let mut action_set = ActionSet::new();

        // Register some actions, and get notifications.
        let (mut nf1, needs_roll) = action_set.register(Action::Destroy);
        assert!(needs_roll);
        let (mut nf2, needs_roll) = action_set.register(Action::Shutdown);
        assert!(needs_roll);
        let (mut nf3, needs_roll) = action_set.register(Action::Destroy);
        assert!(!needs_roll);

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
        let (nf1, needs_roll) = action_set.register(Action::Destroy);
        assert!(needs_roll);
        let (nf2, needs_roll) = action_set.register(Action::Shutdown);
        assert!(needs_roll);
        let (nf3, needs_roll) = action_set.register(Action::Destroy);
        assert!(!needs_roll);

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

    fn is_pending(p: Poll<Result<(), ModelError>>) -> bool {
        match p {
            Poll::Pending => true,
            _ => false,
        }
    }
}
