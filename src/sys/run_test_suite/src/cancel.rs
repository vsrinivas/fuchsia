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
    tracing::warn,
};

/// An extension trait that allows futures to be cancelled. Cancellation is signalled
/// by a second future.
pub trait OrCancel<F: Future, C: Future> {
    /// Create a Future, which polls both the original future and |cancel_fut|.
    /// If |cancel_fut| resolves before the original future, Err(Cancelled) is returned.
    /// Otherwise, the result of the original future is returned.
    fn or_cancelled(self, cancel_fut: C) -> OrCancelledFuture<F, C>;
}

/// An error indicating that a future was cancelled.
#[derive(PartialEq, Debug)]
pub struct Cancelled<T>(pub T);

impl<C: Future, F: Future> OrCancel<F, C> for F {
    fn or_cancelled(self, cancel_fut: C) -> OrCancelledFuture<F, C> {
        OrCancelledFuture { fut: self, cancel_fut }
    }
}

/// Implementation for the future returned by OrCancel::or_cancelled.
#[pin_project]
pub struct OrCancelledFuture<F: Future, C: Future> {
    #[pin]
    fut: F,
    #[pin]
    cancel_fut: C,
}

impl<F: Future, C: Future> Future for OrCancelledFuture<F, C> {
    type Output = Result<<F as Future>::Output, Cancelled<<C as Future>::Output>>;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = self.project();
        match this.cancel_fut.poll(cx) {
            Poll::Ready(ready) => return Poll::Ready(Err(Cancelled(ready))),
            Poll::Pending => (),
        }
        match this.fut.poll(cx) {
            Poll::Ready(ready_result) => Poll::Ready(Ok(ready_result)),
            Poll::Pending => Poll::Pending,
        }
    }
}

// An extension trait that allows naming futures, and exports diagnostics when
// the future is dropped without polling to completion.
pub trait NamedFutureExt<F: Future> {
    /// Produces a named Future. When the Future is dropped, logs a warning if
    /// the Future was not polled to completion.
    fn named(self, name: &'static str) -> WarnOnIncompleteFuture<F>;
}

impl<F: Future> NamedFutureExt<F> for F {
    fn named(self, name: &'static str) -> WarnOnIncompleteFuture<F> {
        WarnOnIncompleteFuture::new(self, name, LogWarn)
    }
}

/// Implementation of the Future returned from
/// |NamedFutureExt::named|.
pub type WarnOnIncompleteFuture<F> = HookOnIncompleteFuture<F, LogWarn>;

/// A future that calls some hook when it is dropped before polling to
/// completion.
#[pin_project]
pub struct HookOnIncompleteFuture<F: Future, LF: OnIncompleteHook> {
    #[pin]
    fut: F,
    inner: HookOnIncompleteFutureInner<LF>,
}

impl<F: Future, LF: OnIncompleteHook> HookOnIncompleteFuture<F, LF> {
    fn new(fut: F, name: &'static str, on_incomplete: LF) -> Self {
        HookOnIncompleteFuture {
            fut,
            inner: HookOnIncompleteFutureInner { name, polled_to_completion: false, on_incomplete },
        }
    }
}

impl<F: Future, LF: OnIncompleteHook> Future for HookOnIncompleteFuture<F, LF> {
    type Output = <F as Future>::Output;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = self.project();
        match this.fut.poll(cx) {
            Poll::Ready(ready_result) => {
                this.inner.polled_to_completion = true;
                Poll::Ready(ready_result)
            }
            Poll::Pending => Poll::Pending,
        }
    }
}

/// Inner state for |HookOnIncompleteFuture|.
struct HookOnIncompleteFutureInner<LF: OnIncompleteHook> {
    name: &'static str,
    polled_to_completion: bool,
    on_incomplete: LF,
}

impl<LF: OnIncompleteHook> std::ops::Drop for HookOnIncompleteFutureInner<LF> {
    fn drop(&mut self) {
        if !self.polled_to_completion {
            self.on_incomplete.on_incomplete(self.name);
        }
    }
}

/// Trait providing implementations for functions called when
/// |HookOnIncompleteFutureInner| is dropped. This exists purely to enable
/// testing |HookOnIncompleteFuture|.
pub trait OnIncompleteHook {
    fn on_incomplete(&self, name: &str);
}

/// |OnIncompleteHook| implementation that logs a warning when a future is
/// incomplete.
pub struct LogWarn;

impl OnIncompleteHook for LogWarn {
    fn on_incomplete(&self, name: &str) {
        warn!("Future {} dropped before polling to completion", name);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::{
        channel::oneshot,
        future::{pending, ready, FutureExt},
    };
    use std::cell::RefCell;

    #[fuchsia::test]
    async fn stop_if_cancelled() {
        assert!(pending::<()>().or_cancelled(ready(())).await.is_err());

        let (_send, recv) = oneshot::channel::<()>();

        assert!(recv.or_cancelled(ready(())).await.is_err());
    }

    #[fuchsia::test]
    async fn resolve_if_not_cancelled() {
        assert!(ready(()).or_cancelled(pending::<()>()).await.is_ok());

        let (send, recv) = oneshot::channel::<()>();

        futures::future::join(
            async move {
                send.send(()).unwrap();
            },
            async move {
                assert_eq!(recv.or_cancelled(pending::<()>()).await.unwrap(), Ok(()));
            },
        )
        .await;
    }

    struct RecordOnDrop<'a>(&'a RefCell<bool>);

    impl OnIncompleteHook for RecordOnDrop<'_> {
        fn on_incomplete(&self, _: &str) {
            self.0.replace(true);
        }
    }

    #[fuchsia::test]
    async fn no_record_when_polled_to_completion() {
        let on_drop_called = RefCell::new(false);
        let record_on_drop = RecordOnDrop(&on_drop_called);

        let fut = HookOnIncompleteFuture::new(ready(()), "my_fut", record_on_drop);
        fut.await;
        assert_eq!(*on_drop_called.borrow(), false);
    }

    #[fuchsia::test]
    async fn record_when_poll_incomplete() {
        let on_drop_called = RefCell::new(false);
        let record_on_drop = RecordOnDrop(&on_drop_called);

        let fut = HookOnIncompleteFuture::new(pending::<()>(), "my_fut", record_on_drop);
        assert!(fut.now_or_never().is_none());
        assert_eq!(*on_drop_called.borrow(), true);
    }
}
