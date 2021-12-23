// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::stream::{FusedStream, Stream};
use futures::task::{Context, Poll};
use std::fmt::Debug;
use std::pin::Pin;

/// A optional `Stream` that returns the values of the wrapped stream until the wrapped stream is
/// exhausted.
///
/// MaybeStream yields items of type S::Item.
/// The `Stream` implementation will return `Poll::Pending` if no stream is set.
#[derive(Debug)]
pub struct MaybeStream<S: Stream>(Option<S>);

impl<S: Stream + Unpin> MaybeStream<S> {
    /// Set the current stream.
    ///
    /// This method will not call `poll` on the submitted stream. The caller must ensure
    /// that `poll_next` is called in order to receive wake-up notifications for the given
    /// stream.
    pub fn set(&mut self, stream: S) {
        self.0 = Some(stream)
    }

    fn poll_next(&mut self, cx: &mut Context<'_>) -> Poll<Option<S::Item>> {
        Pin::new(self.0.as_mut().unwrap()).poll_next(cx)
    }
}

impl<S: Stream> Default for MaybeStream<S> {
    fn default() -> Self {
        Self(None)
    }
}

impl<S: Stream + Unpin> From<Option<S>> for MaybeStream<S> {
    fn from(src: Option<S>) -> Self {
        Self(src)
    }
}

impl<S: Stream + Unpin> Stream for MaybeStream<S> {
    type Item = S::Item;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.0.is_none() {
            return Poll::Pending;
        }
        self.get_mut().poll_next(cx)
    }
}

/// A MaybeStream with no inner stream is never done because a new stream can always be set with
/// items.
impl<S: FusedStream + Stream + Unpin> FusedStream for MaybeStream<S> {
    fn is_terminated(&self) -> bool {
        if self.0.is_none() {
            false
        } else {
            self.0.as_ref().unwrap().is_terminated()
        }
    }
}

impl<S: Stream + std::fmt::Display> std::fmt::Display for MaybeStream<S> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let display_str = if let Some(st) = &self.0 { format!("{}", st) } else { "".to_string() };
        write!(f, "MaybeStream({})", display_str)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async::TestExecutor;
    use futures::stream::StreamExt;

    struct CountStream {
        count: usize,
    }

    impl CountStream {
        fn new() -> CountStream {
            CountStream { count: 0 }
        }
    }

    impl Stream for CountStream {
        type Item = usize;

        fn poll_next(mut self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            self.count += 1;
            Poll::Ready(Some(self.count))
        }
    }

    #[test]
    fn maybestream() {
        let mut exec = TestExecutor::new().unwrap();

        let mut s = MaybeStream::default();

        let mut next_fut = s.next();
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut next_fut));
        next_fut = s.next();
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut next_fut));

        s.set(CountStream::new());

        next_fut = s.next();
        assert_eq!(Poll::Ready(Some(1)), exec.run_until_stalled(&mut next_fut));

        next_fut = s.next();
        assert_eq!(Poll::Ready(Some(2)), exec.run_until_stalled(&mut next_fut));
    }
}
