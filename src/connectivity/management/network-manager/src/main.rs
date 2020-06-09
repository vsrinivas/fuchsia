// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The network manager allows clients to manage router device properties.

// Used because we use `futures::select!`.
//
// From https://docs.rs/futures/0.3.1/futures/macro.select.html:
//   Note that select! relies on proc-macro-hack, and may require to set the compiler's
//   recursion limit very high, e.g. #![recursion_limit="1024"].
#![recursion_limit = "256"]
#![deny(missing_docs)]
#![deny(unreachable_patterns)]

extern crate fuchsia_syslog as syslog;
#[macro_use]
extern crate log;
use argh::FromArgs;

mod eventloop;
mod fidl_worker;
mod oir_worker;
mod overnet_worker;

use crate::eventloop::EventLoop;

#[derive(FromArgs, Debug)]
/// Options for network_manager
pub(crate) struct Opt {
    /// device directory path
    #[argh(
        option,
        long = "devicepath",
        default = "\"/dev\"\
                   .to_string()"
    )]
    dev_path: String,
    /// severity level to set logger
    #[argh(option, long = "severity", default = "-2")]
    severity: i32,
}

fn main() -> Result<(), anyhow::Error> {
    let options: Opt = argh::from_env();

    syslog::init().expect("failed to initialize logger");
    fuchsia_syslog::set_severity(options.severity);

    info!("Starting Network Manager!");
    let mut executor = fuchsia_async::Executor::new()?;

    let eventloop = EventLoop::new()?;
    let r = executor.run_singlethreaded(eventloop.run());
    warn!("Network Manager ended: {:?}", r);
    r
}
