// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_power_logger_args as args_mod,
    fidl_fuchsia_metricslogger_test::{self as fmetrics, Metric, Power, StatisticsArgs},
};

#[ffx_plugin(
    fmetrics::MetricsLoggerProxy = "core/metrics-logger:expose:fuchsia.metricslogger.test.\
    MetricsLogger"
)]
pub async fn logger(
    power_logger: fmetrics::MetricsLoggerProxy,
    cmd: args_mod::Command,
) -> Result<()> {
    match cmd.subcommand {
        args_mod::SubCommand::Start(start_cmd) => start(power_logger, start_cmd).await,
        args_mod::SubCommand::Stop(_) => stop(power_logger).await,
    }
}

pub async fn start(
    power_logger: fmetrics::MetricsLoggerProxy,
    cmd: args_mod::StartCommand,
) -> Result<()> {
    let statistics_args = cmd
        .statistics_interval
        .map(|i| Box::new(StatisticsArgs { statistics_interval_ms: i.as_millis() as u32 }));
    let sampling_interval_ms = cmd.sampling_interval.as_millis() as u32;

    // Dispatch to MetricsLogger.StartLogging or MetricsLogger.StartLoggingForever,
    // depending on whether a logging duration is specified.
    let result = if let Some(duration) = cmd.duration {
        let duration_ms = duration.as_millis() as u32;
        power_logger
            .start_logging(
                "ffx_power",
                &mut vec![&mut Metric::Power(Power { sampling_interval_ms, statistics_args })]
                    .into_iter(),
                duration_ms,
                cmd.output_samples_to_syslog,
                cmd.output_stats_to_syslog,
            )
            .await?
    } else {
        power_logger
            .start_logging_forever(
                "ffx_power",
                &mut vec![&mut Metric::Power(Power { sampling_interval_ms, statistics_args })]
                    .into_iter(),
                cmd.output_samples_to_syslog,
                cmd.output_stats_to_syslog,
            )
            .await?
    };

    match result {
        Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval) => ffx_bail!(
            "MetricsLogger.StartLogging received an invalid sampling interval. \n\
            Please check if `sampling-interval` meets the following requirements: \n\
            1) Must be smaller than `duration` if `duration` is specified; \n\
            2) Must not be smaller than 500ms if `output_samples_to_syslog` is enabled."
        ),
        Err(fmetrics::MetricsLoggerError::AlreadyLogging) => ffx_bail!(
            "Ffx power logging is already active. Use \"stop\" subcommand to stop the active \
            loggingg manually."
        ),
        Err(fmetrics::MetricsLoggerError::NoDrivers) => {
            ffx_bail!("This device has no sensor for logging power.")
        }
        Err(fmetrics::MetricsLoggerError::TooManyActiveClients) => ffx_bail!(
            "MetricsLogger is running too many clients. Retry after any other client is stopped."
        ),
        Err(fmetrics::MetricsLoggerError::InvalidStatisticsInterval) => ffx_bail!(
            "MetricsLogger.StartLogging received an invalid statistics interval. \n\
            Please check if `statistics-interval` meets the following requirements: \n\
            1) Must be equal to or larger than `sampling-interval`; \n\
            2) Must be smaller than `duration` if `duration` is specified; \n\
            3) Must not be smaller than 500ms if `output_stats_to_syslog` is enabled."
        ),
        Err(fmetrics::MetricsLoggerError::Internal) => {
            ffx_bail!("Request failed due to an internal error. Check syslog for more details.")
        }
        _ => Ok(()),
    }
}

