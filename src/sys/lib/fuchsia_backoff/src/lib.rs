// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use futures::Future;
use std::time::Duration;

/// Execute the async task. If it errs out, attempt to run the task again after a delay if the
/// `backoff` yields a duration. Otherwise, return the first error that occurred to the caller.
///
/// # Examples
///
/// `retry_or_first_error` will succeed if the task returns `Ok` before the `backoff` returns None.
///
/// ```
/// # use std::iter::repeat;
/// # use std::sync::{Arc, atomic::{AtomicUsize, Ordering}};
/// let counter = Arc::new(AtomicUsize::new(0));
/// let result = retry_or_first_error(
///     repeat(Duration::from_secs(1)),
///     || async {
///         let count = counter.fetch_add(1, Ordering::SeqCst);
///         if count == 5 {
///             Ok(count)
///         } else {
///             Err(count)
///         }
///     }),
/// ).await;
/// assert_eq!(result, Ok(5));
/// ```
///
/// If the task fails, the `retry_or_first_error` will return the first error.
///
/// ```
/// # use std::iter::repeat;
/// # use std::sync::{Arc, atomic::{AtomicUsize, Ordering}};
/// let counter = Arc::new(AtomicUsize::new(0));
/// let result = retry_or_first_error(
///     repeat(Duration::from_secs(1)).take(5),
///     || async {
///         let count = counter.fetch_add(1, Ordering::SeqCst);
///         Err(count)
///     }),
/// ).await;
/// assert_eq!(result, Err(0));
/// ```
pub async fn retry_or_first_error<B, T>(mut backoff: B, task: T) -> Result<T::Ok, T::Error>
where
    B: Backoff<T::Error>,
    T: Task,
{
    match next(&mut backoff, task).await {
        Ok(value) => Ok(value),
        Err((err, None)) => Err(err),
        Err((err, Some(task))) => match retry_or_last_error(backoff, task).await {
            Ok(value) => Ok(value),
            Err(_) => Err(err),
        },
    }
}

/// Execute the async task. If it errs out, attempt to run the task again after a delay if the
/// `backoff` yields a duration. Otherwise, return the last error that occurred to the caller.
///
/// # Examples
///
/// `retry_or_last_error` will succeed if the task returns `Ok` before the `backoff` returns None.
///
/// ```
/// # use std::iter::repeat;
/// # use std::sync::{Arc, atomic::{AtomicUsize, Ordering}};
/// let counter = Arc::new(AtomicUsize::new(0));
/// let result = retry_or_last_error(
///     repeat(Duration::from_secs(1)),
///     || async {
///         let count = counter.fetch_add(1, Ordering::SeqCst);
///         if count == 5 {
///             Ok(count)
///         } else {
///             Err(count)
///         }
///     }),
/// ).await;
/// assert_eq!(result, Ok(5));
/// ```
///
/// If the task fails, the `retry_or_last_error` will return the last error.
///
/// ```
/// # use std::iter::repeat;
/// # use std::sync::{Arc, atomic::{AtomicUsize, Ordering}};
/// let counter = Arc::new(AtomicUsize::new(0));
/// let result = retry_or_last_error(
///     repeat(Duration::from_secs(1)).take(5),
///     || async {
///         let count = counter.fetch_add(1, Ordering::SeqCst);
///         Err(count)
///     }),
/// ).await;
/// assert_eq!(result, Err(5));
/// ```
pub async fn retry_or_last_error<B, T>(mut backoff: B, mut task: T) -> Result<T::Ok, T::Error>
where
    B: Backoff<T::Error>,
    T: Task,
{
    loop {
        match next(&mut backoff, task).await {
            Ok(value) => {
                return Ok(value);
            }
            Err((err, None)) => {
                return Err(err);
            }
            Err((_, Some(next))) => {
                task = next;
            }
        }
    }
}

/// Execute the async task. If it errs out, attempt to run the task again after a delay if the
/// `backoff` yields a duration. Otherwise, collect all the errors and return them to the caller.
///
/// # Examples
///
/// `retry_or_last_error` will succeed if it returns `Ok` before the `backoff` returns None.
///
/// ```
/// # use std::iter::repeat;
/// # use std::sync::{Arc, atomic::{AtomicUsize, Ordering}};
/// let counter = Arc::new(AtomicUsize::new(0));
/// let result = retry_or_collect_errors(
///     repeat(Duration::from_secs(1)),
///     || async {
///         let count = counter.fetch_add(1, Ordering::SeqCst);
///         if count == 5 {
///             Ok(count)
///         } else {
///             Err(count)
///         }
///     }),
/// ).await;
/// assert_eq!(result, Ok(5));
/// ```
///
/// If the task fails, it will return all the errors.
///
/// ```
/// # use std::iter::repeat;
/// # use std::sync::{Arc, atomic::{AtomicUsize, Ordering}};
/// let counter = Arc::new(AtomicUsize::new(0));
/// let result = retry_or_last_error(
///     repeat(Duration::from_secs(1)).take(5),
///     || async {
///         let count = counter.fetch_add(1, Ordering::SeqCst);
///         Err(count)
///     }),
/// ).await;
/// assert_eq!(result, Err(vec![0, 1, 2, 3, 4]));
/// ```
pub async fn retry_or_collect_errors<B, T, C>(mut backoff: B, mut task: T) -> Result<T::Ok, C>
where
    B: Backoff<T::Error>,
    T: Task,
    C: Default + Extend<T::Error>,
{
    let mut collection = C::default();
    loop {
        match next(&mut backoff, task).await {
            Ok(value) => {
                return Ok(value);
            }
            Err((err, next)) => {
                collection.extend(Some(err));
                match next {
                    Some(next) => {
                        task = next;
                    }
                    None => {
                        return Err(collection);
                    }
                }
            }
        }
    }
}

