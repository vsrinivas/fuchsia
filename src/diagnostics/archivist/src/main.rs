// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Archivist collects and stores diagnostic data from components.

#![warn(missing_docs)]

use {
    anyhow::{Context, Error},
    archivist_lib::{archivist, configs, diagnostics, logs},
    argh::FromArgs,
    fidl_fuchsia_diagnostics_internal::{
        DetectControllerMarker, LogStatsControllerMarker, SamplerControllerMarker,
    },
    fidl_fuchsia_sys2::EventSourceMarker,
    fidl_fuchsia_sys_internal::{ComponentEventProviderMarker, LogConnectorMarker},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::MissingStartupHandle,
    fuchsia_syslog, fuchsia_zircon as zx,
    std::path::PathBuf,
    tracing::{debug, error, info, warn},
};

/// Monitor, collect, and store diagnostics from components.
#[derive(Debug, Default, FromArgs)]
pub struct Args {
    /// disables proxying kernel logger
    #[argh(switch)]
    disable_klog: bool,

    /// disables log connector so that indivisual instances of
    /// observer don't compete for log connector listener.
    #[argh(switch)]
    disable_log_connector: bool,

    /// whether to connecto to event source or not.
    #[argh(switch)]
    disable_event_source: bool,

    /// initializes syslog library with a log socket to itself
    #[argh(switch)]
    consume_own_logs: bool,

    /// send all logs to environment's LogSink
    #[argh(switch)]
    forward_logs: bool,

    /// serve fuchsia.diagnostics.test.Controller
    #[argh(switch)]
    install_controller: bool,

    /// retrieve a fuchsia.process.Lifecycle handle from the runtime and listen to shutdown events
    #[argh(switch)]
    listen_to_lifecycle: bool,

    /// connect to fuchsia.diagnostics.internal.DetectController
    #[argh(switch)]
    connect_to_detect: bool,

    /// connect to fuchsia.diagnostics.internal.LogStatsController
    #[argh(switch)]
    connect_to_log_stats: bool,

    /// connect to fuchsia.diagnostics.internal.SamplerController
    #[argh(switch)]
    connect_to_sampler: bool,

    /// path to a JSON configuration file
    #[argh(option)]
    config_path: PathBuf,
}

fn main() -> Result<(), Error> {
    let opt: Args = argh::from_env();

    let log_name = "archivist";
    let mut log_server = None;
    if opt.consume_own_logs {
        let (log_client, server) = zx::Socket::create(zx::SocketOpts::DATAGRAM)?;
        log_server = Some(server);
        fuchsia_syslog::init_with_socket_and_name(log_client, log_name)?;
        info!("Logging started.");
        logs::redact::emit_canary();
    } else {
        fuchsia_syslog::init_with_tags(&["embedded"])?;
    }

    let mut executor = fasync::Executor::new()?;

    let legacy_event_provider = connect_to_service::<ComponentEventProviderMarker>()
        .context("failed to connect to event provider")?;

    diagnostics::init();

    let archivist_configuration: configs::Config = match configs::parse_config(&opt.config_path) {
        Ok(config) => config,
        Err(parsing_error) => panic!("Parsing configuration failed: {}", parsing_error),
    };
    debug!("Configuration parsed.");

    let num_threads = archivist_configuration.num_threads;

    let mut archivist = archivist::ArchivistBuilder::new(archivist_configuration)?;
    debug!("Archivist initialized from configuration.");

    archivist.install_logger_services().add_event_source("v1", Box::new(legacy_event_provider));

    if !opt.disable_event_source {
        let event_source = connect_to_service::<EventSourceMarker>()
            .context("failed to connect to event source")?;
        archivist.add_event_source("v2", Box::new(event_source));
    }
    if let Some(log_server) = log_server {
        fasync::Task::spawn(
            archivist.data_repo().clone().drain_internal_log_sink(log_server, log_name),
        )
        .detach();
    }

    if opt.forward_logs {
        archivist.data_repo().clone().forward_logs();
    }

    assert!(
        !(opt.install_controller && opt.listen_to_lifecycle),
        "only one shutdown mechanism can be specified."
    );

    if opt.install_controller {
        archivist.install_controller_service();
    }

    if opt.listen_to_lifecycle {
        archivist.install_lifecycle_listener();
    }

    if !opt.disable_log_connector {
        let connector = connect_to_service::<LogConnectorMarker>()?;
        let sender = archivist.log_sender().clone();
        fasync::Task::spawn(archivist.data_repo().clone().handle_log_connector(connector, sender))
            .detach();
    }

    if !opt.disable_klog {
        let debuglog = executor
            .run_singlethreaded(logs::KernelDebugLog::new())
            .context("Failed to read kernel logs")?;
        fasync::Task::spawn(archivist.data_repo().clone().drain_debuglog(debuglog)).detach();
    }

    let _detect;
    if opt.connect_to_detect {
        info!("Starting detect service.");
        _detect = connect_to_service::<DetectControllerMarker>();
        if let Err(e) = &_detect {
            error!("Couldn't connect to detect: {}", e);
        }
    }

    let _stats;
    if opt.connect_to_log_stats {
        info!("Starting log stats service.");
        _stats = connect_to_service::<LogStatsControllerMarker>();
        if let Err(e) = &_stats {
            error!("Couldn't connect to log stats: {}", e);
        }
    }

    let _sampler;
    if opt.connect_to_sampler {
        info!("Starting sampler service.");
        _sampler = connect_to_service::<SamplerControllerMarker>();

        if let Err(e) = &_sampler {
            error!("Couldn't connect to sampler: {}", e);
        }
    }

    let startup_handle =
        fuchsia_runtime::take_startup_handle(fuchsia_runtime::HandleType::DirectoryRequest.into())
            .ok_or(MissingStartupHandle)?;

    debug!("Running executor with {} threads.", num_threads);
    executor.run(archivist.run(zx::Channel::from(startup_handle)), num_threads)?;

    debug!("Exiting.");
    Ok(())
}
