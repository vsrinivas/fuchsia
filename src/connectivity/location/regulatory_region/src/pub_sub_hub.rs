// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple publish-and-subscribe facility.

use {
    std::{
        cell::RefCell,
        collections::BTreeMap,
        future::Future,
        pin::Pin,
        task::{Context, Waker, Poll},
    },
};

/// A rendezvous point for publishers and subscribers.
pub struct PubSubHub {
    // To minimize the risk of run-time errors, we never store the borrowed `inner` in a named
    // variable. By only borrowing `inner` via temporaries, we make any simultaneous borrows easier
    // to spot in review. And spotting simultaneous borrows enables us to spot conflicting borrows
    // (simultaneous `borrow()` and `borrow_mut()`.)
    inner: RefCell<PubSubHubInner>,
}

/// The `Future` used by a subscriber to `await` updates.
pub struct PubSubFuture<'a> {
    // See comment for `PubSubHub::inner`, about how to borrow from `hub`.
    hub: &'a RefCell<PubSubHubInner>,
    id: usize,
    last_value: Option<String>,
}

struct PubSubHubInner {
    item: Option<String>,
    next_future_id: usize,
    wakers: BTreeMap<usize, Waker>,
}

impl PubSubHub {
    pub fn new() -> Self {
        Self {
            inner: RefCell::new(PubSubHubInner {
                item: None,
                next_future_id: 0,
                wakers: BTreeMap::new(),
            }),
        }
    }

    /// Publishes `new_value`.
    /// * All pending futures are woken.
    /// * Later calls to `watch_for_change()` will be evaluated against `new_value`.
    pub fn publish<S>(&self, new_value: S)
    where
        S: Into<String>,
    {
        let hub = &self.inner;
        hub.borrow_mut().item = Some(new_value.into());
        hub.borrow_mut().wakers.values().for_each(|w| w.wake_by_ref());
        hub.borrow_mut().wakers.clear()
    }

    /// Watches the value stored in this hub, resolving when the
    /// stored value differs from `last_value`.
    pub fn watch_for_change<S>(&self, last_value: Option<S>) -> PubSubFuture<'_>
    where
        S: Into<String>,
    {
        let hub = &self.inner;
        let id = hub.borrow().next_future_id;
        hub.borrow_mut().next_future_id = id.checked_add(1).expect("`id` is impossibly large");
        PubSubFuture { hub, id, last_value: last_value.map(|s| s.into()) }
    }
}

impl Future for PubSubFuture<'_> {
    type Output = Option<String>;

    fn poll(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        let hub = &self.hub;
        if hub.borrow().has_value(&self.last_value) {
            hub.borrow_mut().set_waker_for_future(self.id, context.waker().clone());
            Poll::Pending
        } else {
            Poll::Ready(hub.borrow().get_value())
        }
    }
}

impl PubSubHubInner {
    fn set_waker_for_future(&mut self, future_id: usize, waker: Waker) {
        self.wakers.insert(future_id, waker);
    }

    fn has_value(&self, expected: &Option<String>) -> bool {
        self.item == *expected
    }

    fn get_value(&self) -> Option<String> {
        self.item.clone()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::PubSubHub,
        fuchsia_async as fasync,
        futures_test::task::new_count_waker,
        std::future::Future,
        std::pin::Pin,
        std::task::{Context, Poll},
    };

