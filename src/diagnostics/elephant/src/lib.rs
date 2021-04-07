// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `elephant` persists Inspect VMOs and serves them at the next boot.

mod config;
mod constants;
mod file_handler;
mod inspect_server;
mod persist_server;

use {
    crate::config::Config,
    anyhow::{bail, Error},
    argh::FromArgs,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_zircon::Duration,
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
pub struct CommandLine {
    /// how long to wait before we start offering persistence
    /// functionality.
    #[argh(option)]
    startup_delay_seconds: i64,
}

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

pub async fn main(args: CommandLine) -> Result<(), Error> {
    info!("Starting Elephant service");
    let config = on_error!(config::load_configuration_files(), "Error loading configs: {}")?;

    let startup_delay_duration = Duration::from_seconds(args.startup_delay_seconds);

    // Before doing anything else, wait the arg-provided seconds for the /cache directory to stabilize.
    info!("Elephant delaying startup for {} seconds...", args.startup_delay_seconds);
    fasync::Timer::new(fasync::Time::after(startup_delay_duration)).await;
    info!("...done delay, continuing startup");

    info!("Rotating directories");
    file_handler::shuffle_at_boot();

    let mut fs = ServiceFs::new();

    // Add a persistence fidl service for each service defined in the config files.
    spawn_persist_services(config, &mut fs);
    fs.take_and_serve_directory_handle()?;

    // Start serving previous boot data
    info!("Publishing previous boot data");
    let inspector = component::inspector();
    component::health().set_starting_up();
    on_error!(inspector.serve(&mut fs), "Error initializing Inspect: {}")?;
    inspector.root().record_child(PERSIST_NODE_NAME, |node| {
        inspect_server::serve_persisted_data(node).unwrap_or_else(|e| {
            error!(
                "Serving persisted data experienced critical failure. No data available: {:?}",
                e
            )
        })
    });

    component::health().set_ok();
    info!("Elephant ready");
    Ok(fs.collect::<()>().await)
}

// Takes a config and adds all the persist services defined in those configs to the servicefs of
// the component.
fn spawn_persist_services(config: Config, fs: &mut ServiceFs<ServiceObj<'static, ()>>) {
    let mut started_persist_services = 0;
    // We want fault tolerance if only a subset of the service configs fail to initialize.
    for (service_name, tags) in config.into_iter() {
        info!("Launching persist service for {}, tags {:?}", service_name, tags);
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

    info!("Started {} persist services", started_persist_services);
}
