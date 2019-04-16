// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{
    future::{FusedFuture, Future, FutureExt},
    task::Waker,
    Poll,
};
use std::marker::Unpin;
use std::pin::Pin;

pub struct FutureOrEmpty<'a, F>(pub &'a mut Option<F>);

impl<'a, F> Future for FutureOrEmpty<'a, F>
where
    F: Future + Unpin,
{
    type Output = F::Output;
    fn poll(mut self: Pin<&mut Self>, waker: &Waker) -> Poll<F::Output> {
        match &mut self.0 {
            None => Poll::Pending,
            Some(fut) => fut.poll_unpin(waker),
        }
    }
}

impl<'a, F> FusedFuture for FutureOrEmpty<'a, F>
where
    F: Future + FusedFuture + Unpin,
{
    fn is_terminated(&self) -> bool {
        match &self.0 {
            None => true,
            Some(fut) => fut.is_terminated(),
        }
    }
}
