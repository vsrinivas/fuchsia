// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    futures::{
        stream::{iter, FusedStream},
        StreamExt,
    },
    parking_lot::RwLock,
    std::{fmt::Debug, pin::Pin, sync::Arc},
};

#[derive(Debug, PartialEq)]
enum StreamStatus {
    Halted,
    Ready,
}

type SharedAtomicOperationStatus = Arc<RwLock<StreamStatus>>;

// The Token is the sole entity that is allowed to write the operation status.  On creation, it
// sets the state to Running.  One drop, it sets the state to NotRunning.
#[derive(Debug)]
pub struct Token {
    inner: SharedAtomicOperationStatus,
}

impl Token {
    fn new(inner: SharedAtomicOperationStatus) -> Self {
        *inner.write() = StreamStatus::Halted;
        Self { inner }
    }
}

impl Drop for Token {
    fn drop(&mut self) {
        *self.inner.write() = StreamStatus::Ready;
    }
}

// Wrap a stream and an overall atomic operation status for work on that stream such that it can
// produce at most one stream item at a time.
//
// The AtomicOneshotStream respects the state that has been set by its latest Token when doling out
// new atomic streams to callers.  If the state has been set to Running, the caller is handed back
// a stream that is complete.  If the state is NotRunning, a new token is created when the
// underlying stream produces an item.  This token can be held by the caller to prevent the
// AtomicOneshotStream from giving out non-complete streams.  Once the token is dropped, future
// streams produced by the AtomicOneshotStream will be capable of producing items.
pub struct AtomicOneshotStream<S: FusedStream + Unpin + Debug> {
    stream: S,
    status: SharedAtomicOperationStatus,
}

impl<S> AtomicOneshotStream<S>
where
    S: FusedStream + Unpin + Debug,
{
    pub fn new(stream: S) -> Self {
        let status = Arc::new(RwLock::new(StreamStatus::Ready));
        Self { stream, status }
    }

    pub fn get_atomic_oneshot_stream(
        &mut self,
    ) -> Pin<Box<dyn FusedStream<Item = (Token, S::Item)> + '_>> {
        // This prevents the AtomicOneshotStream from handing out multiple Tokens.
        let s = match *self.status.read() {
            StreamStatus::Ready => Some(&mut self.stream),
            // If the status indicates that an atomic operation is running, then the stream should
            // not be processed.
            StreamStatus::Halted => None,
        };

        // The `map` will never actually run in the event that s is None which prevents the Token
        // state from overwriting itself.  Rather, the None-case will trigger the fused
        // `is_terminated` == true case and the stream will be perceived to be complete.
        //
        // The new token will never actually be created unless the underlying stream actually
        // yields something which prevents the stream from accidentally deadlocking itself.  It is
        // guaranteed that there will only ever be one token.  In order for `s` to  be `Some` here,
        // the current state must be `Ready` (ie: no token has been instantiated for this stream).
        // If the underlying stream produces something, a `Token` instance is created and returned.
        // During construction, the `Token` changes the state to `Halted`.  A future attempt to
        // poll this stream, returns `complete`.  Future calls to `get_atomic_oneshot_stream` will
        // result in `s` being `None` unless the previously-issued `Token` is dropped.
        Box::pin(
            iter(s.into_iter())
                .flatten()
                .map(|item| (Token::new(self.status.clone()), item))
                .fuse(),
        )
    }
}

mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        futures::{
            channel::mpsc, future, select, stream::FuturesUnordered, task::Poll, FutureExt,
            StreamExt,
        },
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    #[fuchsia::test]
    fn test_atomic_stream() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (sender, receiver) = mpsc::unbounded();
        let mut atomic_stream = AtomicOneshotStream::new(receiver);

        // Send a message on the sender and observe that the AtomicOneshotStream can see it immediately.
        sender.unbounded_send(()).expect("failed to send test message");
        let token = {
            let mut oneshot_stream = atomic_stream.get_atomic_oneshot_stream();
            assert_variant!(
                exec.run_until_stalled(&mut oneshot_stream.next()),
                Poll::Ready(Some((token, ()))) => token
            )
        };

        // Now hold onto the token and obseve that nothing can be received on the stream.
        sender.unbounded_send(()).expect("failed to send test message");
        {
            let mut oneshot_stream = atomic_stream.get_atomic_oneshot_stream();
            assert_variant!(exec.run_until_stalled(&mut oneshot_stream.next()), Poll::Ready(None),);
        }

        // Now drop the token and observe that the message comes through.
        drop(token);
        let mut oneshot_stream = atomic_stream.get_atomic_oneshot_stream();
        assert_variant!(
            exec.run_until_stalled(&mut oneshot_stream.next()),
            Poll::Ready(Some((_, ())))
        );
    }

    #[derive(Debug)]
    enum SelectResult {
        IntegerFutureReady(u8),
        StreamReady,
    }

    fn select_helper(
        exec: &mut fasync::TestExecutor,
        fut: Pin<Box<dyn future::Future<Output = u8>>>,
        oneshot_stream: &mut Pin<Box<dyn FusedStream<Item = (Token, ())> + '_>>,
    ) -> Poll<SelectResult> {
        let mut futs = FuturesUnordered::new();
        futs.push(fut);

        let fut = async move {
            select! {
                integer = futs.select_next_some() => {
                    return SelectResult::IntegerFutureReady(integer)
                },
                (_token, ()) = oneshot_stream.select_next_some() => {
                    return SelectResult::StreamReady
                }
            }
        };

        pin_mut!(fut);
        exec.run_until_stalled(&mut fut)
    }

    #[fuchsia::test]
    fn test_poll_complete_behavior() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (sender, receiver) = mpsc::unbounded();
        let mut atomic_stream = AtomicOneshotStream::new(receiver);

        // Nothing has been send on the sender, so this is polling the underlying stream which
        // will produce pending.  The ready future will return immediately.
        let fut = future::ready(1).boxed();
        let mut oneshot_stream = atomic_stream.get_atomic_oneshot_stream();
        assert_variant!(
            select_helper(&mut exec, fut, &mut oneshot_stream),
            Poll::Ready(SelectResult::IntegerFutureReady(1)),
        );

        // Poll the underlying stream again to demonstrate that this behavior is safe.
        let fut = future::ready(2).boxed();
        assert_variant!(
            select_helper(&mut exec, fut, &mut oneshot_stream),
            Poll::Ready(SelectResult::IntegerFutureReady(2)),
        );

        // Now send something and let the select statement handle the stream.
        let fut = future::pending().boxed();
        sender.unbounded_send(()).expect("failed to send test message");
        assert_variant!(
            select_helper(&mut exec, fut, &mut oneshot_stream),
            Poll::Ready(SelectResult::StreamReady),
        );

        // Poll a pending future and the now-complete stream to demonstrate that it is safe to poll
        // again.
        let fut = future::pending().boxed();
        assert_variant!(select_helper(&mut exec, fut, &mut oneshot_stream), Poll::Pending,);
    }

    #[fuchsia::test]
    fn test_token_sets_state() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");

        let status = Arc::new(RwLock::new(StreamStatus::Ready));

        // Creating the token should update the state to Running.
        let token = Token::new(status.clone());
        assert_eq!(*status.clone().read(), StreamStatus::Halted);

        // Dropping the token should reset the state to NotRunning.
        drop(token);
        assert_eq!(*status.clone().read(), StreamStatus::Ready)
    }

    #[fuchsia::test]
    fn test_operating_state() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");

        let (sender, receiver) = mpsc::unbounded();
        let mut atomic_stream = AtomicOneshotStream::new(receiver);
        let status = atomic_stream.status.clone();

        // Verify that the atomic stream starts out in the NotRunning state.
        assert_eq!(*atomic_stream.status.read(), StreamStatus::Ready);

        // Create a oneshot stream and verify the status is still NotRunning.
        {
            let mut oneshot_stream = atomic_stream.get_atomic_oneshot_stream();
            assert_eq!(*status.read(), StreamStatus::Ready);

            // After learning that there is no event in the stream, the state should still be
            // NotRunning.
            assert_variant!(exec.run_until_stalled(&mut oneshot_stream.next()), Poll::Pending,);
            assert_eq!(*status.read(), StreamStatus::Ready);
        }

        // Send a message on the sender and observe what happens.
        sender.unbounded_send(()).expect("failed to send message");
        {
            // Initially the state is still NotRunning.
            let mut oneshot_stream = atomic_stream.get_atomic_oneshot_stream();
            assert_eq!(*status.read(), StreamStatus::Ready);

            // But once the item is actually received, the state should change.
            let token = assert_variant!(
                exec.run_until_stalled(&mut oneshot_stream.next()),
                Poll::Ready(Some((token, ()))) => token
            );
            assert_eq!(*status.read(), StreamStatus::Halted);

            // Now drop the token and observe that the state goes back to NotRunning.
            drop(token);
            assert_eq!(*status.read(), StreamStatus::Ready);
        }
    }
}
