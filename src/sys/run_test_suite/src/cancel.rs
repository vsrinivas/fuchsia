// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{
        future::Future,
        task::{Context, Poll},
    },
    pin_project::pin_project,
    std::pin::Pin,
};

/// An extension trait that allows futures to be cancelled. Cancellation is signalled
/// by a second future.
pub trait OrCancel<F: Future + Unpin, C: Future + Unpin> {
    /// Create a Future, which polls both the original future and |cancel_fut|.
    /// If |cancel_fut| resolves before the original future, Err(Cancelled) is returned.
    /// Otherwise, the result of the original future is returned.
    fn or_cancelled(self, cancel_fut: C) -> OrCancelledFuture<F, C>;
}

/// An error indicating that a future was cancelled.
#[derive(PartialEq, Debug)]
pub struct Cancelled;

impl<C: Future + Unpin, F: Future + Unpin> OrCancel<F, C> for F {
    fn or_cancelled(self, cancel_fut: C) -> OrCancelledFuture<F, C> {
        OrCancelledFuture { fut: self, cancel_fut }
    }
}

/// Implementation for the future returned by OrCancel::or_cancelled.
#[pin_project]
pub struct OrCancelledFuture<F: Future + Unpin, C: Future + Unpin> {
    fut: F,
    cancel_fut: C,
}

impl<F: Future + Unpin, C: Future + Unpin> Future for OrCancelledFuture<F, C> {
    type Output = Result<<F as Future>::Output, Cancelled>;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = self.project();
        match Pin::new(this.cancel_fut).poll(cx) {
            Poll::Ready(_) => return Poll::Ready(Err(Cancelled)),
            Poll::Pending => (),
        }
        match Pin::new(this.fut).poll(cx) {
            Poll::Ready(ready_result) => Poll::Ready(Ok(ready_result)),
            Poll::Pending => Poll::Pending,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::{
        channel::oneshot,
        future::{pending, ready},
    };

    #[fuchsia::test]
    async fn stop_if_cancelled() {
        assert_eq!(pending::<()>().or_cancelled(ready(())).await, Err(Cancelled));

        let (_send, recv) = oneshot::channel::<()>();

        assert_eq!(recv.or_cancelled(ready(())).await, Err(Cancelled));
    }

    #[fuchsia::test]
    async fn resolve_if_not_cancelled() {
        assert_eq!(ready(()).or_cancelled(pending::<()>()).await, Ok(()));

        let (send, recv) = oneshot::channel::<()>();

        futures::future::join(
            async move {
                send.send(()).unwrap();
            },
            async move {
                assert_eq!(recv.or_cancelled(pending::<()>()).await, Ok(Ok(())));
            },
        )
        .await;
    }
}
