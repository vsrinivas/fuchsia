// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::ready;
use futures::future::Future;
use futures::stream::{self, Fuse, Stream, StreamExt};
use futures::task::{Poll, LocalWaker};
use pin_utils::{unsafe_pinned, unsafe_unpinned};
use std::marker::Unpin;
use std::pin::Pin;

use crate::Never;

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
        mut self: Pin<&mut Self>,
        lw: &LocalWaker,
    ) -> Poll<Option<Self::Item>> {
        if let Some(e) = self.error().take() {
            return Poll::Ready(Some(Err(e)));
        }
        let mut batch = match ready!(self.stream().poll_next(lw)?) {
            Some(item) => vec![item],
            None => return Poll::Ready(None),
        };
        loop {
            match self.stream().poll_next(lw) {
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

/// Similar to FuturesUnordered, but doesn't terminate when the there are no futures.
/// Also, it is a Future rather than a Stream to make it easier to use with select! macro
pub struct ConcurrentTasks<T> {
    tasks: stream::FuturesUnordered<T>
}

impl<T> ConcurrentTasks<T> where T: Future {
    unsafe_pinned!(tasks: stream::FuturesUnordered<T>);

    pub fn new() -> Self {
        ConcurrentTasks {
            tasks: stream::FuturesUnordered::new(),
        }
    }

    pub fn add(&mut self, task: T) {
        self.tasks.push(task);
    }
}

impl<T> Future for ConcurrentTasks<T> where T: Future<Output = ()> {
    type Output = Never;

    fn poll(mut self: Pin<&mut Self>, lw: &LocalWaker) -> Poll<Self::Output> {
        loop {
            match self.tasks().poll_next(lw) {
                Poll::Ready(Some(())) => {},
                Poll::Pending | Poll::Ready(None) => return Poll::Pending,
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async::{self as fasync, temp::TempStreamExt};
    use futures::channel::mpsc;
    use futures::stream::{self, TryStreamExt};
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};

    use crate::Never;

    #[test]
    fn empty() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let (item, _) = exec.run_singlethreaded(
            stream::empty::<Result<(), Never>>().group_available().try_into_future())
                .unwrap_or_else(Never::into_any);
        assert!(item.is_none());
    }

    #[test]
    fn pending() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let always_pending = stream::poll_fn(|_lw| Poll::Pending::<Option<Result<(), Never>>>);
        let mut group_available = always_pending.group_available();
        let mut fut = group_available.try_next();
        let a = exec.run_until_stalled(&mut fut);
        assert!(a.is_pending());
    }

    #[test]
    fn group_available_items() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
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
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");

        let mut s = stream::iter(vec![Ok(10i32), Ok(20i32), Err(-30i32)])
            .group_available();
        let item = exec.run_singlethreaded(s.try_next())
            .expect("expected a successful value");
        assert_eq!(Some(vec![10i32, 20i32]), item);

        let res = exec.run_singlethreaded(s.try_next());
        assert_eq!(Err(-30i32), res);
    }

    #[test]
    fn concurrent_tasks() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");

        let mut tasks = ConcurrentTasks::new();
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut tasks));

        let count_one = Arc::new(AtomicUsize::new(0));
        tasks.add(simple_future(Arc::clone(&count_one)));
        let count_two = Arc::new(AtomicUsize::new(0));
        tasks.add(simple_future(Arc::clone(&count_two)));

        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut tasks));
        assert_eq!(1, count_one.load(Ordering::SeqCst));
        assert_eq!(1, count_two.load(Ordering::SeqCst));
    }

    async fn simple_future(res: Arc<AtomicUsize>) {
        res.fetch_add(1, Ordering::SeqCst);
    }
}
