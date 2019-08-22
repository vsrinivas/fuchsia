// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fuchsia_async as fasync,
    futures::Future,
    std::io::Write,
};

#[macro_use]
pub mod expect;

// Test harnesses
pub mod control;
pub mod host_driver;
pub mod low_energy_central;
pub mod low_energy_peripheral;
pub mod profile;

/// Trait for a Harness that we can run tests with
pub trait TestHarness: Sized {
    fn run_with_harness<F, Fut>(test_func: F) -> Result<(), Error>
    where
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), Error>>;
}

pub fn run_test<F, H, Fut>(test_func: F) -> Result<(), Error>
where
    F: FnOnce(H) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
    H: TestHarness,
{
    let result = H::run_with_harness(test_func);
    if let Err(err) = &result {
        println!("\x1b[31mFAILED\x1b[0m");
        println!("Error running test: {}", err);
    } else {
        println!("\x1b[32mPASSED\x1b[0m");
    }
    result
}

/// Trait for a Harness that we can run tests with
impl TestHarness for () {
    fn run_with_harness<F, Fut>(test_func: F) -> Result<(), Error>
    where
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().context("error creating event loop")?;
        executor.run_singlethreaded(test_func(()))
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

/// Collect a Vector of Results into a Result of a Vector. If all results are
/// `Ok`, then return `Ok` of the results. Otherwise return the first `Err`.
pub fn collect_results<T, E>(results: Vec<Result<T, E>>) -> Result<Vec<T>, E> {
    results.into_iter().collect()
}

macro_rules! run_suite {
    ($name:tt, [$($test:ident),+]) => {{
        println!(">>> Running {} tests:", $name);
        crate::harness::collect_results(vec![$( run_test!($test), )*])?;
        println!();
        Ok(())
    }}
}
