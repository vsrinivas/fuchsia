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

#[macro_use]
extern crate log;

mod eventloop;
mod fidl_worker;
mod oir_worker;
mod overnet_worker;

use {
    anyhow::Context as _, argh::FromArgs, fuchsia_async as fasync, fuchsia_syslog as fsyslog,
    futures::FutureExt as _,
};

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
    #[argh(option, long = "severity")]
    severity: Option<i32>,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let Opt { dev_path: _, severity } = argh::from_env();

    let () = fsyslog::init_with_tags(&["network-manager"]).context("initializing logger")?;
    let () = fsyslog::set_severity(severity.unwrap_or(fuchsia_syslog::levels::INFO));

    let eventloop = eventloop::EventLoop::new().context("creating event loop")?;

    info!("starting");
    eventloop
        .run()
        .inspect(|result| {
            warn!("exiting: {:?}", result);
        })
        .await
}
