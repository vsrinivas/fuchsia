// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An event that can be signaled and waited on by multiple consumers.

use {
    futures::future::{FusedFuture, Future},
    parking_lot::Mutex,
    slab::Slab,
    std::{
        fmt,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll, Waker},
    },
};

const NULL_WAKER_KEY: usize = usize::max_value();

/// An `Event` is a clonable object that can be signaled once. Calls to `.wait()` produce a future,
/// `EventWait`, that can wait on that signal. Once the `Event` has been signaled, all futures will
/// complete immediately.
#[derive(Clone)]
pub struct Event {
    inner: Arc<EventSignaler>,
}

impl Event {
    /// Create a new `Event` that has not yet been signaled.
    pub fn new() -> Self {
        Self {
            inner: Arc::new(EventSignaler {
                inner: Arc::new(Mutex::new(EventState {
                    state: State::Waiting,
                    wakers: Slab::new(),
                })),
            }),
        }
    }

    /// Signal the `Event`. Once this is done, it cannot be undone. Any tasks waiting on this
    /// `Event` will be notified and its `Future` implementation will complete.
    ///
    /// Returns true if this `Event` was the one that performed the signal operation.
    pub fn signal(&self) -> bool {
        self.inner.set(State::Signaled)
    }

    /// Return true if `Event::signal` has already been called.
    pub fn signaled(&self) -> bool {
        self.inner.inner.lock().state == State::Signaled
    }

    /// Create a new `EventWait` future that will complete after this event has been signaled.
    /// If all signalers are dropped, this future will continue to return `Poll::Pending`. To be
    /// notified when all signalers are dropped without signaling, use `wait_or_dropped`.
    pub fn wait(&self) -> EventWait {
        EventWait { inner: self.wait_or_dropped() }
    }

    /// Create a new `EventWaitResult` future that will complete after this event has been
    /// signaled or all `Event` clones have been dropped.
    ///
    /// This future will output a `Result<(), Dropped>` to indicate what has occurred.
    pub fn wait_or_dropped(&self) -> EventWaitResult {
        EventWaitResult {
            inner: (*self.inner).inner.clone(),
            waker_key: NULL_WAKER_KEY,
            terminated: false,
        }
    }
}

impl fmt::Debug for Event {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Event {{ state: {:?} }}", self.inner.inner.lock().state)
    }
}

/// `Event` state tracking enum.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
enum State {
    /// The `Event` has not yet been signaled. This is the initial state of an `Event`.
    Waiting,
    /// The `signal` method has been called on an `Event`.
    Signaled,
    /// All clones of an `Event` have been dropped without the `signal` method being called. An
    /// `Event` can never move out of the `Dropped` state.
    Dropped,
}

/// Tracks state shared by all Event clones and futures.
struct EventState {
    pub state: State,
    pub wakers: Slab<Waker>,
}

/// A handle shared between all `Event` structs for a given event. Once all `Event`s are dropped,
/// this will be dropped and will notify the `EventState` that it is unreachable by any signalers
/// and will never be signaled if it hasn't been already.
struct EventSignaler {
    inner: Arc<Mutex<EventState>>,
}

impl EventSignaler {
    /// Internal function to set the self.inner.state value if it has not already been set to
    /// `State::Signaled`. Returns true if this function call changed the value of self.inner.state.
    fn set(&self, state: State) -> bool {
        let mut guard = self.inner.lock();
        if let State::Signaled = guard.state {
            // Avoid double panicking.
            if !std::thread::panicking() {
                assert!(
                    guard.wakers.is_empty(),
                    "If there are wakers, a race condition is present"
                );
            }
            false
        } else {
            let mut wakers = std::mem::replace(&mut guard.wakers, Slab::new());
            guard.state = state;
            drop(guard);
            for waker in wakers.drain() {
                waker.wake();
            }
            true
        }
    }
}

impl Drop for EventSignaler {
    fn drop(&mut self) {
        // Indicate that all `Event` clones have been dropped. This does not set the value if it
        // has already been set to `State::Signaled`.
        self.set(State::Dropped);
    }
}

/// Future implementation for `Event::wait_or_dropped`.
#[must_use = "futures do nothing unless polled"]
pub struct EventWaitResult {
    inner: Arc<Mutex<EventState>>,
    waker_key: usize,
    terminated: bool,
}

impl Future for EventWaitResult {
    type Output = Result<(), Dropped>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        // `this: &mut Self` allows the compiler to track access to individual fields of Self as
        // distinct borrows.
        let this = self.get_mut();
        let mut guard = this.inner.lock();

        match guard.state {
            State::Waiting => {
                let mut new_key = None;
                if this.waker_key == NULL_WAKER_KEY {
                    new_key = Some(guard.wakers.insert(cx.waker().clone()));
                } else {
                    guard.wakers[this.waker_key] = cx.waker().clone();
                }

                if let Some(key) = new_key {
                    this.waker_key = key;
                }

                Poll::Pending
            }
            State::Signaled => {
                this.terminated = true;
                this.waker_key = NULL_WAKER_KEY;
                Poll::Ready(Ok(()))
            }
            State::Dropped => {
                this.terminated = true;
                this.waker_key = NULL_WAKER_KEY;
                Poll::Ready(Err(Dropped))
            }
        }
    }
}

