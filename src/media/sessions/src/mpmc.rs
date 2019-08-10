// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Ref, CHANNEL_BUFFER_SIZE};
use crossbeam::queue::MsQueue;
use futures::{channel::mpsc, lock::Mutex, stream::FusedStream, task::Context, Poll, Stream};
use std::{ops::DerefMut, pin::Pin, rc::Rc};

#[derive(Clone)]
struct SenderSet<T> {
    inner: Ref<Vec<mpsc::Sender<T>>>,
    pending_senders: Rc<MsQueue<mpsc::Sender<T>>>,
}

impl<T> Default for SenderSet<T> {
    fn default() -> Self {
        Self { inner: Ref::default(), pending_senders: Rc::new(MsQueue::new()) }
    }
}

impl<T> SenderSet<T> {
    fn new(sender: mpsc::Sender<T>) -> Self {
        Self { inner: Rc::new(Mutex::new(vec![sender])), pending_senders: Rc::new(MsQueue::new()) }
    }

    fn enqueue_sender(&self, new_sender: mpsc::Sender<T>) {
        self.pending_senders.push(new_sender);
    }

    async fn take(&mut self) -> Vec<mpsc::Sender<T>> {
        let mut snapshot = vec![];
        snapshot.append(self.inner.lock().await.deref_mut());

        while let Some(new_sender) = self.pending_senders.try_pop() {
            snapshot.push(new_sender);
        }
        snapshot
    }

    async fn append(&mut self, mut addendum: Vec<mpsc::Sender<T>>) {
        self.inner.lock().await.deref_mut().append(&mut addendum)
    }
}

/// An async sender end of an mpmc channel. Messages sent on this are received by
/// _all_ receivers connected to it (they are duplicated).
#[derive(Clone)]
pub struct Sender<T> {
    inner: SenderSet<T>,
    buffer_size: usize,
}

impl<T> Default for Sender<T> {
    fn default() -> Self {
        Sender { inner: SenderSet::default(), buffer_size: CHANNEL_BUFFER_SIZE }
    }
}

impl<T: Clone> Sender<T> {
    pub async fn send(&mut self, payload: T) {
        let senders = self.inner.take().await;
        let mut living_senders = vec![];
        for mut sender in senders {
            if sender.try_send(payload.clone()).is_ok() {
                living_senders.push(sender);
            }
        }
        self.inner.append(living_senders).await;
    }

    pub fn new_receiver(&self) -> Receiver<T> {
        let (sender, receiver) = mpsc::channel(self.buffer_size);
        self.inner.enqueue_sender(sender);
        Receiver { sources: self.inner.clone(), inner: receiver, buffer_size: self.buffer_size }
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
    (
        Sender { inner: senders.clone(), buffer_size },
        Receiver { sources: senders, inner: receiver, buffer_size },
    )
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

        s.send(20).await;
        assert_eq!(r1.next().await, Some(20));
        assert_eq!(r2.next().await, Some(20));
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn sender_side_initialization() {
        let mut s = Sender::default();
        let mut r1 = s.new_receiver();
        let mut r2 = s.new_receiver();

        s.send(20).await;
        assert_eq!(r1.next().await, Some(20));
        assert_eq!(r2.next().await, Some(20));
    }
}
