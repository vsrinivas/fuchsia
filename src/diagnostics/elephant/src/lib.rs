// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `elephant` persists Inspect VMOs and serves them at the next boot.

mod config;
mod constants;
mod file_handler;
mod inspect_fetcher;
mod inspect_server;
mod persist_server;

use {
    crate::config::Config,
    anyhow::{bail, format_err, Error},
    argh::FromArgs,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_inspect::component,
    fuchsia_zircon::DurationNum,
    futures::StreamExt,
    log::*,
    persist_server::PersistServer,
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
    inspector.root().record_child(PERSIST_NODE_NAME, |node| {
        inspect_server::serve_persisted_data(node).unwrap_or_else(|e| {
            error!(
                "Serving persisted data experienced critical failure. No data available: {:?}",
                e
            )
        })
    });

    // Add a persistence fidl service for each service defined in the config files.
    spawn_persist_services(config, &mut fs)?;

    Ok(fs.collect::<()>().await)
}

// Takes a config and adds all the persist services defined in those configs to the servicefs of
// the component.
fn spawn_persist_services(
    config: Config,
    fs: &mut ServiceFs<ServiceObj<'static, ()>>,
) -> Result<(), Error> {
    let mut started_persist_services = 0;
    // We want fault tolerance if only a subset of the service configs fail to initialize.
    for (service_name, tags) in config.into_iter() {
        match PersistServer::create(service_name.clone(), tags) {
            Ok(persist_service) => {
                if let Err(e) = persist_service.launch_server(fs) {
                    warn!(
                        "Encountered error launching persist service for service name: {}, Error: {:?}",
                        service_name, e
                    );
                } else {
                    started_persist_services += 1;
                }
            }
            Err(e) => warn!(
                "Encountered error instantiating persist service for service name: {}, Error: {:?}",
                service_name, e
            ),
        }
    }

    if started_persist_services == 0 {
        Err(format_err!("Failed to start any persist services"))
    } else {
        Ok(())
    }
}
