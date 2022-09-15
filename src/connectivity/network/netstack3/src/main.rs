// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.
#![deny(missing_docs)]
#![deny(unreachable_patterns)]
#![recursion_limit = "256"]

#[cfg(feature = "instrumented")]
extern crate netstack3_core_instrumented as netstack3_core;

mod bindings;

use bindings::NetstackSeed;

#[fuchsia::main(logging_minimum_severity = "debug")]
fn main() -> Result<(), anyhow::Error> {
    let mut executor = fuchsia_async::LocalExecutor::new()?;

    let seed = NetstackSeed::default();
    executor.run_singlethreaded(seed.serve())
}
