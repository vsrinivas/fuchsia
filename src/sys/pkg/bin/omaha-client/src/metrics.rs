// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
#[cfg(test)]
use fidl_fuchsia_cobalt::CobaltEvent;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use futures::prelude::*;
use log::{info, warn};
use omaha_client::metrics::{Metrics, MetricsReporter};
use std::{convert::TryFrom, time::Duration};

/// A MetricsReporter trait implementation that send metrics to Cobalt.
pub struct CobaltMetricsReporter {
    cobalt_sender: CobaltSender,
}

impl CobaltMetricsReporter {
    pub fn new() -> (Self, impl Future<Output = ()>) {
        let (cobalt_sender, fut) = CobaltConnector::default()
            .serve(ConnectionType::project_id(mos_metrics_registry::PROJECT_ID));
        (CobaltMetricsReporter { cobalt_sender }, fut)
    }

    #[cfg(test)]
    fn new_mock() -> (Self, futures::channel::mpsc::Receiver<CobaltEvent>) {
        let (sender, receiver) = futures::channel::mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);
        (CobaltMetricsReporter { cobalt_sender }, receiver)
    }
}

fn duration_to_cobalt_micros(duration: Duration, metric_name: &str) -> Option<i64> {
    if let Ok(micros) = i64::try_from(duration.as_micros()) {
        Some(micros)
    } else {
        warn!("Unable to report {} due to overflow: {:?}", metric_name, duration);
        None
    }
}

