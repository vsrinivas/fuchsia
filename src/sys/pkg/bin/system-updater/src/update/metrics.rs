// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::update::config::Initiator,
    cobalt_client::traits::AsEventCode,
    cobalt_sw_delivery_registry as metrics,
    futures::future::Future,
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

pub fn connect_to_cobalt() -> (Client, impl Future<Output = ()>) {
    let (cobalt, cobalt_fut) = fuchsia_cobalt::CobaltConnector::default()
        .serve(fuchsia_cobalt::ConnectionType::project_id(metrics::PROJECT_ID));
    (Client(cobalt), cobalt_fut)
}

pub fn result_to_status_code(res: Result<(), &anyhow::Error>) -> StatusCode {
    use fuchsia_zircon::Status;

    match res {
        Ok(()) => StatusCode::Success,

        Err(e) => {
            if let Some(crate::update::ResolveError::Status(status)) = e.downcast_ref() {
                match *status {
                    Status::IO => StatusCode::ErrorStorage,
                    Status::NO_SPACE => StatusCode::ErrorStorageOutOfSpace,
                    Status::ADDRESS_UNREACHABLE => StatusCode::ErrorUntrustedTufRepo,
                    Status::UNAVAILABLE => StatusCode::ErrorNetworking,
                    _ => StatusCode::Error,
                }
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
pub struct Client(fuchsia_cobalt::CobaltSender);

impl Client {
    pub fn log_ota_start(&mut self, target_version: &str, initiator: Initiator, when: SystemTime) {
        self.0.with_component().log_event_count(
            metrics::OTA_START_METRIC_ID,
            (initiator, hour_of_day(when)),
            target_version,
            0, // duration
            1, // count
        );
    }

    pub fn log_ota_result_attempt(
        &mut self,
        target_version: &str,
        initiator: Initiator,
        attempts: u32,
        phase: Phase,
        status: StatusCode,
    ) {
        self.0.with_component().log_event_count(
            metrics::OTA_RESULT_ATTEMPTS_METRIC_ID,
            (initiator, phase, status),
            target_version,
            0,
            attempts.into(), // "attempts" is not "count", but that's what the go impl does.
        );
    }

    pub fn log_ota_result_duration(
        &mut self,
        target_version: &str,
        initiator: Initiator,
        phase: Phase,
        status: StatusCode,
        duration: Duration,
    ) {
        self.0.with_component().log_elapsed_time(
            metrics::OTA_RESULT_DURATION_METRIC_ID,
            (initiator, phase, status),
            target_version,
            duration_to_micros(duration),
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
