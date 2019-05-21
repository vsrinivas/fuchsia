// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
#[cfg(test)]
use fidl_fuchsia_cobalt::CobaltEvent;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use futures::prelude::*;
use log::info;
use omaha_client::metrics::{Metrics, MetricsReporter};

/// A MetricsReporter trait implementation that send metrics to Cobalt.
pub struct CobaltMetricsReporter {
    cobalt_sender: CobaltSender,
}

impl CobaltMetricsReporter {
    pub fn new() -> (Self, impl Future<Output = ()>) {
        let (cobalt_sender, fut) = CobaltConnector::default()
            .serve(ConnectionType::project_name(mos_metrics_registry::PROJECT_NAME));
        (CobaltMetricsReporter { cobalt_sender }, fut)
    }

    #[cfg(test)]
    fn new_mock() -> (Self, futures::channel::mpsc::Receiver<CobaltEvent>) {
        let (sender, receiver) = futures::channel::mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);
        (CobaltMetricsReporter { cobalt_sender }, receiver)
    }
}

impl MetricsReporter for CobaltMetricsReporter {
    fn report_metrics(&mut self, metrics: Metrics) -> Result<(), Error> {
        info!("Reporting metrics to Cobalt: {:?}", metrics);
        match metrics {
            Metrics::UpdateCheckResponseTime(duration) => {
                self.cobalt_sender.log_elapsed_time(
                    mos_metrics_registry::UPDATE_CHECK_RESPONSE_TIME_METRIC_ID,
                    mos_metrics_registry::UpdateCheckResponseTimeMetricDimensionResult::Success
                        as u32,
                    duration.as_micros() as i64,
                );
            }
            Metrics::UpdateCheckInterval(duration) => {
                self.cobalt_sender.log_elapsed_time(
                    mos_metrics_registry::UPDATE_CHECK_INTERVAL_METRIC_ID,
                    mos_metrics_registry::UpdateCheckIntervalMetricDimensionResult::Success as u32,
                    duration.as_micros() as i64,
                );
            }
            Metrics::SuccessfulUpdateDuration(duration) => {
                self.cobalt_sender.log_elapsed_time(
                    mos_metrics_registry::UPDATE_DURATION_METRIC_ID,
                    mos_metrics_registry::UpdateDurationMetricDimensionResult::Success as u32,
                    duration.as_micros() as i64,
                );
            }
            Metrics::FailedUpdateDuration(duration) => {
                self.cobalt_sender.log_elapsed_time(
                    mos_metrics_registry::UPDATE_DURATION_METRIC_ID,
                    mos_metrics_registry::UpdateDurationMetricDimensionResult::Failed as u32,
                    duration.as_micros() as i64,
                );
            }
            Metrics::SuccessfulUpdateFromFirstSeen(duration) => {
                self.cobalt_sender.log_elapsed_time(
                    mos_metrics_registry::UPDATE_DURATION_FROM_FIRST_SEEN_METRIC_ID,
                    mos_metrics_registry::UpdateDurationFromFirstSeenMetricDimensionResult::Success
                        as u32,
                    duration.as_micros() as i64,
                );
            }
            Metrics::UpdateCheckFailureReason(reason) => {
                self.cobalt_sender
                    .log_event(mos_metrics_registry::UPDATE_CHECK_FAILURE_METRIC_ID, reason as u32);
            }
            Metrics::UpdateCheckRetries(count) => {
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::UPDATE_CHECK_RETRIES_METRIC_ID,
                    mos_metrics_registry::UpdateCheckRetriesMetricDimensionResult::Success as u32,
                    0,
                    count as i64,
                );
            }
            Metrics::AttemptsToSucceed(count) => {
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::ATTEMPTS_TO_SUCCEED_METRIC_ID,
                    mos_metrics_registry::AttemptsToSucceedMetricDimensionResult::Success as u32,
                    0,
                    count as i64,
                );
            }
            Metrics::WaitedForRebootDuration(duration) => {
                self.cobalt_sender.log_elapsed_time(
                    mos_metrics_registry::WAITED_FOR_REBOOT_DURATION_METRIC_ID,
                    mos_metrics_registry::WaitedForRebootDurationMetricDimensionResult::Success
                        as u32,
                    duration.as_micros() as i64,
                );
            }
            Metrics::FailedBootAttempts(count) => {
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::FAILED_BOOT_ATTEMPTS_METRIC_ID,
                    mos_metrics_registry::FailedBootAttemptsMetricDimensionResult::Success as u32,
                    0,
                    count as i64,
                );
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_cobalt::{CountEvent, EventPayload};
    use omaha_client::metrics::UpdateCheckFailureReason;
    use std::time::Duration;

    #[test]
    fn test_report_update_check_response_time() {
        let (mut reporter, mut receiver) = CobaltMetricsReporter::new_mock();
        reporter
            .report_metrics(Metrics::UpdateCheckResponseTime(Duration::from_millis(10)))
            .unwrap();
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_RESPONSE_TIME_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::UpdateCheckResponseTimeMetricDimensionResult::Success
                        as u32
                ],
                component: None,
                payload: EventPayload::ElapsedMicros(10 * 1000),
            }
        );
    }

    #[test]
    fn test_report_update_check_failure_reason() {
        let (mut reporter, mut receiver) = CobaltMetricsReporter::new_mock();
        reporter
            .report_metrics(Metrics::UpdateCheckFailureReason(
                UpdateCheckFailureReason::Configuration,
            ))
            .unwrap();
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_FAILURE_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::UpdateCheckFailureMetricDimensionReason::Configuration
                        as u32
                ],
                component: None,
                payload: EventPayload::Event(fidl_fuchsia_cobalt::Event),
            }
        );
    }

    #[test]
    fn test_report_update_check_retries() {
        let (mut reporter, mut receiver) = CobaltMetricsReporter::new_mock();
        reporter.report_metrics(Metrics::UpdateCheckRetries(3)).unwrap();
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_RETRIES_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::UpdateCheckRetriesMetricDimensionResult::Success as u32
                ],
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 3
                }),
            }
        );
    }

}
