// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Streams always signal exhaustion with `None` return values. A stream epitaph can be used when
//! a specific value is desired as the last item returned by a stream before it is exhausted.
//!
//! Example usecase: often streams will be used without having direct access to the stream itself
//! such as from a `streammap::StreamMap` or a `futures::stream::FuturesUnordered`. Occasionally,
//! it is necessary to perform some cleanup procedure outside of a stream when it is exhausted. An
//! `epitaph` can be used to uniquely identify which stream has ended within a collection of
//! streams.

use {
    futures::stream::{FusedStream, Stream},
    std::pin::Pin,
    std::task::{Context, Poll},
};

/// Values returned from a stream with an epitaph are of type `StreamItem`.
#[derive(Debug, PartialEq)]
pub enum StreamItem<T, E> {
    /// Item polled from the underlying `Stream`
    Item(T),
    /// Epitaph value returned after the underlying `Stream` is exhausted.
    Epitaph(E),
}

/// A `Stream` that returns the values of the wrapped stream until the wrapped stream is exhausted.
/// Then it returns a single epitaph value before being exhausted
pub struct StreamWithEpitaph<S, E> {
    inner: S,
    epitaph: Option<E>,
    terminated: bool,
}

// The `Unpin` bounds are not strictly necessary, but make for a more convenient
// implementation. The bounds can be relaxed if !Unpin support is desired.
impl<S, T, E> Stream for StreamWithEpitaph<S, E>
where
    S: Stream<Item = T> + FusedStream + Unpin,
    E: Unpin,
    T: Unpin,
{
    type Item = StreamItem<T, E>;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Option<Self::Item>> {
        if self.terminated {
            return Poll::Ready(None);
        }
        match Pin::new(&mut self.inner).poll_next(cx) {
            Poll::Ready(None) => {
                let mut this = self.get_mut();
                this.terminated = true;
                let ep = this.epitaph.take().map(StreamItem::Epitaph);
                assert!(ep.is_some(), "epitaph must be present if stream is not terminated");
                Poll::Ready(ep)
            }
            Poll::Ready(item) => Poll::Ready(item.map(StreamItem::Item)),
            Poll::Pending => Poll::Pending,
        }
    }
}

impl<S, T, E> FusedStream for StreamWithEpitaph<S, E>
where
    S: Stream<Item = T> + FusedStream + Unpin,
    E: Unpin,
    T: Unpin,
{
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

/// Extension trait to allow for easy creation of a `StreamWithEpitaph` from a `Stream`.
pub trait WithEpitaph: Sized {
    fn with_epitaph<E>(self, epitaph: E) -> StreamWithEpitaph<Self, E>;
}

impl<T> WithEpitaph for T
where
    T: Stream,
{
    fn with_epitaph<E>(self, epitaph: E) -> StreamWithEpitaph<T, E> {
        StreamWithEpitaph { inner: self, epitaph: Some(epitaph), terminated: false }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        futures::{
            future::ready,
            stream::{empty, iter, once, Empty, StreamExt},
        },
    };

    #[fasync::run_until_stalled(test)]
    async fn empty_stream_returns_epitaph_only() {
        let s: Empty<i32> = empty();
        let s = s.with_epitaph(0i64);
        let actual: Vec<_> = s.collect().await;
        let expected = vec![StreamItem::Epitaph(0i64)];
        assert_eq!(actual, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn populated_stream_returns_items_and_epitaph() {
        let s = iter(0i32..3).fuse().with_epitaph(3i64);
        let actual: Vec<_> = StreamExt::collect::<Vec<_>>(s).await;
        let expected = vec![
            StreamItem::Item(0),
            StreamItem::Item(1),
            StreamItem::Item(2),
            StreamItem::Epitaph(3i64),
        ];
        assert_eq!(actual, expected);
    }

    #[fasync::run_until_stalled(test)]
    async fn stream_is_terminated_after_end() {
        let mut s = once(ready(0i32)).with_epitaph(3i64);
        s.next().await;
        s.next().await;
        assert!(s.is_terminated());
    }
}
