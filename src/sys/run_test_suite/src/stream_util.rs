// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{
    stream::Stream,
    task::{Context, Poll},
};
use std::pin::Pin;

/// An extension trait providing additional combinator methods for Streams.
pub(crate) trait StreamUtil<S: Stream<Item = T> + Unpin, T> {
    /// Similar to StreamExt::take_while. Take elements from the stream while
    /// |stop_after| evaluates false. After |stop_after| evaluates true, take
    /// one more element then terminates the stream.
    ///
    /// eg given a stream [0, 1, 2, 3, 4, 5] and stop_after |e| *e == 3,
    /// generates a stream [0, 1, 2, 3]
    fn take_until_stop_after<F: Fn(&T) -> bool + Unpin>(
        self,
        stop_after: F,
    ) -> TakeUntilStopAfterStream<F, S, T>;
}

impl<S, T> StreamUtil<S, T> for S
where
    S: Stream<Item = T> + Unpin,
{
    fn take_until_stop_after<F: Fn(&T) -> bool + Unpin>(
        self,
        stop_after_fn: F,
    ) -> TakeUntilStopAfterStream<F, S, T> {
        TakeUntilStopAfterStream { stop_after_fn, inner: self, stopped: false }
    }
}

/// Implementation of the stream returned by StreamUtil::take_until_stop_after
pub struct TakeUntilStopAfterStream<F, S, T>
where
    F: Fn(&T) -> bool + Unpin,
    S: Stream<Item = T> + Unpin,
{
    stop_after_fn: F,
    inner: S,
    stopped: bool,
}

impl<F, S, T> Stream for TakeUntilStopAfterStream<F, S, T>
where
    F: Fn(&T) -> bool + Unpin,
    S: Stream<Item = T> + Unpin,
{
    type Item = <S as Stream>::Item;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let self_mut = self.get_mut();
        if self_mut.stopped {
            return Poll::Ready(None);
        }

        let inner_poll = Pin::new(&mut self_mut.inner).poll_next(cx);
        self_mut.stopped = match &inner_poll {
            Poll::Ready(None) => true,
            Poll::Ready(Some(item)) => (self_mut.stop_after_fn)(item),
            Poll::Pending => false,
        };
        inner_poll
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::stream::StreamExt;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn stops_after_test_fn_returns_true() {
        let stream = futures::stream::iter(0..u32::MAX);
        let results: Vec<_> = stream.take_until_stop_after(|num| *num == 5).collect().await;
        assert_eq!(vec![0, 1, 2, 3, 4, 5], results);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn does_not_poll_after_test_fn_returns_true() {
        let stream = futures::stream::iter(0..6).chain(futures::stream::pending());
        let results: Vec<_> = stream.take_until_stop_after(|num| *num == 5).collect().await;
        assert_eq!(vec![0, 1, 2, 3, 4, 5], results);
    }
}
