// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    bt_avctp::Error as AvctpError,
    futures::{
        stream::{FusedStream, Stream},
        task::AtomicWaker,
    },
    std::{
        pin::Pin,
        sync::{
            atomic::{AtomicBool, Ordering::SeqCst},
            Arc,
        },
        task::{Context, Poll},
    },
    thiserror::Error,
};

use crate::packets::Error as PacketError;

// TODO(BT-2197): change to the BT shared peer id type when the BrEdr protocol changes over.
pub type PeerId = String;

/// The error types for peer management.
#[derive(Error, Debug)]
pub enum PeerError {
    /// Error encoding/decoding packet
    #[error("Packet encoding/decoding error: {:?}", _0)]
    PacketError(PacketError),

    /// Error in protocol layer
    #[error("Protocol layer error: {:?}", _0)]
    AvctpError(AvctpError),

    #[error("Remote device was not connected")]
    RemoteNotFound,

    #[error("Remote command is unsupported")]
    CommandNotSupported,

    #[error("Target already set")]
    TargetBound,

    #[error("Remote command rejected")]
    CommandFailed,

    #[error("Unable to connect")]
    ConnectionFailure(Error),

    #[error("Unexpected response to command")]
    UnexpectedResponse,

    #[error("Generic errors")]
    GenericError(Error),

    #[doc(hidden)]
    #[error("__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

impl From<AvctpError> for PeerError {
    fn from(error: AvctpError) -> Self {
        PeerError::AvctpError(error)
    }
}

impl From<PacketError> for PeerError {
    fn from(error: PacketError) -> Self {
        PeerError::PacketError(error)
    }
}

impl From<Error> for PeerError {
    fn from(error: Error) -> Self {
        PeerError::GenericError(error)
    }
}

/// Provides a way to signal that a state change has occurred or signal that whatever is causing
/// state changes to occur has terminated. Wakes any taken polling `StateChangeStream` tasks to
/// react to the state change or to terminate themselves.
#[derive(Debug)]
pub struct StateChangeListener {
    inner: Arc<StateChangeListenerInner>,
}

#[derive(Debug)]
struct StateChangeListenerInner {
    state_changed: AtomicBool,
    terminated: AtomicBool,
    waiter: AtomicWaker,
}

impl StateChangeListener {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(StateChangeListenerInner {
                state_changed: AtomicBool::new(false),
                terminated: AtomicBool::new(false),
                waiter: AtomicWaker::new(),
            }),
        }
    }

    /// Signals that a state change has happened. Wakes the registered streams to respond.
    pub fn state_changed(&self) {
        self.inner.state_changed.store(true, SeqCst);
        self.inner.waiter.wake();
    }

    /// Signal that no more state changes will occur and to close any state watching streams.
    pub fn terminate(&self) {
        self.inner.terminated.store(true, SeqCst);
        self.inner.waiter.wake();
    }

    /// Takes a state change watching stream. Only one stream should be taken per listener.
    pub fn take_change_stream(&self) -> StateChangeStream {
        StateChangeStream { state_change_listener: self.inner.clone() }
    }
}

impl Drop for StateChangeListener {
    fn drop(&mut self) {
        self.inner.terminated.store(true, SeqCst);
        self.inner.waiter.wake();
    }
}

/// Stream taken from `StateChangeListener` to monitor state changes. Returns `Some()` when a
/// state change has occurred that should be handled or `None` when the underlying state change
/// flagging mechanism has terminated.
#[derive(Debug)]
pub struct StateChangeStream {
    state_change_listener: Arc<StateChangeListenerInner>,
}

impl Stream for StateChangeStream {
    type Item = ();

    fn poll_next(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.state_change_listener.terminated.load(SeqCst) {
            return Poll::Ready(None);
        }

        if self.state_change_listener.state_changed.swap(false, SeqCst) {
            return Poll::Ready(Some(()));
        }

        self.state_change_listener.waiter.register(&context.waker());
        Poll::Pending
    }
}

impl FusedStream for StateChangeStream {
    fn is_terminated(&self) -> bool {
        self.state_change_listener.terminated.load(SeqCst)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use futures::StreamExt;

    #[test]
    fn test_state_change_listener() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");

        let scl = StateChangeListener::new();

        let mut change_stream = scl.take_change_stream();

        let mut fut = change_stream.next();
        assert_eq!(exec.run_until_stalled(&mut fut), Poll::Pending);

        scl.state_changed();
        let mut fut = change_stream.next();
        assert_eq!(exec.run_until_stalled(&mut fut), Poll::Ready(Some(())));

        let mut fut = change_stream.next();
        assert_eq!(exec.run_until_stalled(&mut fut), Poll::Pending);

        scl.terminate();
        let mut fut = change_stream.next();
        assert_eq!(exec.run_until_stalled(&mut fut), Poll::Ready(None));
    }
}
