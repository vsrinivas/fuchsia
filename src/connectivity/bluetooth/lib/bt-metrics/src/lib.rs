// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_metrics as metrics;
use fuchsia_async as fasync;
use tracing::{info, warn};

pub use bt_metrics_registry::*;

/// Connects to the MetricEventLoggerFactory service to create a
/// MetricEventLoggerProxy for the caller.
pub fn create_metrics_logger() -> Result<metrics::MetricEventLoggerProxy, Error> {
    let factory_proxy =
        fuchsia_component::client::connect_to_protocol::<metrics::MetricEventLoggerFactoryMarker>()
            .context("connecting to metrics")?;

    let (cobalt_proxy, cobalt_server) =
        fidl::endpoints::create_proxy::<metrics::MetricEventLoggerMarker>()
            .context("creating MetricEventLoggerProxy")?;

    let project_spec = metrics::ProjectSpec {
        customer_id: None, // defaults to fuchsia
        project_id: Some(PROJECT_ID),
        ..metrics::ProjectSpec::EMPTY
    };

    fasync::Task::spawn(async move {
        match factory_proxy.create_metric_event_logger(project_spec, cobalt_server).await {
            Err(e) => warn!("FIDL failure setting up event logger: {e:?}"),
            Ok(Err(e)) => warn!("CreateMetricEventLogger failure: {e:?}"),
            // TODO(fxbug.dev/114705): remove "ok" logs
            Ok(Ok(())) => info!("Metric logger connected"),
        }
    })
    .detach();

    Ok(cobalt_proxy)
}

pub fn log_on_failure(result: Result<Result<(), metrics::Error>, fidl::Error>) {
    match result {
        Ok(Ok(())) => (),
        e => warn!("failed to log metrics: {:?}", e),
    };
}

/// Test-only
pub fn respond_to_metrics_req_for_test(
    request: metrics::MetricEventLoggerRequest,
) -> metrics::MetricEvent {
    match request {
        metrics::MetricEventLoggerRequest::LogOccurrence {
            metric_id,
            count,
            event_codes,
            responder,
        } => {
            let _ = responder.send(&mut Ok(())).unwrap();
            metrics::MetricEvent {
                metric_id,
                event_codes,
                payload: metrics::MetricEventPayload::Count(count),
            }
        }
        metrics::MetricEventLoggerRequest::LogInteger {
            metric_id,
            value,
            event_codes,
            responder,
        } => {
            let _ = responder.send(&mut Ok(())).unwrap();
            metrics::MetricEvent {
                metric_id,
                event_codes,
                payload: metrics::MetricEventPayload::IntegerValue(value),
            }
        }
        _ => panic!("unexpected logging to Cobalt"),
    }
}
