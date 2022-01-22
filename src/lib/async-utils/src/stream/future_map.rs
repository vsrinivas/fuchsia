// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{
        stream::{FusedStream, Stream},
        Future,
    },
    std::{
        collections::HashMap,
        hash::Hash,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// A collection of Future indexed by key, allowing removal by Key. When polled, a FutureMap yields
/// from whichever member future is ready first.
/// The Future type `Fut` can be `?Unpin`, as all futures are stored as pins inside the map. The Key
/// type `K` must be `Unpin`; it is unlikely that an `!Unpin` type would ever be needed as a Key.
/// FutureMap yields items of type Fut::Output.
pub struct FutureMap<K, Fut> {
    inner: HashMap<K, Pin<Box<Fut>>>,
    is_terminated: bool,
}

impl<K, Fut> Default for FutureMap<K, Fut> {
    fn default() -> Self {
        Self { inner: Default::default(), is_terminated: false }
    }
}

impl<K: Unpin, Fut> Unpin for FutureMap<K, Fut> {}

impl<K: Eq + Hash + Unpin, Fut: Future> FutureMap<K, Fut> {
    /// Returns an empty `FutureMap`.
    pub fn new() -> Self {
        Self::default()
    }

    /// Insert a future identified by `key` to the map.
    ///
    /// This method will not call `poll` on the submitted stream. The caller must ensure
    /// that `poll_next` is called in order to receive wake-up notifications for the given
    /// stream.
    pub fn insert(&mut self, key: K, future: Fut) -> Option<Pin<Box<Fut>>> {
        let Self { inner, is_terminated } = self;
        *is_terminated = false;
        inner.insert(key, Box::new(future).into())
    }

    /// Returns `true` if the `FutureMap` contains `key`.
    pub fn contains_key(&self, key: &K) -> bool {
        self.inner.contains_key(key)
    }

    /// Remove the future identified by `key`, returning it if it exists.
    pub fn remove(&mut self, key: &K) -> Option<Pin<Box<Fut>>> {
        self.inner.remove(key)
    }

    /// Provide mutable access to the inner hashmap.
    /// This is safe as if the future were being polled, we would not be able to access a mutable
    /// reference to self to pass to this method.
    pub fn inner(&mut self) -> &mut HashMap<K, Pin<Box<Fut>>> {
        &mut self.inner
    }
}

impl<K: Clone + Eq + Hash + Unpin, Fut: Future> Stream for FutureMap<K, Fut> {
    type Item = Fut::Output;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        // We can pull the inner value out as FutureMap is `Unpin`
        let Self { inner, is_terminated } = Pin::into_inner(self);

        if inner.is_empty() {
            *is_terminated = true;
            Poll::Ready(None)
        } else {
            match inner.iter_mut().find_map(|(key, future)| match Pin::new(future).poll(cx) {
                Poll::Ready(req) => Some((key.clone(), req)),
                Poll::Pending => None,
            }) {
                Some((key, req)) => {
                    assert!(inner.remove(&key).is_some());
                    Poll::Ready(Some(req))
                }
                None => Poll::Pending,
            }
        }
    }
}

impl<K: Clone + Eq + Hash + Unpin, Fut: Future> FusedStream for FutureMap<K, Fut> {
    fn is_terminated(&self) -> bool {
        let Self { inner: _, is_terminated } = self;
        *is_terminated
    }
}

#[cfg(test)]
mod test {
    //! We validate the behavior of the FutureMap stream by enumerating all possible external
    //! events, and then generating permutations of valid sequences of those events. These model
    //! the possible executions sequences the stream could go through in program execution. We
    //! then assert that:
    //!   a) At all points during execution, all invariants are held
    //!   b) The final result is as expected
    //!
    //! In this case, the invariants are:
    //!   * If the map is empty, it is pending
    //!   * If all futures are pending, the map is pending
    //!   * otherwise the map is ready
    //!
    //! The result is:
    //!   * All test messages have been injected
    //!   * All test messages have been yielded
    //!   * All test futures have terminated
    //!   * No event is yielded with a given key after the future for that key has terminated
    //!
    //! Together these show:
    //!   * Progress is always eventually made - the Stream cannot be stalled
    //!   * All inserted elements will eventually be yielded
    //!   * Elements are never duplicated
    use {
        super::*,
        crate::stream::WithTag,
        futures::{channel::oneshot, StreamExt},
        proptest::prelude::*,
        std::{collections::HashSet, fmt::Debug},
    };

