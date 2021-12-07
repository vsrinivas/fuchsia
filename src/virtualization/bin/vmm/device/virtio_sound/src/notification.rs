// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::rc::Rc;

/// A single bit notification.
/// Derives Clone to simplify handoff to async tasks.
#[derive(Clone)]
pub struct Notification {
    inner: Rc<Inner>,
}

struct Inner {
    send: tokio::sync::watch::Sender<bool>,
    recv: tokio::sync::watch::Receiver<bool>,
}

impl Notification {
    pub fn new() -> Self {
        let (s, r) = tokio::sync::watch::channel(false);
        Self { inner: Rc::new(Inner { send: s, recv: r }) }
    }

    /// Set the notification. Once the notification has been set,
    /// it is never unset. If set is called multiple times, the
    /// behavior is idempotent.
    pub fn set(&self) {
        // This cannot fail unless all receiver handles are dropped, but self must
        // outlive at least one receiver handle.
        let _ = self.inner.send.broadcast(true);
    }

    /// Reports whether the notification has been set.
    pub fn get(&self) -> bool {
        *self.inner.recv.borrow()
    }

    /// Returns a future that is ready when the notification is set.
    /// Takes ownership of self to make it easy for callers to create a temporary
    /// Notification clone that lives for the duration of when_set().await.
    pub async fn when_set(self) {
        // Clone to avoid holding an exclusive mutable borrow on self.recv.
        let mut recv = self.inner.recv.clone();
        // Note from tokio::sync::watch::Receiver:
        //
        //    If this is the first time [recv] is called on a `Receiver`
        //    instance, then [recv] completes immediately with the **current**
        //    value held by the channel. On the next call, [recv] waits until
        //    a new value is sent in the channel.
        //
        // Hence if the first recv call returns false, we should try again.
        loop {
            match recv.recv().await {
                Some(true) => return,
                Some(false) => continue,
                // Cannot happen: None means the sender was dropped, but self must
                // outlive the sender.
                None => panic!("impossible"),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[fasync::run_until_stalled(test)]
    async fn test() {
        let n = Notification::new();
        assert!(!n.get());

        // On the second iteration, the set() call should be idempotent.
        for _ in 0..2 {
            futures::join!(
                // In practice, futures::join!() tries to resolve these futures in
                // order, so include some waiters before and after the set().
                {
                    let n = n.clone();
                    async move {
                        n.clone().when_set().await;
                        assert!(n.get());
                    }
                },
                {
                    let n = n.clone();
                    async move {
                        n.clone().when_set().await;
                        assert!(n.get());
                    }
                },
                {
                    let n = n.clone();
                    async move {
                        n.set();
                    }
                },
                {
                    let n = n.clone();
                    async move {
                        n.clone().when_set().await;
                        assert!(n.get());
                    }
                },
                {
                    let n = n.clone();
                    async move {
                        n.clone().when_set().await;
                        assert!(n.get());
                    }
                },
            );
            assert!(n.get());
        }
    }
}
