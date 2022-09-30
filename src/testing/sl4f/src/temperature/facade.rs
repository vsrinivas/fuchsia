// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::temperature::types;
use anyhow::Error;
use fidl_fuchsia_hardware_temperature::{DeviceMarker, DeviceProxy};
use fidl_fuchsia_metricslogger_test::{
    Metric, MetricsLoggerMarker, MetricsLoggerProxy, Temperature,
};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use serde_json::Value;
use std::path::Path;

const CLIENT_ID: &'static str = "sl4f_temperature";

/// Perform Temperature operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct TemperatureFacade {
    /// Temperature device proxy that may be optionally provided for testing. The proxy is not
    /// cached during normal operation.
    device_proxy: Option<DeviceProxy>,

    /// Optional logger proxy for testing, similar to `device_proxy`.
    logger_proxy: Option<MetricsLoggerProxy>,
}

impl TemperatureFacade {
    pub fn new() -> TemperatureFacade {
        TemperatureFacade { device_proxy: None, logger_proxy: None }
    }

    /// Connect to the temperature device specified by `device_path`.
    ///
    /// # Arguments
    /// * `device_path` - String representing the temperature device path (e.g.,
    ///   /dev/class/temperature/000)
    fn get_device_proxy(&self, device_path: String) -> Result<DeviceProxy, Error> {
        let tag = "TemperatureFacade::get_device_proxy";

        if let Some(proxy) = &self.device_proxy {
            Ok(proxy.clone())
        } else {
            let (proxy, server) = match fidl::endpoints::create_proxy::<DeviceMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to create proxy {:?}", e)
                ),
            };

