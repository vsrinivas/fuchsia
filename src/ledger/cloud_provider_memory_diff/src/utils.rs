// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{
        channel::oneshot,
        future::{self, FusedFuture, Future, FutureExt},
        task::Waker,
        Poll,
    },
    std::{marker::Unpin, mem, pin::Pin},
};

pub struct FutureOrEmpty<'a, F>(pub Option<&'a mut F>);

impl<'a, F> Future for FutureOrEmpty<'a, F>
where
    F: Future + Unpin,
{
    type Output = F::Output;
    fn poll(mut self: Pin<&mut Self>, waker: &Waker) -> Poll<F::Output> {
        match &mut self.0 {
            None => Poll::Pending,
            Some(fut) => fut.poll_unpin(waker),
        }
    }
}

impl<'a, F> FusedFuture for FutureOrEmpty<'a, F>
where
    F: Future + FusedFuture + Unpin,
{
    fn is_terminated(&self) -> bool {
        match &self.0 {
            None => true,
            Some(fut) => fut.is_terminated(),
        }
    }
}

/// A Signal fires once and can be watched by many clients.
pub struct Signal {
    sender: oneshot::Sender<()>,
    receiver: future::Shared<oneshot::Receiver<()>>,
}

pub type SignalWatcher = future::Shared<oneshot::Receiver<()>>;

impl Signal {
    /// Returns a new signal.
    pub fn new() -> Signal {
        let (sender, receiver) = oneshot::channel();
        Signal { sender, receiver: receiver.shared() }
    }

    /// Returns a future that completes when the signal is asserted.
    pub fn watch(&self) -> SignalWatcher {
        self.receiver.clone()
    }

    /// Asserts the signal. All associated futures will complete.
    pub fn signal(self) {
        // Unwrap ok because we hold one end of the channel.
        self.sender.send(()).unwrap()
    }

    /// Asserts the signal, then create a new signal in-place.
    pub fn signal_and_rearm(&mut self) {
        mem::replace(self, Self::new()).signal()
    }
}