impl MetricsReporter for CobaltMetricsReporter {
    fn report_metrics(&mut self, metrics: Metrics) -> Result<(), Error> {
        info!("Reporting metrics to Cobalt: {:?}", metrics);
        match metrics {
            Metrics::UpdateCheckResponseTime { response_time, successful } => {
                if let Some(response_time) =
                    duration_to_cobalt_micros(response_time, "Metrics::UpdateCheckResponseTime")
                {
                    self.cobalt_sender.log_elapsed_time(
                        mos_metrics_registry::UPDATE_CHECK_RESPONSE_TIME_METRIC_ID,
                        match successful {
                            true => {
                                mos_metrics_registry::UpdateCheckResponseTimeMetricDimensionResult::Success
                            }
                            false => {
                                mos_metrics_registry::UpdateCheckResponseTimeMetricDimensionResult::Failed
                            }
                        },
                        response_time,
                    );
                }
            }
            Metrics::UpdateCheckInterval(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::UpdateCheckInterval")
                {
                    self.cobalt_sender.log_elapsed_time(
                        mos_metrics_registry::UPDATE_CHECK_INTERVAL_METRIC_ID,
                        mos_metrics_registry::UpdateCheckIntervalMetricDimensionResult::Success,
                        duration,
                    );
                }
            }
            Metrics::SuccessfulUpdateDuration(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::SuccessfulUpdateDuration")
                {
                    self.cobalt_sender.log_elapsed_time(
                        mos_metrics_registry::UPDATE_DURATION_METRIC_ID,
                        mos_metrics_registry::UpdateDurationMetricDimensionResult::Success,
                        duration,
                    );
                }
            }
            Metrics::FailedUpdateDuration(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::FailedUpdateDuration")
                {
                    self.cobalt_sender.log_elapsed_time(
                        mos_metrics_registry::UPDATE_DURATION_METRIC_ID,
                        mos_metrics_registry::UpdateDurationMetricDimensionResult::Failed,
                        duration,
                    );
                }
            }
            Metrics::SuccessfulUpdateFromFirstSeen(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::SuccessfulUpdateFromFirstSeen")
                {
                    self.cobalt_sender.log_elapsed_time(
                        mos_metrics_registry::UPDATE_DURATION_FROM_FIRST_SEEN_METRIC_ID,
                        mos_metrics_registry::UpdateDurationFromFirstSeenMetricDimensionResult::Success,
                        duration,
                    );
                }
            }
            Metrics::UpdateCheckFailureReason(reason) => {
                let event_code = reason as u32;
                self.cobalt_sender
                    .log_event(mos_metrics_registry::UPDATE_CHECK_FAILURE_METRIC_ID, event_code);
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::UPDATE_CHECK_FAILURE_COUNT_METRIC_ID,
                    event_code,
                    0,
                    1,
                );
            }
            Metrics::RequestsPerCheck { count, successful } => {
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::REQUESTS_PER_CHECK_METRIC_ID,
                    match successful {
                        true => {
                            mos_metrics_registry::RequestsPerCheckMetricDimensionResult::Success
                        }
                        false => {
                            mos_metrics_registry::RequestsPerCheckMetricDimensionResult::Failed
                        }
                    },
                    0,
                    count as i64,
                );
            }
            Metrics::AttemptsToSuccessfulCheck(count) => {
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::ATTEMPTS_TO_SUCCESSFUL_CHECK_METRIC_ID,
                    mos_metrics_registry::AttemptsToSuccessfulCheckMetricDimensionResult::Success,
                    0,
                    count as i64,
                );
            }
            Metrics::WaitedForRebootDuration(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::WaitedForRebootDuration")
                {
                    self.cobalt_sender.log_elapsed_time(
                        mos_metrics_registry::WAITED_FOR_REBOOT_DURATION_METRIC_ID,
                        mos_metrics_registry::WaitedForRebootDurationMetricDimensionResult::Success,
                        duration,
                    );
                }
            }
            Metrics::FailedBootAttempts(count) => {
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::FAILED_BOOT_ATTEMPTS_METRIC_ID,
                    mos_metrics_registry::FailedBootAttemptsMetricDimensionResult::Success,
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
    use cobalt_client::traits::AsEventCodes;
    use fidl_fuchsia_cobalt::{CountEvent, EventPayload};
    use omaha_client::metrics::UpdateCheckFailureReason;
    use std::time::Duration;

    fn assert_metric(metrics: Metrics, event: CobaltEvent) {
        let (mut reporter, mut receiver) = CobaltMetricsReporter::new_mock();
        reporter.report_metrics(metrics).unwrap();
        assert_eq!(receiver.try_next().unwrap().unwrap(), event);
    }

    #[test]
    fn test_report_update_check_response_time() {
        assert_metric(
            Metrics::UpdateCheckResponseTime {
                response_time: Duration::from_millis(10),
                successful: true,
            },
            CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_RESPONSE_TIME_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::UpdateCheckResponseTimeMetricDimensionResult::Success,
                ]
                .as_event_codes(),
                component: None,
                payload: EventPayload::ElapsedMicros(10 * 1000),
            },
        );

        assert_metric(
            Metrics::UpdateCheckResponseTime {
                response_time: Duration::from_millis(10),
                successful: false,
            },
            CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_RESPONSE_TIME_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::UpdateCheckResponseTimeMetricDimensionResult::Failed,
                ]
                .as_event_codes(),
                component: None,
                payload: EventPayload::ElapsedMicros(10 * 1000),
            },
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
                ]
                .as_event_codes(),
                component: None,
                payload: EventPayload::Event(fidl_fuchsia_cobalt::Event),
            }
        );
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_FAILURE_COUNT_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::UpdateCheckFailureMetricDimensionReason::Configuration
                        as u32
                ],
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 1
                }),
            }
        );
    }

    #[test]
    fn test_report_requests_per_check() {
        assert_metric(
            Metrics::RequestsPerCheck { count: 3, successful: true },
            CobaltEvent {
                metric_id: mos_metrics_registry::REQUESTS_PER_CHECK_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::RequestsPerCheckMetricDimensionResult::Success,
                ]
                .as_event_codes(),
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 3,
                }),
            },
        );
    }

    #[test]
    fn test_report_attempts_to_successful_check() {
        assert_metric(
            Metrics::AttemptsToSuccessfulCheck(3),
            CobaltEvent {
                metric_id: mos_metrics_registry::ATTEMPTS_TO_SUCCESSFUL_CHECK_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::AttemptsToSuccessfulCheckMetricDimensionResult::Success,
                ]
                .as_event_codes(),
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 3,
                }),
            },
        );
    }

    #[test]
    fn test_duration_to_cobalt_metrics() {
        assert_eq!(duration_to_cobalt_micros(Duration::from_micros(0), "test"), Some(0));
        assert_eq!(
            duration_to_cobalt_micros(Duration::from_micros(std::i64::MAX as u64), "test"),
            Some(std::i64::MAX)
        );
        assert_eq!(duration_to_cobalt_micros(Duration::from_micros(std::u64::MAX), "test"), None);
    }
}
