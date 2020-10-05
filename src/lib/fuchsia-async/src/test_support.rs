// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::TimeoutExt;
use futures::{lock::Mutex, prelude::*, stream};
use std::pin::Pin;
use std::sync::Arc;
#[cfg(target_os = "fuchsia")]
use std::task::Poll;
use std::time::Duration;

// Apply the timeout from config to test
// Ideally this would be a function like Config::with_timeout, but we need to handle Send and !Send
// and it's likely better not to have to duplicate this code.
macro_rules! apply_timeout {
    ($config:expr, $test:expr) => {{
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
    fn run_singlethreaded(
        test: Arc<dyn Send + Sync + Fn(usize) -> Pin<Box<dyn Future<Output = Self>>>>,
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
        test: F,
        threads: usize,
        cfg: Config,
    ) -> Self;
}

impl<E: 'static + std::fmt::Debug> TestResult for Result<(), E> {
    fn run_singlethreaded(
        test: Arc<dyn Send + Sync + Fn(usize) -> Pin<Box<dyn Future<Output = Self>>>>,
        cfg: Config,
    ) -> Self {
        let run_stream = Arc::new(Mutex::new(stream::iter(0..cfg.repeat_count).fuse()));
        let test = apply_timeout!(cfg, test);
        cfg.in_parallel(Arc::new(move || {
            let run_stream = run_stream.clone();
            let test = test.clone();
            crate::Executor::new().expect("Failed to create executor").run_singlethreaded(
                async move {
                    while let Some(run) = run_stream.lock().await.next().await {
                        if let Err(e) = test(run).await {
                            panic!("run {} failed with error {:?}", run, e)
                        }
                    }
                },
            )
        }));
        Ok(())
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
        test: F,
        threads: usize,
        cfg: Config,
    ) -> Self {
        crate::Executor::new().expect("Failed to create executor").run(
            stream::iter(0..cfg.repeat_count)
                .map(Ok)
                .try_for_each_concurrent(cfg.max_concurrency, apply_timeout!(cfg, test)),
            cfg.scale_threads(threads),
        )
    }
}

impl TestResult for () {
    fn run_singlethreaded(
        test: Arc<dyn Send + Sync + Fn(usize) -> Pin<Box<dyn Future<Output = Self>>>>,
        cfg: Config,
    ) -> Self {
        let run_stream = Arc::new(Mutex::new(stream::iter(0..cfg.repeat_count).fuse()));
        let test = apply_timeout!(cfg, Arc::new(test));
        cfg.in_parallel(Arc::new(move || {
            let run_stream = run_stream.clone();
            let test = test.clone();
            crate::Executor::new().expect("Failed to create executor").run_singlethreaded(
                async move {
                    while let Some(run) = run_stream.lock().await.next().await {
                        test(run).await;
                    }
                },
            )
        }));
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
        test: F,
        threads: usize,
        cfg: Config,
    ) -> Self {
        crate::Executor::new().expect("Failed to create executor").run(
            stream::iter(0..cfg.repeat_count)
                .for_each_concurrent(cfg.max_concurrency, apply_timeout!(cfg, test)),
            cfg.scale_threads(threads),
        )
    }
}

/// Configuration variables for a single test run.
#[derive(Clone)]
pub struct Config {
    repeat_count: usize,
    max_concurrency: usize,
    max_threads: usize,
    timeout: Option<Duration>,
}

fn env_var<T: std::str::FromStr>(name: &str, default: T) -> T {
    std::env::var(name).unwrap_or_default().parse().unwrap_or(default)
}

impl Config {
    fn get() -> Self {
        let repeat_count = std::cmp::max(1, env_var("FASYNC_TEST_REPEAT_COUNT", 1));
        let max_concurrency = env_var("FASYNC_TEST_MAX_CONCURRENCY", 0);
        let timeout_seconds = env_var("FASYNC_TEST_TIMEOUT_SECONDS", 0);
        let max_threads = env_var("FASYNC_TEST_MAX_THREADS", 0);
        let timeout =
            if timeout_seconds == 0 { None } else { Some(Duration::from_secs(timeout_seconds)) };
        Self { repeat_count, max_concurrency, max_threads, timeout }
    }

    // Scale a hard-coded thread count by some factor to account for increased concurrency in the config.
    fn scale_threads(&self, test_threads: usize) -> usize {
        // We try to scale up by the maximum concurrency we'll see.
        let scale = if self.max_concurrency == 0 {
            // If concurrency is unbounded, we'll see a maximum of the test repetitions.
            self.repeat_count
        } else {
            // Otherwise the maximum concurrency will be the lower of repeat count and the concurrency cap.
            std::cmp::min(self.repeat_count, self.max_concurrency)
        };
        // Without resource constraints, we'd like to run with our test threads scaled up by the cooncurrency.
        let desired_threads = test_threads * scale;
        // Account for resource constraints
        let capped_threads = if self.max_threads == 0 {
            desired_threads
        } else {
            std::cmp::min(desired_threads, self.max_threads)
        };
        // Always run *AT LEAST* the typed in number of threads in the test definition.
        std::cmp::max(capped_threads, test_threads)
    }

    fn in_parallel(&self, f: Arc<dyn 'static + Send + Sync + Fn()>) {
        // N-1 background threads...
        let threads: Vec<_> = std::iter::repeat_with(|| {
            let f = f.clone();
            std::thread::spawn(move || f())
        })
        .take(self.scale_threads(1) - 1)
        .collect();
        // ... and we'll consume this thread to get to N.
        f();
        threads.into_iter().for_each(|t| t.join().expect("worker threads should be joinable"));
    }
}

/// Runs a test in an executor, potentially repeatedly and concurrently
pub fn run_singlethreaded_test<F, Fut, R>(test: F) -> R
where
    F: 'static + Send + Sync + Fn(usize) -> Fut,
    Fut: 'static + Future<Output = R>,
    R: TestResult,
{
    TestResult::run_singlethreaded(Arc::new(move |run| test(run).boxed_local()), Config::get())
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
pub fn run_test<F, Fut, R>(test: F, threads: usize) -> R
where
    F: 'static + Send + Fn(usize) -> Fut,
    Fut: 'static + Send + Future<Output = R>,
    R: MultithreadedTestResult,
{
    MultithreadedTestResult::run(test, threads, Config::get())
}

#[cfg(test)]
mod tests {
    use super::{Config, MultithreadedTestResult, TestResult};
    use futures::lock::Mutex;
    use futures::prelude::*;
    use std::collections::HashSet;
    use std::sync::Arc;
    use std::time::Duration;

    #[test]
    fn scale_threads() {
        let cfg = |repeat_count, max_concurrency, max_threads| Config {
            repeat_count,
            max_concurrency,
            max_threads,
            timeout: None,
        };
        // Unbounded work should look like tests_per_thread * repeat_count
        assert_eq!(cfg(1, 0, 0).scale_threads(1), 1);
        assert_eq!(cfg(1, 0, 0).scale_threads(20), 20);
        assert_eq!(cfg(30, 0, 0).scale_threads(1), 30);
        assert_eq!(cfg(30, 0, 0).scale_threads(20), 600);
        // Max concurrency should affect repetition count
        assert_eq!(cfg(30, 1, 0).scale_threads(20), 20);
        // Capping thread count should not decrease programmatic maximum
        assert_eq!(cfg(1, 0, 5).scale_threads(10), 10);
        assert_eq!(cfg(1, 0, 15).scale_threads(10), 10);
    }

    #[test]
    fn run_singlethreaded() {
        const REPEAT_COUNT: usize = 1000;
        const MAX_THREADS: usize = 10;
        let pending_runs: Arc<Mutex<HashSet<_>>> =
            Arc::new(Mutex::new((0..REPEAT_COUNT).collect()));
        let pending_runs_child = pending_runs.clone();
        TestResult::run_singlethreaded(
            Arc::new(move |i| {
                let pending_runs_child = pending_runs_child.clone();
                async move {
                    assert!(pending_runs_child.lock().await.remove(&i));
                }
                .boxed_local()
            }),
            Config {
                repeat_count: REPEAT_COUNT,
                max_concurrency: 0,
                max_threads: MAX_THREADS,
                timeout: None,
            },
        );
        assert!(pending_runs.try_lock().unwrap().is_empty());
    }

    // TODO(fxbug.dev/60525): should_panic tests trigger LSAN
    #[ignore]
    #[test]
    #[should_panic]
    fn run_singlethreaded_with_timeout() {
        TestResult::run_singlethreaded(
            Arc::new(move |_| {
                async move {
                    futures::future::pending::<()>().await;
                }
                .boxed_local()
            }),
            Config {
                repeat_count: 1,
                max_concurrency: 0,
                max_threads: 0,
                timeout: Some(Duration::from_millis(1)),
            },
        );
    }

    #[test]
    #[cfg(target_os = "fuchsia")]
    fn run_until_stalled() {
        const REPEAT_COUNT: usize = 1000;
        let pending_runs: Arc<Mutex<HashSet<_>>> =
            Arc::new(Mutex::new((0..REPEAT_COUNT).collect()));
        let pending_runs_child = pending_runs.clone();
        match TestResult::run_until_stalled(
            &mut crate::Executor::new().unwrap(),
            move |i| {
                let pending_runs_child = pending_runs_child.clone();
                async move {
                    assert!(pending_runs_child.lock().await.remove(&i));
                }
            },
            Config {
                repeat_count: REPEAT_COUNT,
                max_concurrency: 1,
                max_threads: 1,
                timeout: None,
            },
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
            move |i| {
                let pending_runs_child = pending_runs_child.clone();
                async move {
                    assert!(pending_runs_child.lock().await.remove(&i));
                }
            },
            THREADS,
            Config {
                repeat_count: REPEAT_COUNT,
                max_concurrency: 0,
                max_threads: THREADS,
                timeout: None,
            },
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
            move |_| async move {
                futures::future::pending::<()>().await;
            },
            THREADS,
            Config {
                repeat_count: 1,
                max_concurrency: 0,
                max_threads: 0,
                timeout: Some(Duration::from_millis(1)),
            },
        );
    }
}
