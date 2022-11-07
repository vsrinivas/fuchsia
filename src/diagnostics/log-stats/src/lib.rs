// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Result};
use argh::FromArgs;
use core::panic;
use diagnostics_reader::{ArchiveReader, Logs};
use fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, ArchiveAccessorProxy};
use fidl_fuchsia_ui_activity as factivity;
use fuchsia_async as fasync;
use fuchsia_component::{client::connect_to_protocol, server::ServiceFs};
use fuchsia_inspect::{
    component::{health, inspector},
    health::Reporter,
};
use fuchsia_inspect_derive::WithInspect;
use futures::{future::join, prelude::*, select};
use std::fs::File;
use std::io::BufReader;
use tracing::*;

mod stats;
use stats::{LogManagerStats, LogSource};

mod metric_logger;
use metric_logger::MetricLogger;

/// The name of the subcommand and the logs-tag.
pub const PROGRAM_NAME: &str = "log-stats";

/// Path to the json file that contains the MetricSpecs for granular error stats metric.
pub const GRANULAR_STATS_METRIC_SPECS_FILE_PATH: &str =
    "/config/data/granular_stats_metric_specs.json";

/// Path the the json file that contains the mapping between component URLs and Cobalt event codes.
pub const COMPONENT_EVENT_CODES_FILE_PATH: &str = "/config/data/component_event_codes.json";

/// Empty command line args, just to give Launcher the subcommand name "log-stats"
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "log-stats")]
pub struct CommandLine {}

pub async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();
    service_fs.take_and_serve_directory_handle()?;

    inspect_runtime::serve(inspector(), &mut service_fs)?;
    health().set_starting_up();
    let stats = LogManagerStats::default().with_inspect(inspector().root(), "log_stats")?;

    let accessor = connect_to_protocol::<ArchiveAccessorMarker>()?;

    let activity_provider = connect_to_protocol::<factivity::ProviderMarker>()?;

    let metric_logger = create_metric_logger().await;

    health().set_ok();
    info!("Maintaining.");
    join(maintain(stats, accessor, metric_logger, activity_provider), service_fs.collect::<()>())
        .await;
    info!("Exiting.");
    Ok(())
}

async fn create_metric_logger() -> Option<MetricLogger> {
    // Open and parse the ComponentEventCodeMap file. If this fails, metrics are not logged.
    let component_map = match File::open(COMPONENT_EVENT_CODES_FILE_PATH) {
        Ok(f) => match serde_json::from_reader(BufReader::new(f)) {
            Ok(map) => map,
            Err(err) => {
                error!(%err, "Failed to parse component map file");
                return None;
            }
        },
        Err(err) => {
            info!(%err, "Failed to open component map file");
            return None;
        }
    };

    // Open and parse the MetricSpecs file. If this fails, metrics are not logged.
    let specs = match File::open(GRANULAR_STATS_METRIC_SPECS_FILE_PATH) {
        Ok(f) => match serde_json::from_reader(BufReader::new(f)) {
            Ok(specs) => specs,
            Err(err) => {
                error!(%err, "Failed to parse metric specs file");
                return None;
            }
        },
        Err(err) => {
            info!(%err, "Failed to open metric specs file");
            return None;
        }
    };

    match MetricLogger::new(specs, component_map).await {
        Ok(logger) => Some(logger),
        Err(err) => {
            error!(%err, "Failed to instantiate MetricLogger");
            None
        }
    }
}

pub const KERNEL_URL: &str = "fuchsia-boot://kernel";

async fn maintain(
    mut stats: LogManagerStats,
    archive: ArchiveAccessorProxy,
    mut metric_logger: Option<MetricLogger>,
    activity_provider: factivity::ProviderProxy,
) {
    let mut reader = ArchiveReader::new();
    reader.with_archive(archive);

    let (mut logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _errors = fasync::Task::spawn(async move {
        while let Some(error) = errors.next().await {
            panic!("Error encountered while retrieving logs: {}", error);
        }
    });

    let mut listener_stream = match connect_activity_service(&activity_provider) {
        Ok(stream) => stream,
        Err(e) => {
            panic!("Failed to create listener stream: {}", e);
        }
    };
    // State tuple to send a state of the device and transition time of that state
    let mut prev_state = None;
    let mut curr_state = None;

    loop {
        select! {
            state = listener_stream.next().fuse() => {
                match state {
                    Some(Ok(factivity::ListenerRequest::OnStateChanged {
                        state,
                        responder,
                        transition_time,
                        ..
                    })) => {
                        prev_state = curr_state;
                        curr_state = Some((state, transition_time));

                        let _ = responder.send();
                    }
                    Some(Err(e)) => log::error!("Error polling listener_stream: {}", e),
                    None => {
                        log::error!("Listener stream closed. Reconnecting...");
                        match connect_activity_service(&activity_provider) {
                            Ok(stream) => listener_stream = stream,
                            Err(e) => {
                                log::error!("{}", e);
                            }
                        }
                    }
                }
            },

            next_log = logs.next().fuse() => {
                match next_log {
                    Some(log) => {
                        let source = if log.metadata.component_url == Some(KERNEL_URL.to_string()) {
                            LogSource::Kernel
                        } else {
                            LogSource::LogSink
                        };
                        stats.record_log(&log, source);
                        if let Some(ref url) = log.metadata.component_url {
                            stats.get_component_log_stats(url.as_str()).await.record_log(&log);
                            if let Some(ref mut metric_logger) = metric_logger {
                                let res = metric_logger.process(&log, prev_state, curr_state).await;
                                if let Err(err) = res {
                                    warn!(%err, "MetricLogger failed");
                                }
                            }
                        }
                    }
                    None => continue,
                }
            },
        }
    }
}

fn connect_activity_service(
    activity_provider: &factivity::ProviderProxy,
) -> Result<factivity::ListenerRequestStream> {
    let (client, stream) = fidl::endpoints::create_request_stream::<factivity::ListenerMarker>()
        .context("Failed to create request stream")?;
    activity_provider
        .watch_state(client)
        .map_err(|e| format_err!("watch_state failed: {:?}", e))?;

    Ok(stream)
}

#[cfg(test)]
mod tests {
    #[fuchsia::test]
    async fn smoke_test() {
        assert!(true);
    }
}
