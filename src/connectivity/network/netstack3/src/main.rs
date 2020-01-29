// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A networking stack.
#![deny(missing_docs)]
#![deny(unreachable_patterns)]
#![recursion_limit = "256"]

mod bindings;

use bindings::Netstack;

fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init()?;
    // Severity is set to debug during development.
    fuchsia_syslog::set_severity(-1);

    let mut executor = fuchsia_async::Executor::new()?;

    let eventloop = Netstack::new();
    executor.run_singlethreaded(eventloop.serve())
}
