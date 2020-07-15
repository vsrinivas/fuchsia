// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{prelude::*, stream};
#[cfg(target_os = "fuchsia")]
use std::task::Poll;

/// Defines how to compose multiple test runs for a kind of test result.
pub trait TestResult: Sized {
    /// How to repeatedly run a test with this result in a single threaded executor.
    fn run_singlethreaded<F: 'static + Fn(usize) -> Fut, Fut: 'static + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        cfg: Config,
    ) -> Self;

    /// Similarly, but use run_until_stalled
    #[cfg(target_os = "fuchsia")]
    fn run_until_stalled<F: 'static + Fn(usize) -> Fut, Fut: 'static + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        cfg: Config,
    ) -> Poll<Self>;
}

/// Defines how to compose multiple test runs for a kind of test result in a multithreaded executor.
pub trait MultithreadedTestResult: Sized {
    /// How to repeatedly run a test with this result in a multi threaded executor.
    fn run<F: 'static + Send + Fn(usize) -> Fut, Fut: 'static + Send + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        threads: usize,
        cfg: Config,
    ) -> Self;
}

impl<E: 'static> TestResult for Result<(), E> {
    fn run_singlethreaded<F: 'static + Fn(usize) -> Fut, Fut: 'static + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        cfg: Config,
    ) -> Self {
        executor.run_singlethreaded(
            stream::iter(0..cfg.repeat_count)
                .map(Ok)
                .try_for_each_concurrent(cfg.max_concurrency, test),
        )
    }

    #[cfg(target_os = "fuchsia")]
    fn run_until_stalled<F: 'static + Fn(usize) -> Fut, Fut: 'static + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        cfg: Config,
    ) -> Poll<Self> {
        executor.run_until_stalled(
            &mut stream::iter(0..cfg.repeat_count)
                .map(Ok)
                .try_for_each_concurrent(cfg.max_concurrency, test),
        )
    }
}

impl<E: 'static + Send> MultithreadedTestResult for Result<(), E> {
    fn run<F: 'static + Send + Fn(usize) -> Fut, Fut: 'static + Send + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        threads: usize,
        cfg: Config,
    ) -> Self {
        executor.run(
            stream::iter(0..cfg.repeat_count)
                .map(Ok)
                .try_for_each_concurrent(cfg.max_concurrency, test),
            threads,
        )
    }
}

impl TestResult for () {
    fn run_singlethreaded<F: 'static + Fn(usize) -> Fut, Fut: 'static + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        cfg: Config,
    ) -> Self {
        executor.run_singlethreaded(
            stream::iter(0..cfg.repeat_count).for_each_concurrent(cfg.max_concurrency, test),
        )
    }

    #[cfg(target_os = "fuchsia")]
    fn run_until_stalled<F: 'static + Fn(usize) -> Fut, Fut: 'static + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        cfg: Config,
    ) -> Poll<Self> {
        // TODO(ctiller): figure out why this is necessary and unify the loops
        if cfg.repeat_count == 1 {
            let test = test(1);
            crate::pin_mut!(test);
            executor.run_until_stalled(&mut test)
        } else {
            executor.run_until_stalled(
                &mut stream::iter(0..cfg.repeat_count)
                    .for_each_concurrent(cfg.max_concurrency, test),
            )
        }
    }
}

impl MultithreadedTestResult for () {
    fn run<F: 'static + Send + Fn(usize) -> Fut, Fut: 'static + Send + Future<Output = Self>>(
        executor: &mut crate::Executor,
        test: F,
        threads: usize,
        cfg: Config,
    ) -> Self {
        executor.run(
            stream::iter(0..cfg.repeat_count).for_each_concurrent(cfg.max_concurrency, test),
            threads,
        )
    }
}

/// Configuration variables for a single test run.
pub struct Config {
    repeat_count: usize,
    max_concurrency: usize,
}

impl Config {
    fn get() -> Self {
        let repeat_count =
            std::env::var("FASYNC_TEST_REPEAT_COUNT").unwrap_or_default().parse().unwrap_or(1);
        let max_concurrency =
            std::env::var("FASYNC_TEST_MAX_CONCURRENCY").unwrap_or_default().parse().unwrap_or(0);
        Self { repeat_count, max_concurrency }
    }
}

