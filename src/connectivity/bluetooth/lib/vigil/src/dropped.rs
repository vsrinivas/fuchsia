// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::{FusedFuture, Shared};
use futures::{Future, FutureExt};
use std::task::{Context, Poll};

use crate::DropWatch;

/// A future that can be constructed from any impl DropWatch, that completes when the item is dropped.
#[derive(Clone)]
#[must_use = "futures do nothing unless you await or poll them"]
pub struct Dropped(Shared<futures::channel::oneshot::Receiver<()>>);

impl Dropped {
    pub fn new<DW: DropWatch<U> + ?Sized, U: ?Sized>(watchable: &DW) -> Self {
        let (sender, receiver) = futures::channel::oneshot::channel();
        DropWatch::watch(watchable, move |_| drop(sender.send(())));
        Self(receiver.shared())
    }
}

impl Future for Dropped {
    type Output = ();

    fn poll(mut self: std::pin::Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.0.poll_unpin(cx).map(|_| ())
    }
}

impl FusedFuture for Dropped {
    fn is_terminated(&self) -> bool {
        self.0.is_terminated()
    }
}

#[cfg(test)]
mod tests {
    use async_utils::PollExt;
    use futures::task::Context;
    use futures_test::task::new_count_waker;

    use super::*;
    use crate::Vigil;

    #[test]
    fn obits() {
        let v = Vigil::new(());

        let mut obit = Vigil::dropped(&v);
        let mut obit2 = Vigil::dropped(&v);
        let mut obit_never_polled = Vigil::dropped(&v);

        let (waker, count) = new_count_waker();

        let mut cx = Context::from_waker(&waker);

        obit.poll_unpin(&mut cx).expect_pending("shouldn't be done");
        obit2.poll_unpin(&mut cx).expect_pending("shouldn't be done");

        // when dropped, both should be woken.
        drop(v);
        // The data should have been dropped.
        assert_eq!(2, count.get());

        // polling them should be ready.
        obit.poll_unpin(&mut cx).expect("should be done");
        obit2.poll_unpin(&mut cx).expect("should also be done");

        // polling the one that hasn't been polled yet should also be ready
        obit_never_polled.poll_unpin(&mut cx).expect("be done");
    }
}