impl FusedFuture for EventWaitResult {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

impl Unpin for EventWaitResult {}

impl Drop for EventWaitResult {
    fn drop(&mut self) {
        if self.waker_key != NULL_WAKER_KEY {
            let mut guard = self.inner.lock();
            // Avoid double panicking.
            if !std::thread::panicking() {
                assert!(
                    guard.wakers.contains(self.waker_key),
                    "EventWait contained invalid waker key"
                );
            }
            guard.wakers.remove(self.waker_key);
        }
    }
}

/// Future implementation for `Event::wait`. This future only completes when the event is signaled.
/// If all signalers are dropped, `EventWait` continues to return `Poll::Pending`.
#[must_use = "futures do nothing unless polled"]
pub struct EventWait {
    inner: EventWaitResult,
}

impl Future for EventWait {
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match Pin::new(&mut self.inner).poll(cx) {
            Poll::Ready(Ok(())) => Poll::Ready(()),
            _ => Poll::Pending,
        }
    }
}

impl FusedFuture for EventWait {
    fn is_terminated(&self) -> bool {
        self.inner.is_terminated()
    }
}

impl Unpin for EventWait {}

/// Error returned from an `EventWait` when the Event is dropped.
#[derive(Debug, Eq, PartialEq, Clone, Copy)]
pub struct Dropped;

impl fmt::Display for Dropped {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "event dropped")
    }
}

impl std::error::Error for Dropped {}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[test]
    fn signaled_method_respects_signaling() {
        let event = Event::new();
        let event_clone = event.clone();

        assert!(!event.signaled());
        assert!(!event_clone.signaled());

        event.signal();

        assert!(event.signaled());
        assert!(event_clone.signaled());
    }

    #[test]
    fn unsignaled_event_is_pending() {
        let mut ex = fasync::Executor::new().unwrap();

        let event = Event::new();
        let mut wait = event.wait();
        let mut wait_or_dropped = event.wait_or_dropped();
        assert!(ex.run_until_stalled(&mut wait).is_pending());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_pending());
    }

    #[test]
    fn signaled_event_is_ready() {
        let mut ex = fasync::Executor::new().unwrap();

        let event = Event::new();
        let mut wait = event.wait();
        let mut wait_or_dropped = event.wait_or_dropped();
        event.signal();
        assert!(ex.run_until_stalled(&mut wait).is_ready());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_ready());
    }

    #[test]
    fn event_is_ready_and_wakes_after_stalled() {
        let mut ex = fasync::Executor::new().unwrap();

        let event = Event::new();
        let mut wait = event.wait();
        let mut wait_or_dropped = event.wait_or_dropped();
        assert!(ex.run_until_stalled(&mut wait).is_pending());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_pending());
        event.signal();
        assert!(ex.run_until_stalled(&mut wait).is_ready());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_ready());
    }

    #[test]
    fn signaling_event_registers_and_wakes_multiple_waiters_properly() {
        let mut ex = fasync::Executor::new().unwrap();

        let event = Event::new();
        let mut wait_1 = event.wait();
        let mut wait_2 = event.wait();
        let mut wait_3 = event.wait();

        // Multiple waiters events are pending correctly.
        assert!(ex.run_until_stalled(&mut wait_1).is_pending());
        assert!(ex.run_until_stalled(&mut wait_2).is_pending());

        event.signal();

        // Both previously registered and unregistered event waiters complete correctly.
        assert!(ex.run_until_stalled(&mut wait_1).is_ready());
        assert!(ex.run_until_stalled(&mut wait_2).is_ready());
        assert!(ex.run_until_stalled(&mut wait_3).is_ready());
    }

    #[test]
    fn event_is_terminated_after_complete() {
        let mut ex = fasync::Executor::new().unwrap();

        let event = Event::new();
        let mut wait = event.wait();
        let mut wait_or_dropped = event.wait_or_dropped();
        assert!(ex.run_until_stalled(&mut wait).is_pending());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_pending());
        assert!(!wait.is_terminated());
        assert!(!wait_or_dropped.is_terminated());
        event.signal();
        assert!(ex.run_until_stalled(&mut wait).is_ready());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_ready());
        assert!(wait.is_terminated());
        assert!(wait_or_dropped.is_terminated());
    }

    #[test]
    fn waiter_drops_gracefully() {
        let mut ex = fasync::Executor::new().unwrap();

        let event = Event::new();
        let mut wait = event.wait();
        let mut wait_or_dropped = event.wait();
        assert!(ex.run_until_stalled(&mut wait).is_pending());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_pending());
        assert!(!wait.is_terminated());
        assert!(!wait_or_dropped.is_terminated());
        drop(wait);
        drop(wait_or_dropped);
        event.signal();
    }

    #[test]
    fn waiter_completes_after_all_events_drop() {
        let mut ex = fasync::Executor::new().unwrap();

        let event = Event::new();
        let event_clone = Event::new();
        let mut wait = event.wait();
        let mut wait_or_dropped = event.wait_or_dropped();
        assert!(ex.run_until_stalled(&mut wait).is_pending());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_pending());
        assert!(!wait.is_terminated());
        assert!(!wait_or_dropped.is_terminated());
        drop(event);
        drop(event_clone);
        assert!(ex.run_until_stalled(&mut wait).is_pending());
        assert!(ex.run_until_stalled(&mut wait_or_dropped).is_ready());
    }
}
