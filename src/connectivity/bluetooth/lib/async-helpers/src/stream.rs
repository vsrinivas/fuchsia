// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::stream_epitaph::StreamWithEpitaph,
    futures::{
        stream::{Stream, StreamExt},
        Future,
    },
    pin_utils::unsafe_pinned,
    std::{
        collections::HashMap,
        hash::Hash,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// A Stream where each yielded item is tagged with a uniform key
/// Items yielded are (K, St::Item)
///
/// Tagged streams can be easily created by using the `.tagged()` function on the `WithTag` trait.
/// The stream produced by:
///   stream.tagged(k)
/// is equivalent to that created by
///   stream.map(move |v|, (k.clone(), v)
/// BUT the Tagged type combinator provides a statically nameable type that can easily be expressed
/// in type signatures such as `IndexedStreams` below.
pub struct Tagged<K, St> {
    tag: K,
    stream: St,
}

impl<K, St: Unpin> Unpin for Tagged<K, St> {}

impl<K, St> Tagged<K, St> {
    // It is safe to take a pinned projection to `stream` as:
    // * Tagged does not implement `Drop`
    // * Tagged only implements Unpin if `stream` is Unpin.
    // * Tagged is not #[repr(packed)].
    // see: pin_utils::unsafe_pinned docs for details
    unsafe_pinned!(stream: St);
}

impl<K: Clone, St> Tagged<K, St> {
    pub fn tag(&self) -> K {
        self.tag.clone()
    }
}

/// Extension trait to allow for easy creation of a `Tagged` stream from a `Stream`.
pub trait WithTag: Sized {
    /// Produce a new stream from this one which yields item tupled with a constant tag
    fn tagged<T>(self, tag: T) -> Tagged<T, Self>;
}

impl<St: Sized> WithTag for St {
    fn tagged<T>(self, tag: T) -> Tagged<T, Self> {
        Tagged { tag, stream: self }
    }
}

impl<K: Clone, Fut: Future> Future for Tagged<K, Fut> {
    type Output = (K, Fut::Output);

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let k = self.tag.clone();
        match self.stream().poll(cx) {
            Poll::Ready(out) => Poll::Ready((k, out)),
            Poll::Pending => Poll::Pending,
        }
    }
}

impl<K: Clone, St: Stream> Stream for Tagged<K, St> {
    type Item = (K, St::Item);

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let k = self.tag.clone();
        match self.stream().poll_next(cx) {
            Poll::Ready(Some(item)) => Poll::Ready(Some((k, item))),
            Poll::Ready(None) => Poll::Ready(None),
            Poll::Pending => Poll::Pending,
        }
    }
}

/// A collection of Stream indexed by key, allowing removal by Key. When polled, a StreamMap yields
/// from whichever member stream is ready first.
/// The Stream type `St` can be `?Unpin`, as all streams are stored as pins inside the map. The Key
/// type `K` must be `Unpin`; it is unlikely that an `!Unpin` type would ever be needed as a Key.
/// StreamMap yields items of type St::Item; For a stream that yields messages tagged with their
/// Key, consider using the `IndexedStreams` type alias or using the `Tagged` combinator.
pub struct StreamMap<K, St> {
    /// Streams `St` identified by key `K`
    inner: HashMap<K, Pin<Box<St>>>,
}

impl<K: Unpin, St> Unpin for StreamMap<K, St> {}

impl<K: Eq + Hash + Unpin, St: Stream> StreamMap<K, St> {
    /// Returns an empty `StreamMap`.
    pub fn empty() -> StreamMap<K, St> {
        StreamMap { inner: HashMap::new() }
    }

    /// Insert a stream identified by `key` to the map.
    ///
    /// This method will not call `poll` on the submitted stream. The caller must ensure
    /// that `poll_next` is called in order to receive wake-up notifications for the given
    /// stream.
    pub fn insert(&mut self, key: K, stream: St) -> Option<Pin<Box<St>>> {
        self.inner.insert(key, Box::new(stream).into())
    }

    /// Returns `true` if the `StreamMap` contains `key`.
    pub fn contains_key(&self, key: &K) -> bool {
        self.inner.contains_key(key)
    }

