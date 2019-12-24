// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The network manager allows clients to manage router device properties.

#![deny(missing_docs)]
#![deny(unreachable_patterns)]

extern crate fuchsia_syslog as syslog;
#[macro_use]
extern crate log;
use argh::FromArgs;

mod event;
mod event_worker;
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
