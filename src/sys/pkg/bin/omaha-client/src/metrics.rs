// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
#[cfg(test)]
use fidl_fuchsia_cobalt::CobaltEvent;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use futures::prelude::*;
use log::{info, warn};
use omaha_client::{
    metrics::{ClockType, Metrics, MetricsReporter},
    protocol::request::{EventResult, EventType, InstallSource},
};
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

fn mos_event_type_from_event_type(
    t: EventType,
) -> mos_metrics_registry::OmahaEventLostMetricDimensionEventType {
    match t {
        EventType::Unknown => mos_metrics_registry::OmahaEventLostMetricDimensionEventType::Unknown,
        EventType::DownloadComplete => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventType::DownloadComplete
        }
        EventType::InstallComplete => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventType::InstallComplete
        }
        EventType::UpdateComplete => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventType::UpdateComplete
        }
        EventType::UpdateDownloadStarted => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventType::UpdateDownloadStarted
        }
        EventType::UpdateDownloadFinished => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventType::UpdateDownloadFinished
        }
        EventType::RebootedAfterUpdate => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventType::RebootedAfterUpdate
        }
    }
}

fn mos_event_result_from_event_result(
    r: EventResult,
) -> mos_metrics_registry::OmahaEventLostMetricDimensionEventResult {
    match r {
        EventResult::Error => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventResult::Error
        },
        EventResult::Success => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventResult::Success
        },
        EventResult::SuccessAndRestartRequired => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventResult::SuccessAndRestartRequired
        },
        EventResult::SuccessAndAppRestartRequired => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventResult::SuccessAndAppRestartRequired
        },
        EventResult::Cancelled => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventResult::Cancelled
        },
        EventResult::ErrorInSystemInstaller => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventResult::ErrorInSystemInstaller
        },
        EventResult::UpdateDeferred => {
            mos_metrics_registry::OmahaEventLostMetricDimensionEventResult::UpdateDeferred
        },
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
            Metrics::UpdateCheckInterval { interval, clock, install_source } => {
                if let Some(interval) =
                    duration_to_cobalt_micros(interval, "Metrics::UpdateCheckInterval")
                {
                    let clock = match clock {
                        ClockType::Monotonic => {
                            mos_metrics_registry::UpdateCheckIntervalMetricDimensionClock::Monotonic
                        }
                        ClockType::Wall => {
                            mos_metrics_registry::UpdateCheckIntervalMetricDimensionClock::Wall
                        }
                    };
                    let install_source = match install_source {
                        InstallSource::ScheduledTask => {
                            mos_metrics_registry::UpdateCheckIntervalMetricDimensionInitiator::ScheduledTask
                        }
                        InstallSource::OnDemand => {
                            mos_metrics_registry::UpdateCheckIntervalMetricDimensionInitiator::OnDemand
                        }
                    };
                    self.cobalt_sender.log_elapsed_time(
                        mos_metrics_registry::UPDATE_CHECK_INTERVAL_METRIC_ID,
                        (
                            mos_metrics_registry::UpdateCheckIntervalMetricDimensionResult::Success,
                            clock,
                            install_source,
                        ),
                        interval,
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
            Metrics::AttemptsToSuccessfulInstall { count, successful } => {
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::ATTEMPTS_PER_DEVICE_DAY_METRIC_ID,
                    if successful {
                        mos_metrics_registry::AttemptsPerDeviceDayMetricDimensionResult::Success
                    } else {
                        mos_metrics_registry::AttemptsPerDeviceDayMetricDimensionResult::Failed
                    },
                    0,
                    1,
                );

                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::ATTEMPTS_TO_REACH_SUCCESS_METRIC_ID,
                    if successful {
                        mos_metrics_registry::AttemptsToReachSuccessMetricDimensionResult::Success
                    } else {
                        mos_metrics_registry::AttemptsToReachSuccessMetricDimensionResult::Failed
                    },
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
            Metrics::OmahaEventLost(event) => {
                let event_type = mos_event_type_from_event_type(event.event_type);
                let result = mos_event_result_from_event_result(event.event_result);
                self.cobalt_sender.log_event_count(
                    mos_metrics_registry::OMAHA_EVENT_LOST_METRIC_ID,
                    (event_type, result),
                    0,
                    1,
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
    use fuchsia_async as fasync;
    use futures::stream::StreamExt;
    use omaha_client::{metrics::UpdateCheckFailureReason, protocol::request::Event};
    use std::time::Duration;

    async fn assert_metrics(metrics: Metrics, expected_events: &[CobaltEvent]) {
        let receiver = {
            let (mut reporter, receiver) = CobaltMetricsReporter::new_mock();
            reporter.report_metrics(metrics).unwrap();
            receiver
        };
        let actual_events = receiver.collect::<Vec<_>>().await;

        assert_eq!(actual_events, expected_events,);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_update_check_response_time() {
        assert_metrics(
            Metrics::UpdateCheckResponseTime {
                response_time: Duration::from_millis(10),
                successful: true,
            },
            &[CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_RESPONSE_TIME_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::UpdateCheckResponseTimeMetricDimensionResult::Success,
                ]
                .as_event_codes(),
                component: None,
                payload: EventPayload::ElapsedMicros(10 * 1000),
            }],
        )
        .await;

        assert_metrics(
            Metrics::UpdateCheckResponseTime {
                response_time: Duration::from_millis(10),
                successful: false,
            },
            &[CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_RESPONSE_TIME_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::UpdateCheckResponseTimeMetricDimensionResult::Failed,
                ]
                .as_event_codes(),
                component: None,
                payload: EventPayload::ElapsedMicros(10 * 1000),
            }],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_update_check_interval() {
        assert_metrics(
            Metrics::UpdateCheckInterval {
                interval: Duration::from_millis(10),
                clock: ClockType::Monotonic,
                install_source: InstallSource::OnDemand,
            },
            &[CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_INTERVAL_METRIC_ID,
                event_codes: (
                    mos_metrics_registry::UpdateCheckIntervalMetricDimensionResult::Success,
                    mos_metrics_registry::UpdateCheckIntervalMetricDimensionClock::Monotonic,
                    mos_metrics_registry::UpdateCheckIntervalMetricDimensionInitiator::OnDemand,
                )
                    .as_event_codes(),
                component: None,
                payload: EventPayload::ElapsedMicros(10 * 1000),
            }],
        )
        .await;

        assert_metrics(
            Metrics::UpdateCheckInterval {
                interval: Duration::from_millis(20),
                clock: ClockType::Wall,
                install_source: InstallSource::ScheduledTask,
            },
            &[CobaltEvent {
                metric_id: mos_metrics_registry::UPDATE_CHECK_INTERVAL_METRIC_ID,
                event_codes: (
                    mos_metrics_registry::UpdateCheckIntervalMetricDimensionResult::Success,
                    mos_metrics_registry::UpdateCheckIntervalMetricDimensionClock::Wall,
                    mos_metrics_registry::UpdateCheckIntervalMetricDimensionInitiator::ScheduledTask,
                )
                    .as_event_codes(),
                component: None,
                payload: EventPayload::ElapsedMicros(20 * 1000),
            }],
        ).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_update_check_failure_reason() {
        assert_metrics(
            Metrics::UpdateCheckFailureReason(UpdateCheckFailureReason::Configuration),
            &[
                CobaltEvent {
                    metric_id: mos_metrics_registry::UPDATE_CHECK_FAILURE_METRIC_ID,
                    event_codes: (
                    mos_metrics_registry::UpdateCheckFailureMetricDimensionReason::Configuration
                )
                    .as_event_codes(),
                    component: None,
                    payload: EventPayload::Event(fidl_fuchsia_cobalt::Event),
                },
                CobaltEvent {
                    metric_id: mos_metrics_registry::UPDATE_CHECK_FAILURE_COUNT_METRIC_ID,
                    event_codes: (
                        mos_metrics_registry::UpdateCheckFailureMetricDimensionReason::Configuration
                    ).as_event_codes(),
                    component: None,
                    payload: EventPayload::EventCount(CountEvent {
                        period_duration_micros: 0,
                        count: 1,
                    }),
                },
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_requests_per_check() {
        assert_metrics(
            Metrics::RequestsPerCheck { count: 3, successful: true },
            &[CobaltEvent {
                metric_id: mos_metrics_registry::REQUESTS_PER_CHECK_METRIC_ID,
                event_codes:
                    (mos_metrics_registry::RequestsPerCheckMetricDimensionResult::Success,)
                        .as_event_codes(),
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 3,
                }),
            }],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_attempts_to_successful_check() {
        assert_metrics(
            Metrics::AttemptsToSuccessfulCheck(3),
            &[CobaltEvent {
                metric_id: mos_metrics_registry::ATTEMPTS_TO_SUCCESSFUL_CHECK_METRIC_ID,
                event_codes: (
                    mos_metrics_registry::AttemptsToSuccessfulCheckMetricDimensionResult::Success,
                )
                    .as_event_codes(),
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 3,
                }),
            }],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_attempts_to_successful_install() {
        assert_metrics(
            Metrics::AttemptsToSuccessfulInstall { count: 3, successful: true },
            &[
                CobaltEvent {
                    metric_id: mos_metrics_registry::ATTEMPTS_PER_DEVICE_DAY_METRIC_ID,
                    event_codes: (
                        mos_metrics_registry::AttemptsPerDeviceDayMetricDimensionResult::Success,
                    )
                        .as_event_codes(),
                    component: None,
                    payload: EventPayload::EventCount(CountEvent {
                        period_duration_micros: 0,
                        count: 1,
                    }),
                },
                CobaltEvent {
                    metric_id: mos_metrics_registry::ATTEMPTS_TO_REACH_SUCCESS_METRIC_ID,
                    event_codes: (
                        mos_metrics_registry::AttemptsToReachSuccessMetricDimensionResult::Success,
                    )
                        .as_event_codes(),
                    component: None,
                    payload: EventPayload::EventCount(CountEvent {
                        period_duration_micros: 0,
                        count: 3,
                    }),
                },
            ],
        )
        .await;

        assert_metrics(
            Metrics::AttemptsToSuccessfulInstall { count: 3, successful: false },
            &[
                CobaltEvent {
                    metric_id: mos_metrics_registry::ATTEMPTS_PER_DEVICE_DAY_METRIC_ID,
                    event_codes: (
                        mos_metrics_registry::AttemptsPerDeviceDayMetricDimensionResult::Failed,
                    )
                        .as_event_codes(),
                    component: None,
                    payload: EventPayload::EventCount(CountEvent {
                        period_duration_micros: 0,
                        count: 1,
                    }),
                },
                CobaltEvent {
                    metric_id: mos_metrics_registry::ATTEMPTS_TO_REACH_SUCCESS_METRIC_ID,
                    event_codes: (
                        mos_metrics_registry::AttemptsToReachSuccessMetricDimensionResult::Failed,
                    )
                        .as_event_codes(),
                    component: None,
                    payload: EventPayload::EventCount(CountEvent {
                        period_duration_micros: 0,
                        count: 3,
                    }),
                },
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_failed_boot_attempts() {
        assert_metrics(
            Metrics::FailedBootAttempts(42),
            &[CobaltEvent {
                metric_id: mos_metrics_registry::FAILED_BOOT_ATTEMPTS_METRIC_ID,
                event_codes: vec![
                    mos_metrics_registry::FailedBootAttemptsMetricDimensionResult::Success,
                ]
                .as_event_codes(),
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 42,
                }),
            }],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_omaha_event_lost() {
        macro_rules! assert_lost_combo {
            ($typeId:ident, $resId:ident) => {
                assert_metrics(
                    Metrics::OmahaEventLost(Event {
                        event_type: EventType::$typeId,
                        event_result: EventResult::$resId,
                        ..Event::default()
                    }),
                    &[CobaltEvent {
                        metric_id: mos_metrics_registry::OMAHA_EVENT_LOST_METRIC_ID,
                        event_codes: (
                            mos_metrics_registry::OmahaEventLostMetricDimensionEventType::$typeId,
                            mos_metrics_registry::OmahaEventLostMetricDimensionEventResult::$resId,
                        )
                            .as_event_codes(),
                        component: None,
                        payload: EventPayload::EventCount(CountEvent {
                            period_duration_micros: 0,
                            count: 1,
                        }),
                    }],
                )
                .await;
            };
        }
        assert_lost_combo!(Unknown, Error);
        assert_lost_combo!(DownloadComplete, Success);
        assert_lost_combo!(InstallComplete, SuccessAndRestartRequired);
        assert_lost_combo!(UpdateComplete, SuccessAndAppRestartRequired);
        assert_lost_combo!(UpdateDownloadStarted, Cancelled);
        assert_lost_combo!(UpdateDownloadFinished, ErrorInSystemInstaller);
        assert_lost_combo!(RebootedAfterUpdate, UpdateDeferred);
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