    /// Remove the stream identified by `key`, returning it if it exists.
    pub fn remove(&mut self, key: &K) -> Option<Pin<Box<St>>> {
        self.inner.remove(key)
    }

    /// Provide mutable access to the inner hashmap.
    /// This is safe as if the stream were being polled, we would not be able to access a mutable
    /// reference to self to pass to this method.
    pub fn inner(&mut self) -> &mut HashMap<K, Pin<Box<St>>> {
        &mut self.inner
    }
}

impl<K: Clone + Eq + Hash + Unpin, St: Stream> Stream for StreamMap<K, St> {
    type Item = St::Item;

    // TODO(fxbug.dev/52050) - This implementation is a simple one, which is convenient to write but
    // suffers from a couple of known issues:
    // * The implementation is O(n) wrt the number of streams in the map. We should
    //   be able to produce an O(1) implementation at the cost of internal complexity by
    //   implementing a ready-to-run queue similarly to futures::stream::FuturesUnordered
    // * The implementation uses a stable order of iteration which could result in one particular
    //   stream starving following streams from ever being polled. The implementation makes no
    //   promises about fairness but clients may well expect a fairer distribution. We should be
    //   able to provide a round-robin implementation using a similar transformation as resolves the
    //   O(1) issue
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut result = Poll::Pending;
        let mut to_remove = Vec::new();
        // We can pull the inner value out as StreamMap is `Unpin`
        let streams = Pin::into_inner(self);
        for (key, stream) in streams.inner.iter_mut() {
            match Pin::new(&mut stream.next()).poll(cx) {
                Poll::Ready(Some(req)) => {
                    result = Poll::Ready(Some(req));
                    break;
                }
                // if a stream returns None, remove it and continue
                Poll::Ready(None) => {
                    to_remove.push(key.clone());
                }
                Poll::Pending => (),
            }
        }
        for key in to_remove {
            streams.remove(&key);
        }
        result
    }
}

/// Convenient alias for a collection of Streams indexed by key where each message is tagged and
/// stream termination is notified by key. This is especially useful for maintaining a collection
/// of fidl client request streams, and being notified when each terminates
pub type IndexedStreams<K, St> = StreamMap<K, StreamWithEpitaph<Tagged<K, St>, K>>;

#[cfg(test)]
mod test {
    use super::*;

    use {
        async_utils::stream_epitaph::{StreamItem, WithEpitaph},
        futures::channel::mpsc,
        proptest::prelude::*,
        std::{collections::HashSet, fmt::Debug},
    };

    ///! We validate the behavior of the StreamMap stream by enumerating all possible external
    ///! events, and then generating permutations of valid sequences of those events. These model
    ///! the possible executions sequences the stream could go through in program execution. We
    ///! then assert that:
    ///!   a) At all points during execution, all invariants are held
    ///!   b) The final result is as expected
    ///!
    ///! In this case, the invariants are:
    ///!   * If the map is empty, it is pending
    ///!   * If all streams are pending, the map is pending
    ///!   * otherwise the map is ready
    ///!
    ///! The result is:
    ///!   * All test messages have been injected
    ///!   * All test messages have been yielded
    ///!   * All test streams have terminated
    ///!   * No event is yielded with a given key after the stream for that key has terminated
    ///!
    ///! Together these show:
    ///!   * Progress is always eventually made - the Stream cannot be stalled
    ///!   * All inserted elements will eventually be yielded
    ///!   * Elements are never duplicated

    /// Possible actions to take in evaluating the stream
    enum Event<K> {
        /// Insert a new request stream
        InsertStream(K, mpsc::Receiver<Result<u64, ()>>),
        /// Send a new request
        SendRequest(K, mpsc::Sender<Result<u64, ()>>),
        /// Close an existing request stream
        CloseStream(K, mpsc::Sender<Result<u64, ()>>),
        /// Schedule the executor. The executor will only run the task if awoken, otherwise it will
        /// do nothing
        Execute,
    }

