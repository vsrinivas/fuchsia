// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides utilities for working with futures.

use std::pin::Pin;

use futures::{future::FusedFuture, task, Future};

/// Future for the [`FutureExt::replace_value`] method.
#[derive(Debug)]
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct ReplaceValue<Fut: Future<Output = ()>, T> {
    future: Fut,
    value: Option<T>,
}

/// An extension trait for [`futures::Future`] that provides specialized adapters.
pub trait FutureExt: Future<Output = ()> {
    /// Map this future's output to a different type, returning a new future of
    /// the resulting type.
    ///
    /// This function is similar to futures::FutureExt::map except:
    ///
    /// - it takes a value instead of a closure
    ///
    /// - it returns a type that can be named
    ///
    /// This function is useful when a mapped future is needed and boxing is not
    /// desired.
    fn replace_value<T>(self, value: T) -> ReplaceValue<Self, T>
    where
        Self: Sized,
    {
        ReplaceValue::new(self, value)
    }
}

impl<Fut: Future<Output = ()>, T> ReplaceValue<Fut, T> {
    fn new(future: Fut, value: T) -> Self {
        Self { future, value: Some(value) }
    }
}

impl<T: ?Sized + Future<Output = ()>> FutureExt for T {}

impl<Fut: Future<Output = ()> + Unpin, T: Unpin> Future for ReplaceValue<Fut, T> {
    type Output = T;

    fn poll(self: Pin<&mut Self>, cx: &mut task::Context<'_>) -> task::Poll<Self::Output> {
        let Self { future, value } = self.get_mut();
        let () = futures::ready!(Pin::new(future).poll(cx));
        task::Poll::Ready(
            value.take().expect("ReplaceValue must not be polled after it returned `Poll::Ready`"),
        )
    }
}

impl<Fut: Future<Output = ()> + Unpin, T: Unpin> FusedFuture for ReplaceValue<Fut, T> {
    fn is_terminated(&self) -> bool {
        self.value.is_none()
    }
}

#[cfg(test)]
mod tests {
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn replace_value_trivial() {
        use super::FutureExt as _;

        let value = "hello world";
        assert_eq!(futures::future::ready(()).replace_value(value).await, value);
    }

    #[fasync::run_singlethreaded(test)]
    async fn is_terminated() {
        use futures::future::{FusedFuture as _, FutureExt as _};

        let fut = &mut futures::future::ready(());
        assert!(!fut.is_terminated());
        assert_eq!(fut.now_or_never(), Some(()));
        assert!(fut.is_terminated());
    }
}
