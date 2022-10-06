// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_network_activity_args as args_mod,
    fidl_fuchsia_metricslogger_test::{self as fmetrics, Metric, NetworkActivity},
};

#[ffx_plugin(
    fmetrics::MetricsLoggerProxy = "core/metrics-logger:expose:fuchsia.metricslogger.test.\
    MetricsLogger"
)]
pub async fn activity(
    network_logger: fmetrics::MetricsLoggerProxy,
    cmd: args_mod::Command,
) -> Result<()> {
    match cmd.subcommand {
        args_mod::SubCommand::Start(start_cmd) => start(network_logger, start_cmd).await,
        args_mod::SubCommand::Stop(_) => stop(network_logger).await,
    }
}

pub async fn start(
    network_logger: fmetrics::MetricsLoggerProxy,
    cmd: args_mod::StartCommand,
) -> Result<()> {
    let interval_ms = cmd.interval.as_millis() as u32;

    // Dispatch to MetricsLogger.StartLogging or MetricsLogger.StartLoggingForever,
    // depending on whether a logging duration is specified.
    let result = if let Some(duration) = cmd.duration {
        let duration_ms = duration.as_millis() as u32;
        network_logger
            .start_logging(
                "ffx_network",
                &mut vec![&mut Metric::NetworkActivity(NetworkActivity { interval_ms })]
                    .into_iter(),
                duration_ms,
                cmd.output_to_syslog,
                false,
            )
            .await?
    } else {
        network_logger
            .start_logging_forever(
                "ffx_network",
                &mut vec![&mut Metric::NetworkActivity(NetworkActivity { interval_ms })]
                    .into_iter(),
                cmd.output_to_syslog,
                false,
            )
            .await?
    };

    match result {
        Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval) => ffx_bail!(
            "MetricsLogger.StartLogging received an invalid sampling interval. \n\
            Please check if `interval` meets the following requirements: \n\
            1) Must be smaller than `duration` if `duration` is specified; \n\
            2) Must not be smaller than 500ms if `output_to_syslog` is enabled."
        ),
        Err(fmetrics::MetricsLoggerError::AlreadyLogging) => ffx_bail!(
            "Ffx network activity is already active. Use \"stop\" subcommand to stop the active \
            loggingg manually."
        ),
        Err(fmetrics::MetricsLoggerError::NoDrivers) => {
            ffx_bail!("This device has no compatible network driver.")
        }
        Err(fmetrics::MetricsLoggerError::TooManyActiveClients) => ffx_bail!(
            "MetricsLogger is running too many clients. Retry after any other client is stopped."
        ),
        Err(fmetrics::MetricsLoggerError::Internal) => {
            ffx_bail!("Request failed due to an internal error. Check syslog for more details.")
        }
        _ => Ok(()),
    }
}

