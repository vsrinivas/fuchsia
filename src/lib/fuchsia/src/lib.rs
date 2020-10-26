// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros for creating Fuchsia components and tests.
//!
//! These macros work on Fuchsia, and also on host with some limitations (that are called out
//! where they exist).

// Features from those macros are expected to be implemented by exactly one function in this
// module. We strive for simple, independent, single purpose functions as building blocks to allow
// dead code elimination to have the very best chance of removing unused code that might be
// otherwise pulled in here.

#![deny(missing_docs)]

pub use fuchsia_macro::{component, test};
use std::future::Future;

#[cfg(not(target_os = "fuchsia"))]
mod host;

//
// LOGGING INITIALIZATION
//

/// Initialize logging for a component.
#[doc(hidden)]
#[cfg(target_os = "fuchsia")]
pub fn init_logging_for_component() {
    fuchsia_syslog::init_with_tags(&[fuchsia_syslog::COMPONENT_NAME_PLACEHOLDER_TAG])
        .expect("initialize logging")
}

/// Initialize logging for a test.
#[doc(hidden)]
#[cfg(target_os = "fuchsia")]
pub fn init_logging_for_test(test_name: &str) {
    fuchsia_syslog::init_with_tags(&[fuchsia_syslog::COMPONENT_NAME_PLACEHOLDER_TAG, test_name])
        .expect("initialize logging")
}

/// Initialize logging
#[doc(hidden)]
#[cfg(not(target_os = "fuchsia"))]
pub fn init_logging_for_component() {
    crate::host::logger::init()
}

/// Initialize logging
#[doc(hidden)]
#[cfg(not(target_os = "fuchsia"))]
pub fn init_logging_for_test(_name: &'static str) {
    crate::host::logger::init()
}

//
// MAIN FUNCTION WRAPPERS
//

/// Run a non-async main function.
#[doc(hidden)]
pub fn main_not_async<F, R>(f: F) -> R
where
    F: FnOnce() -> R,
{
    f()
}

/// Run an async main function with a single threaded executor.
#[doc(hidden)]
pub fn main_singlethreaded<F, Fut, R>(f: F) -> R
where
    F: FnOnce() -> Fut,
    Fut: Future<Output = R> + 'static,
{
    fuchsia_async::Executor::new().expect("Failed to create executor").run_singlethreaded(f())
}

/// Run an async main function with a multi threaded executor (containing `num_threads`).
#[doc(hidden)]
pub fn main_multithreaded<F, Fut, R>(f: F, num_threads: usize) -> R
where
    F: FnOnce() -> Fut,
    Fut: Future<Output = R> + Send + 'static,
    R: Send + 'static,
{
    fuchsia_async::Executor::new().expect("Failed to create executor").run(f(), num_threads)
}

//
// TEST FUNCTION WRAPPERS
//

/// Run a non-async test function.
#[doc(hidden)]
pub fn test_not_async<F, R>(f: F) -> R
where
    F: FnOnce(usize) -> R,
{
    f(0)
}

/// Run an async test function with a single threaded executor.
#[doc(hidden)]
pub fn test_singlethreaded<F, Fut, R>(f: F) -> R
where
    F: Fn(usize) -> Fut + Send + Sync + 'static,
    Fut: Future<Output = R> + 'static,
    R: fuchsia_async::test_support::TestResult,
{
    fuchsia_async::test_support::run_singlethreaded_test(f)
}

/// Run an async test function with a multi threaded executor (containing `num_threads`).
#[doc(hidden)]
pub fn test_multithreaded<F, Fut, R>(f: F, num_threads: usize) -> R
where
    F: Fn(usize) -> Fut + Send + 'static,
    Fut: Future<Output = R> + Send + 'static,
    R: fuchsia_async::test_support::MultithreadedTestResult,
{
    fuchsia_async::test_support::run_test(f, num_threads)
}

/// Run an async test function until it stalls.
#[doc(hidden)]
#[cfg(target_os = "fuchsia")]
pub fn test_until_stalled<F, Fut, R>(f: F) -> R
where
    F: 'static + Fn(usize) -> Fut,
    Fut: 'static + Future<Output = R>,
    R: fuchsia_async::test_support::TestResult,
{
    fuchsia_async::test_support::run_until_stalled_test(
        &mut fuchsia_async::Executor::new().expect("Failed to create executor"),
        f,
    )
}

//
// FUNCTION ARGUMENT ADAPTERS
//

/// Take a main function `f` that takes an argument and return a function that takes none but calls
/// `f` with the arguments parsed via argh.
#[doc(hidden)]
pub fn adapt_to_parse_arguments<A, R>(f: impl FnOnce(A) -> R) -> impl FnOnce() -> R
where
    A: argh::TopLevelCommand,
{
    move || f(argh::from_env())
}

/// Take a test function `f` that takes no parameters and return a function that takes the run
/// number as required by our test runners.
#[doc(hidden)]
pub fn adapt_to_take_test_run_number<R>(f: impl Fn() -> R) -> impl Fn(usize) -> R {
    move |_| f()
}
