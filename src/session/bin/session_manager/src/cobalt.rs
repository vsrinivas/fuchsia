// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_metrics::{MetricEventLoggerFactoryMarker, MetricEventLoggerProxy, ProjectSpec},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    session_framework_metrics_registry::cobalt_registry as metrics,
    tracing::error,
};

/// Creates a LoggerProxy connected to Cobalt.
///
/// The connection is performed in a Future run on the global executor, but the `LoggerProxy`
/// can be used immediately.
///
/// # Returns
/// `LoggerProxy` for log messages to be sent to.
pub fn get_logger() -> Result<MetricEventLoggerProxy, Error> {
    let (logger_proxy, server_end) =
        fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
    let logger_factory = connect_to_protocol::<MetricEventLoggerFactoryMarker>()
        .context("Failed to connect to the Cobalt MetricEventLoggerFactory")?;

    fasync::Task::spawn(async move {
        if let Err(err) = logger_factory
            .create_metric_event_logger(
                ProjectSpec { project_id: Some(metrics::PROJECT_ID), ..ProjectSpec::EMPTY },
                server_end,
            )
            .await
        {
            error!(%err, "Failed to create Cobalt logger");
        }
    })
    .detach();

    Ok(logger_proxy)
}

/// Reports the time elapsed while launching a session.
///
/// # Parameters
/// - `logger_proxy`: The cobalt logger.
/// - `start_time`: The time when session_manager starts launching a session.
/// - `end_time`: The time when session_manager has bound to a session. This must be strictly after
///               `start_time`.
///
/// # Returns
/// `Ok` if the time elapsed was logged successfully.
pub async fn log_session_launch_time(
    logger_proxy: MetricEventLoggerProxy,
    start_time: zx::Time,
    end_time: zx::Time,
) -> Result<(), Error> {
    let elapsed_time = (end_time - start_time).into_micros();
    if elapsed_time < 0 {
        return Err(format_err!("End time must be after start time."));
    }

    logger_proxy
        .log_integer(
            metrics::SESSION_LAUNCH_TIME_MIGRATED_METRIC_ID,
            elapsed_time,
            &[metrics::SessionLaunchTimeMigratedMetricDimensionStatus::Success as u32],
        )
        .await
        .context("Could not log session launch time.")?
        .map_err(|e| format_err!("Logging session launch time returned an error: {:?}", e))?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_metrics::{MetricEventLoggerMarker, MetricEventLoggerRequest},
        futures::TryStreamExt,
    };

    /// Tests that the right payload is sent to Cobalt when logging the session launch time.
    #[fasync::run_singlethreaded(test)]
    async fn test_log_session_launch_time() {
        let (logger_proxy, mut logger_server) =
            create_proxy_and_stream::<MetricEventLoggerMarker>()
                .expect("Failed to create Logger FIDL.");
        let start_time = zx::Time::from_nanos(0);
        let end_time = zx::Time::from_nanos(5000);

        fasync::Task::spawn(async move {
            let _ = log_session_launch_time(logger_proxy, start_time, end_time).await;
        })
        .detach();

        if let Some(log_request) = logger_server.try_next().await.unwrap() {
            if let MetricEventLoggerRequest::LogInteger {
                metric_id,
                value,
                event_codes,
                responder: _,
            } = log_request
            {
                assert_eq!(metric_id, metrics::SESSION_LAUNCH_TIME_MIGRATED_METRIC_ID);
                assert_eq!(
                    event_codes,
                    vec![metrics::SessionLaunchTimeMigratedMetricDimensionStatus::Success as u32]
                );
                assert_eq!(value, 5);
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
        let (logger_proxy, _logger_server) = create_proxy_and_stream::<MetricEventLoggerMarker>()
            .expect("Failed to create Logger FIDL.");
        let start_time = zx::Time::from_nanos(0);
        let end_time = zx::Time::from_nanos(5000);

        assert!(log_session_launch_time(logger_proxy, end_time, start_time).await.is_err());
    }
}
