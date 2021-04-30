// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::system_metrics::types::{
    StartLoggingForeverRequest, StartLoggingRequest, SystemMetricsLoggerResult,
};
use anyhow::Error;
use fidl_fuchsia_systemmetrics_test::SystemMetricsLoggerMarker;
use fuchsia_component::client::connect_to_protocol;
use serde_json::{from_value, Value};

#[derive(Debug)]
pub struct SystemMetricsFacade {}

impl SystemMetricsFacade {
    pub fn new() -> SystemMetricsFacade {
        SystemMetricsFacade {}
    }

    /// Start logging system metrics into trace events for a specified duration.
    ///
    /// [start_logging] is preferred over [start_logging_forever] for automated tests, so that the
    /// logging session will end after some time even if the test fails to stop it due to failing
    /// or crashing.
    pub async fn start_logging(&self, args: Value) -> Result<SystemMetricsLoggerResult, Error> {
        let params: StartLoggingRequest = from_value(args)?;
        connect_to_protocol::<SystemMetricsLoggerMarker>()?
            .start_logging(params.interval_ms, params.duration_ms)
            .await?
            .map_err(|e| format_err!("Received SystemMetricsLoggerError: {:?}", e))?;
        Ok(SystemMetricsLoggerResult::Success)
    }

    /// Start logging system metrics into trace events until a call to [stop_logging].
    pub async fn start_logging_forever(
        &self,
        args: Value,
    ) -> Result<SystemMetricsLoggerResult, Error> {
        let params: StartLoggingForeverRequest = from_value(args)?;
        connect_to_protocol::<SystemMetricsLoggerMarker>()?
            .start_logging_forever(params.interval_ms)
            .await?
            .map_err(|e| format_err!("Received SystemMetricsLoggerError: {:?}", e))?;
        Ok(SystemMetricsLoggerResult::Success)
    }

    /// Stop system metrics logging.
    ///
    /// This function will succeed even if system metrics logging is not in progress, so automated
    /// tests can call this before starting their logging session to clean up in case a prior test
    /// failed without stopping logging.
    pub async fn stop_logging(&self, _args: Value) -> Result<SystemMetricsLoggerResult, Error> {
        let logger = connect_to_protocol::<SystemMetricsLoggerMarker>()?;
        logger.stop_logging().await?;
        Ok(SystemMetricsLoggerResult::Success)
    }
}
