// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fuchsia_async as fasync,
    futures::future::BoxFuture,
    futures::{future, select, Future, FutureExt},
    pin_utils::pin_mut,
    std::io::Write,
};

#[macro_use]
pub mod expect;

// Test harnesses
pub mod control;
pub mod emulator;
pub mod host_driver;
pub mod low_energy_central;
pub mod low_energy_peripheral;
pub mod profile;

/// A `TestHarness` is a type that provides an interface to test cases for interacting with
/// functionality under test. For example, a WidgetHarness might provide controls for interacting
/// with and meausuring a Widget, allowing us to easily write tests for Widget functionality.
///
/// A `TestHarness` defines how to initialize (via `init()`) the harness resources and how to
/// terminate (via `terminate()`) them when done. The `init()` function can also provide some
/// environment resources (`env`) to be held for the test duration, and also a task (`runner`) that
/// can be executed to perform asynchronous behavior necessary for the test harness to function
/// correctly.
pub trait TestHarness: Sized {
    /// The type of environment needed to be held whilst the test runs. This is normally used to
    /// keep resources open during the test, and allow a graceful shutdown in `terminate`.
    type Env: Send + 'static;

    /// A future that models any background computation the harness needs to process. If no
    /// processing is needed, implementations should use `future::Pending` to model a future that
    /// never returns Poll::Ready
    type Runner: Future<Output = Result<(), Error>> + Unpin + Send + 'static;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>>;
    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>>;
}

/// We can run any test which is an async function from some harness `H` to a result
async fn run_with_harness<H, F, Fut>(test_func: F) -> Result<(), Error>
where
    H: TestHarness,
    F: FnOnce(H) -> Fut + Send + 'static,
    Fut: Future<Output = Result<(), Error>> + Send + 'static,
{
    let (harness, env, runner) = H::init().await?;

    let run_test = test_func(harness);
    pin_mut!(run_test);
    pin_mut!(runner);

    let result = select! {
        test_result = run_test.fuse() => test_result,
        runner_result = runner.fuse() => runner_result,
    };

    let term_result = H::terminate(env).await;
    // Return test failure if it exists, else return terminate failure, else return the
    // successful test result
    result.and_then(|ok| term_result.map(|_| ok))
}

/// Sets up the test environment and the given test case. Each integration test case is an
/// asynchronous function from some harness `H` to the result of the test run.
pub fn run_test<F, H, Fut>(test_func: F) -> Result<(), Error>
where
    F: FnOnce(H) -> Fut + Send + 'static,
    Fut: Future<Output = Result<(), Error>> + Send + 'static,
    H: TestHarness,
{
    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    let result = executor.run_singlethreaded(run_with_harness(test_func));
    if let Err(err) = &result {
        println!("\x1b[31mFAILED\x1b[0m");
        println!("Error running test: {}", err);
    } else {
        println!("\x1b[32mPASSED\x1b[0m");
    }
    result
}

/// The Unit type can be used as the empty test-harness - it does no initialization and no
/// termination. This is largely useful when using the `run_suite!` macro, which takes a sequence
/// of tests to run with harnesses - if there are tests that don't actually need a harness, then a
/// unit parameter can be passed.
impl TestHarness for () {
    type Env = ();
    type Runner = future::Pending<Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async { Ok(((), (), future::pending())) }.boxed()
    }
    fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        future::ok(()).boxed()
    }
}

/// If A and B are test harnesses, then so is the pair (A,B). This is recursive, so (A, (B, C)) is
/// also a valid harness (where C is a harness). Any heterogenous list of Harness types is a valid
/// harness.
impl<A, B> TestHarness for (A, B)
where
    A: TestHarness + Send,
    B: TestHarness + Send,
{
    type Env = (A::Env, B::Env);
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let a = A::init().await?;
            let b = B::init().await?;
            // We want to return the first error immediately, if there is one, otherwise
            // continue until the second future returns either successfully or an error
            let fut = future::try_select(a.2, b.2)
                .then(|res| match res {
                    // One of the futures returned successfully; continue processing the
                    // other
                    Ok(future::Either::Left((_, b))) => b.boxed(),
                    Ok(future::Either::Right((_, a))) => a.boxed(),
                    // We've received an error; return it
                    Err(e) => future::ready(Err(e.factor_first().0)).boxed(),
                })
                .boxed();
            Ok(((a.0, b.0), (a.1, b.1), fut))
        }
        .boxed()
    }
    fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        let (env_a, env_b) = _env;
        async {
            let a = A::terminate(env_a).await;
            let b = B::terminate(env_b).await;
            a.and(b)
        }
        .boxed()
    }
}

pub fn print_test_name(name: &str) {
    print!("  {}...", name);
    std::io::stdout().flush().unwrap();
}

// Prints out the test name and runs the test.
macro_rules! run_test {
    ($name:ident) => {{
        crate::harness::print_test_name(stringify!($name));
        crate::harness::run_test($name)
    }};
}

macro_rules! run_suite {
    ($name:tt, [$($test:ident),+]) => {{
        println!(">>> Running {} tests:", $name);
        {
            use fuchsia_bluetooth::util::CollectExt;
            vec![$( run_test!($test), )*].into_iter().collect_results()?;
        }
        println!();
        Ok(())
    }}
}
