// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{
    stream::{FusedStream, Stream, StreamExt},
    task::Poll,
};

/// `Stream`s should indicate their termination by returning an item of `Poll::Ready(None)`.
/// Once a Stream terminated it is generally not safe to poll the Stream any longer.
/// Support for safely polling already terminated Streams is provided by the `Fuse` wrapper.
/// The wrapper only polls not yet terminated Streams. If the underlying Stream already terminated
/// Fuse immediately returns `Poll::Ready(None)` without further polling its wrapped Stream.
/// Some Streams never terminate, such as `FuturesOrdered`. To indicate that there is currently
/// no work scheduled, these Streams return `Poll::Ready(None)`. However, when these Streams are
/// used in combination with Fuse the Streams would get polled once and immediately declared to have
/// terminated due to their return value oof `Poll::Ready(None)`. Instead, such Streams should
/// return `Poll::Pending`. `FusePending` provides this mapping.
/// Note: This wrapper should only be used if the underlying Stream defined its behavior if no
/// work is scheduled. Usually, such Streams are expected to never terminate.
pub struct FusePending<F>(pub F);

impl<F> std::ops::Deref for FusePending<F> {
    type Target = F;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<F> std::ops::DerefMut for FusePending<F> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl<F: Stream + std::marker::Unpin> Stream for FusePending<F> {
    type Item = F::Item;

    fn poll_next(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> futures::task::Poll<Option<Self::Item>> {
        match self.0.poll_next_unpin(cx) {
            Poll::Ready(None) => Poll::Pending,
            other => other,
        }
    }
}

impl<F: Stream + Unpin> FusedStream for FusePending<F> {
    fn is_terminated(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        futures::{channel::mpsc, future, select, stream::FuturesOrdered},
        pin_utils::pin_mut,
    };

    #[test]
    fn infinite_stream() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");

        let (sink, stream) = mpsc::unbounded();

        let fut = do_correct_work(stream);
        pin_mut!(fut);

        // Pass over explicit next() call.
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        sink.unbounded_send(42).expect("failed sending message");

        assert_eq!(Poll::Ready(Ok(42)), exec.run_until_stalled(&mut fut));
    }

    #[test]
    fn no_infinite_stream() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");

        let (sink, stream) = mpsc::unbounded();
        let fut = do_broken_work(stream);
        pin_mut!(fut);

        // Pass over explicit next() call.
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        sink.unbounded_send(42).expect("failed sending message");

        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
    }

    async fn do_broken_work(mut stream: mpsc::UnboundedReceiver<u8>) -> Result<u8, ()> {
        let mut queue = FuturesOrdered::new().fuse();

        loop {
            select! {
                x = stream.next() => if let Some(x) = x {
                    queue.get_mut().push(future::ok::<_, ()>(x));
                },
                x = queue.next() => if let Some(x) = x {
                    return x;
                }
            }
        }
    }

    async fn do_correct_work(mut stream: mpsc::UnboundedReceiver<u8>) -> Result<u8, ()> {
        let mut queue = FusePending(FuturesOrdered::new());

        loop {
            select! {
                x = stream.next() => if let Some(x) = x {
                    queue.push(future::ok::<_, ()>(x));
                },
                x = queue.next() => if let Some(x) = x {
                    return x;
                }
            }
        }
    }
}