pub async fn stop(network_logger: fmetrics::MetricsLoggerProxy) -> Result<()> {
    if !network_logger.stop_logging("ffx_network").await? {
        ffx_bail!("Stop logging returned false; Check if logging is already inactive.");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_metricslogger_test::{self as fmetrics, Metric, NetworkActivity},
        futures::channel::mpsc,
        std::time::Duration,
    };

    // Create a metrics-logger that expects a specific request type (Start, StartForever, or
    // Stop), and returns a specific error
    macro_rules! make_proxy {
        ($request_type:tt, $error_type:tt) => {
            setup_fake_network_logger(move |req| match req {
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

    /// Confirms that the start logging request is dispatched to FIDL requests as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_dispatch_start_logging() {
        // Start logging: interval=1s, duration=4s
        let args = args_mod::StartCommand {
            interval: ONE_SEC,
            duration: Some(4 * ONE_SEC),
            output_to_syslog: false,
        };
        let (mut sender, mut receiver) = mpsc::channel(1);
        let proxy = setup_fake_network_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StartLogging {
                client_id,
                metrics,
                duration_ms,
                output_samples_to_syslog,
                output_stats_to_syslog,
                responder,
            } => {
                assert_eq!(String::from("ffx_network"), client_id);
                assert_eq!(metrics.len(), 1);
                assert_eq!(
                    metrics[0],
                    Metric::NetworkActivity(NetworkActivity { interval_ms: 1000 }),
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
        start(proxy, args).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));
    }

    /// Confirms that the start logging forever request is dispatched to FIDL requests as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_dispatch_start_logging_forever() {
        // Start logging: interval=1s, duration=forever
        let args =
            args_mod::StartCommand { interval: ONE_SEC, duration: None, output_to_syslog: false };
        let (mut sender, mut receiver) = mpsc::channel(1);
        let proxy = setup_fake_network_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StartLoggingForever {
                client_id,
                metrics,
                output_samples_to_syslog,
                output_stats_to_syslog,
                responder,
                ..
            } => {
                assert_eq!(String::from("ffx_network"), client_id);
                assert_eq!(metrics.len(), 1);
                assert_eq!(
                    metrics[0],
                    Metric::NetworkActivity(NetworkActivity { interval_ms: 1000 }),
                );
                assert_eq!(output_samples_to_syslog, false);
                assert_eq!(output_stats_to_syslog, false);
                let mut result = Ok(());
                responder.send(&mut result).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StartLoggingForever; got {:?}", req),
        });
        start(proxy, args).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));
    }

    /// Confirms that the stop logging request is dispatched to FIDL requests as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_dispatch_stop_logging() {
        // Stop logging
        let (mut sender, mut receiver) = mpsc::channel(1);
        let proxy = setup_fake_network_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StopLogging { client_id, responder } => {
                assert_eq!(String::from("ffx_network"), client_id);
                responder.send(true).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StopLogging; got {:?}", req),
        });
        stop(proxy).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_stop_logging_error() {
        let proxy = setup_fake_network_logger(move |req| match req {
            fmetrics::MetricsLoggerRequest::StopLogging { responder, .. } => {
                responder.send(false).unwrap();
            }
            _ => panic!("Expected MetricsLoggerRequest::StopLogging; got {:?}", req),
        });
        let error = stop(proxy).await.unwrap_err();
        assert!(error.to_string().contains("Stop logging returned false"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_interval_error() {
        let args = args_mod::StartCommand {
            interval: ONE_SEC,
            duration: Some(2 * ONE_SEC),
            output_to_syslog: false,
        };
        let proxy = make_proxy!(StartLogging, InvalidSamplingInterval);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("invalid sampling interval"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_interval_error() {
        let args =
            args_mod::StartCommand { interval: ONE_SEC, duration: None, output_to_syslog: false };
        let proxy = make_proxy!(StartLoggingForever, InvalidSamplingInterval);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("invalid sampling interval"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_already_active_error() {
        let args = args_mod::StartCommand {
            interval: ONE_SEC,
            duration: Some(2 * ONE_SEC),
            output_to_syslog: false,
        };
        let proxy = make_proxy!(StartLogging, AlreadyLogging);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("already active"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_already_active_error() {
        let args =
            args_mod::StartCommand { interval: ONE_SEC, duration: None, output_to_syslog: false };
        let proxy = make_proxy!(StartLoggingForever, AlreadyLogging);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("already active"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_too_many_clients_error() {
        let args = args_mod::StartCommand {
            interval: ONE_SEC,
            duration: Some(2 * ONE_SEC),
            output_to_syslog: false,
        };
        let proxy = make_proxy!(StartLogging, TooManyActiveClients);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("too many clients"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_too_many_clients_error() {
        let args =
            args_mod::StartCommand { interval: ONE_SEC, duration: None, output_to_syslog: false };
        let proxy = make_proxy!(StartLoggingForever, TooManyActiveClients);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("too many clients"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_no_driver_error() {
        let args = args_mod::StartCommand {
            interval: ONE_SEC,
            duration: Some(2 * ONE_SEC),
            output_to_syslog: false,
        };
        let proxy = make_proxy!(StartLogging, NoDrivers);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("no compatible network driver"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_no_driver_error() {
        let args =
            args_mod::StartCommand { interval: ONE_SEC, duration: None, output_to_syslog: false };
        let proxy = make_proxy!(StartLoggingForever, NoDrivers);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("no compatible network driver"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_internal_error() {
        let args = args_mod::StartCommand {
            interval: ONE_SEC,
            duration: Some(2 * ONE_SEC),
            output_to_syslog: false,
        };
        let proxy = make_proxy!(StartLogging, Internal);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("an internal error"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_start_logging_forever_internal_error() {
        let args =
            args_mod::StartCommand { interval: ONE_SEC, duration: None, output_to_syslog: false };
        let proxy = make_proxy!(StartLoggingForever, Internal);
        let error = start(proxy, args).await.unwrap_err();
        assert!(error.to_string().contains("an internal error"));
    }
}
