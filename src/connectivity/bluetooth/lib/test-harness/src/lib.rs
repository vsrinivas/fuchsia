// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fuchsia_async as fasync,
    futures::future::BoxFuture,
    futures::{future, select, stream::TryStreamExt, Future, FutureExt},
    log::{error, info},
    pin_utils::pin_mut,
};

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

    /// Initialize a TestHarness, creating the harness itself, any hidden environment, and a runner
    /// task to execute background behavior
    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>>;

    /// Terminate the TestHarness. This should clear up any and all resources that were created by
    /// init()
    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>>;
}

/// We can run any test which is an async function from some harness `H` to a result
async fn run_with_harness<H, F, Fut>(test_func: F) -> Result<(), Error>
where
    H: TestHarness,
    F: FnOnce(H) -> Fut + Send + 'static,
    Fut: Future<Output = Result<(), Error>> + Send + 'static,
{
    let (harness, env, runner) = H::init().await.context("Error initializing harness")?;

    let run_test = test_func(harness);
    pin_mut!(run_test);
    pin_mut!(runner);

    let result = select! {
        test_result = run_test.fuse() => test_result,
        runner_result = runner.fuse() => runner_result.context("Error running harness background task"),
    };

    let term_result = H::terminate(env).await.context("Error terminating harness");
    // Return test failure if it exists, else return terminate failure, else return the
    // successful test result
    result.and_then(|ok| term_result.map(|_| ok))
}

/// Sets up the test environment and the given test case. Each integration test case is an
/// asynchronous function from some harness `H` to the result of the test run.
pub fn run_test<H, Fut>(
    test: impl FnOnce(H) -> Fut + Send + 'static,
    name: &str,
) -> Result<(), Error>
where
    Fut: Future<Output = Result<(), Error>> + Send + 'static,
    H: TestHarness,
{
    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    info!("[ RUN      ] {}...", name);
    let result = executor.run_singlethreaded(run_with_harness(test));
    if let Err(err) = &result {
        error!("[   \x1b[31mFAILED\x1b[0m ] {}: Error running test: {:?}", name, err);
    } else {
        info!("[   \x1b[32mPASSED\x1b[0m ] {}", name);
    }
    result.context(format!("Error running test '{}'", name))
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

// Prints out the test name and runs the test.
#[macro_export]
macro_rules! run_test {
    ($name:ident) => {{
        test_harness::run_test($name, stringify!($name))
    }};
}

#[macro_export]
macro_rules! run_suite {
    ($name:tt, [$($test:ident),+]) => {{
        log::info!(">>> Running {} tests:", $name);
        {
            use fuchsia_bluetooth::util::CollectExt;
            let res = vec![$( test_harness::run_test($test, stringify!($test)), )*]
                .into_iter()
                .collect_results();
            anyhow::Context::context(res, format!("Error running suite {}", $name))?;
        }
        Ok(())
    }}
}

/// We can implement TestHarness for any tuples of types that are TestHarnesses - this macro can
/// generate boilerplate implementations for tuples of arities by combining the init() and
/// terminate() functions of the tuple components.
///
/// ----
///
/// To elaborate: This implementation is implied by the nature of _applicative functors_, which are
/// a class of types that support 'tupling' of their type members. In particular, Functions, Tuples,
/// Futures and Results are all applicative, and compositions of applicatives are _also_ applicative
/// by definition. So a `Function that returns a Result of a Tuple` is applicative (e.g. `init()`),
/// as is a `Function that returns a Future of a Result` (such as `terminate()`).
///
/// A TestHarness impl itself is really equivalent to a tuple of two functions - init() and
/// terminate(). Again, by the composition of applicative functors, this itself is applicative, and
/// therefore a tuple of TestHarness impls can be turned into an TestHarness impl of a tuple. Rustc
/// isn't able to derive this automatically for us so we write this macro here to do the heavy
/// lifting.
///
/// (Further caveat: The tupling of terminate() is technically slightly more complex as it's a
/// function indexed by a type parameter (Self::Env), but it still shakes out much the same)
macro_rules! generate_tuples {
    ($(
        ($($Harness:ident),*),
    )*) => ($(
            // The impl below re-uses type names as identifiers, so we allow non_snake_case to
            // suppress warnings over using 'A' instead of 'a', etc.
            #[allow(non_snake_case)]
            impl<$($Harness: TestHarness + Send),*> TestHarness for ($($Harness),*) {
                type Env = ($($Harness::Env),*);
                type Runner = BoxFuture<'static, Result<(), Error>>;

                fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
                    async {
                        $(
                            let $Harness = $Harness::init().await?;
                        )*

                        // Create a stream of the result of each future
                        let runners = futures::stream::select_all(
                            vec![
                                $( $Harness.2.boxed().into_stream()),*
                            ]
                        );

                        // Use try_collect to return first error or continue to completion
                        let runner = runners.try_collect::<()>().boxed();

                        let harness = ($($Harness.0),*);
                        let env = ($($Harness.1),*);
                        Ok((harness, env, runner))
                    }
                    .boxed()
                }

                fn terminate(environment: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
                    let ($($Harness),*) = environment;
                    async {
                        $(
                            let $Harness = $Harness::terminate($Harness).await;
                        )*
                        let done = Ok(());
                        $(
                            let done = done.and($Harness);
                        )*
                        done
                    }.boxed()
                }
            }
    )*)
}

// Generate TestHarness impls for tuples up to arity-6
generate_tuples! {
  (A, B),
  (A, B, C),
  (A, B, C, D),
  (A, B, C, D, E),
  (A, B, C, D, E, F),
}
