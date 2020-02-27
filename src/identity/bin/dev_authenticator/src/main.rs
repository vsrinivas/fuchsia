// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Dev authenticator is a test-only authenticator that produces fake authentication
//! events to be consumed by identity components during tests.

mod storage_unlock;

use crate::storage_unlock::{Mode, StorageUnlockMechanism};
use anyhow::{anyhow, Error};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::StreamExt;
use log::{error, info};
use std::convert::TryFrom;

/// This required command line option (prefixed with `--`) determines the mode of operation.
const MODE_OPTION: &str = "mode";
/// These are the valid arguments for the mode command line option.
const MODE_ARG_ALWAYS_SUCCEED: &str = "ALWAYS_SUCCEED";
const MODE_ARG_ALWAYS_FAIL_AUTHENTICATION: &str = "ALWAYS_FAIL_AUTHENTICATION";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut opts = getopts::Options::new();
    opts.reqopt("", MODE_OPTION, "set the mode of operation", "MODE");
    let args: Vec<String> = std::env::args().collect();
    let options = opts.parse(args)?;
    let mode_arg_str = options.opt_str(MODE_OPTION).expect("Internal getopts error");
    let mode = Mode::try_from(mode_arg_str.as_ref())?;

    fuchsia_syslog::init_with_tags(&["auth"]).expect("Failed to initialize logger");
    info!("Starting dev authenticator");

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(async move {
            let mechanism = StorageUnlockMechanism::new(mode);
            mechanism
                .handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|e| error!("Error handling storage unlock stream: {:?}", e));
        });
    });

    fs.take_and_serve_directory_handle().expect("Failed to serve directory");

    fs.collect::<()>().await;
    Ok(())
}

impl TryFrom<&str> for Mode {
    type Error = Error;
    fn try_from(s: &str) -> Result<Mode, Self::Error> {
        match s {
            MODE_ARG_ALWAYS_SUCCEED => Ok(Mode::AlwaysSucceed),
            MODE_ARG_ALWAYS_FAIL_AUTHENTICATION => Ok(Mode::AlwaysFailAuthentication),
            s => Err(anyhow!("Unrecognized mode: {}", s)),
        }
    }
}
