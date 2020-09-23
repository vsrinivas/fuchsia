// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::TimeoutExt;
use futures::{prelude::*, stream};
#[cfg(target_os = "fuchsia")]
use std::task::Poll;
use std::time::Duration;

// Apply the timeout from config to test
// Ideally this would be a function like Config::with_timeout, but we need to handle Send and !Send
// and it's likely better not to have to duplicate this code.
macro_rules! apply_timeout {
    ($config:ident, $test:ident) => {{
        let timeout = $config.timeout;
        let test = $test;
        move |run| {
            let test = test(run);
            async move {
                if let Some(timeout) = timeout {
                    test.on_timeout(timeout, || panic!("timeout on run {}", run)).await
                } else {
                    test.await
                }
            }
        }
    }};
}

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
                .try_for_each_concurrent(cfg.max_concurrency, apply_timeout!(cfg, test)),
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
                .try_for_each_concurrent(cfg.max_concurrency, apply_timeout!(cfg, test)),
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
                .try_for_each_concurrent(cfg.max_concurrency, apply_timeout!(cfg, test)),
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
            stream::iter(0..cfg.repeat_count)
                .for_each_concurrent(cfg.max_concurrency, apply_timeout!(cfg, test)),
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
                    .for_each_concurrent(cfg.max_concurrency, apply_timeout!(cfg, test)),
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
            stream::iter(0..cfg.repeat_count)
                .for_each_concurrent(cfg.max_concurrency, apply_timeout!(cfg, test)),
            threads,
        )
    }
}

/// Configuration variables for a single test run.
pub struct Config {
    repeat_count: usize,
    max_concurrency: usize,
    timeout: Option<Duration>,
}

fn env_var<T: std::str::FromStr>(name: &str, default: T) -> T {
    std::env::var(name).unwrap_or_default().parse().unwrap_or(default)
}

impl Config {
    fn get() -> Self {
        let repeat_count = env_var("FASYNC_TEST_REPEAT_COUNT", 1);
        let max_concurrency = env_var("FASYNC_TEST_MAX_CONCURRENCY", 0);
        let timeout_seconds = env_var("FASYNC_TEST_TIMEOUT_SECONDS", 0);
        let timeout =
            if timeout_seconds == 0 { None } else { Some(Duration::from_secs(timeout_seconds)) };
        Self { repeat_count, max_concurrency, timeout }
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
    use std::time::Duration;

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
            Config { repeat_count: REPEAT_COUNT, max_concurrency: 0, timeout: None },
        );
        assert!(pending_runs.try_lock().unwrap().is_empty());
    }

    // TODO(fxbug.dev/60525): should_panic tests trigger LSAN
    #[ignore]
    #[test]
    #[should_panic]
    fn run_singlethreaded_with_timeout() {
        TestResult::run_singlethreaded(
            &mut crate::Executor::new().unwrap(),
            move |_| async move {
                futures::future::pending::<()>().await;
            },
            Config { repeat_count: 1, max_concurrency: 0, timeout: Some(Duration::from_millis(1)) },
        );
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
            Config { repeat_count: REPEAT_COUNT, max_concurrency: 1, timeout: None },
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
            Config { repeat_count: REPEAT_COUNT, max_concurrency: 0, timeout: None },
        );
        assert!(pending_runs.try_lock().unwrap().is_empty());
    }

    // TODO(fxbug.dev/60525): should_panic tests trigger LSAN
    #[ignore]
    #[test]
    #[should_panic]
    fn run_with_timeout() {
        const THREADS: usize = 4;
        MultithreadedTestResult::run(
            &mut crate::Executor::new().unwrap(),
            move |_| async move {
                futures::future::pending::<()>().await;
            },
            THREADS,
            Config { repeat_count: 1, max_concurrency: 0, timeout: Some(Duration::from_millis(1)) },
        );
    }
}
