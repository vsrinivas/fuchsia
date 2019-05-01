// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crossbeam::queue::MsQueue;
use futures::{channel::mpsc, lock::Mutex, stream::FusedStream, task::Context, Poll, Stream};
use std::{ops::DerefMut, pin::Pin, sync::Arc};

#[derive(Clone)]
struct SenderSet<T> {
    inner: Arc<Mutex<Vec<mpsc::Sender<T>>>>,
    pending_senders: Arc<MsQueue<mpsc::Sender<T>>>,
}

impl<T> SenderSet<T> {
    fn new(sender: mpsc::Sender<T>) -> Self {
        Self {
            inner: Arc::new(Mutex::new(vec![sender])),
            pending_senders: Arc::new(MsQueue::new()),
        }
    }

    fn enqueue_sender(&self, new_sender: mpsc::Sender<T>) {
        self.pending_senders.push(new_sender);
    }

    async fn take(&mut self) -> Vec<mpsc::Sender<T>> {
        let mut snapshot = vec![];
        snapshot.append(await!(self.inner.lock()).deref_mut());

        while let Some(new_sender) = self.pending_senders.try_pop() {
            snapshot.push(new_sender);
        }
        snapshot
    }

    async fn append(&mut self, mut addendum: Vec<mpsc::Sender<T>>) {
        await!(self.inner.lock()).deref_mut().append(&mut addendum)
    }
}

/// An async sender end of an mpmc channel. Messages sent on this are received by
/// _all_ receivers connected to it (they are duplicated).
#[derive(Clone)]
pub struct Sender<T> {
    inner: SenderSet<T>,
}

impl<T: Clone> Sender<T> {
    pub async fn send(&mut self, payload: T) {
        let senders = await!(self.inner.take());
        let mut living_senders = vec![];
        for mut sender in senders {
            if sender.try_send(payload.clone()).is_ok() {
                living_senders.push(sender);
            }
        }
        await!(self.inner.append(living_senders));
    }
}

/// An async receiver end of an mpmc channel. All receivers connected to the same
/// sender receive the same duplicated message sequence.
///
/// The message sequence is duplicated starting from the beginning of the
/// instance's lifetime; messages sent before the receiver is added to the
/// channel are not duplicated.
pub struct Receiver<T> {
    sources: SenderSet<T>,
    inner: mpsc::Receiver<T>,
    buffer_size: usize,
}

impl<T: Clone> Clone for Receiver<T> {
    fn clone(&self) -> Self {
        let (sender, receiver) = mpsc::channel(self.buffer_size);
        let sources = self.sources.clone();
        sources.enqueue_sender(sender);
        Self { sources, inner: receiver, buffer_size: self.buffer_size }
    }
}

impl<T: Clone> Stream for Receiver<T> {
    type Item = T;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        Stream::poll_next(Pin::new(&mut self.inner), cx)
    }
}

impl<T: Clone> FusedStream for Receiver<T> {
    fn is_terminated(&self) -> bool {
        self.inner.is_terminated()
    }
}

pub fn channel<T: Clone>(buffer_size: usize) -> (Sender<T>, Receiver<T>) {
    let (sender, receiver) = mpsc::channel(buffer_size);
    let senders = SenderSet::new(sender);
    (Sender { inner: senders.clone() }, Receiver { sources: senders, inner: receiver, buffer_size })
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use futures::StreamExt;

    #[fasync::run_singlethreaded]
    #[test]
    async fn it_works() {
        let (mut s, mut r1) = channel(100);
        let mut r2 = r1.clone();

        await!(s.send(20));
        assert_eq!(await!(r1.next()), Some(20));
        assert_eq!(await!(r2.next()), Some(20));
    }
}
