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
    fuchsia_async::SendExecutor,
    fuchsia_component::server::MissingStartupHandle,
    fuchsia_inspect::component,
    fuchsia_zircon as zx,
    std::str::FromStr,
    tracing::{debug, error, info, warn, Level, Subscriber},
    tracing_subscriber::{
        fmt::{
            format::{self, FormatEvent, FormatFields},
            FmtContext,
        },
        registry::LookupSpan,
    },
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
    let num_threads = config.num_threads;
    debug!("Running executor with {} threads.", num_threads);
    let mut executor = SendExecutor::new(num_threads as usize)?;
    executor.run(async_main(config)).context("async main")?;
    debug!("Exiting.");
    Ok(())
}

async fn init_diagnostics(config: &Config) -> Result<(), Error> {
    if config.log_to_debuglog {
        stdout_to_debuglog::init().await.unwrap();
        tracing_subscriber::fmt()
            .event_format(DebugLogEventFormatter)
            .with_writer(std::io::stdout)
            .with_max_level(Level::INFO)
            .init();
    } else {
        diagnostics_log::init!(&["embedded"]);
    }

    if config.log_to_debuglog {
        info!("Logging started.");
    }

    diagnostics::init();

    fuchsia_trace_provider::trace_provider_create_with_fdio();
    Ok(())
}

async fn async_main(config: Config) -> Result<(), Error> {
    init_diagnostics(&config).await.context("initializing diagnostics")?;
    component::inspector()
        .root()
        .record_child("config", |config_node| config.record_inspect(config_node));

    let mut archivist = Archivist::new(&config);
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

    for name in config.bind_services {
        info!("Connecting to service {}", name);
        let (_local, remote) = zx::Channel::create().expect("cannot create channels");
        if let Err(e) = fdio::service_connect(&format!("/svc/{}", name), remote) {
            error!("Couldn't connect to service {}: {:?}", name, e);
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

struct DebugLogEventFormatter;

impl<S, N> FormatEvent<S, N> for DebugLogEventFormatter
where
    S: Subscriber + for<'a> LookupSpan<'a>,
    N: for<'a> FormatFields<'a> + 'static,
{
    fn format_event(
        &self,
        ctx: &FmtContext<'_, S, N>,
        mut writer: format::Writer<'_>,
        event: &tracing::Event<'_>,
    ) -> std::fmt::Result {
        let level = *event.metadata().level();
        write!(writer, "[archivist] {}: ", level)?;
        ctx.field_format().format_fields(writer.by_ref(), event)?;
        writeln!(writer)
    }
}
