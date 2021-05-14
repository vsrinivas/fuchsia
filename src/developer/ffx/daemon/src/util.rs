// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Attempts to run an operation that has a Result<T> inside of a
/// loop which intends not to exit/crash when errors are encountered,
/// instead logging a warning and continuing the loop.
///
/// The formatting for the log is just a straight string conversion,
/// so make sure to attach a context in the event of an error using
/// something like `anyhow::Context`.
///
/// If a context cannot be given, an optional context argument can be supplied.
///
/// # Example:
///
/// ```rust
/// fn main() {
///     loop {
///         ok_or_continue!(
///             function_that_can_fail(),
///             "function that could fail failed",
///         );
///         computation_that_matters();
///     }
/// }
/// ```
#[macro_export]
macro_rules! ok_or_continue {
    ($op:expr $(,)?) => {
        match $op {
            Ok(t) => t,
            Err(e) => {
                log::warn!("{}", e);
                continue;
            }
        }
    };
    ($op:expr, $ctx:expr $(,)?) => {
        match $op {
            Ok(t) => t,
            Err(e) => {
                log::warn!("{}: {}", $ctx, e);
                continue;
            }
        }
    };
}

/// Attempts to run an operation that has a Result<T> inside a function
/// that isn't intended to return an error, instead returning after failure with
/// a warning log.
///
/// # Example:
///
/// ```rust
/// fn fun_function() [
///   ok_or_return!(some_operation());
///   some_other_operation_that_depends_on_above_succeeding();
/// }
/// ```
#[macro_export]
macro_rules! ok_or_return {
    ($op:expr $(,)?) => {
        match $op {
            Ok(t) => t,
            Err(e) => {
                log::warn!("{}", e);
                return;
            }
        }
    };
}
