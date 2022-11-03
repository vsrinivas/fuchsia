// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Archivist collects and stores diagnostic data from components.

#![warn(clippy::all)]
#![warn(missing_docs)]

use {
    anyhow::{format_err, Context, Error},
    archivist_config::Config,
    archivist_lib::{archivist::Archivist, constants, diagnostics, events::router::RouterOptions},
    argh::FromArgs,
    fdio::service_connect,
    fuchsia_async::{LocalExecutor, SendExecutor},
    fuchsia_component::server::MissingStartupHandle,
    fuchsia_inspect::component,
    fuchsia_zircon as zx,
    std::str::FromStr,
    tracing::{debug, error, info, warn},
};

/// The archivist.
#[derive(Debug, Default, FromArgs)]
pub struct Args {
    /// must be passed when running the archivist in CFv1.
    #[argh(option)]
    v1: Vec<ArchivistOptionV1>,
}

/// Available options for the V1 archivist.
#[derive(Debug)]
pub enum ArchivistOptionV1 {
    /// Default mode for v1.
    Default,
    /// Don't connect to the LogConnector.
    NoLogConnector,
}

impl FromStr for ArchivistOptionV1 {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "default" => Ok(ArchivistOptionV1::Default),
            "no-log-connector" => Ok(ArchivistOptionV1::NoLogConnector),
            s => Err(format_err!("Invalid V1 flavor {}", s)),
        }
    }
}

fn load_v1_config(options: Vec<ArchivistOptionV1>) -> Config {
    let mut config = Config {
        // All v1 flavors include the event provider
        enable_component_event_provider: true,
        enable_klog: false,
        enable_event_source: false,
        // All v1 flavors use the log connector unless turned off.
        enable_log_connector: true,
        // All v1 flavors include the controller.
        install_controller: true,
        listen_to_lifecycle: false,
        log_to_debuglog: false,
        logs_max_cached_original_bytes: constants::LEGACY_DEFAULT_MAXIMUM_CACHED_LOGS_BYTES as u64,
        num_threads: 1,
        pipelines_path: constants::DEFAULT_PIPELINES_PATH.into(),
        bind_services: vec![],
    };
    for option in options {
        match option {
            ArchivistOptionV1::Default => {}
            ArchivistOptionV1::NoLogConnector => {
                config.enable_log_connector = false;
            }
        }
    }
    config
}

fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    let config = if args.v1.is_empty() {
        Config::take_from_startup_handle()
    } else {
        load_v1_config(args.v1)
    };
    init_diagnostics(&config).context("initializing diagnostics")?;
    component::inspector()
        .root()
        .record_child("config", |config_node| config.record_inspect(config_node));

    let num_threads = config.num_threads;
    debug!("Running executor with {} threads.", num_threads);
    SendExecutor::new(num_threads as usize)?.run(async_main(config)).context("async main")?;
    debug!("Exiting.");
    Ok(())
}

fn init_diagnostics(config: &Config) -> Result<(), Error> {
    if config.log_to_debuglog {
        LocalExecutor::new()?.run_singlethreaded(stdout_to_debuglog::init()).unwrap();

        log::set_logger(&STDOUT_LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Info);
    } else {
        fuchsia_syslog::init_with_tags(&["embedded"])?;
    }

    if config.log_to_debuglog {
        info!("Logging started.");
    }

    diagnostics::init();
    Ok(())
}

async fn async_main(config: Config) -> Result<(), Error> {
    let mut archivist = Archivist::new(&config).await?;
    debug!("Archivist initialized from configuration.");

    archivist.install_log_services().await;

    if config.enable_event_source {
        archivist.install_event_source().await;
    }

    if config.enable_component_event_provider {
        archivist.install_component_event_provider().await;
    }

    assert!(
        !(config.install_controller && config.listen_to_lifecycle),
        "only one shutdown mechanism can be specified."
    );

    if config.install_controller {
        archivist.serve_test_controller_protocol();
    }

    if config.listen_to_lifecycle {
        archivist.serve_lifecycle_protocol();
    }

    if config.enable_log_connector {
        archivist.install_log_connector();
    }

    if config.enable_klog {
        archivist.start_draining_klog().await?;
    }

    let mut services = vec![];

    for name in config.bind_services {
        info!("Connecting to service {}", name);
        let (local, remote) = zx::Channel::create().expect("cannot create channels");
        match service_connect(&format!("/svc/{}", name), remote) {
            Ok(_) => {
                services.push(local);
            }
            Err(e) => {
                error!("Couldn't connect to service {}: {:?}", name, e);
            }
        }
    }

    let startup_handle =
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest.into())
            .ok_or(MissingStartupHandle)?;

    archivist
        .run(
            fidl::endpoints::ServerEnd::new(zx::Channel::from(startup_handle)),
            RouterOptions {
                validate: config.enable_event_source || config.enable_component_event_provider,
            },
        )
        .await?;

    Ok(())
}

static STDOUT_LOGGER: StdoutLogger = StdoutLogger;
struct StdoutLogger;

impl log::Log for StdoutLogger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() <= log::Level::Info
    }
    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            println!("[archivist] {}: {}", record.level(), record.args());
        }
    }
    fn flush(&self) {}
}
