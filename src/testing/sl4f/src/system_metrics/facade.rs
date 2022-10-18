// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::system_metrics::types::{
    CpuLoadLoggerResult, StartLoggingForeverRequest, StartLoggingRequest,
};
use anyhow::Error;
use fidl_fuchsia_metricslogger_test::{CpuLoad, Metric, MetricsLoggerMarker};
use fuchsia_component::client::connect_to_protocol;
use serde_json::{from_value, Value};

const CLIENT_ID: &'static str = "sl4f_cpu_load";

#[derive(Debug)]
pub struct SystemMetricsFacade {}

impl SystemMetricsFacade {
    pub fn new() -> SystemMetricsFacade {
        SystemMetricsFacade {}
    }

    /// Start logging cpu load into trace events for a specified duration.
    ///
    /// [start_logging] is preferred over [start_logging_forever] for automated tests, so that the
    /// logging session will end after some time even if the test fails to stop it due to failing
    /// or crashing.
    pub async fn start_logging(&self, args: Value) -> Result<CpuLoadLoggerResult, Error> {
        let params: StartLoggingRequest = from_value(args)?;
        connect_to_protocol::<MetricsLoggerMarker>()?
            .start_logging(
                CLIENT_ID,
                &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: params.interval_ms })]
                    .into_iter(),
                params.duration_ms,
                /* output_samples_to_syslog */ false,
                /* output_stats_to_syslog */ false,
            )
            .await?
            .map_err(|e| format_err!("Received MetricsLoggerError: {:?}", e))?;
        Ok(CpuLoadLoggerResult::Success)
    }

    /// Start logging cpu load into trace events until a call to [stop_logging].
    pub async fn start_logging_forever(&self, args: Value) -> Result<CpuLoadLoggerResult, Error> {
        let params: StartLoggingForeverRequest = from_value(args)?;
        connect_to_protocol::<MetricsLoggerMarker>()?
            .start_logging_forever(
                CLIENT_ID,
                &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: params.interval_ms })]
                    .into_iter(),
                /* output_samples_to_syslog */ false,
                /* output_stats_to_syslog */ false,
            )
            .await?
            .map_err(|e| format_err!("Received MetricsLoggerError: {:?}", e))?;
        Ok(CpuLoadLoggerResult::Success)
    }

    /// Stop cpu load logging.
    ///
    /// This function will succeed even if cpu load logging is not in progress, so automated
    /// tests can call this before starting their logging session to clean up in case a prior test
    /// failed without stopping logging.
    pub async fn stop_logging(&self, _args: Value) -> Result<CpuLoadLoggerResult, Error> {
        let logger = connect_to_protocol::<MetricsLoggerMarker>()?;
        logger.stop_logging(CLIENT_ID).await?;
        Ok(CpuLoadLoggerResult::Success)
    }
}
