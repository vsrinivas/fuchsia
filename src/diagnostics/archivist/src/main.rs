// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Archivist collects and stores diagnostic data from components.

#![warn(missing_docs)]

use {
    anyhow::{Context, Error},
    archivist_lib::{
        archivist::{Archivist, LogOpts},
        configs, diagnostics,
        events::sources::{ComponentEventProvider, EventSourceConnection, LogConnectorEventSource},
        logs,
    },
    argh::FromArgs,
    fdio::service_connect,
    fidl_fuchsia_sys2::EventSourceMarker,
    fidl_fuchsia_sys_internal::{ComponentEventProviderMarker, LogConnectorMarker},
    fuchsia_async::{LocalExecutor, SendExecutor},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::MissingStartupHandle,
    fuchsia_syslog, fuchsia_zircon as zx,
    std::path::PathBuf,
    tracing::{debug, error, info, warn},
};

/// Monitor, collect, and store diagnostics from components.
// TODO(fxbug.dev/67983) make flags positive rather than negative
#[derive(Debug, Default, FromArgs)]
pub struct Args {
    /// disables proxying kernel logger
    #[argh(switch)]
    disable_klog: bool,

    /// disables log connector so that indivisual instances of
    /// observer don't compete for log connector listener.
    #[argh(switch)]
    disable_log_connector: bool,

    /// whether to connecto to event source or not. This can be set to true when the archivist won't
    /// consume events from the Component Framework v2 to remove log spam.
    #[argh(switch)]
    disable_event_source: bool,

    /// whether to connecto to the component event provider or not. This can be set to true when the
    /// archivist won't consume events from the Component Framework v1 to remove log spam.
    #[argh(switch)]
    disable_component_event_provider: bool,

    // TODO(fxbug.dev/72046) delete when netemul no longer using
    /// initializes syslog library with a log socket to itself
    #[argh(switch)]
    consume_own_logs: bool,

    /// initializes logging to debuglog via fuchsia.boot.WriteOnlyLog
    #[argh(switch)]
    log_to_debuglog: bool,

    /// serve fuchsia.diagnostics.test.Controller
    #[argh(switch)]
    install_controller: bool,

    /// retrieve a fuchsia.process.Lifecycle handle from the runtime and listen to shutdown events
    #[argh(switch)]
    listen_to_lifecycle: bool,

    /// path to a JSON configuration file
    #[argh(option)]
    config_path: PathBuf,

    /// path to additional configuration for services to connect to
    #[argh(option)]
    service_config_path: Option<PathBuf>,
}

fn main() -> Result<(), Error> {
    let opt: Args = argh::from_env();
    let log_server = init_diagnostics(&opt).context("initializing diagnostics")?;

    let config = configs::parse_config(&opt.config_path).context("parsing configuration")?;
    debug!("Configuration parsed.");

    let num_threads = config.num_threads;
    debug!("Running executor with {} threads.", num_threads);
    SendExecutor::new(num_threads)?
        .run(async_main(opt, config, log_server))
        .context("async main")?;
    debug!("Exiting.");
    Ok(())
}

fn init_diagnostics(opt: &Args) -> Result<Option<zx::Socket>, Error> {
    let mut log_server = None;
    if opt.consume_own_logs {
        assert!(!opt.log_to_debuglog, "cannot specify both consume-own-logs and log-to-debuglog");
        let (log_client, server) = zx::Socket::create(zx::SocketOpts::DATAGRAM)?;
        log_server = Some(server);
        fuchsia_syslog::init_with_socket_and_name(log_client, "archivist")?;
    } else if opt.log_to_debuglog {
        assert!(!opt.consume_own_logs, "cannot specify both consume-own-logs and log-to-debuglog");
        LocalExecutor::new()?.run_singlethreaded(stdout_to_debuglog::init()).unwrap();

        log::set_logger(&STDOUT_LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Info);
    } else {
        fuchsia_syslog::init_with_tags(&["embedded"])?;
    }

    if opt.consume_own_logs || opt.log_to_debuglog {
        info!("Logging started.");
        // Always emit the log redaction canary during startup.
        logs::redact::emit_canary();
    }

    diagnostics::init();
    Ok(log_server)
}

async fn async_main(
    opt: Args,
    archivist_configuration: configs::Config,
    log_server: Option<zx::Socket>,
) -> Result<(), Error> {
    let mut archivist = Archivist::new(archivist_configuration)?;
    debug!("Archivist initialized from configuration.");

    archivist.install_log_services(LogOpts { ingest_v2_logs: !opt.disable_event_source }).await;
    if !opt.disable_component_event_provider {
        let legacy_event_provider: ComponentEventProvider =
            connect_to_protocol::<ComponentEventProviderMarker>()
                .context("failed to connect to event provider")?
                .into();
        archivist.add_event_source("v1", Box::new(legacy_event_provider)).await;
    }

    if !opt.disable_event_source {
        let event_source: EventSourceConnection = connect_to_protocol::<EventSourceMarker>()
            .context("failed to connect to event source")?
            .into();
        archivist.add_event_source("v2", Box::new(event_source)).await;
    }

    if let Some(socket) = log_server {
        archivist.consume_own_logs(socket);
    }

    assert!(
        !(opt.install_controller && opt.listen_to_lifecycle),
        "only one shutdown mechanism can be specified."
    );

    if opt.install_controller {
        archivist.serve_test_controller_protocol();
    }

    if opt.listen_to_lifecycle {
        archivist.serve_lifecycle_protocol();
    }

    if !opt.disable_log_connector {
        let connector = connect_to_protocol::<LogConnectorMarker>()?;
        archivist
            .add_event_source("log_connector", Box::new(LogConnectorEventSource::new(connector)))
            .await;
    }

    if !opt.disable_klog {
        archivist.start_draining_klog().await?;
    }

    let mut services = vec![];

    if let Some(service_config_path) = &opt.service_config_path {
        match configs::parse_service_config(service_config_path) {
            Err(e) => {
                error!("Couldn't parse service config: {}", e);
            }
            Ok(config) => {
                for name in config.service_list.iter() {
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
