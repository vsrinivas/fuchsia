// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, errors::ffx_bail, ffx_core::ffx_plugin,
    ffx_temperature_logger_args as args_mod, fidl_fuchsia_thermal_test as fthermal,
};

#[ffx_plugin(
    fthermal::TemperatureLoggerProxy = "core/temperature-logger:expose:fuchsia.thermal.test.TemperatureLogger"
)]
pub async fn logger(
    temperature_logger: fthermal::TemperatureLoggerProxy,
    cmd: args_mod::Command,
) -> Result<()> {
    match cmd.subcommand {
        args_mod::Subcommand::Start(start_cmd) => start(temperature_logger, start_cmd).await,
        args_mod::Subcommand::Stop(_) => stop(temperature_logger).await,
    }
}

pub async fn start(
    temperature_logger: fthermal::TemperatureLoggerProxy,
    cmd: args_mod::StartCommand,
) -> Result<()> {
    if cmd.interval.is_zero() {
        ffx_bail!("Interval must be nonzero.");
    }
    if let Some(duration) = cmd.duration {
        if duration.is_zero() {
            ffx_bail!("Duration, if specified, must be nonzero.");
        }
        if cmd.interval > duration {
            ffx_bail!("Interval ({:?}) must not exceed duration ({:?})", cmd.interval, duration);
        }
    }

    let interval_ms = cmd.interval.as_millis() as u32;

    // Dispatch to TemperatureLogger.StartLogging or TemperatureLogger.StartLoggingForever,
    // depending on whether a logging duration is specified.
    if let Some(duration) = cmd.duration {
        let duration_ms = duration.as_millis() as u32;
        let result = temperature_logger.start_logging(interval_ms, duration_ms).await?;
        match result {
            Err(fthermal::TemperatureLoggerError::InvalidArgument) => ffx_bail!(
                "TemperatureLogger.StartLogging received an invalid argument \
                (interval_ms={}, duration_ms={})",
                interval_ms,
                duration_ms
            ),
            Err(fthermal::TemperatureLoggerError::AlreadyLogging) => ffx_bail!(
                "Temperature logging is already active. Use \"stop\" subcommand to stop manually."
            ),
            Ok(()) => Ok(()),
        }
    } else {
        let result = temperature_logger.start_logging_forever(interval_ms).await?;
        match result {
            Err(fthermal::TemperatureLoggerError::InvalidArgument) => ffx_bail!(
                "TemperatureLogger.StartLoggingForever received an invalid argument \
                (interval_ms={})",
                interval_ms,
            ),
            Err(fthermal::TemperatureLoggerError::AlreadyLogging) => ffx_bail!(
                "Temperature logging is already active. Use \"stop\" subcommand to stop manually."
            ),
            Ok(()) => Ok(()),
        }
    }
}

pub async fn stop(temperature_logger: fthermal::TemperatureLoggerProxy) -> Result<()> {
    temperature_logger.stop_logging().await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, futures::channel::mpsc, std::time::Duration};

    const ONE_SEC: Duration = Duration::from_secs(1);

    /// Verify that invalid arguments are rejected
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_invalid_args() {
        // Zero interval
        let result = start(
            setup_fake_temperature_logger(|_| {}),
            args_mod::StartCommand { interval: 0 * ONE_SEC, duration: Some(ONE_SEC) },
        )
        .await;
        assert!(result.is_err());

        // Zero duration
        let result = start(
            setup_fake_temperature_logger(|_| {}),
            args_mod::StartCommand { interval: ONE_SEC, duration: Some(0 * ONE_SEC) },
        )
        .await;
        assert!(result.is_err());

        // Interval exceeds duration
        let result = start(
            setup_fake_temperature_logger(|_| {}),
            args_mod::StartCommand { interval: 2 * ONE_SEC, duration: Some(ONE_SEC) },
        )
        .await;
        assert!(result.is_err());
    }

    /// Confirms that commandline args are dispatched to FIDL requests as expected.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_dispatch() {
        // Start logging: interval=1s, duration=2s
        let args = args_mod::StartCommand { interval: ONE_SEC, duration: Some(2 * ONE_SEC) };
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_temperature_logger(move |req| match req {
            fthermal::TemperatureLoggerRequest::StartLogging { responder, .. } => {
                let mut result = Ok(());
                responder.send(&mut result).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected TemperatureLoggerRequest::StartLogging; got {:?}", req),
        });
        start(logger, args).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));

        // Start logging: interval=1s, duration=forever
        let args = args_mod::StartCommand { interval: ONE_SEC, duration: None };
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_temperature_logger(move |req| match req {
            fthermal::TemperatureLoggerRequest::StartLoggingForever { responder, .. } => {
                let mut result = Ok(());
                responder.send(&mut result).unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected TemperatureLoggerRequest::StartLoggingForever; got {:?}", req),
        });
        start(logger, args).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));

        // Stop logging
        let (mut sender, mut receiver) = mpsc::channel(1);
        let logger = setup_fake_temperature_logger(move |req| match req {
            fthermal::TemperatureLoggerRequest::StopLogging { responder, .. } => {
                responder.send().unwrap();
                sender.try_send(()).unwrap();
            }
            _ => panic!("Expected TemperatureLoggerRequest::StopLogging; got {:?}", req),
        });
        stop(logger).await.unwrap();
        assert_matches!(receiver.try_next().unwrap(), Some(()));
    }

    // Confirms that errors returned by temperature-logger are handled reasonably.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_errors() {
        // Create a temperature-logger that expects a specific request type (Start, StartForever, or
        // Stop), and returns a specific error
        macro_rules! make_logger {
            ($request_type:tt, $error_type:tt) => {
                setup_fake_temperature_logger(move |req| match req {
                    fthermal::TemperatureLoggerRequest::$request_type { responder, .. } => {
                        let mut result = Err(fthermal::TemperatureLoggerError::$error_type);
                        responder.send(&mut result).unwrap();
                    }
                    _ => panic!(
                        "Expected TemperatureLoggerRequest::{}; got {:?}",
                        stringify!($request_type),
                        req
                    ),
                })
            };
        }

        let args = args_mod::StartCommand { interval: ONE_SEC, duration: Some(2 * ONE_SEC) };
        let logger = make_logger!(StartLogging, InvalidArgument);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("invalid argument"));

        let args = args_mod::StartCommand { interval: ONE_SEC, duration: Some(2 * ONE_SEC) };
        let logger = make_logger!(StartLogging, AlreadyLogging);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("already active"));

        let args = args_mod::StartCommand { interval: ONE_SEC, duration: None };
        let logger = make_logger!(StartLoggingForever, InvalidArgument);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("invalid argument"));

        let args = args_mod::StartCommand { interval: ONE_SEC, duration: None };
        let logger = make_logger!(StartLoggingForever, AlreadyLogging);
        let error = start(logger, args).await.unwrap_err();
        assert!(error.to_string().contains("already active"));
    }
}
