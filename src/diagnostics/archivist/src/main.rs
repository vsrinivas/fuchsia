// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Archivist collects and stores diagnostic data from components.

#![warn(clippy::all)]
#![warn(missing_docs)]

use {
    anyhow::{format_err, Context, Error},
    archivist_config::Config,
    archivist_lib::{archivist::Archivist, constants, diagnostics},
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
    /// Drain klog.
    WithKlog,
    /// Don't connect to the LogConnector.
    NoLogConnector,
    /// Archivist will consume and attribute its own logs.
    ConsumeOwnLogs,
}

impl FromStr for ArchivistOptionV1 {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "default" => Ok(ArchivistOptionV1::Default),
            "with-klog" => Ok(ArchivistOptionV1::WithKlog),
            "no-log-connector" => Ok(ArchivistOptionV1::NoLogConnector),
            "consume-own-logs" => Ok(ArchivistOptionV1::ConsumeOwnLogs),
            s => Err(format_err!("Invalid V1 flavor {}", s)),
        }
    }
}

fn load_v1_config(options: Vec<ArchivistOptionV1>) -> Config {
    let mut config = Config {
        consume_own_logs: false,
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
            ArchivistOptionV1::WithKlog => {
                config.enable_klog = true;
            }
            ArchivistOptionV1::NoLogConnector => {
                config.enable_log_connector = false;
            }
            ArchivistOptionV1::ConsumeOwnLogs => {
                config.consume_own_logs = true;
            }
        }
    }
    config
}

fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    let mut config = if args.v1.is_empty() { Config::from_args() } else { load_v1_config(args.v1) };
    let log_server = init_diagnostics(&config).context("initializing diagnostics")?;
    config = config.record_to_inspect(component::inspector().root());

    let num_threads = config.num_threads;
    debug!("Running executor with {} threads.", num_threads);
    SendExecutor::new(num_threads as usize)?
        .run(async_main(config, log_server))
        .context("async main")?;
    debug!("Exiting.");
    Ok(())
}

fn init_diagnostics(config: &Config) -> Result<Option<zx::Socket>, Error> {
    let mut log_server = None;
    if config.consume_own_logs {
        assert!(
            !config.log_to_debuglog,
            "cannot specify both consume-own-logs and log-to-debuglog"
        );
        let (log_client, server) = zx::Socket::create(zx::SocketOpts::DATAGRAM)?;
        log_server = Some(server);
        fuchsia_syslog::init_with_socket_and_name(log_client, "archivist")?;
    } else if config.log_to_debuglog {
        assert!(
            !config.consume_own_logs,
            "cannot specify both consume-own-logs and log-to-debuglog"
        );
        LocalExecutor::new()?.run_singlethreaded(stdout_to_debuglog::init()).unwrap();

        log::set_logger(&STDOUT_LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Info);
    } else {
        fuchsia_syslog::init_with_tags(&["embedded"])?;
    }

    if config.consume_own_logs || config.log_to_debuglog {
        info!("Logging started.");
    }

    diagnostics::init();
    Ok(log_server)
}

async fn async_main(config: Config, log_server: Option<zx::Socket>) -> Result<(), Error> {
    let mut archivist = Archivist::new(&config)?;
    debug!("Archivist initialized from configuration.");

    archivist.install_log_services().await;

    if config.enable_event_source {
        archivist.install_event_source().await;
    }

    if config.enable_component_event_provider {
        archivist.install_component_event_provider();
    }

    if let Some(socket) = log_server {
        archivist.consume_own_logs(socket);
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

    archivist.run(zx::Channel::from(startup_handle)).await?;

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
