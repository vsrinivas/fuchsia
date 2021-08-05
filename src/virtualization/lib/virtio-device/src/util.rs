// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Small utility wrappers and trait implementations.
//!
//! This contains some commonly useful wrappers and trait implementations that are obvious enough to
//! warrant an implementation, but have an amount of policy that you may want to opt out of.

use {
    crate::queue::{DescChain, DriverNotify, Queue},
    futures::{task::AtomicWaker, Stream},
    std::{
        pin::Pin,
        sync::{
            atomic::{AtomicBool, AtomicU32, Ordering},
            Arc,
        },
        task::{Context, Poll},
    },
};

struct BufferedNotifyInner<N: DriverNotify> {
    notify: N,
    was_notified: AtomicBool,
}

impl<N: DriverNotify> Drop for BufferedNotifyInner<N> {
    fn drop(&mut self) {
        self.flush()
    }
}

impl<N: DriverNotify> BufferedNotifyInner<N> {
    fn flush(&self) {
        if self.was_notified.swap(false, Ordering::Relaxed) {
            self.notify.notify();
        }
    }
}

/// Buffering wrapper for [`DriverNotify`]
///
/// Typically notifying a driver is an expensive operation and when processing large numbers of
/// chains a device may wish to trade of latency for throughput and delay submitting a notification.
/// [`BufferedNotify`] implements [`DriverNotify`], but just stores the notification locally until
/// the [`BufferedNotify::flush`] method is called, which then calls the underlying
/// [`DriverNotify::notify`].
///
/// Any outstanding notifications will automatically be flushed on `drop`.
///
/// The [`BufferedNotify`] uses an [`Arc`] internally and so can be [`Clone`] to easily give to
/// multiple queues.
pub struct BufferedNotify<N: DriverNotify>(Arc<BufferedNotifyInner<N>>);

impl<N: DriverNotify> Clone for BufferedNotify<N> {
    fn clone(&self) -> BufferedNotify<N> {
        BufferedNotify(self.0.clone())
    }
}

impl<N: DriverNotify> BufferedNotify<N> {
    /// Construct a [`BufferedNotify`] wrapping a [`DriverNotify`]
    pub fn new(notify: N) -> BufferedNotify<N> {
        BufferedNotify(Arc::new(BufferedNotifyInner {
            notify,
            was_notified: AtomicBool::new(false),
        }))
    }

    /// Flush any pending notification.
    ///
    /// If this [`BufferedNotify`] has been [`notify`](#notify) since the last call to `flush`,
    /// calls [`DriverNotify::notify`] on the wrapped [`DriverNotify`].
    pub fn flush(&self) {
        self.0.flush()
    }
}

impl<N: DriverNotify> DriverNotify for BufferedNotify<N> {
    fn notify(&self) {
        self.0.was_notified.store(true, Ordering::Relaxed);
    }
}

/// Counting version of [`DriverNotify`]
///
/// [`NotificationCounter`] is largely aimed at writing unit tests and it just records how many
/// times it has been notified, providing an interface to [`NotificationCounter::get`] and [reset]
/// (NotificationCounter::set) the count.
#[derive(Debug, Clone)]
pub struct NotificationCounter {
    notify_count: Arc<AtomicU32>,
}

impl DriverNotify for NotificationCounter {
    fn notify(&self) {
        self.notify_count.fetch_add(1, Ordering::SeqCst);
    }
}

impl NotificationCounter {
    /// Construct a new [`NotificationCounter`]
    pub fn new() -> NotificationCounter {
        NotificationCounter { notify_count: Arc::new(AtomicU32::new(0)) }
    }

    /// Retrieve the current notification count.
    pub fn get(&self) -> u32 {
        self.notify_count.load(Ordering::SeqCst)
    }

    /// Set the stored notification count to the given value.
    pub fn set(&self, val: u32) {
        self.notify_count.store(val, Ordering::SeqCst)
    }
}

/// Async [`Stream`] implementation that resolves [`DescChain`]
///
/// This allows for treating a [`Queue`] as something that asynchronously produces [`DescChain`].
/// As this library has no knowledge of the underlying virtio transport the user is still
/// responsible for hooking up the provided [`DescChainStream::waker`] for the stream to function
/// correctly.
pub struct DescChainStream<'a, 'b, N> {
    queue: &'b Queue<'a, N>,
    task: Arc<AtomicWaker>,
}
impl<'a, 'b, N> DescChainStream<'a, 'b, N> {
    /// Create a [`Stream`] for a [`Queue`].
    ///
    /// The produced [`DescChainStream`] must have its [`waker`](#waker) signaled by the virtio
    /// transport that receives guest notifications, otherwise the stream will not work.
    pub fn new(queue: &'b Queue<'a, N>) -> DescChainStream<'a, 'b, N> {
        DescChainStream { queue, task: Arc::new(AtomicWaker::new()) }
    }

    /// Retrieve the internal [`AtomicWaker`].
    ///
    /// This should be signaled by the virtio transport when a guest notification is received for
    /// the underlying queue.
    pub fn waker(&self) -> Arc<AtomicWaker> {
        self.task.clone()
    }
}

impl<'a, 'b, N: DriverNotify> Stream for DescChainStream<'a, 'b, N> {
    type Item = DescChain<'a, 'b, N>;
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if let Some(desc) = self.queue.next_chain() {
            return Poll::Ready(Some(desc));
        }
        self.task.register(cx.waker());
        match self.queue.next_chain() {
            Some(desc) => Poll::Ready(Some(desc)),
            None => Poll::Pending,
        }
    }
}
