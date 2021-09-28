// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use diagnostics_reader::{ArchiveReader, Logs};
use fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, ArchiveAccessorProxy};
use fuchsia_async as fasync;
use fuchsia_component::{client::connect_to_protocol, server::ServiceFs};
use fuchsia_inspect::{
    component::{health, inspector},
    health::Reporter,
};
use fuchsia_inspect_derive::WithInspect;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use futures::{future::join, prelude::*};
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

    let metric_logger = create_metric_logger().await;

    health().set_ok();
    info!("Maintaining.");
    join(maintain(stats, accessor, metric_logger), service_fs.collect::<()>()).await;
    info!("Exiting.");
    Ok(())
}

async fn create_metric_logger() -> Option<MetricLogger> {
    // Open and parse the ComponentEventCodeMap file. If this fails, metrics are not logged.
    let component_map = match File::open(COMPONENT_EVENT_CODES_FILE_PATH) {
        Ok(f) => match serde_json::from_reader(BufReader::new(f)) {
            Ok(map) => map,
            Err(err) => {
                fx_log_err!("Failed to parse component map file: {}", err);
                return None;
            }
        },
        Err(err) => {
            fx_log_info!("Failed to open component map file: {}", err);
            return None;
        }
    };

    // Open and parse the MetricSpecs file. If this fails, metrics are not logged.
    let specs = match File::open(GRANULAR_STATS_METRIC_SPECS_FILE_PATH) {
        Ok(f) => match serde_json::from_reader(BufReader::new(f)) {
            Ok(specs) => specs,
            Err(err) => {
                fx_log_err!("Failed to parse metric specs file: {}", err);
                return None;
            }
        },
        Err(err) => {
            fx_log_info!("Failed to open metric specs file: {}", err);
            return None;
        }
    };

    match MetricLogger::new(specs, component_map).await {
        Ok(logger) => Some(logger),
        Err(err) => {
            fx_log_err!("Failed to instantiate MetricLogger: {}", err);
            None
        }
    }
}

pub const KERNEL_URL: &str = "fuchsia-boot://kernel";

async fn maintain(
    mut stats: LogManagerStats,
    archive: ArchiveAccessorProxy,
    mut metric_logger: Option<MetricLogger>,
) {
    let mut reader = ArchiveReader::new();
    reader.with_archive(archive);

    let (mut logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _errors = fasync::Task::spawn(async move {
        while let Some(error) = errors.next().await {
            panic!("Error encountered while retrieving logs: {}", error);
        }
    });

    while let Some(log) = logs.next().await {
        let source = if log.metadata.component_url == Some(KERNEL_URL.to_string()) {
            LogSource::Kernel
        } else {
            LogSource::LogSink
        };
        stats.record_log(&log, source);
        if let Some(ref url) = log.metadata.component_url {
            stats.get_component_log_stats(url.as_str()).await.record_log(&log);
            if let Some(ref mut metric_logger) = metric_logger {
                let res = metric_logger.process(&log).await;
                if let Err(err) = res {
                    fx_log_warn!("MetricLogger failed: {}", err);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    #[fuchsia::test]
    async fn smoke_test() {
        assert!(true);
    }
}