    /// Possible actions to take in evaluating the stream
    enum Event<K> {
        /// Insert a new future
        InsertFuture(K, oneshot::Receiver<Result<u64, ()>>),
        /// Send a value, completing a future.
        CompleteFuture(K, oneshot::Sender<Result<u64, ()>>),
        /// Schedule the executor. The executor will only run the task if awoken, otherwise it will
        /// do nothing
        Execute,
    }

    impl<K: Debug> Debug for Event<K> {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            match self {
                Event::InsertFuture(k, _) => write!(f, "InsertFuture({:?})", k),
                Event::CompleteFuture(k, _) => write!(f, "SendRequest({:?})", k),
                Event::Execute => write!(f, "Execute"),
            }
        }
    }

    fn stream_events<K: Clone + Eq + Hash>(key: K) -> Vec<Event<K>> {
        let (sender, receiver) = oneshot::channel::<Result<u64, ()>>();
        vec![Event::InsertFuture(key.clone(), receiver), Event::CompleteFuture(key, sender)]
    }

    /// Determine how many events are sent on open channels (a channel is open if it has not been
    /// closed, even if it has not yet been inserted into the FutureMap)
    fn expected_yield<K: Eq + Hash>(events: &Vec<Event<K>>) -> usize {
        events
            .iter()
            .fold((HashSet::new(), 0), |(mut terminated, closed), event| match event {
                Event::CompleteFuture(k, _) => {
                    assert!(
                        !terminated.contains(k),
                        "There should be no more than one future per key"
                    );
                    let _: bool = terminated.insert(k);
                    (terminated, closed + 1)
                }
                _ => (terminated, closed),
            })
            .1
    }

    /// Strategy that produces random permutations of a set of events, corresponding to inserting,
    /// sending and completing up to n different streams in random order, also interspersed with
    /// running the executor
    fn execution_sequences(n: u64) -> impl Strategy<Value = Vec<Event<u64>>> {
        fn generate_events(n: u64) -> Vec<Event<u64>> {
            let mut events = (0..n).flat_map(|n| stream_events(n)).collect::<Vec<_>>();
            events.extend(std::iter::repeat_with(|| Event::Execute).take((n * 3) as usize));
            events
        }

        // We want to produce random permutations of these events
        (0..n).prop_map(generate_events).prop_shuffle()
    }

    proptest! {
        #[test]
        fn test_invariants(mut execution in execution_sequences(4)) {
            let expected = expected_yield(&execution);

            // Add enough execution events to ensure we will complete, no matter the order
            execution.extend(std::iter::repeat_with(|| Event::Execute).take((expected * 3) as usize));

            let (waker, count) = futures_test::task::new_count_waker();
            let mut futures = FutureMap::new();
            let expected = expected as u64;
            let mut next_wake = 0;
            let mut yielded = 0;
            let mut inserted = 0;
            let mut events = vec![];
            for event in execution {
                match event {
                    Event::InsertFuture(key, future) => {
                        assert_matches::assert_matches!(futures.insert(key, future.tagged(key)), None);
                        // FutureMap does *not* wake on inserting new futures, matching the
                        // behavior of streams::SelectAll. The client *must* arrange for it to be
                        // polled again after a future is inserted; we model that here by forcing a
                        // wake up
                        next_wake = count.get();
                    }
                    Event::CompleteFuture(_, sender) => {
                        prop_assert_eq!(sender.send(Ok(1)), Ok(()));
                        inserted = inserted + 1;
                    }
                    Event::Execute if count.get() >= next_wake => {
                        match Pin::new(&mut futures.next()).poll(&mut Context::from_waker(&waker)) {
                            Poll::Ready(Some((k, v))) => {
                                events.push((k, v));
                                yielded = yielded + 1;
                                // Ensure that we wake up next time;
                                next_wake = count.get();
                                // Invariant: future(k) must be in the map
                                prop_assert!(!futures.contains_key(&k))
                            }
                            Poll::Ready(None) => {
                                // // the Stream impl for FutureMap never completes
                                // unreachable!()
                                prop_assert!(futures.inner.is_empty());
                            }
                            Poll::Pending => {
                                next_wake = count.get() + 1;
                            }
                        };
                    }
                    Event::Execute => (),
                }
            }
            prop_assert_eq!(inserted, expected, "All expected requests inserted");
            prop_assert_eq!((next_wake, count.get(), yielded), (next_wake, count.get(), expected), "All expected requests yielded");
            let all_keys = 0..expected;
            for k in all_keys {
                prop_assert!(!futures.contains_key(&k), "All futures should now have been removed");
            }
        }
    }
}