/// Runs a test in an executor, potentially repeatedly and concurrently
pub fn run_singlethreaded_test<F, Fut, R>(executor: &mut crate::Executor, test: F) -> R
where
    F: 'static + Fn(usize) -> Fut,
    Fut: 'static + Future<Output = R>,
    R: TestResult,
{
    TestResult::run_singlethreaded(executor, test, Config::get())
}

/// Runs a test in an executor until it's stalled
#[cfg(target_os = "fuchsia")]
pub fn run_until_stalled_test<F, Fut, R>(executor: &mut crate::Executor, test: F) -> R
where
    F: 'static + Fn(usize) -> Fut,
    Fut: 'static + Future<Output = R>,
    R: TestResult,
{
    match TestResult::run_until_stalled(executor, test, Config::get()) {
        Poll::Ready(result) => result,
        _ => panic!(
            "Stalled without completing. Consider using \"run_singlethreaded\", or check for a deadlock."
        ),
    }
}

/// Runs a test in an executor, potentially repeatedly and concurrently
pub fn run_test<F, Fut, R>(executor: &mut crate::Executor, test: F, threads: usize) -> R
where
    F: 'static + Send + Fn(usize) -> Fut,
    Fut: 'static + Send + Future<Output = R>,
    R: MultithreadedTestResult,
{
    MultithreadedTestResult::run(executor, test, threads, Config::get())
}

#[cfg(test)]
mod tests {
    use super::{Config, MultithreadedTestResult, TestResult};
    use futures::lock::Mutex;
    use std::collections::HashSet;
    use std::rc::Rc;
    use std::sync::Arc;

    #[test]
    fn run_singlethreaded() {
        const REPEAT_COUNT: usize = 1000;
        let pending_runs: Rc<Mutex<HashSet<_>>> = Rc::new(Mutex::new((0..REPEAT_COUNT).collect()));
        let pending_runs_child = pending_runs.clone();
        TestResult::run_singlethreaded(
            &mut crate::Executor::new().unwrap(),
            move |i| {
                let pending_runs_child = pending_runs_child.clone();
                async move {
                    assert!(pending_runs_child.lock().await.remove(&i));
                }
            },
            Config { repeat_count: REPEAT_COUNT, max_concurrency: 0 },
        );
        assert!(pending_runs.try_lock().unwrap().is_empty());
    }

    #[test]
    #[cfg(target_os = "fuchsia")]
    fn run_until_stalled() {
        const REPEAT_COUNT: usize = 1000;
        let pending_runs: Rc<Mutex<HashSet<_>>> = Rc::new(Mutex::new((0..REPEAT_COUNT).collect()));
        let pending_runs_child = pending_runs.clone();
        match TestResult::run_until_stalled(
            &mut crate::Executor::new().unwrap(),
            move |i| {
                let pending_runs_child = pending_runs_child.clone();
                async move {
                    assert!(pending_runs_child.lock().await.remove(&i));
                }
            },
            Config { repeat_count: REPEAT_COUNT, max_concurrency: 1 },
        ) {
            std::task::Poll::Ready(()) => (),
            _ => panic!("Expected everything stalled"),
        }
        assert!(pending_runs.try_lock().unwrap().is_empty());
    }

    #[test]
    fn run() {
        const REPEAT_COUNT: usize = 1000;
        const THREADS: usize = 4;
        let pending_runs: Arc<Mutex<HashSet<_>>> =
            Arc::new(Mutex::new((0..REPEAT_COUNT).collect()));
        let pending_runs_child = pending_runs.clone();
        MultithreadedTestResult::run(
            &mut crate::Executor::new().unwrap(),
            move |i| {
                let pending_runs_child = pending_runs_child.clone();
                async move {
                    assert!(pending_runs_child.lock().await.remove(&i));
                }
            },
            THREADS,
            Config { repeat_count: REPEAT_COUNT, max_concurrency: 0 },
        );
        assert!(pending_runs.try_lock().unwrap().is_empty());
    }
}
