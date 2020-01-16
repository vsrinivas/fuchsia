// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_cobalt::{LoggerFactoryMarker, LoggerProxy},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog as syslog, fuchsia_zircon as zx,
    session_framework_metrics_registry::cobalt_registry::{self as metrics},
};

/// Creates a LoggerProxy connected to Cobalt.
///
/// The connection is performed in a Future run on the global executor, but the `LoggerProxy`
/// can be used immediately.
///
/// # Returns
/// `LoggerProxy` for log messages to be sent to.
pub fn get_logger() -> Result<LoggerProxy, Error> {
    let (logger_proxy, server_end) =
        fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
    let logger_factory = connect_to_service::<LoggerFactoryMarker>()
        .context("Failed to connect to the Cobalt LoggerFactory")?;

    fasync::spawn(async move {
        if let Err(e) =
            logger_factory.create_logger_from_project_id(metrics::PROJECT_ID, server_end).await
        {
            syslog::fx_log_err!("Failed to create Cobalt logger: {}", e);
        }
    });

    Ok(logger_proxy)
}

/// Reports the time elapsed while launching a session.
///
/// # Parameters
/// - `logger_proxy`: The cobalt logger.
/// - `session_url`: The url of the session.
/// - `start_time`: The time when session_manager starts launching a session.
/// - `end_time`: The time when session_manager has bound to a session. This must be strictly after
///               `start_time`.
///
/// # Returns
/// `Ok` if the time elapsed was logged successfully.
pub async fn log_session_launch_time(
    logger_proxy: LoggerProxy,
    session_url: &str,
    start_time: zx::Time,
    end_time: zx::Time,
) -> Result<(), Error> {
    let elapsed_time = (end_time - start_time).into_micros();
    if elapsed_time < 0 {
        return Err(format_err!("End time must be after start time."));
    }

    logger_proxy
        .log_elapsed_time(
            metrics::SESSION_LAUNCH_TIME_METRIC_ID,
            metrics::SessionLaunchTimeMetricDimensionStatus::Success as u32,
            &session_url,
            elapsed_time,
        )
        .await
        .context("Could not log session launch time.")?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy_and_stream, fidl_fuchsia_cobalt::LoggerMarker,
        futures::TryStreamExt,
    };

    /// Tests that the right payload is sent to Cobalt when logging the session launch time.
    #[fasync::run_singlethreaded(test)]
    async fn test_log_session_launch_time() {
        let (logger_proxy, mut logger_server) =
            create_proxy_and_stream::<LoggerMarker>().expect("Failed to create Logger FIDL.");
        let start_time = zx::Time::from_nanos(0);
        let end_time = zx::Time::from_nanos(5000);
        let session_url = "fuchsia-pkg://fuchsia.com/whale_session#meta/whale_session.cm";

        fasync::spawn(async move {
            let _ = log_session_launch_time(logger_proxy, session_url, start_time, end_time).await;
        });

        if let Some(log_request) = logger_server.try_next().await.unwrap() {
            if let fidl_fuchsia_cobalt::LoggerRequest::LogElapsedTime {
                metric_id,
                event_code,
                component,
                elapsed_micros,
                responder: _,
            } = log_request
            {
                assert_eq!(metric_id, metrics::SESSION_LAUNCH_TIME_METRIC_ID);
                assert_eq!(
                    event_code,
                    metrics::SessionLaunchTimeMetricDimensionStatus::Success as u32
                );
                assert_eq!(component, session_url.to_string());
                assert_eq!(elapsed_micros, 5);
            } else {
                assert!(false);
            }
        } else {
            assert!(false);
        }
    }

    /// Tests that an error is raised if end_time < start_time.
    #[fasync::run_singlethreaded(test)]
    async fn test_log_session_launch_time_swap_start_end_time() {
        let (logger_proxy, _logger_server) =
            create_proxy_and_stream::<LoggerMarker>().expect("Failed to create Logger FIDL.");
        let start_time = zx::Time::from_nanos(0);
        let end_time = zx::Time::from_nanos(5000);
        let session_url = "fuchsia-pkg://fuchsia.com/whale_session#meta/whale_session.cm";

        assert!(log_session_launch_time(logger_proxy, session_url, end_time, start_time)
            .await
            .is_err());
    }
}
