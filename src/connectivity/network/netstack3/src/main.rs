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
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);

    let mut executor = fuchsia_async::LocalExecutor::new()?;

    let eventloop = Netstack::new();
    executor.run_singlethreaded(eventloop.serve())
}
