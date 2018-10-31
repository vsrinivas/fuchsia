// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::{
        future::Future,
        stream::{self, Stream},
        task::{LocalWaker, Poll},
    },
    pin_utils::unsafe_pinned,
    std::pin::Pin,
};

/// Similar to FuturesUnordered, but doesn't terminate when the there are no futures.
/// Also, it is a Future rather than a Stream to make it easier to use with select! macro
pub(crate) struct ConcurrentTasks<T> {
    tasks: stream::FuturesUnordered<T>,
}

impl<T> ConcurrentTasks<T>
where
    T: Future,
{
    unsafe_pinned!(tasks: stream::FuturesUnordered<T>);

    pub fn new() -> Self {
        ConcurrentTasks {
            tasks: stream::FuturesUnordered::new(),
        }
    }

    pub fn add(&mut self, task: T) {
        self.tasks.push(task);
    }

    #[allow(dead_code)] // used in tests
    pub fn len(&self) -> usize {
        self.tasks.len()
    }
}

impl<T, U> Stream for ConcurrentTasks<T>
where
    T: Future<Output = U>,
{
    type Item = U;

    fn poll_next(mut self: Pin<&mut Self>, lw: &LocalWaker) -> Poll<Option<Self::Item>> {
        match self.tasks().poll_next(lw) {
            Poll::Pending | Poll::Ready(None) => Poll::Pending,
            Poll::Ready(Some(item)) => Poll::Ready(Some(item)),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        futures::{future::ready, StreamExt},
        std::sync::{
            atomic::{AtomicUsize, Ordering},
            Arc,
        },
    };

    #[test]
    fn test_concurrent_tasks() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");

        let mut tasks = ConcurrentTasks::new();
        assert_eq!(
            Poll::Pending,
            exec.run_until_stalled(&mut (tasks.by_ref().for_each(|_| ready(()))))
        );

        let count_one = Arc::new(AtomicUsize::new(0));
        tasks.add(simple_future(Arc::clone(&count_one)));
        let count_two = Arc::new(AtomicUsize::new(0));
        tasks.add(simple_future(Arc::clone(&count_two)));

        assert_eq!(
            Poll::Pending,
            exec.run_until_stalled(&mut (tasks.by_ref().for_each(|_| ready(()))))
        );
        assert_eq!(1, count_one.load(Ordering::SeqCst));
        assert_eq!(1, count_two.load(Ordering::SeqCst));
    }

    async fn simple_future(res: Arc<AtomicUsize>) {
        res.fetch_add(1, Ordering::SeqCst);
    }
}
