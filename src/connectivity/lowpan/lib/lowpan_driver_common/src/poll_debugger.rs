// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::future::Future;
use core::pin::Pin;
use core::task::{Context, Poll};
use futures::prelude::*;
use futures::stream::Stream;
use log::trace;

#[derive(Debug)]
pub struct PollDebugger<F>(F, &'static str);
unsafe impl<F: Send> Send for PollDebugger<F> {}

impl<F: Future + Unpin> Future for PollDebugger<F> {
    type Output = F::Output;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = self;
        futures::pin_mut!(this);

        trace!(target: this.1, "POLL: Entering...");
        let ret = this.0.poll_unpin(cx);
        match ret {
            Poll::Ready(_) => trace!(target: this.1, "POLL: ...exited: READY"),
            Poll::Pending => trace!(target: this.1, "POLL: ...exited: PENDING"),
        }
        ret
    }
}
impl<F: Stream + Unpin> Stream for PollDebugger<F> {
    type Item = F::Item;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = self;
        futures::pin_mut!(this);

        trace!(target: this.1, "POLL_NEXT: Entering...");
        let ret = this.0.poll_next_unpin(cx);
        match ret {
            Poll::Ready(Some(_)) => trace!(target: this.1, "POLL_NEXT: ...exited: READY SOME"),
            Poll::Ready(None) => trace!(target: this.1, "POLL_NEXT: ...exited: READY NONE"),
            Poll::Pending => trace!(target: this.1, "POLL_NEXT: ...exited: PENDING"),
        }
        ret
    }
}

#[cfg(feature = "poll_debugger")]
mod exts {
    use super::*;

    pub trait FutureDebugExt: Sized {
        fn debug_poll(self, what: &'static str) -> PollDebugger<Self> {
            PollDebugger(self, what)
        }
    }

    pub trait StreamDebugExt: Sized {
        fn debug_poll_next(self, what: &'static str) -> PollDebugger<Self> {
            PollDebugger(self, what)
        }
    }

    impl<F: Future + Unpin> FutureDebugExt for F {}
    impl<F: Stream + Unpin> StreamDebugExt for F {}
}

#[cfg(not(feature = "poll_debugger"))]
mod exts {
    use super::*;

    pub trait FutureDebugExt: Sized {
        fn debug_poll(self, _: &'static str) -> Self {
            self
        }
    }

    pub trait StreamDebugExt: Sized {
        fn debug_poll_next(self, _: &'static str) -> Self {
            self
        }
    }

    impl<F: Future + Unpin> FutureDebugExt for F {}
    impl<F: Stream + Unpin> StreamDebugExt for F {}
}

pub use exts::*;
