// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::client::QueryResponseFut,
    futures::{
        future::{FusedFuture as _, FutureExt as _, MaybeDone},
        stream::{FusedStream, Stream},
        task::{Context, Poll},
    },
    pin_project::pin_project,
    std::pin::Pin,
};

/// HangingGetStream is a [`Stream`] that is oriented towards being a client to the
/// "Hanging Get" design pattern for flow control as in //docs/development/api/fidl.md#flow_control
#[must_use = "streams do nothing unless polled"]
#[pin_project]
pub struct HangingGetStream<P, O, Q = fn(&P) -> QueryResponseFut<O>> {
    proxy: P,
    query: Q,
    response: QueryResponseFut<O>,
}

impl<P, O, Q> Stream for HangingGetStream<P, O, Q>
where
    O: Unpin,
    Q: FnMut(&P) -> QueryResponseFut<O>,
{
    type Item = fidl::Result<O>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut this = self.project();
        if this.response.is_terminated() {
            *this.response = (&mut this.query)(&this.proxy);
        }
        this.response.poll_unpin(cx).map(Some)
    }
}

impl<P, O, Q> FusedStream for HangingGetStream<P, O, Q>
where
    O: Unpin,
    Q: FnMut(&P) -> QueryResponseFut<O>,
{
    fn is_terminated(&self) -> bool {
        false
    }
}

impl<P, O, Q> HangingGetStream<P, O, Q>
where
    Q: FnMut(&P) -> QueryResponseFut<O>,
    O: Unpin,
{
    /// Creates a new hanging-get stream.
    pub fn new(proxy: P, query: Q) -> Self {
        Self { proxy, query, response: QueryResponseFut(MaybeDone::Gone) }
    }
}

impl<P, O> HangingGetStream<P, O>
where
    O: Unpin,
{
    /// Creates a new hanging-get stream with a function pointer.
    pub fn new_with_fn_ptr(proxy: P, query: fn(&P) -> QueryResponseFut<O>) -> Self {
        Self::new(proxy, query)
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use {fuchsia_async as fasync, futures::TryStreamExt as _, std::cell::Cell};

    struct TestProxy {
        state: Cell<usize>,
    }

    impl TestProxy {
        fn watch(&self) -> QueryResponseFut<usize> {
            let cur = self.state.get();
            self.state.set(cur + 1);
            QueryResponseFut(MaybeDone::Done(Ok(cur)))
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn generates_items() {
        let proxy = TestProxy { state: Cell::new(0) };
        let mut watcher = HangingGetStream::new(proxy, TestProxy::watch);

        const ITERS: usize = 3;
        for i in 0..ITERS {
            assert!(watcher.response.is_terminated());
            assert_eq!(watcher.try_next().await.expect("failed to get next item"), Some(i));
        }
    }
}
