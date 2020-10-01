// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::hanging_get::error::HangingGetServerError,
    core::hash::Hash,
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc},
};

/// A broker used to create `Publishers` and `Subscribers` of updates to some state.
pub struct HangingGet<S, O, F: Fn(&S, O) -> bool> {
    inner: Arc<Mutex<HangingGetInner<S, subscriber_key::Key, O, F>>>,
    /// A `subscriber_key::Key` held by the broker to track the next unique key that the broker can
    /// assign to a `Subscriber`.
    subscriber_key_generator: subscriber_key::Generator,
}

impl<S, O, F> HangingGet<S, O, F>
where
    F: Fn(&S, O) -> bool,
{
    /// Create a new broker.
    /// `state` is the initial state of the HangingGet
    /// `notify` is a `Fn` that is used to notify observers of state.
    pub fn new(state: S, notify: F) -> Self {
        Self {
            inner: Arc::new(Mutex::new(HangingGetInner::new(state, notify))),
            subscriber_key_generator: subscriber_key::Generator::default(),
        }
    }

    /// Create a new `Publisher` that can make atomic updates to the state value.
    pub fn new_publisher(&self) -> Publisher<S, O, F> {
        Publisher { inner: self.inner.clone() }
    }

    /// Create a unique `Subscriber` that represents a single hanging get client.
    pub fn new_subscriber(&mut self) -> Subscriber<S, O, F> {
        Subscriber { inner: self.inner.clone(), key: self.subscriber_key_generator.next().unwrap() }
    }
}

/// A `Subscriber` can be used to register observation requests with the `HangingGet`.
/// These observations will be fulfilled when the state changes or immediately the first time
/// a `Subscriber` registers an observation.
pub struct Subscriber<S, O, F: Fn(&S, O) -> bool> {
    inner: Arc<Mutex<HangingGetInner<S, subscriber_key::Key, O, F>>>,
    key: subscriber_key::Key,
}

impl<S, O, F> Subscriber<S, O, F>
where
    F: Fn(&S, O) -> bool,
{
    /// Register a new observation.
    /// Errors occur when:
    /// * A Subscriber attempts to register an observation when there is already an outstanding
    ///   observation waiting on updates.
    pub fn register(&self, observation: O) -> Result<(), HangingGetServerError> {
        self.inner.lock().subscribe(self.key, observation)
    }
}

/// A `Publisher` is used to make changes to the state contained within a `HangingGet`.
/// It is designed to be cheaply cloned and `Send`.
pub struct Publisher<S, O, F: Fn(&S, O) -> bool> {
    inner: Arc<Mutex<HangingGetInner<S, subscriber_key::Key, O, F>>>,
}

impl<S, O, F: Fn(&S, O) -> bool> Clone for Publisher<S, O, F> {
    fn clone(&self) -> Self {
        Self { inner: self.inner.clone() }
    }
}

impl<S, O, F> Publisher<S, O, F>
where
    F: Fn(&S, O) -> bool,
{
    /// `set` is a specialized form of `update` that sets the hanging get state to the value
    /// passed in as the `state` parameter.
    /// Any subscriber that has registered an observer will immediately be notified of the
    /// update.
    pub fn set(&mut self, state: S) {
        self.inner.lock().set(state)
    }

    /// Pass a function to the hanging get that can update the hanging get state in place.
    /// Any subscriber that has registered an observer will immediately be notified of the
    /// update.
    pub fn update<UpdateFn>(&mut self, update: UpdateFn)
    where
        UpdateFn: FnOnce(&mut S) -> bool,
    {
        self.inner.lock().update(update)
    }
}

/// A `HangingGetInner` object manages some internal state `S` and notifies observers of type `O`
/// when their view of the state is outdated.
///
/// `S` - the type of State to be watched
/// `O` - the type of Observers of `S`
/// `F` - the type of observer notification behavior, where `F: Fn(&S, O)`
/// `K` - the Key by which Observers are identified
///
/// While it _can_ be used directly, most API consumers will want to use the higher level
/// `HangingGet` object. `HangingGet` and its companion types provide `Send`
/// for use from multiple threads or async tasks.
pub struct HangingGetInner<S, K, O, F: Fn(&S, O) -> bool> {
    state: S,
    notify: F,
    observers: HashMap<K, Window<O>>,
}

