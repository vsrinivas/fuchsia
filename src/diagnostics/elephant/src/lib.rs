// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `elephant` persists Inspect VMOs and serves them at the next boot.

mod config;
mod file_handler;
mod inspect_fetcher;
mod inspect_server;
mod persist_server;

use {
    anyhow::{bail, Error},
    argh::FromArgs,
    fuchsia_async as fasync, //fuchsia_zircon as zx,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::component,
    //glob::glob,
    //injectable_time::MonotonicTime,
    fuchsia_zircon::DurationNum,
    futures::StreamExt,
    log::*,
    persist_server::PersistServer,
    //serde_derive::Deserialize,
    //std::collections::HashMap,
};

/// The name of the subcommand and the logs-tag.
pub const PROGRAM_NAME: &str = "elephant";
pub const PERSIST_NODE_NAME: &str = "persist";

/// Command line args
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "elephant")]
pub struct CommandLine {}

// on_error logs any errors from `value` and then returns a Result.
// value must return a Result; error_message must contain one {} to put the error in.
macro_rules! on_error {
    ($value:expr, $error_message:expr) => {
        $value.or_else(|e| {
            let message = format!($error_message, e);
            warn!("{}", message);
            bail!("{}", message)
        })
    };
}

pub async fn main() -> Result<(), Error> {
    let config = on_error!(config::load_configuration_files(), "Error loading configs: {}")?;

    // Before doing anything else, wait 2 minutes for the /cache directory to stabilize.
    fasync::Timer::new(fasync::Time::after(2_i64.minutes())).await;

    file_handler::shuffle_at_boot();

    info!("About to start servers");
    let mut fs = ServiceFs::new();

    // Start serving previous boot data
    let inspector = component::inspector();
    on_error!(inspector.serve(&mut fs), "Error initializing Inspect: {}")?;
    inspector
        .root()
        .record_child(PERSIST_NODE_NAME, |node| inspect_server::serve_persisted_data(node));

    // Start listening for persist requests
    let persist_object = PersistServer::create(config)?;
    let _persist_server = persist_object.launch_server(&mut fs);
    Ok(fs.collect::<()>().await)
}
