// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use archivist_lib::logs::debuglog::KERNEL_URL;
use argh::FromArgs;
use diagnostics_reader::{ArchiveReader, Logs};
use fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, ArchiveAccessorProxy};
use fuchsia_async as fasync;
use fuchsia_component::{client::connect_to_service, server::ServiceFs};
use fuchsia_inspect::{
    component::{health, inspector},
    health::Reporter,
};
use fuchsia_inspect_derive::WithInspect;
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use futures::{future::join, prelude::*};
use std::convert::TryFrom;
use std::fs::File;
use std::io::BufReader;
use tracing::*;

mod stats;
use stats::{LogIdentifier, LogManagerStats, LogSource};

mod metric_logger;
use metric_logger::MetricLogger;

/// The name of the subcommand and the logs-tag.
pub const PROGRAM_NAME: &str = "log-stats";

/// Path to the json file that contains the MetricSpecs for granular error stats metric.
pub const GRANULAR_STATS_METRIC_SPECS_FILE_PATH: &str =
    "/config/data/granular_stats_metric_specs.json";

/// Empty command line args, just to give Launcher the subcommand name "log-stats"
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "log-stats")]
pub struct CommandLine {}

pub async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();
    service_fs.take_and_serve_directory_handle()?;

    // Try to read and parse the MetricSpecs file and create a MetricLogger. If this fails, we
    // continue but metrics are not logged.
    let metric_logger = match File::open(GRANULAR_STATS_METRIC_SPECS_FILE_PATH) {
        Ok(file) => {
            let reader = BufReader::new(file);
            match serde_json::from_reader(reader) {
                Ok(specs) => match MetricLogger::new(specs).await {
                    Ok(metric_logger) => Some(metric_logger),
                    Err(err) => {
                        fx_log_err!(
                            "Cannot log metrics because MetricLogger instantiation failed: {}",
                            err
                        );
                        None
                    }
                },
                Err(err) => {
                    fx_log_err!(
                        "Cannot log metrics because the MetricSpecs file could not be parsed: {}",
                        err
                    );
                    None
                }
            }
        }
        Err(err) => {
            fx_log_warn!(
                "Cannot log metrics because the MetricSpecs file could not be opened: {}",
                err
            );
            None
        }
    };

    inspector().serve(&mut service_fs)?;
    health().set_starting_up();
    let stats = LogManagerStats::default().with_inspect(inspector().root(), "log_stats")?;

    let accessor = connect_to_service::<ArchiveAccessorMarker>()?;

    health().set_ok();
    info!("Maintaining.");
    join(maintain(stats, accessor, metric_logger), service_fs.collect::<()>()).await;
    info!("Exiting.");
    Ok(())
}

async fn maintain(
    mut stats: LogManagerStats,
    archive: ArchiveAccessorProxy,
    metric_logger_opt: Option<MetricLogger>,
) {
    let reader = ArchiveReader::new().with_archive(archive);

    let (mut logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
    let _errors = fasync::Task::spawn(async move {
        while let Some(error) = errors.next().await {
            panic!("Error encountered while retrieving logs: {}", error);
        }
    });

    while let Some(log) = logs.next().await {
        let source = if log.metadata.component_url == KERNEL_URL {
            LogSource::Kernel
        } else {
            LogSource::LogSink
        };
        stats.record_log(&log, source);
        stats.get_component_log_stats(&log.metadata.component_url).await.record_log(&log);
        if let Some(ref metric_logger) = metric_logger_opt {
            if let Ok(log_identifier) = LogIdentifier::try_from(&log) {
                let res =
                    metric_logger.log(&log_identifier.file_path, log_identifier.line_no).await;
                if let Err(err) = res {
                    fx_log_err!("Metric logger failed: {}", err);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn smoke_test() {
        assert!(true);
    }
}