impl<S, K, O, F> HangingGetInner<S, K, O, F>
where
    K: Eq + Hash,
    F: Fn(&S, O) -> bool,
{
    fn notify_all(&mut self) {
        for window in self.observers.values_mut() {
            window.notify(&self.notify, &self.state);
        }
    }

    /// Create a new `HangingGetInner`.
    /// `state` is the initial state of the HangingGetInner
    /// `notify` is a `Fn` that is used to notify observers of state.
    pub fn new(state: S, notify: F) -> Self {
        Self { state, notify, observers: HashMap::new() }
    }

    /// Set the internal state value to `state` and notify all observers.
    /// Note that notification is not conditional on the _value_ set by calling the `set` function.
    /// Notification will occur regardless of whether the `state` value differs from the value
    /// currently stored by `HangingGetInner`.
    pub fn set(&mut self, state: S) {
        self.state = state;
        self.notify_all();
    }

    /// Modify the internal state in-place using the `state_update` function. Notify all
    /// observers if `state_update` returns true.
    pub fn update(&mut self, state_update: impl FnOnce(&mut S) -> bool) {
        if state_update(&mut self.state) {
            self.notify_all();
        }
    }

    /// Register an observer as a subscriber of the state. Observers are grouped by key and
    /// all observers will the same key are assumed to have received the latest state update.
    /// If an observer with a previously unseen key subscribes, it is immediately notified
    /// to the stated. If an observer with a known key subscribes, it will only be
    /// notified when the state is updated since last sent to an observer with the same
    /// key. All unresolved observers will be resolved to the same value immediately after the state
    /// is updated.
    pub fn subscribe(&mut self, key: K, observer: O) -> Result<(), HangingGetServerError> {
        self.observers.entry(key).or_insert_with(Window::new).observe(
            observer,
            &self.notify,
            &self.state,
        )
    }

    /// Deregister all observers that subscribed with `key`. If an observer is subsequently
    /// subscribed with the same `key` value, it will be treated as a previously unseen `key`.
    pub fn unsubscribe(&mut self, key: K) {
        self.observers.remove(&key);
    }
}

/// Window tracks all observers for a given `key` and whether their view of the state is
/// `dirty` or not.
struct Window<O> {
    dirty: bool,
    observer: Option<O>,
}

impl<O> Window<O> {
    /// Create a new `Window` without an `observer` and an initial `dirty` value of `true`.
    pub fn new() -> Self {
        Window { dirty: true, observer: None }
    }

    /// Register a new observer. The observer will be notified immediately if the `Window`
    /// has a dirty view of the state. The observer will be stored for future notification
    /// if the `Window` does not have a dirty view.
    pub fn observe<S>(
        &mut self,
        observer: O,
        f: impl Fn(&S, O) -> bool,
        current_state: &S,
    ) -> Result<(), HangingGetServerError> {
        if self.observer.is_some() {
            return Err(HangingGetServerError::MultipleObservers);
        }
        self.observer = Some(observer);
        if self.dirty {
            self.notify(f, current_state);
        }
        Ok(())
    }

    /// Notify the observer _if and only if_ the `Window` has a dirty view of `state`.
    /// If an observer was present and notified, the `Window` no longer has a dirty view
    /// after this method returns.
    pub fn notify<S>(&mut self, f: impl Fn(&S, O) -> bool, state: &S) {
        match self.observer.take() {
            Some(observer) => {
                if f(state, observer) {
                    self.dirty = false;
                }
            }
            None => self.dirty = true,
        }
    }
}

/// Submodule used to keep the internals of `Key`s and `Generator`s inaccessable.
mod subscriber_key {
    /// Manages the creation and distribution of unique `Key`s
    pub struct Generator {
        next: Key,
    }

    impl Default for Generator {
        fn default() -> Self {
            Self { next: Key(0) }
        }
    }

    impl Generator {
        /// Get a unique key.
        /// Returns `None` if 2^64-2 keys have already been generated.
        pub fn next(&mut self) -> Option<Key> {
            let key = self.next.clone();
            if let Some(next) = self.next.0.checked_add(1) {
                self.next.0 = next;
                Some(key)
            } else {
                None
            }
        }
    }

