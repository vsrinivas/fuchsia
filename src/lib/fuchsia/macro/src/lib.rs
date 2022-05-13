// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro::TokenStream;
use transformer::Transformer;

/// Define a fuchsia main function.
///
/// This attribute should be applied to the process `main` function.
/// It will take care of setting up various Fuchsia crates for the component.
/// If an async function is provided, a fuchsia-async Executor will be used to execute it.
///
/// Arguments:
///  - `threads` - integer worker thread count for the component. Must be >0. Default 1.
///  - `logging` - boolean toggle for whether to initialize logging (or not). Default true.
///  - `logging_tags` - optional list of string to be used as tags for logs. Default: None.
///  - `logging_minimum_severity` - optional minimum severity to be set for logs. Default: None,
///                                 the logging library will choose it (typically `info`).
///
/// The main function can return either () or a Result<(), E> where E is an error type.
#[proc_macro_attribute]
pub fn main(args: TokenStream, input: TokenStream) -> TokenStream {
    Transformer::parse_main(args.into(), input.into()).unwrap().finish().into()
}

/// Define a fuchsia test.
///
/// This attribute should be applied to a function in a cfg(test) module.
/// It will take care of setting up various Fuchsia crates for the test.
/// If an async function is provided, a fuchsia-async Executor will be used to execute it.
///
/// Arguments:
///  - `threads`       - integer worker thread count for the test. Must be >0. Default 1.
///  - `logging`       - boolean toggle for whether to initialize logging (or not). Default true.
///                      This currently does nothing on host. On Fuchsia fuchsia-syslog is used.
///  - `logging_tags` - optional list of string to be used as tags for logs. Default: None.
///  - `logging_minimum_severity` - optional minimum severity to be set for logs. Default: None,
///                                 the logging library will choose it (typically `info`).
///  - `allow_stalls`  - boolean toggle for whether the async test is allowed to stall during
///                      execution (if true), or whether the function must complete without pausing
///                      (if false).
///                      `.await` is not a stall if something preceding the await will guarantee
///                      that it finishes within one loop of the Executor. Defaults to true.
///                      This argument is not currently available for host tests.
///  - `add_test_attr` - boolean toggle for whether to apply the `#[test]` attribute to the
///                      function. When daisy-chaining with other proc macros, it may be desirable
///                      to omit this attribute. Default true.
///
/// The test function can return either () or a Result<(), E> where E is an error type.
/// The test function can either take no arguments, or a single usize argument. If it takes an
/// argument, that value will be the current iteration count of running this test repeatedly.
#[proc_macro_attribute]
pub fn test(args: TokenStream, input: TokenStream) -> TokenStream {
    Transformer::parse_test(args.into(), input.into()).unwrap().finish().into()
}
