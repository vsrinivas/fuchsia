// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_metrics::{
        MetricEventLoggerFactoryMarker, MetricEventLoggerFactoryProxy, MetricEventLoggerProxy,
        ProjectSpec,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    recovery_metrics_registry::cobalt_registry as metrics,
    tracing::error,
};

/// Creates a LoggerProxy connected to Cobalt.
///
/// The connection is performed in a Future run on the global executor, but the `LoggerProxy`
/// can be used immediately.
///
/// This function takes a `MetricEventLoggerFactoryProxy` as argument, so it's testable.
///
/// # Returns
/// `LoggerProxy` for log messages to be sent to.
pub fn get_logger_from_factory(
    factory_proxy: MetricEventLoggerFactoryProxy,
) -> Result<MetricEventLoggerProxy, Error> {
    let (logger_proxy, server_end) =
        fidl::endpoints::create_proxy().context("Failed to create endpoints")?;
    fasync::Task::spawn(async move {
        if let Err(err) = factory_proxy
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

/// Creates a LoggerProxy connected to Cobalt.
///
/// The connection is performed in a Future run on the global executor, but the `LoggerProxy`
/// can be used immediately.
///
/// # Returns
/// `LoggerProxy` for log messages to be sent to.
pub fn get_logger() -> Result<MetricEventLoggerProxy, Error> {
    let logger_factory = connect_to_protocol::<MetricEventLoggerFactoryMarker>()
        .context("Failed to connect to the Cobalt MetricEventLoggerFactory")?;

    get_logger_from_factory(logger_factory)
}

/// Reports the duration of the OTA download.
///
/// # Parameters
/// - `logger_proxy`: The cobalt logger.
/// - `duration`: seconds
///
/// # Returns
/// `Ok` if the duration was logged successfully.
pub async fn log_ota_duration(
    logger_proxy: &MetricEventLoggerProxy,
    duration: i64,
) -> Result<(), Error> {
    if duration < 0 {
        return Err(format_err!("duration must not be negative"));
    }

    logger_proxy
        .log_integer(metrics::OTA_DOWNLOAD_DURATION_METRIC_ID, duration, &[])
        .await
        .context("Could not log ota dowload duration.")?
        .map_err(|e| format_err!("Logging ota download duration returned an error: {:?}", e))
}

/// Reports the Recovery stages
///
/// # Parameters
/// - `logger_proxy`: The cobalt logger.
/// - `code`:
///      refer to the metrics.yaml
///
/// # Returns
/// `Ok` if the status was logged successfully.
pub async fn log_recovery_stage(
    logger_proxy: &MetricEventLoggerProxy,
    status: metrics::RecoveryEventMetricDimensionResult,
) -> Result<(), Error> {
    logger_proxy
        .log_occurrence(metrics::RECOVERY_EVENT_METRIC_ID, 1, &[status as u32])
        .await
        .context("Could not log recovery stage event.")?
        .map_err(|e| format_err!("Logging recovery stage event returned an error: {:?}", e))
}

// Call cobalt log functions and checks error code
#[macro_export]
macro_rules! log_metric {
    ($func_name:expr,$arg:expr) => {
        if let Ok(cobalt_logger) = cobalt::get_logger() {
            if let Err(err) = $func_name(&cobalt_logger, $arg).await {
                eprintln!("Failed to log metric ({}): {:?}", stringify!($func_name), err)
            }
        }
    };
}

pub(crate) use log_metric;

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_metrics::{
            MetricEvent, MetricEventLoggerMarker, MetricEventLoggerRequest, MetricEventPayload,
        },
        futures::TryStreamExt,
        mock_metrics::MockMetricEventLoggerFactory,
        std::sync::Arc,
    };

    /// Tests that the get_logger_from_factory can return the correct logger proxy
    #[fasync::run_singlethreaded(test)]
    async fn test_get_logger_from_factory() {
        let mock = Arc::new(MockMetricEventLoggerFactory::with_id(metrics::PROJECT_ID));
        let (factory, stream) =
            create_proxy_and_stream::<MetricEventLoggerFactoryMarker>().unwrap();
        let task = fasync::Task::spawn(mock.clone().run_logger_factory(stream));
        let logger = get_logger_from_factory(factory).unwrap();
        task.await;

        let mut event = MetricEvent {
            metric_id: 42,
            event_codes: vec![],
            payload: MetricEventPayload::Count(1),
        };
        logger.log_metric_events(&mut std::iter::once(&mut event)).await.unwrap().unwrap();
        let events = mock.wait_for_at_least_n_events_with_metric_id(1, 42).await;
        assert_eq!(events, vec![event]);
    }

    /// Tests that the right payload is sent to Cobalt when logging the ota download time.
    #[fasync::run_singlethreaded(test)]
    async fn test_log_ota_duration() {
        let (logger_proxy, mut logger_server) =
            create_proxy_and_stream::<MetricEventLoggerMarker>()
                .expect("Failed to create Logger FIDL.");
        let duration = 255;

        fasync::Task::spawn(async move {
            let _ = log_ota_duration(&logger_proxy, duration).await;
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
                assert_eq!(metric_id, metrics::OTA_DOWNLOAD_DURATION_METRIC_ID);
                assert!(event_codes.is_empty());
                assert_eq!(value, duration);
            } else {
                panic!("LogInteger failed");
            }
        } else {
            panic!("logger_server.try_next failed");
        }
    }

    /// Tests that an error is raised if duration < 0.
    #[fasync::run_singlethreaded(test)]
    async fn test_log_ota_negative_duration() {
        let (logger_proxy, _logger_server) = create_proxy_and_stream::<MetricEventLoggerMarker>()
            .expect("Failed to create Logger FIDL.");

        let result = log_ota_duration(&logger_proxy, -5).await;
        assert!(result.is_err());
        assert_eq!(format!("{}", result.unwrap_err()), "duration must not be negative");
    }

    /// Tests that the right payload is sent to Cobalt when logging the recovery stages.
    #[fasync::run_singlethreaded(test)]
    async fn test_log_recovery_stage() {
        let (logger_proxy, mut logger_server) =
            create_proxy_and_stream::<MetricEventLoggerMarker>()
                .expect("Failed to create Logger FIDL.");
        let status = metrics::RecoveryEventMetricDimensionResult::OtaStarted;

        fasync::Task::spawn(async move {
            let _ = log_recovery_stage(&logger_proxy, status).await;
        })
        .detach();

        if let Some(log_request) = logger_server.try_next().await.unwrap() {
            if let MetricEventLoggerRequest::LogOccurrence {
                metric_id,
                count,
                event_codes,
                responder: _,
            } = log_request
            {
                assert_eq!(metric_id, metrics::RECOVERY_EVENT_METRIC_ID);
                assert_eq!(
                    event_codes,
                    &[metrics::RecoveryEventMetricDimensionResult::OtaStarted as u32]
                );
                assert_eq!(count, 1);
            } else {
                panic!("LogOccurance failed");
            }
        } else {
            panic!("logger_server.try_next failed");
        }
    }
}
