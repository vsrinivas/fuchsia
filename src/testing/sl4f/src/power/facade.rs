// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::power::types;
use anyhow::Error;
use fidl_fuchsia_metricslogger_test::{
    Metric, MetricsLoggerMarker, MetricsLoggerProxy, Power, StatisticsArgs,
};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_syslog::macros::fx_log_err;
use serde_json::Value;

const CLIENT_ID: &'static str = "sl4f";

#[derive(Debug)]
pub struct PowerFacade {
    /// Optional logger proxy for testing.
    logger_proxy: Option<MetricsLoggerProxy>,
}

impl PowerFacade {
    pub fn new() -> PowerFacade {
        PowerFacade { logger_proxy: None }
    }

    fn get_logger_proxy(&self) -> Result<MetricsLoggerProxy, Error> {
        if let Some(proxy) = &self.logger_proxy {
            Ok(proxy.clone())
        } else {
            match connect_to_protocol::<MetricsLoggerMarker>() {
                Ok(proxy) => Ok(proxy),
                Err(e) => fx_err_and_bail!(
                    &with_line!("PowerFacade::get_logger_proxy"),
                    format_err!("Failed to create proxy: {:?}", e)
                ),
            }
        }
    }

    /// Initiates fixed-duration logging with the MetricsLogger service.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the StartLoggingRequest information:
    ///   Key `sampling_interval_ms` specifies the interval for polling the sensors.
    ///   Key `duration_ms` specifies the duration of logging.
    ///   Key `statistics_interval_ms` specifies the interval for summarizing statistics; if
    ///   omitted, statistics is disabled. Refer to `metricslogger.test.fidl` for more details.
    pub async fn start_logging(&self, args: Value) -> Result<types::MetricsLoggerResult, Error> {
        let req: types::StartLoggingRequest = serde_json::from_value(args)?;
        let statistics_args = req
            .statistics_interval_ms
            .map(|i| Box::new(StatisticsArgs { statistics_interval_ms: i }));
        let proxy = self.get_logger_proxy()?;
        // Calls into `metricslogger.test.fidl`.
        proxy
            .start_logging(
                CLIENT_ID,
                &mut vec![&mut Metric::Power(Power {
                    sampling_interval_ms: req.sampling_interval_ms,
                    statistics_args,
                })]
                .into_iter(),
                req.duration_ms,
                /* output_samples_to_syslog */ false,
                /* output_stats_to_syslog */ false,
            )
            .await?
            .map_err(|e| format_err!("Received MetricsLoggerError: {:?}", e))?;
        Ok(types::MetricsLoggerResult::Success)
    }

    /// Initiates durationless logging with the MetricsLogger service.
    ///
    /// # Arguments
    /// * `args`: JSON value containing the StartLoggingRequest information:
    ///   Key `sampling_interval_ms` specifies the interval for polling the sensors.
    ///   Key `statistics_interval_ms` specifies the interval for summarizing statistics; if
    ///   omitted, statistics is disabled. Refer to `metricslogger.test.fidl` for more details.
    pub async fn start_logging_forever(
        &self,
        args: Value,
    ) -> Result<types::MetricsLoggerResult, Error> {
        let req: types::StartLoggingForeverRequest = serde_json::from_value(args)?;
        let statistics_args = req
            .statistics_interval_ms
            .map(|i| Box::new(StatisticsArgs { statistics_interval_ms: i }));
        let proxy = self.get_logger_proxy()?;
        // Calls into `metricslogger.test.fidl`.
        proxy
            .start_logging_forever(
                CLIENT_ID,
                &mut vec![&mut Metric::Power(Power {
                    sampling_interval_ms: req.sampling_interval_ms,
                    statistics_args,
                })]
                .into_iter(),
                /* output_samples_to_syslog */ false,
                /* output_stats_to_syslog */ false,
            )
            .await?
            .map_err(|e| format_err!("Received MetricsLoggerError: {:?}", e))?;
        Ok(types::MetricsLoggerResult::Success)
    }

    /// Terminates logging by the MetricsLogger service.
    pub async fn stop_logging(&self) -> Result<types::MetricsLoggerResult, Error> {
        self.get_logger_proxy()?.stop_logging(CLIENT_ID).await?;
        Ok(types::MetricsLoggerResult::Success)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_metricslogger_test::MetricsLoggerRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use serde_json::json;

    /// Tests that the `start_logging` method correctly queries the logger.
    #[fasync::run_singlethreaded(test)]
    async fn test_start_logging() {
        let query_sampling_interval_ms = 500;
        let query_duration_ms = 10_000;
        let query_statitics_interval_ms = 1_000;

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
                    assert_eq!(String::from("sl4f"), client_id);
                    assert_eq!(metrics.len(), 1);
                    assert_eq!(
                        metrics[0],
                        Metric::Power(Power {
                            sampling_interval_ms: query_sampling_interval_ms,
                            statistics_args: Some(Box::new(StatisticsArgs {
                                statistics_interval_ms: query_statitics_interval_ms,
                            })),
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

        let facade = PowerFacade { logger_proxy: Some(proxy) };

        assert_matches!(
            facade
                .start_logging(json!({
                    "sampling_interval_ms": query_sampling_interval_ms,
                    "statistics_interval_ms": query_statitics_interval_ms,
                    "duration_ms": query_duration_ms
                }))
                .await,
            Ok(types::MetricsLoggerResult::Success)
        );
    }

    /// Tests that the `start_logging_forever` method correctly queries the logger.
    #[fasync::run_singlethreaded(test)]
    async fn test_start_logging_forever() {
        let query_sampling_interval_ms = 500;

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
                    assert_eq!(String::from("sl4f"), client_id);
                    assert_eq!(metrics.len(), 1);
                    assert_eq!(
                        metrics[0],
                        Metric::Power(Power {
                            sampling_interval_ms: query_sampling_interval_ms,
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

        let facade = PowerFacade { logger_proxy: Some(proxy) };

        assert_matches!(
            facade
                .start_logging_forever(json!({
                    "sampling_interval_ms": query_sampling_interval_ms
                }))
                .await,
            Ok(types::MetricsLoggerResult::Success)
        );
    }

    /// Tests that the `stop_logging` method correctly queries the logger.
    #[fasync::run_singlethreaded(test)]
    async fn test_stop_logging() {
        let (proxy, mut stream) = create_proxy_and_stream::<MetricsLoggerMarker>().unwrap();

        let _stream_task = fasync::Task::local(async move {
            match stream.try_next().await {
                Ok(Some(MetricsLoggerRequest::StopLogging { client_id, responder })) => {
                    assert_eq!(String::from("sl4f"), client_id);
                    responder.send(true).unwrap()
                }
                other => panic!("Unexpected stream item: {:?}", other),
            }
        });

        let facade = PowerFacade { logger_proxy: Some(proxy) };

        assert_matches!(facade.stop_logging().await, Ok(types::MetricsLoggerResult::Success));
    }
}