    impl<K: Debug> Debug for Event<K> {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            match self {
                Event::InsertStream(k, _) => write!(f, "InsertStream({:?})", k),
                Event::SendRequest(k, _) => write!(f, "SendRequest({:?})", k),
                Event::CloseStream(k, _) => write!(f, "CloseStream({:?})", k),
                Event::Execute => write!(f, "Execute"),
            }
        }
    }

    fn stream_events<K: Clone + Eq + Hash>(key: K) -> Vec<Event<K>> {
        // Ensure that the channel is big enough to always handle all the Sends we make
        let (sender, receiver) = mpsc::channel::<Result<u64, ()>>(10);
        vec![
            Event::InsertStream(key.clone(), receiver),
            Event::SendRequest(key.clone(), sender.clone()),
            Event::CloseStream(key, sender),
        ]
    }

    /// Determine how many events are sent on open channels (a channel is open if it has not been
    /// closed, even if it has not yet been inserted into the StreamMap)
    fn expected_yield<K: Eq + Hash>(events: &Vec<Event<K>>) -> usize {
        events
            .iter()
            .fold((HashSet::new(), 0), |(mut terminated, closed), event| match event {
                Event::CloseStream(k, _) => {
                    terminated.insert(k);
                    (terminated, closed)
                }
                Event::SendRequest(k, _) if !terminated.contains(k) => (terminated, closed + 1),
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
            let expected_count:u64 = execution.iter()
                .filter(|event| match event {
                    Event::CloseStream(_, _) => true,
                    _ => false,
                }).count() as u64;

            // Add enough execution events to ensure we will complete, no matter the order
            execution.extend(std::iter::repeat_with(|| Event::Execute).take((expected_count * 3) as usize));

            let (waker, count) = futures_test::task::new_count_waker();
            let send_waker = futures_test::task::noop_waker();
            let mut streams = StreamMap::empty();
            let mut next_wake = 0;
            let mut yielded = 0;
            let mut inserted = 0;
            let mut closed = 0;
            let mut events = vec![];
            for event in execution {
                match event {
                    Event::InsertStream(key, stream) => {
                        streams.insert(key, stream.tagged(key).with_epitaph(key));
                        // StreamMap does *not* wake on inserting new streams, matching the
                        // behavior of streams::SelectAll. The client *must* arrange for it to be
                        // polled again after a stream is inserted; we model that here by forcing a
                        // wake up
                        next_wake = count.get();
                    }
                    Event::SendRequest(_, mut sender) => {
                        if let Poll::Ready(Ok(())) = sender.poll_ready(&mut Context::from_waker(&send_waker)) {
                            prop_assert_eq!(sender.start_send(Ok(1)), Ok(()));
                            inserted = inserted + 1;
                        }
                    }
                    Event::CloseStream(_, mut stream) => {
                        stream.close_channel();
                    }
                    Event::Execute if count.get() >= next_wake => {
                        match Pin::new(&mut streams.next()).poll(&mut Context::from_waker(&waker)) {
                            Poll::Ready(Some(StreamItem::Item((k, v)))) => {
                                events.push(StreamItem::Item((k, v)));
                                yielded = yielded + 1;
                                // Ensure that we wake up next time;
                                next_wake = count.get();
                                // Invariant: stream(k) must be in the map
                                prop_assert!(streams.contains_key(&k))
                            }
                            Poll::Ready(Some(StreamItem::Epitaph(k))) => {
                                events.push(StreamItem::Epitaph(k));
                                closed = closed + 1;
                                // Ensure that we wake up next time;
                                next_wake = count.get();
                                // stream(k) is now terminated, but until polled again (Yielding
                                // `None`), will still be in the map
                            }
                            Poll::Ready(None) => {
                                // the Stream impl for StreamMap never completes
                                unreachable!()
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
            prop_assert_eq!(closed, expected_count, "All streams closed");
            let not_terminated =
                |key: u64, e: &StreamItem<(u64, Result<u64, ()>), u64>| match e {
                    StreamItem::Epitaph(k) if *k == key => false,
                    _ => true,
                };
            let event_of =
                |key: u64, e: &StreamItem<(u64, Result<u64, ()>), u64>| match e {
                    StreamItem::Item((k, _)) if *k == key => true,
                    _ => false,
                };
            let all_keys = 0..expected_count;
            for k in all_keys {
                prop_assert!(!streams.contains_key(&k), "All streams should now have been removed");
                prop_assert!(!events.iter().skip_while(|e| not_terminated(k, e)).any(|e| event_of(k, e)), "No events should have been yielded from a stream after it terminated");
            }
        }
    }
}