            if Path::new(&device_path).exists() {
                fdio::service_connect(device_path.as_ref(), server.into_channel())?;
                Ok(proxy)
            } else {
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to find device: {}", device_path)
                );
            }
        }
    }

    /// Connect to the discoverable MetricsLogger service and return the proxy.
    fn get_logger_proxy(&self) -> Result<MetricsLoggerProxy, Error> {
        let tag = "TemperatureFacade::get_logger_proxy";

        if let Some(proxy) = &self.logger_proxy {
            Ok(proxy.clone())
        } else {
            match connect_to_protocol::<MetricsLoggerMarker>() {
                Ok(proxy) => Ok(proxy),
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to create proxy: {:?}", e)
                ),
            }
        }
    }

    /// Reads the temperature from a specified temperature device.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the TemperatureRequest information, where TemperatureRequest
    ///   contains the device path to read from.
    pub async fn get_temperature_celsius(&self, args: Value) -> Result<f32, Error> {
        let req: types::TemperatureRequest = serde_json::from_value(args)?;
        let (status, temperature) =
            self.get_device_proxy(req.device_path)?.get_temperature_celsius().await?;
        zx::Status::ok(status)?;
        Ok(temperature)
    }

    /// Initiates fixed-duration logging with the MetricsLogger service.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the StartLoggingRequest information. Key "interval_ms"
    ///   specifies the logging interval, and "duration_ms" specifies the duration of logging.
    pub async fn start_logging(
        &self,
        args: Value,
    ) -> Result<types::TemperatureLoggerResult, Error> {
        let req: types::StartLoggingRequest = serde_json::from_value(args)?;
        let proxy = self.get_logger_proxy()?;

        proxy
            .start_logging(
                CLIENT_ID,
                &mut vec![&mut Metric::Temperature(Temperature {
                    sampling_interval_ms: req.interval_ms,
                    statistics_args: None,
                })]
                .into_iter(),
                req.duration_ms,
                /* output_samples_to_syslog */ false,
                /* output_stats_to_syslog */ false,
            )
            .await?
            .map_err(|e| format_err!("Received TemperatureLoggerError: {:?}", e))?;
        Ok(types::TemperatureLoggerResult::Success)
    }

    /// Initiates durationless logging with the TemperatureLogger service.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the StartLoggingRequest information. Key "interval_ms"
    ///   specifies the logging interval.
    pub async fn start_logging_forever(
        &self,
        args: Value,
    ) -> Result<types::TemperatureLoggerResult, Error> {
        let req: types::StartLoggingForeverRequest = serde_json::from_value(args)?;
        let proxy = self.get_logger_proxy()?;

        proxy
            .start_logging_forever(
                CLIENT_ID,
                &mut vec![&mut Metric::Temperature(Temperature {
                    sampling_interval_ms: req.interval_ms,
                    statistics_args: None,
                })]
                .into_iter(),
                /* output_samples_to_syslog */ false,
                /* output_stats_to_syslog */ false,
            )
            .await?
            .map_err(|e| format_err!("Received TemperatureLoggerError: {:?}", e))?;
        Ok(types::TemperatureLoggerResult::Success)
    }

    /// Terminates logging by the TemperatureLogger service.
    pub async fn stop_logging(&self) -> Result<types::TemperatureLoggerResult, Error> {
        self.get_logger_proxy()?.stop_logging(CLIENT_ID).await?;
        Ok(types::TemperatureLoggerResult::Success)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_hardware_temperature::DeviceRequest;
    use fidl_fuchsia_metricslogger_test::MetricsLoggerRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use serde_json::json;

    /// Tests that the `get_temperature_celsius` method correctly queries the temperature device and
    /// returns the expected temperature value.
    #[fasync::run_singlethreaded(test)]
    async fn test_get_temperature_celsius() {
        let (proxy, mut stream) = create_proxy_and_stream::<DeviceMarker>().unwrap();
        let facade = TemperatureFacade { device_proxy: Some(proxy), logger_proxy: None };
        let facade_fut = async move {
            assert_eq!(
                facade
                    .get_temperature_celsius(json!({
                        "device_path": "/dev/class/temperature/000"
                    }))
                    .await
                    .unwrap(),
                12.34
            );
        };
        let stream_fut = async move {
            match stream.try_next().await {
                Ok(Some(DeviceRequest::GetTemperatureCelsius { responder })) => {
                    responder.send(0, 12.34).unwrap();
                }
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(facade_fut, stream_fut).await;
    }

    /// Tests that the `start_logging` method correctly queries the logger.
    #[fasync::run_singlethreaded(test)]
    async fn test_start_logging() {
        let query_interval_ms = 500;
        let query_duration_ms = 10_000;

        let (proxy, mut stream) = create_proxy_and_stream::<MetricsLoggerMarker>().unwrap();

        let _stream_task = fasync::Task::local(async move {
            match stream.try_next().await {
                Ok(Some(MetricsLoggerRequest::StartLogging {
                    client_id,
                    metrics,
                    duration_ms,
                    output_samples_to_syslog,
                    output_stats_to_syslog,
                    responder,
                })) => {
                    assert_eq!(String::from("sl4f_temperature"), client_id);
                    assert_eq!(metrics.len(), 1);
                    assert_eq!(
                        metrics[0],
                        Metric::Temperature(Temperature {
                            sampling_interval_ms: query_interval_ms,
                            statistics_args: None,
                        }),
                    );
                    assert_eq!(output_samples_to_syslog, false);
                    assert_eq!(output_stats_to_syslog, false);
                    assert_eq!(duration_ms, query_duration_ms);

                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("Unexpected stream item: {:?}", other),
            }
        });

        let facade = TemperatureFacade { device_proxy: None, logger_proxy: Some(proxy) };

        assert_matches!(
            facade
                .start_logging(json!({
                    "interval_ms": query_interval_ms,
                    "duration_ms": query_duration_ms
                }))
                .await,
            Ok(types::TemperatureLoggerResult::Success)
        );
    }

    /// Tests that the `start_logging_forever` method correctly queries the logger.
    #[fasync::run_singlethreaded(test)]
    async fn test_start_logging_forever() {
        let query_interval_ms = 500;

        let (proxy, mut stream) = create_proxy_and_stream::<MetricsLoggerMarker>().unwrap();

        let _stream_task = fasync::Task::local(async move {
            match stream.try_next().await {
                Ok(Some(MetricsLoggerRequest::StartLoggingForever {
                    client_id,
                    metrics,
                    output_samples_to_syslog,
                    output_stats_to_syslog,
                    responder,
                })) => {
                    assert_eq!(String::from("sl4f_temperature"), client_id);
                    assert_eq!(metrics.len(), 1);
                    assert_eq!(
                        metrics[0],
                        Metric::Temperature(Temperature {
                            sampling_interval_ms: query_interval_ms,
                            statistics_args: None,
                        }),
                    );
                    assert_eq!(output_samples_to_syslog, false);
                    assert_eq!(output_stats_to_syslog, false);

                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("Unexpected stream item: {:?}", other),
            }
        });

        let facade = TemperatureFacade { device_proxy: None, logger_proxy: Some(proxy) };

        assert_matches!(
            facade.start_logging_forever(json!({ "interval_ms": query_interval_ms })).await,
            Ok(types::TemperatureLoggerResult::Success)
        );
    }

    /// Tests that the `stop_logging` method correctly queries the logger.
    #[fasync::run_singlethreaded(test)]
    async fn test_stop_logging() {
        let (proxy, mut stream) = create_proxy_and_stream::<MetricsLoggerMarker>().unwrap();

        let _stream_task = fasync::Task::local(async move {
            match stream.try_next().await {
                Ok(Some(MetricsLoggerRequest::StopLogging { client_id, responder })) => {
                    assert_eq!(String::from("sl4f_temperature"), client_id);
                    responder.send(true).unwrap()
                }
                other => panic!("Unexpected stream item: {:?}", other),
            }
        });

        let facade = TemperatureFacade { device_proxy: None, logger_proxy: Some(proxy) };

        assert_matches!(facade.stop_logging().await, Ok(types::TemperatureLoggerResult::Success));
    }
}
