// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    futures::{channel::oneshot, future::Shared, lock::Mutex, pin_mut, FutureExt},
    std::{future::Future, pin::Pin, sync::Arc, task::Poll},
};

/// A `Future` whose output value can be cleared and replaced. The future is in a `Poll::Pending`
/// state if and only if it has no value.
///
/// The future can be cloned arbitrarily, and can be awaited while it is being set.
#[derive(Debug, Clone)]
pub struct ResettableFuture<T>
where
    T: Clone + 'static,
{
    inner: Arc<Mutex<ResettableFutureInner<T>>>,
}

struct ResettableFutureInner<T>
where
    T: Clone + 'static,
{
    /// If `Some`, that means that the future currently has no value.
    sender: Option<oneshot::Sender<T>>,
    receiver: Shared<oneshot::Receiver<T>>,
}

impl<T> ResettableFuture<T>
where
    T: Clone + 'static,
{
    /// Creates a new, empty [`ResettableFuture`].
    pub fn new() -> Self {
        let (sender, receiver) = oneshot::channel();
        let inner = ResettableFutureInner { sender: Some(sender), receiver: receiver.shared() };
        return Self { inner: Arc::new(Mutex::new(inner)) };
    }

    /// Sets or replaces the future's value. Any current or future pollers will receive the new
    /// value.
    pub async fn set(&self, value: T) {
        let sender = {
            let mut inner = self.inner.lock().await;
            if let Some(unused_sender) = inner.sender.take() {
                unused_sender
            } else {
                let (new_sender, new_receiver) = oneshot::channel();
                // Don't bother replacing inner.sender because we're about to consume it immediately.
                inner.receiver = new_receiver.shared();
                new_sender
            }
        };
        sender.send(value).map_err(|_| format_err!("Receiver dropped before sender")).unwrap()
    }

    /// Clears the future's stored value. If the future is already empty, it is untouched, and
    /// existing pollers remain valid.
    pub async fn clear(&self) {
        let mut inner = self.inner.lock().await;
        if inner.sender.is_none() {
            let (new_sender, new_receiver) = oneshot::channel();
            inner.sender = Some(new_sender);
            inner.receiver = new_receiver.shared();
        }
    }

    /// Convenience method for awaiting the future such that it can still be manipulated with `set`
    /// or `clear`.
    pub async fn get(&self) -> T {
        // Warning: Don't await the lock and the receiver in the same statement. The lock would
        // remain held for too long, causing deadlocks.
        let receiver = self.inner.lock().await.receiver.clone();
        // The borrow checker doesn't allow a SettableFuture to be dropped while one of its futures
        // is still live.
        receiver.await.expect("Sender dropped unexpectedly while Future was live")
    }
}

/// The container itself can be `.await`ed, if so inclined.
impl<T> Future for ResettableFuture<T>
where
    T: Clone + 'static,
{
    type Output = T;

    fn poll(self: Pin<&mut Self>, cx: &mut std::task::Context<'_>) -> Poll<Self::Output> {
        let fut = self.get();
        pin_mut!(fut);
        fut.poll(cx)
    }
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        assert_matches::assert_matches,
        futures::{join, poll},
    };

    #[fuchsia::test]
    async fn basic_get_set_and_clear() {
        let f = ResettableFuture::<u32>::new();
        assert_matches!(poll!(f.clone()), Poll::Pending);

        f.set(17).await;
        assert_matches!(poll!(f.clone()), Poll::Ready(17));

        assert_matches!(poll!(f.clone()), Poll::Ready(17));
        assert_matches!(f.get().await, 17);

        f.set(34).await;
        assert_matches!(f.get().await, 34);

        f.clear().await;
        assert_matches!(poll!(f.clone()), Poll::Pending);

        f.set(51).await;
        assert_matches!(f.get().await, 51);
    }

    #[fuchsia::test]
    async fn multiple_readers() {
        let f = ResettableFuture::<u32>::new();

        let fut_a = f.get();
        pin_mut!(fut_a);
        let fut_b = f.get();
        pin_mut!(fut_b);
        let fut_c = f.clone();
        pin_mut!(fut_c);
        assert_matches!(poll!(&mut fut_a), Poll::Pending);
        assert_matches!(poll!(&mut fut_b), Poll::Pending);
        assert_matches!(poll!(&mut fut_c), Poll::Pending);

        f.set(17).await;
        assert_matches!(poll!(&mut fut_a), Poll::Ready(17));
        assert_matches!(poll!(&mut fut_b), Poll::Ready(17));
        assert_matches!(poll!(&mut fut_c), Poll::Ready(17));
    }

    #[fuchsia::test]
    async fn clear_while_awaited() {
        let f = ResettableFuture::<u32>::new();

        let fut_a = f.get();
        pin_mut!(fut_a);
        assert_matches!(poll!(&mut fut_a), Poll::Pending);

        f.clear().await;
        assert_matches!(poll!(&mut fut_a), Poll::Pending);

        let (val, _) = join!(fut_a, f.set(17));
        assert_eq!(val, 17);
    }
}
