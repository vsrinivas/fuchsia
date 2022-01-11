// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helpers for working with streams.

use {
    futures::{ready, Stream, StreamExt},
    std::{
        future::Future,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// Given a stream `stream` and an auxiliary item `with`,
/// converts the `stream` into a future of `(next_item, tail_of_stream, with)`,
/// where `next_item` and `tail_of_stream` are respectively the head and tail of the `stream`.
/// This is logically equivalent to
/// `stream.into_future().map(move |(value, stream)| (value, stream, with))`
pub(super) struct NextWith<St, With> {
    opt: Option<(St, With)>,
}

const USED_AFTER_COMPLETION: &str = "`NextWith` used after completion";

impl<St, With> NextWith<St, With> {
    /// Create a new `NextWith` future.
    pub fn new(stream: St, with: With) -> Self {
        NextWith { opt: Some((stream, with)) }
    }

    fn stream(&mut self) -> &mut St {
        &mut self.opt.as_mut().expect(USED_AFTER_COMPLETION).0
    }

    fn take(&mut self) -> (St, With) {
        self.opt.take().expect(USED_AFTER_COMPLETION)
    }
}

impl<St, With> Unpin for NextWith<St, With> {}

impl<St, With> Future for NextWith<St, With>
where
    St: Stream + Unpin,
{
    type Output = Option<(St::Item, St, With)>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let opt = ready!(self.stream().poll_next_unpin(cx));
        Poll::Ready(opt.map(|next| {
            let (st, with) = self.take();
            (next, st, with)
        }))
    }
}