async fn next<B, T>(backoff: &mut B, mut task: T) -> Result<T::Ok, (T::Error, Option<T>)>
where
    B: Backoff<T::Error>,
    T: Task,
{
    match task.run().await {
        Ok(value) => Ok(value),
        Err(err) => match backoff.next_backoff(&err) {
            Some(delay) => {
                let delay = fasync::Time::after(delay.into());
                fasync::Timer::new(delay).await;
                Err((err, Some(task)))
            }
            None => Err((err, None)),
        },
    }
}

/// A task produces an asynchronous computation that can be retried if the returned future fails
/// with some error.
pub trait Task {
    /// The type of successful values yielded by the task future.
    type Ok;

    /// The type of failures yielded by the task future.
    type Error;

    /// The future returned when executing this task.
    type Future: Future<Output = Result<Self::Ok, Self::Error>>;

    /// Return a future.
    fn run(&mut self) -> Self::Future;
}

impl<F, Fut, T, E> Task for F
where
    F: FnMut() -> Fut,
    Fut: Future<Output = Result<T, E>>,
{
    type Ok = T;
    type Error = E;
    type Future = Fut;

    fn run(&mut self) -> Self::Future {
        (self)()
    }
}

/// A backoff policy for deciding to retry an operation.
pub trait Backoff<E> {
    fn next_backoff(&mut self, err: &E) -> Option<Duration>;
}

impl<E, I> Backoff<E> for I
where
    I: Iterator<Item = Duration>,
{
    fn next_backoff(&mut self, _: &E) -> Option<Duration> {
        self.next()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async::DurationExt;
    use fuchsia_zircon::DurationNum;
    use futures::prelude::*;
    use futures::task::Poll;
    use std::iter;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;

    #[derive(Clone)]
    struct Counter {
        counter: Arc<AtomicUsize>,
        ok_at: Option<usize>,
    }

    impl Counter {
        fn ok_at(count: usize) -> Self {
            Counter { counter: Arc::new(AtomicUsize::new(0)), ok_at: Some(count) }
        }

        fn never_ok() -> Self {
            Counter { counter: Arc::new(AtomicUsize::new(0)), ok_at: None }
        }
    }

    impl Task for Counter {
        type Ok = usize;
        type Error = usize;
        type Future = future::Ready<Result<usize, usize>>;

        fn run(&mut self) -> Self::Future {
            let count = self.counter.fetch_add(1, Ordering::SeqCst);
            match self.ok_at {
                Some(ok_at) if ok_at == count => future::ready(Ok(count)),
                _ => future::ready(Err(count)),
            }
        }
    }

    fn run<F>(future: F, pending_count: usize) -> F::Output
    where
        F: Future + Send,
        F::Output: std::fmt::Debug + PartialEq + Eq,
    {
        let mut future = future.boxed();
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();

        for _ in 0..pending_count {
            assert_eq!(executor.run_until_stalled(&mut future), Poll::Pending);
            assert_eq!(executor.wake_expired_timers(), false);
            executor.set_fake_time(2.seconds().after_now());
            assert_eq!(executor.wake_expired_timers(), true);
        }

        match executor.run_until_stalled(&mut future) {
            Poll::Ready(value) => value,
            Poll::Pending => panic!("expected future to be ready"),
        }
    }

    // Return `attempts` durations.
    fn backoff(attempts: usize) -> impl Iterator<Item = Duration> {
        iter::repeat(Duration::from_secs(1)).take(attempts)
    }

    #[test]
    fn test_should_succeed() {
        for i in 0..10 {
            // to test passing, always attempt one more attempt than necessary before Counter
            // succeeds.

            assert_eq!(run(retry_or_first_error(backoff(i + 1), Counter::ok_at(i)), i), Ok(i));
            assert_eq!(run(retry_or_last_error(backoff(i + 1), Counter::ok_at(i)), i), Ok(i));
            assert_eq!(
                run(retry_or_collect_errors(backoff(i + 1), Counter::ok_at(i)), i),
                Ok::<usize, Vec<usize>>(i)
            );

            // Check FnMut impl works. It always succeeds during the first iteration.
            let task = || future::ready(Ok::<_, ()>(i));
            assert_eq!(run(retry_or_last_error(backoff(i + 1), task), 0), Ok(i));
        }
    }

    #[test]
    fn test_should_error() {
        for i in 0..10 {
            assert_eq!(run(retry_or_first_error(backoff(i), Counter::never_ok()), i), Err(0));
            assert_eq!(run(retry_or_last_error(backoff(i), Counter::never_ok()), i), Err(i));
            assert_eq!(
                run(retry_or_collect_errors(backoff(i), Counter::never_ok()), i),
                Err::<usize, Vec<usize>>((0..=i).collect())
            );

            // Check FnMut impl works.
            let task = || future::ready(Err::<(), _>(i));
            assert_eq!(run(retry_or_last_error(backoff(i), task), i), Err(i));
        }
    }
}
