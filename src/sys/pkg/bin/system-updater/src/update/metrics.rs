// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::update::{config::Initiator, FetchError, PrepareError, ResolveError},
    anyhow::{format_err, Context, Error},
    cobalt_client::traits::AsEventCode,
    cobalt_sw_delivery_registry as metrics,
    fidl_contrib::{
        protocol_connector::{ConnectedProtocol, ProtocolSender},
        ProtocolConnector,
    },
    fidl_fuchsia_metrics::{
        MetricEvent, MetricEventLoggerFactoryMarker, MetricEventLoggerProxy, ProjectSpec,
    },
    fuchsia_cobalt_builders::MetricEventExt,
    fuchsia_component::client::connect_to_protocol,
    futures::{
        future::{self, Future},
        FutureExt,
    },
    std::{
        convert::TryInto,
        time::{Duration, Instant, SystemTime},
    },
};

// See $FUCHSIA_OUT_DIR/gen/src/sys/pkg/bin/amber/cobalt_sw_delivery_registry.rs for more info.
pub use {
    metrics::SoftwareDeliveryMetricDimensionPhase as Phase,
    metrics::SoftwareDeliveryMetricDimensionStatusCode as StatusCode,
};

pub struct CobaltConnectedService;
impl ConnectedProtocol for CobaltConnectedService {
    type Protocol = MetricEventLoggerProxy;
    type ConnectError = Error;
    type Message = MetricEvent;
    type SendError = Error;

    fn get_protocol(&mut self) -> future::BoxFuture<'_, Result<MetricEventLoggerProxy, Error>> {
        async {
            let (logger_proxy, server_end) =
                fidl::endpoints::create_proxy().context("failed to create proxy endpoints")?;
            let metric_event_logger_factory =
                connect_to_protocol::<MetricEventLoggerFactoryMarker>()
                    .context("Failed to connect to fuchsia::metrics::MetricEventLoggerFactory")?;

            metric_event_logger_factory
                .create_metric_event_logger(
                    ProjectSpec { project_id: Some(metrics::PROJECT_ID), ..ProjectSpec::EMPTY },
                    server_end,
                )
                .await?
                .map_err(|e| format_err!("Connection to MetricEventLogger refused {e:?}"))?;

            Ok(logger_proxy)
        }
        .boxed()
    }

    fn send_message<'a>(
        &'a mut self,
        protocol: &'a MetricEventLoggerProxy,
        mut msg: MetricEvent,
    ) -> future::BoxFuture<'a, Result<(), Error>> {
        async move {
            let fut = protocol.log_metric_events(&mut std::iter::once(&mut msg));
            fut.await?.map_err(|e| format_err!("Failed to log metric {e:?}"))?;
            Ok(())
        }
        .boxed()
    }
}

pub fn connect_to_cobalt() -> (Client, impl Future<Output = ()>) {
    let (cobalt, cobalt_fut) =
        ProtocolConnector::new(CobaltConnectedService).serve_and_log_errors();
    (Client(cobalt), cobalt_fut)
}

fn error_to_status_code(error: &fidl_fuchsia_pkg_ext::ResolveError) -> StatusCode {
    match *error {
        fidl_fuchsia_pkg_ext::ResolveError::Io => StatusCode::ErrorStorage,
        fidl_fuchsia_pkg_ext::ResolveError::NoSpace => StatusCode::ErrorStorageOutOfSpace,
        fidl_fuchsia_pkg_ext::ResolveError::AccessDenied => StatusCode::ErrorUntrustedTufRepo,
        fidl_fuchsia_pkg_ext::ResolveError::UnavailableBlob => StatusCode::ErrorNetworking,
        _ => StatusCode::Error,
    }
}

pub fn result_to_status_code(res: Result<(), &anyhow::Error>) -> StatusCode {
    match res {
        Ok(()) => StatusCode::Success,

        Err(e) => {
            if let Some(FetchError::Resolve(ResolveError::Error(error, _))) = e.downcast_ref() {
                error_to_status_code(error)
            } else if let Some(PrepareError::ResolveUpdate(ResolveError::Error(error, _))) =
                e.downcast_ref()
            {
                error_to_status_code(error)
            } else {
                // Fallback to a generic catch-all error status code when the error didn't contain
                // context indicating more clearly what type of error happened.
                StatusCode::Error
            }
        }
    }
}

impl AsEventCode for Initiator {
    fn as_event_code(&self) -> u32 {
        match self {
            Initiator::Automatic => {
                metrics::SoftwareDeliveryMetricDimensionInitiator::AutomaticUpdateCheck
            }
            Initiator::Manual => {
                metrics::SoftwareDeliveryMetricDimensionInitiator::UserInitiatedCheck
            }
        }
        .as_event_code()
    }
}

fn hour_of_day(when: SystemTime) -> u32 {
    use chrono::Timelike;

    let when = chrono::DateTime::<chrono::Utc>::from(when);
    // TODO insert UTC to local time conversion here...when that is possible to do.
    when.hour()
}

fn duration_to_micros(duration: Duration) -> i64 {
    duration.as_micros().try_into().unwrap_or(i64::MAX)
}

/// Attempts to determine the monotonic instant that correlates with the given wall time by
/// subtracting the duration from the given time to now (wall time) from now (monotonic time). If
/// `time` is in the future or it corresponds with an invalid monotonic time, returns None.
///
/// This conversion is flawed as there is no guarantee that the monotonic and wall clocks tick at
/// the same rate or that the system was up at the given wall time.
///
/// FIXME: switch start time to initially be based on monotonic time to remove the need for this
/// conversion.
pub fn system_time_to_monotonic_time(time: SystemTime) -> Option<Instant> {
    Instant::now().checked_sub(time.elapsed().ok()?)
}

#[derive(Debug, Clone)]
pub struct Client(ProtocolSender<MetricEvent>);

impl Client {
    pub fn log_ota_start(&mut self, initiator: Initiator, when: SystemTime) {
        self.0.send(
            MetricEvent::builder(metrics::OTA_START_MIGRATED_METRIC_ID)
                .with_event_codes((initiator, hour_of_day(when)))
                .as_occurrence(1),
        );
    }

    pub fn log_ota_result_attempt(
        &mut self,
        initiator: Initiator,
        attempts: u32,
        phase: Phase,
        status: StatusCode,
    ) {
        self.0.send(
            MetricEvent::builder(metrics::OTA_RESULT_ATTEMPTS_MIGRATED_METRIC_ID)
                .with_event_codes((initiator, phase, status))
                .as_occurrence(attempts.into()),
        );
    }

    pub fn log_ota_result_duration(
        &mut self,
        initiator: Initiator,
        phase: Phase,
        status: StatusCode,
        duration: Duration,
    ) {
        self.0.send(
            MetricEvent::builder(metrics::OTA_RESULT_DURATION_MIGRATED_METRIC_ID)
                .with_event_codes((initiator, phase, status))
                .as_integer(duration_to_micros(duration)),
        );
    }
}

#[cfg(test)]
mod tests {
    use {super::*, chrono::prelude::*};

    #[test]
    fn hour_of_day_returns_hour_of_day() {
        let date = Utc.ymd(1971, 1, 1);

        assert_eq!(hour_of_day(date.and_hms(4, 0, 0).into()), 4);
        assert_eq!(hour_of_day(date.and_hms(23, 0, 0).into()), 23);
    }
}
