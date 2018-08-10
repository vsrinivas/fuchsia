// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
use futures::task;
use futures::stream::{Fuse, Stream, StreamExt};
use std::marker::Unpin;
use std::mem::PinMut;

pub struct GroupAvailable<S, T, E> where S: Stream<Item = Result<T, E>> {
    stream: Fuse<S>,
    error: Option<E>,
}

impl<S, T, E> Unpin for GroupAvailable<S, T, E>
where
    S: Unpin + Stream<Item = Result<T, E>>
{}

impl<S, T, E> GroupAvailable<S, T, E>
where
    S: Unpin + Stream<Item = Result<T, E>>
{
    // Safety: projecting to `Fuse<S>` is safe because GroupAvailable is `!Unpin`
    // when `S` is `!Unpin`, and `GroupAvailable` doesn't move out of `stream`.
    unsafe_pinned!(stream: Fuse<S>);
    // Safety: nothing requires `error` not to move.
    unsafe_unpinned!(error: Option<E>);
}

impl<S, T, E> Stream for GroupAvailable<S, T, E>
where
    S: Unpin + Stream<Item = Result<T, E>>
{
    type Item = Result<Vec<T>, E>;

    fn poll_next(
        mut self: PinMut<Self>,
        cx: &mut task::Context,
    ) -> Poll<Option<Self::Item>> {
        if let Some(e) = self.error().take() {
            return Poll::Ready(Some(Err(e)));
        }
        let mut batch = match ready!(self.stream().poll_next(cx)?) {
            Some(item) => vec![item],
            None => return Poll::Ready(None),
        };
        loop {
            match self.stream().poll_next(cx) {
                Poll::Ready(Some(Ok(item))) => batch.push(item),
                Poll::Ready(None) | Poll::Pending => break,
                Poll::Ready(Some(Err(e))) => {
                    *self.error() = Some(e);
                    break;
                }
            }
        }
        Poll::Ready(Some(Ok(batch)))
    }
}

pub trait GroupAvailableExt: Stream {
    /// An adaptor for grouping readily available messages into a single Vec item.
    ///
    /// Similar to StreamExt.chunks(), except the size of produced batches can be arbitrary,
    /// and only depends on how many items were available when the stream was polled.
    fn group_available<T, E>(self) -> GroupAvailable<Self, T, E>
    where
        Self: Stream<Item = Result<T, E>>,
        Self: Sized,
    {
        GroupAvailable {
            stream: self.fuse(),
            error: None
        }
    }
}

impl<T> GroupAvailableExt for T where T: Stream + ?Sized {}

#[cfg(test)]
mod tests {
    use async;
    use async::temp::TempStreamExt;
    use Never;
    use futures::prelude::*;
    use futures::stream;
    use super::GroupAvailableExt;
    use futures::channel::mpsc;

    #[test]
    fn empty() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");
        let (item, _) = exec.run_singlethreaded(
            stream::empty::<Result<(), Never>>().group_available().try_into_future())
                .unwrap_or_else(Never::into_any);
        assert!(item.is_none());
    }

    #[test]
    fn pending() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");
        let always_pending = stream::poll_fn(|_cx| Poll::Pending::<Option<Result<(), Never>>>);
        let mut group_available = always_pending.group_available();
        let mut fut = group_available.try_next();
        let a = exec.run_until_stalled(&mut fut);
        assert!(a.is_pending());
    }

    #[test]
    fn group_available_items() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");
        let (send, recv) = mpsc::unbounded();

        send.unbounded_send(10i32).unwrap();
        send.unbounded_send(20i32).unwrap();
        let mut s = recv.map(Ok).group_available();
        let item = exec.run_singlethreaded(s.try_next()).unwrap_or_else(Never::into_any);
        assert_eq!(Some(vec![10i32, 20i32]), item);

        send.unbounded_send(30i32).unwrap();
        let item = exec.run_singlethreaded(s.try_next()).unwrap_or_else(Never::into_any);
        assert_eq!(Some(vec![30i32]), item);
    }

    #[test]
    fn buffer_error() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");

        let mut s = stream::iter(vec![Ok(10i32), Ok(20i32), Err(-30i32)])
            .group_available();
        let item = exec.run_singlethreaded(s.try_next())
            .expect("expected a successful value");
        assert_eq!(Some(vec![10i32, 20i32]), item);

        let res = exec.run_singlethreaded(s.try_next());
        assert_eq!(Err(-30i32), res);
    }
}
