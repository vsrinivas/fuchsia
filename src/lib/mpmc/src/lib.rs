// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! A library with futures-aware mpmc channels.

use crossbeam::queue::MsQueue;
use futures::{channel::mpsc, lock::Mutex, sink::SinkExt, stream::FusedStream, Stream};
use std::{
    pin::Pin,
    sync::{Arc, Weak},
    task::{Context, Poll},
};

/// The default number of messages that will be buffered per-receiver.
pub const DEFAULT_CHANNEL_BUFFER_SIZE: usize = 100;

/// An async sender end of an mpmc channel. Messages sent on this are received by
/// _all_ receivers connected to it (they are duplicated).
#[derive(Clone)]
pub struct Sender<T> {
    inner: Arc<Mutex<Vec<mpsc::Sender<T>>>>,
    enqueued_senders: Arc<MsQueue<mpsc::Sender<T>>>,
    buffer_size: usize,
}

impl<T> Default for Sender<T> {
    fn default() -> Self {
        Sender {
            inner: Arc::default(),
            enqueued_senders: Arc::default(),
            buffer_size: DEFAULT_CHANNEL_BUFFER_SIZE,
        }
    }
}

impl<T: Clone> Sender<T> {
    /// Construct a sender whose receivers will buffer the given number of messages.
    pub fn with_buffer_size(buffer_size: usize) -> Self {
        Self { buffer_size, ..Default::default() }
    }

    /// Sends `payload` to all receivers that exist at the time of send.
    ///
    /// Sending is never an error, even if there are no receivers.
    pub async fn send(&self, payload: T) {
        let mut inner = self.inner.lock().await;
        while let Some(new_sender) = self.enqueued_senders.try_pop() {
            inner.push(new_sender);
        }

        let mut living_senders = vec![];
        for mut sender in inner.drain(0..) {
            let should_live = match sender.try_send(payload.clone()).err() {
                None => true,
                Some(send_error) if send_error.is_disconnected() => false,
                Some(e) => {
                    // The receiver is full. Apply backpressure.
                    let payload = e.into_inner();
                    sender.send(payload).await.is_ok()
                }
            };

            if should_live {
                living_senders.push(sender);
            }
        }
        inner.append(&mut living_senders);
    }

    /// Creates a new receiver who will receive a copy of all messages sent after its creation.
    pub fn new_receiver(&self) -> Receiver<T> {
        let (sender, receiver) = mpsc::channel(self.buffer_size);
        self.enqueued_senders.push(sender);
        Receiver {
            sources: Arc::downgrade(&self.enqueued_senders),
            inner: receiver,
            buffer_size: self.buffer_size,
        }
    }
}

/// An async receiver end of an mpmc channel. All receivers connected to the same
/// sender receive the same duplicated message sequence.
///
/// The message sequence is duplicated starting from the beginning of the
/// instance's lifetime; messages sent before the receiver is added to the
/// channel are not duplicated.
pub struct Receiver<T> {
    sources: Weak<MsQueue<mpsc::Sender<T>>>,
    inner: mpsc::Receiver<T>,
    buffer_size: usize,
}

impl<T: Clone> Clone for Receiver<T> {
    fn clone(&self) -> Self {
        if let Some(sender_set) = self.sources.upgrade() {
            let (sender, receiver) = mpsc::channel(self.buffer_size);
            let sources = sender_set.clone();
            sources.push(sender);
            Self {
                sources: Arc::downgrade(&sources),
                inner: receiver,
                buffer_size: self.buffer_size,
            }
        } else {
            // The senders have all been dropped; clone to a dummy channel that just yields `None`
            // to be consistent.
            let (_, receiver) = mpsc::channel(1);
            Self { sources: Weak::new(), inner: receiver, buffer_size: 1 }
        }
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