    #[fasync::run_until_stalled(test)]
    async fn watch_for_change_future_is_pending_when_both_values_are_none() {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));
        assert_eq!(0, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn watch_for_change_future_is_pending_when_values_are_same_and_not_none() {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        hub.publish("US");

        let mut future = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));
        assert_eq!(0, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn watch_for_change_future_is_immediately_ready_when_argument_differs_from_published_value(
    ) {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        hub.publish("US");

        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Ready(Some("US".to_string())), Pin::new(&mut future).poll(&mut context));
        assert_eq!(0, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn single_watcher_is_woken_correctly_on_change_from_none_to_some() {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Change value, and expect wake, and new value.
        hub.publish("US");
        assert_eq!(1, count.get());
        assert_eq!(Poll::Ready(Some("US".to_string())), Pin::new(&mut future).poll(&mut context));
    }

    #[fasync::run_until_stalled(test)]
    async fn single_watcher_is_woken_correctly_on_change_from_some_to_new_some() {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        hub.publish("US");

        let mut future = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Change value, and expect wake, and new value.
        hub.publish("SU");
        assert_eq!(1, count.get());
        assert_eq!(Poll::Ready(Some("SU".to_string())), Pin::new(&mut future).poll(&mut context));
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_watchers_are_woken_correctly_on_change_from_some_to_new_some() {
        let hub = PubSubHub::new();
        let (waker_a, wake_count_a) = new_count_waker();
        let (waker_b, wake_count_b) = new_count_waker();
        let mut context_a = Context::from_waker(&waker_a);
        let mut context_b = Context::from_waker(&waker_b);
        hub.publish("US");

        let mut future_a = hub.watch_for_change(Some("US"));
        let mut future_b = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future_a).poll(&mut context_a), "for future a");
        assert_eq!(Poll::Pending, Pin::new(&mut future_b).poll(&mut context_b), "for future b");

        // Change value, and expect wakes, and new value for both futures.
        hub.publish("SU");
        assert_eq!(1, wake_count_a.get(), "for waker a");
        assert_eq!(1, wake_count_b.get(), "for waker b");
        assert_eq!(
            Poll::Ready(Some("SU".to_string())),
            Pin::new(&mut future_a).poll(&mut context_a),
            "for future a"
        );
        assert_eq!(
            Poll::Ready(Some("SU".to_string())),
            Pin::new(&mut future_b).poll(&mut context_b),
            "for future b"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_watchers_are_woken_correctly_after_spurious_update() {
        let hub = PubSubHub::new();
        let (waker_a, wake_count_a) = new_count_waker();
        let (waker_b, wake_count_b) = new_count_waker();
        let mut context_a = Context::from_waker(&waker_a);
        let mut context_b = Context::from_waker(&waker_b);
        hub.publish("US");

        let mut future_a = hub.watch_for_change(Some("US"));
        let mut future_b = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future_a).poll(&mut context_a), "for future a");
        assert_eq!(Poll::Pending, Pin::new(&mut future_b).poll(&mut context_b), "for future b");

        // Generate spurious update.
        hub.publish("US");
        assert_eq!(Poll::Pending, Pin::new(&mut future_a).poll(&mut context_a), "for future a");
        assert_eq!(Poll::Pending, Pin::new(&mut future_b).poll(&mut context_b), "for future b");

        // Generate a real update. Expect wakes, and new value for both futures.
        let old_wake_count_a = wake_count_a.get();
        let old_wake_count_b = wake_count_b.get();
        hub.publish("SU");
        assert_eq!(1, wake_count_a.get() - old_wake_count_a);
        assert_eq!(1, wake_count_b.get() - old_wake_count_b);
        assert_eq!(
            Poll::Ready(Some("SU".to_string())),
            Pin::new(&mut future_a).poll(&mut context_a),
            "for future a"
        );
        assert_eq!(
            Poll::Ready(Some("SU".to_string())),
            Pin::new(&mut future_b).poll(&mut context_b),
            "for future b"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_watchers_can_share_a_waker() {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future_a = hub.watch_for_change(Option::<String>::None);
        let mut future_b = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future_a).poll(&mut context), "for future a");
        assert_eq!(Poll::Pending, Pin::new(&mut future_b).poll(&mut context), "for future b");

        // Change value, and expect wakes, and new value for both futures.
        hub.publish("US");
        assert_eq!(2, count.get());
        assert_eq!(
            Poll::Ready(Some("US".to_string())),
            Pin::new(&mut future_a).poll(&mut context),
            "for future a"
        );
        assert_eq!(
            Poll::Ready(Some("US".to_string())),
            Pin::new(&mut future_b).poll(&mut context),
            "for future b"
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn single_watcher_is_not_woken_again_after_future_is_ready() {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Publish an update, which resolves `future`.
        hub.publish("US");
        assert_eq!(1, count.get());
        assert_eq!(Poll::Ready(Some("US".to_string())), Pin::new(&mut future).poll(&mut context));

        // Further updates should leave `count` unchanged, since they should not wake `waker`.
        hub.publish("SU");
        assert_eq!(1, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn second_watcher_is_woken_for_second_update() {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Publish first update, which resolves `future`.
        hub.publish("US");
        assert_eq!(1, count.get());
        assert_eq!(Poll::Ready(Some("US".to_string())), Pin::new(&mut future).poll(&mut context));

        // Create a new `future`, and verify that a second update resolves the new `future`.
        let mut future = hub.watch_for_change(Some("US"));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));
        hub.publish("SU");
        assert!(count.get() > 1, "Count should be >1, but is {}", count.get());
        assert_eq!(Poll::Ready(Some("SU".to_string())), Pin::new(&mut future).poll(&mut context));
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_polls_of_single_watcher_do_not_cause_multiple_wakes_when_waker_is_reused() {
        let hub = PubSubHub::new();
        let (waker, count) = new_count_waker();
        let mut context = Context::from_waker(&waker);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context));

        // Publish an update, which resolves `future`.
        hub.publish("US");
        assert_eq!(1, count.get());
    }

    #[fasync::run_until_stalled(test)]
    async fn multiple_polls_of_single_watcher_do_not_cause_multiple_wakes_when_waker_is_replaced() {
        let hub = PubSubHub::new();
        let (waker_a, wake_count_a) = new_count_waker();
        let (waker_b, wake_count_b) = new_count_waker();
        let mut context_a = Context::from_waker(&waker_a);
        let mut context_b = Context::from_waker(&waker_b);
        let mut future = hub.watch_for_change(Option::<String>::None);
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context_a));
        assert_eq!(Poll::Pending, Pin::new(&mut future).poll(&mut context_b));

        // Publish an update, which resolves `future`.
        hub.publish("US");
        assert_eq!(0, wake_count_a.get());
        assert_eq!(1, wake_count_b.get());
    }
}