    /// An internal per-subscriber key that is intended to be unique.
    #[derive(PartialEq, Eq, Hash, Debug, Clone, Copy)]
    pub struct Key(u64);
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::hanging_get::test_util::TestObserver, async_utils::PollExt,
        fuchsia_async as fasync, futures::channel::oneshot,
    };

    #[test]
    fn subscriber_key_generator_creates_unique_keys() {
        let mut gen = subscriber_key::Generator::default();
        let key1 = gen.next();
        let key2 = gen.next();
        assert!(key1 != key2);
    }

    #[test]
    fn window_add_first_observer_notifies() {
        let state = 0;
        let mut window = Window::new();
        window.observe(TestObserver::expect_value(state), TestObserver::observe, &state).unwrap();
    }

    #[test]
    fn window_add_second_observer_does_not_notify() {
        let state = 0;
        let mut window = Window::new();
        window.observe(TestObserver::expect_value(state), TestObserver::observe, &state).unwrap();

        // Second observer added without updating the value
        window.observe(TestObserver::expect_no_value(), TestObserver::observe, &state).unwrap();
    }

    #[test]
    fn window_add_second_observer_notifies_after_notify_call() {
        let mut state = 0;
        let mut window = Window::new();
        window.observe(TestObserver::expect_value(state), TestObserver::observe, &state).unwrap();

        state = 1;
        window.notify(TestObserver::observe, &state);

        // Second observer added without updating the value
        window.observe(TestObserver::expect_value(state), TestObserver::observe, &state).unwrap();
    }

    #[test]
    fn window_add_multiple_observers_are_notified() {
        let mut state = 0;
        let mut window = Window::new();
        window.observe(TestObserver::expect_value(state), TestObserver::observe, &state).unwrap();

        // Second observer added without updating the value
        let o1 = TestObserver::expect_value(1);
        let o2 = TestObserver::expect_no_value();
        window.observe(o1.clone(), TestObserver::observe, &state).unwrap();
        let result = window.observe(o2.clone(), TestObserver::observe, &state);
        assert_eq!(result.unwrap_err(), HangingGetServerError::MultipleObservers);
        assert!(!o1.has_value());
        state = 1;
        window.notify(TestObserver::observe, &state);
    }

    #[test]
    fn window_dirty_flag_state() {
        let state = 0;
        let mut window = Window::new();
        let o = TestObserver::expect_value(state);
        window.observe(o, TestObserver::observe, &state).unwrap();
        assert!(window.observer.is_none());
        assert!(!window.dirty);
        window.notify(TestObserver::observe, &state);
        assert!(window.dirty);
        let o = TestObserver::expect_value(state);
        window.observe(o, TestObserver::observe, &state).unwrap();
        assert!(!window.dirty);
    }

    #[test]
    fn window_dirty_flag_respects_consumed_flag() {
        let state = 0;
        let mut window = Window::new();

        let o = TestObserver::expect_value(state);
        window.observe(o, TestObserver::observe_incomplete, &state).unwrap();
        assert!(window.dirty);
    }

    #[test]
    fn hanging_get_inner_subscribe() {
        let mut hanging = HangingGetInner::new(0, TestObserver::observe);
        let o = TestObserver::expect_value(0);
        assert!(!o.has_value());
        hanging.subscribe(0, o.clone()).unwrap();
    }

    #[test]
    fn hanging_get_inner_subscribe_then_set() {
        let mut hanging = HangingGetInner::new(0, TestObserver::observe);
        let o = TestObserver::expect_value(0);
        hanging.subscribe(0, o.clone()).unwrap();

        // No change without a new subscription
        hanging.set(1);
    }

    #[test]
    fn hanging_get_inner_subscribe_twice_then_set() {
        let mut hanging = HangingGetInner::new(0, TestObserver::observe);
        hanging.subscribe(0, TestObserver::expect_value(0)).unwrap();

        hanging.subscribe(0, TestObserver::expect_value(1)).unwrap();
        hanging.set(1);
    }

    #[test]
    fn hanging_get_inner_subscribe_multiple_then_set() {
        let mut hanging = HangingGetInner::new(0, TestObserver::observe);
        hanging.subscribe(0, TestObserver::expect_value(0)).unwrap();

        // A second subscription with the same client key should not be notified
        let o2 = TestObserver::expect_value(1);
        hanging.subscribe(0, o2.clone()).unwrap();
        assert!(!o2.has_value());

        // A third subscription will queue up along the other waiting observer
        hanging.subscribe(0, TestObserver::expect_no_value()).unwrap_err();

        // Set should notify all observers to the change
        hanging.set(1);
    }

    #[test]
    fn hanging_get_inner_subscribe_with_two_clients_then_set() {
        let mut hanging = HangingGetInner::new(0, TestObserver::observe);
        hanging.subscribe(0, TestObserver::expect_value(0)).unwrap();
        hanging.subscribe(0, TestObserver::expect_value(1)).unwrap();
        hanging.subscribe(1, TestObserver::expect_value(0)).unwrap();
        hanging.subscribe(1, TestObserver::expect_value(1)).unwrap();
        hanging.set(1);
    }

    #[test]
    fn hanging_get_inner_unsubscribe() {
        let mut hanging = HangingGetInner::new(0, TestObserver::observe);
        hanging.subscribe(0, TestObserver::expect_value(0)).unwrap();
        hanging.subscribe(0, TestObserver::expect_no_value()).unwrap();
        hanging.unsubscribe(0);
        hanging.set(1);
    }

    #[test]
    fn hanging_get_inner_unsubscribe_one_of_many() {
        let mut hanging = HangingGetInner::new(0, TestObserver::observe);

        hanging.subscribe(0, TestObserver::expect_value(0)).unwrap();
        hanging.subscribe(0, TestObserver::expect_no_value()).unwrap();
        hanging.subscribe(1, TestObserver::expect_value(0)).unwrap();
        hanging.subscribe(1, TestObserver::expect_no_value()).unwrap();

        // Unsubscribe one of the two observers
        hanging.unsubscribe(0);
        assert!(!hanging.observers.contains_key(&0));
        assert!(hanging.observers.contains_key(&1));
    }

    #[test]
    fn sync_pub_sub_updates_and_observes() {
        let mut ex = fasync::Executor::new().unwrap();
        let mut broker = HangingGet::new(0i32, |s, o: oneshot::Sender<_>| {
            o.send(s.clone()).map(|()| true).unwrap()
        });
        let mut publisher = broker.new_publisher();
        let subscriber = broker.new_subscriber();

        // Initial observation is immediate
        let (sender, mut receiver) = oneshot::channel();
        subscriber.register(sender).unwrap();
        let observation =
            ex.run_until_stalled(&mut receiver).expect("received initial observation");
        assert_eq!(observation, Ok(0));

        // Subsequent observations do not happen until after an update
        let (sender, mut receiver) = oneshot::channel();
        subscriber.register(sender).unwrap();
        assert!(ex.run_until_stalled(&mut receiver).is_pending());

        publisher.set(1);

        let observation =
            ex.run_until_stalled(&mut receiver).expect("received subsequent observation");
        assert_eq!(observation, Ok(1));
    }

    #[test]
    fn sync_pub_sub_multiple_subscribers() {
        let mut ex = fasync::Executor::new().unwrap();
        let mut broker = HangingGet::new(0i32, |s, o: oneshot::Sender<_>| {
            o.send(s.clone()).map(|()| true).unwrap()
        });
        let mut publisher = broker.new_publisher();

        let sub1 = broker.new_subscriber();
        let sub2 = broker.new_subscriber();

        // Initial observation for subscribers is immediate
        let (sender, mut receiver) = oneshot::channel();
        sub1.register(sender).unwrap();
        let observation =
            ex.run_until_stalled(&mut receiver).expect("received initial observation");
        assert_eq!(observation, Ok(0));

        let (sender, mut receiver) = oneshot::channel();
        sub2.register(sender).unwrap();
        let observation =
            ex.run_until_stalled(&mut receiver).expect("received initial observation");
        assert_eq!(observation, Ok(0));

        // Subsequent observations do not happen until after an update
        let (sender, mut recv1) = oneshot::channel();
        sub1.register(sender).unwrap();
        assert!(ex.run_until_stalled(&mut recv1).is_pending());

        let (sender, mut recv2) = oneshot::channel();
        sub2.register(sender).unwrap();
        assert!(ex.run_until_stalled(&mut recv2).is_pending());

        publisher.set(1);
        let obs1 =
            ex.run_until_stalled(&mut recv1).expect("receiver 1 received subsequent observation");
        assert_eq!(obs1, Ok(1));
        let obs2 =
            ex.run_until_stalled(&mut recv2).expect("receiver 2 received subsequent observation");
        assert_eq!(obs2, Ok(1));
    }
}