pub async fn stop(power_logger: fmetrics::MetricsLoggerProxy) -> Result<()> {
    if !power_logger.stop_logging("ffx_power").await? {
        ffx_bail!("Stop logging returned false; Check if logging is already inactive.");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_metricslogger_test::{self as fmetrics, Metric, Power, StatisticsArgs},
        futures::channel::mpsc,
        std::time::Duration,
    };

    // Create a metrics-logger that expects a specific request type (Start, StartForever, or
    // Stop), and returns a specific error
    macro_rules! make_logger {
        ($request_type:tt, $error_type:tt) => {
            setup_fake_power_logger(move |req| match req {
                fmetrics::MetricsLoggerRequest::$request_type { responder, .. } => {
                    let mut result = Err(fmetrics::MetricsLoggerError::$error_type);
                    responder.send(&mut result).unwrap();
                }
                _ => panic!(
                    "Expected MetricsLoggerRequest::{}; got {:?}",
                    stringify!($request_type),
                    req
                ),
            })
        };
    }

    const ONE_SEC: Duration = Duration::from_secs(1);

    /// Confirms that commandline args are dispatched to FIDL requests as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_dispatch() {
        // Start logging: sampling_interval=1s, statistics_interval=2s, duration=4s
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(2 * ONE_SEC),
            duration: Some(4 * ONE_SEC),
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_power_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StartLogging {
                client_id,
                metrics,
                duration_ms,
                output_samples_to_syslog,
                output_stats_to_syslog,
                responder,
            } => {
                assert_eq!(String::from("ffx_power"), client_id);
                assert_eq!(metrics.len(), 1);
                assert_eq!(
                    metrics[0],
                    Metric::Power(Power {
                        sampling_interval_ms: 1000,
                        statistics_args: Some(Box::new(StatisticsArgs {
                            statistics_interval_ms: 2000
                        })),
                    }),
                );
                assert_eq!(output_samples_to_syslog, false);
                assert_eq!(output_stats_to_syslog, false);
                assert_eq!(duration_ms, 4000);
                let mut result = Ok(());
                responder.send(&mut result).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StartLogging; got {:?}", req),
        });
        start(logger, args).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));

        // Start logging: sampling_interval=1s, statistics_interval=2s, duration=forever
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(2 * ONE_SEC),
            duration: None,
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_power_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StartLoggingForever {
                client_id,
                metrics,
                output_samples_to_syslog,
                output_stats_to_syslog,
                responder,
                ..
            } => {
                assert_eq!(String::from("ffx_power"), client_id);
                assert_eq!(metrics.len(), 1);
                assert_eq!(
                    metrics[0],
                    Metric::Power(Power {
                        sampling_interval_ms: 1000,
                        statistics_args: Some(Box::new(StatisticsArgs {
                            statistics_interval_ms: 2000
                        })),
                    }),
                );
                assert_eq!(output_samples_to_syslog, false);
                assert_eq!(output_stats_to_syslog, false);
                let mut result = Ok(());
                responder.send(&mut result).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StartLoggingForever; got {:?}", req),
        });
        start(logger, args).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));

        // Stop logging
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_power_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StopLogging { client_id, responder } => {
                assert_eq!(String::from("ffx_power"), client_id);
                responder.send(true).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StopLogging; got {:?}", req),
        });
        stop(logger).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));
    }

    /// Confirms that the start logging request is dispatched to FIDL requests as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_dispatch_start_logging() {
        // Start logging: sampling_interval=1s, statistics_interval=2s, duration=4s
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(2 * ONE_SEC),
            duration: Some(4 * ONE_SEC),
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_power_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StartLogging {
                client_id,
                metrics,
                duration_ms,
                output_samples_to_syslog,
                output_stats_to_syslog,
                responder,
            } => {
                assert_eq!(String::from("ffx_power"), client_id);
                assert_eq!(metrics.len(), 1);
                assert_eq!(
                    metrics[0],
                    Metric::Power(Power {
                        sampling_interval_ms: 1000,
                        statistics_args: Some(Box::new(StatisticsArgs {
                            statistics_interval_ms: 2000
                        })),
                    }),
                );
                assert_eq!(output_samples_to_syslog, false);
                assert_eq!(output_stats_to_syslog, false);
                assert_eq!(duration_ms, 4000);
                let mut result = Ok(());
                responder.send(&mut result).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StartLogging; got {:?}", req),
        });
        start(logger, args).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));
    }

    /// Confirms that the start logging forever request is dispatched to FIDL requests as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_dispatch_start_logging_forever() {
        // Start logging: sampling_interval=1s, statistics_interval=2s, duration=forever
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(2 * ONE_SEC),
            duration: None,
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_power_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StartLoggingForever {
                client_id,
                metrics,
                output_samples_to_syslog,
                output_stats_to_syslog,
                responder,
                ..
            } => {
                assert_eq!(String::from("ffx_power"), client_id);
                assert_eq!(metrics.len(), 1);
                assert_eq!(
                    metrics[0],
                    Metric::Power(Power {
                        sampling_interval_ms: 1000,
                        statistics_args: Some(Box::new(StatisticsArgs {
                            statistics_interval_ms: 2000
                        })),
                    }),
                );
                assert_eq!(output_samples_to_syslog, false);
                assert_eq!(output_stats_to_syslog, false);
                let mut result = Ok(());
                responder.send(&mut result).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StartLoggingForever; got {:?}", req),
        });
        start(logger, args).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));
    }

    /// Confirms that the stop logging request is dispatched to FIDL requests as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_dispatch_stop_logging() {
        // Stop logging
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_power_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StopLogging { client_id, responder } => {
                assert_eq!(String::from("ffx_power"), client_id);
                responder.send(true).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StopLogging; got {:?}", req),
        });
        stop(logger).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stop_logging_error() {
        let logger = setup_fake_power_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StopLogging { responder, .. } => {
                responder.send(false).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StopLogging; got {:?}", req),
        });
        let error = stop(logger).await.unwrap_err();
        assert!(error.to_string().contains("Stop logging returned false"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_samplingg_interval_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: Some(2 * ONE_SEC),
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLogging, InvalidSamplingInterval);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("invalid sampling interval"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_sampling_interval_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: None,
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLoggingForever, InvalidSamplingInterval);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("invalid sampling interval"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_statistics_interval_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: Some(2 * ONE_SEC),
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLogging, InvalidStatisticsInterval);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("invalid statistics interval"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_statistics_interval_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: None,
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLoggingForever, InvalidStatisticsInterval);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("invalid statistics interval"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_already_active_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: Some(2 * ONE_SEC),
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLogging, AlreadyLogging);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("already active"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_already_active_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: None,
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLoggingForever, AlreadyLogging);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("already active"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_too_many_clients_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: Some(2 * ONE_SEC),
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLogging, TooManyActiveClients);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("too many clients"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_too_many_clients_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: None,
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLoggingForever, TooManyActiveClients);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("too many clients"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_no_sensor_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: Some(2 * ONE_SEC),
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLogging, NoDrivers);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("no sensor"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_no_sensor_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: Some(ONE_SEC),
            duration: None,
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLoggingForever, NoDrivers);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("no sensor"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_internal_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: None,
            duration: Some(2 * ONE_SEC),
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLogging, Internal);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("an internal error"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_internal_error() {
        let args = args_mod::StartCommand {
            sampling_interval: ONE_SEC,
            statistics_interval: None,
            duration: None,
            output_samples_to_syslog: false,
            output_stats_to_syslog: false,
        };
        let logger = make_logger!(StartLoggingForever, Internal);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("an internal error"));
    }
}
