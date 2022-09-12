// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use fidl_contrib::protocol_connector::ConnectedProtocol;
use fidl_contrib::{protocol_connector::ProtocolSender, ProtocolConnector};
use fidl_fuchsia_metrics::{
    MetricEvent, MetricEventLoggerFactoryMarker, MetricEventLoggerProxy, ProjectSpec,
};
use fuchsia_cobalt_builders::MetricEventExt;
use fuchsia_component::client::connect_to_protocol;
use futures::prelude::*;
use mos_metrics_registry as mos_registry;
use omaha_client::{
    metrics::{ClockType, Metrics, MetricsReporter},
    protocol::request::{EventResult, EventType, InstallSource},
};
use std::{convert::TryFrom, time::Duration};
use tracing::{info, warn};

pub struct CobaltConnectedService(pub u32);
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
                    ProjectSpec { project_id: Some(self.0), ..ProjectSpec::EMPTY },
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

/// A MetricsReporter trait implementation that send metrics to Cobalt.
#[derive(Debug, Clone)]
pub struct CobaltMetricsReporter {
    cobalt_sender: ProtocolSender<MetricEvent>,
}

impl CobaltMetricsReporter {
    pub fn new() -> (Self, impl Future<Output = ()>) {
        let (cobalt_sender, fut) = ProtocolConnector::new(crate::metrics::CobaltConnectedService(
            mos_registry::PROJECT_ID,
        ))
        .serve_and_log_errors();
        (CobaltMetricsReporter { cobalt_sender }, fut)
    }

    #[cfg(test)]
    pub fn new_mock() -> (Self, futures::channel::mpsc::Receiver<MetricEvent>) {
        let (sender, receiver) = futures::channel::mpsc::channel(1);
        let cobalt_sender = ProtocolSender::new(sender);
        (CobaltMetricsReporter { cobalt_sender }, receiver)
    }

    /// Emit the update_check_opt_out_preference metric.
    pub fn report_update_check_opt_out_preference(
        &mut self,
        preference: fidl_fuchsia_update_config::OptOutPreference,
    ) {
        use fidl_fuchsia_update_config::OptOutPreference as Fidl;
        use mos_registry::UpdateCheckOptOutPreferenceMigratedMetricDimensionPreference as Cobalt;

        let preference = match preference {
            Fidl::AllowAllUpdates => Cobalt::AllowAllUpdates,
            Fidl::AllowOnlySecurityUpdates => Cobalt::AllowOnlySecurityUpdates,
        };
        self.cobalt_sender.send(
            MetricEvent::builder(mos_registry::UPDATE_CHECK_OPT_OUT_PREFERENCE_MIGRATED_METRIC_ID)
                .with_event_codes(preference)
                .as_occurrence(1),
        );
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
) -> mos_registry::OmahaEventLostMigratedMetricDimensionEventType {
    match t {
        EventType::Unknown => mos_registry::OmahaEventLostMigratedMetricDimensionEventType::Unknown,
        EventType::DownloadComplete => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventType::DownloadComplete
        }
        EventType::InstallComplete => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventType::InstallComplete
        }
        EventType::UpdateComplete => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventType::UpdateComplete
        }
        EventType::UpdateDownloadStarted => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventType::UpdateDownloadStarted
        }
        EventType::UpdateDownloadFinished => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventType::UpdateDownloadFinished
        }
        EventType::RebootedAfterUpdate => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventType::RebootedAfterUpdate
        }
    }
}

fn mos_event_result_from_event_result(
    r: EventResult,
) -> mos_registry::OmahaEventLostMigratedMetricDimensionEventResult {
    match r {
        EventResult::Error => mos_registry::OmahaEventLostMigratedMetricDimensionEventResult::Error,
        EventResult::Success => mos_registry::OmahaEventLostMigratedMetricDimensionEventResult::Success,
        EventResult::SuccessAndRestartRequired => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventResult::SuccessAndRestartRequired
        }
        EventResult::SuccessAndAppRestartRequired => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventResult::SuccessAndAppRestartRequired
        }
        EventResult::Cancelled => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventResult::Cancelled
        }
        EventResult::ErrorInSystemInstaller => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventResult::ErrorInSystemInstaller
        }
        EventResult::UpdateDeferred => {
            mos_registry::OmahaEventLostMigratedMetricDimensionEventResult::UpdateDeferred
        }
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
                    self.cobalt_sender.send(
                        MetricEvent::builder(mos_registry::UPDATE_CHECK_RESPONSE_TIME_MIGRATED_METRIC_ID)
                            .with_event_codes(match successful {
                                true => {
                                    mos_registry::UpdateCheckResponseTimeMigratedMetricDimensionResult::Success
                                }
                                false => {
                                    mos_registry::UpdateCheckResponseTimeMigratedMetricDimensionResult::Failed
                                }
                            })
                            .as_integer(response_time),
                    );
                }
            }
            Metrics::UpdateCheckInterval { interval, clock, install_source } => {
                if let Some(interval) =
                    duration_to_cobalt_micros(interval, "Metrics::UpdateCheckInterval")
                {
                    let clock = match clock {
                        ClockType::Monotonic => {
                            mos_registry::UpdateCheckIntervalMigratedMetricDimensionClock::Monotonic
                        }
                        ClockType::Wall => {
                            mos_registry::UpdateCheckIntervalMigratedMetricDimensionClock::Wall
                        }
                    };
                    let install_source = match install_source {
                        InstallSource::ScheduledTask => {
                            mos_registry::UpdateCheckIntervalMigratedMetricDimensionInitiator::ScheduledTask
                        }
                        InstallSource::OnDemand => {
                            mos_registry::UpdateCheckIntervalMigratedMetricDimensionInitiator::OnDemand
                        }
                    };
                    self.cobalt_sender.send(
                        MetricEvent::builder(
                            mos_registry::UPDATE_CHECK_INTERVAL_MIGRATED_METRIC_ID,
                        )
                        .with_event_codes((
                            mos_registry::UpdateCheckIntervalMigratedMetricDimensionResult::Success,
                            clock,
                            install_source,
                        ))
                        .as_integer(interval),
                    );
                }
            }
            Metrics::SuccessfulUpdateDuration(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::SuccessfulUpdateDuration")
                {
                    self.cobalt_sender.send(
                        MetricEvent::builder(mos_registry::UPDATE_DURATION_MIGRATED_METRIC_ID)
                            .with_event_codes(
                                mos_registry::UpdateDurationMigratedMetricDimensionResult::Success,
                            )
                            .as_integer(duration),
                    );
                }
            }
            Metrics::FailedUpdateDuration(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::FailedUpdateDuration")
                {
                    self.cobalt_sender.send(
                        MetricEvent::builder(mos_registry::UPDATE_DURATION_MIGRATED_METRIC_ID)
                            .with_event_codes(
                                mos_registry::UpdateDurationMigratedMetricDimensionResult::Failed,
                            )
                            .as_integer(duration),
                    );
                }
            }
            Metrics::SuccessfulUpdateFromFirstSeen(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::SuccessfulUpdateFromFirstSeen")
                {
                    self.cobalt_sender.send(
                        MetricEvent::builder(
                            mos_registry::UPDATE_DURATION_FROM_FIRST_SEEN_MIGRATED_METRIC_ID,
                        )
                        .with_event_codes(
                            mos_registry::UpdateDurationFromFirstSeenMigratedMetricDimensionResult::Success,
                        )
                        .as_integer(duration),
                    );
                }
            }
            Metrics::UpdateCheckFailureReason(reason) => {
                let event_code = reason as u32;
                self.cobalt_sender.send(
                    MetricEvent::builder(mos_registry::UPDATE_CHECK_FAILURE_MIGRATED_METRIC_ID)
                        .with_event_codes(event_code)
                        .as_occurrence(1),
                );
                self.cobalt_sender.send(
                    MetricEvent::builder(
                        mos_registry::UPDATE_CHECK_FAILURE_COUNT_MIGRATED_METRIC_ID,
                    )
                    .with_event_codes(event_code)
                    .as_occurrence(1),
                );
            }
            Metrics::RequestsPerCheck { count, successful } => {
                self.cobalt_sender.send(
                    MetricEvent::builder(mos_registry::REQUESTS_PER_CHECK_MIGRATED_METRIC_ID)
                        .with_event_codes(match successful {
                            true => {
                                mos_registry::RequestsPerCheckMigratedMetricDimensionResult::Success
                            }
                            false => {
                                mos_registry::RequestsPerCheckMigratedMetricDimensionResult::Failed
                            }
                        })
                        .as_integer(count as i64),
                );
            }
            Metrics::AttemptsToSuccessfulCheck(count) => {
                self.cobalt_sender.send(
                    MetricEvent::builder(mos_registry::ATTEMPTS_TO_SUCCESSFUL_CHECK_MIGRATED_METRIC_ID)
                        .with_event_codes(
                            mos_registry::AttemptsToSuccessfulCheckMigratedMetricDimensionResult::Success,
                        )
                        .as_integer(count as i64),
                );
            }
            Metrics::AttemptsToSuccessfulInstall { count, successful } => {
                self.cobalt_sender.send(
                    MetricEvent::builder(mos_registry::ATTEMPTS_PER_DEVICE_DAY_MIGRATED_METRIC_ID)
                        .with_event_codes(if successful {
                            mos_registry::AttemptsPerDeviceDayMigratedMetricDimensionResult::Success
                        } else {
                            mos_registry::AttemptsPerDeviceDayMigratedMetricDimensionResult::Failed
                        })
                        .as_occurrence(1),
                );

                self.cobalt_sender.send(
                    MetricEvent::builder(
                        mos_registry::ATTEMPTS_TO_REACH_SUCCESS_MIGRATED_METRIC_ID,
                    )
                    .with_event_codes(if successful {
                        mos_registry::AttemptsToReachSuccessMigratedMetricDimensionResult::Success
                    } else {
                        mos_registry::AttemptsToReachSuccessMigratedMetricDimensionResult::Failed
                    })
                    .as_occurrence(count),
                );
            }
            Metrics::WaitedForRebootDuration(duration) => {
                if let Some(duration) =
                    duration_to_cobalt_micros(duration, "Metrics::WaitedForRebootDuration")
                {
                    self.cobalt_sender.send(
                        MetricEvent::builder(
                            mos_registry::WAITED_FOR_REBOOT_DURATION_MIGRATED_METRIC_ID,
                        )
                        .with_event_codes(
                            mos_registry::WaitedForRebootDurationMigratedMetricDimensionResult::Success,
                        )
                        .as_integer(duration),
                    );
                }
            }
            Metrics::FailedBootAttempts(count) => {
                self.cobalt_sender.send(
                    MetricEvent::builder(mos_registry::FAILED_BOOT_ATTEMPTS_MIGRATED_METRIC_ID)
                        .with_event_codes(
                            mos_registry::FailedBootAttemptsMigratedMetricDimensionResult::Success,
                        )
                        .as_integer(count as i64),
                );
            }
            Metrics::OmahaEventLost(event) => {
                let event_type = mos_event_type_from_event_type(event.event_type);
                let result = mos_event_result_from_event_result(event.event_result);
                self.cobalt_sender.send(
                    MetricEvent::builder(mos_registry::OMAHA_EVENT_LOST_MIGRATED_METRIC_ID)
                        .with_event_codes((event_type, result))
                        .as_occurrence(1),
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
    use fidl_fuchsia_metrics::MetricEventPayload;
    use fuchsia_async as fasync;
    use futures::stream::StreamExt;
    use omaha_client::{metrics::UpdateCheckFailureReason, protocol::request::Event};
    use std::time::Duration;

    async fn assert_metrics(metrics: Metrics, expected_events: &[MetricEvent]) {
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
            &[MetricEvent {
                metric_id: mos_registry::UPDATE_CHECK_RESPONSE_TIME_MIGRATED_METRIC_ID,
                event_codes: vec![
                    mos_registry::UpdateCheckResponseTimeMigratedMetricDimensionResult::Success,
                ]
                .as_event_codes(),
                payload: MetricEventPayload::IntegerValue(10 * 1000),
            }],
        )
        .await;

        assert_metrics(
            Metrics::UpdateCheckResponseTime {
                response_time: Duration::from_millis(10),
                successful: false,
            },
            &[MetricEvent {
                metric_id: mos_registry::UPDATE_CHECK_RESPONSE_TIME_MIGRATED_METRIC_ID,
                event_codes: vec![
                    mos_registry::UpdateCheckResponseTimeMigratedMetricDimensionResult::Failed,
                ]
                .as_event_codes(),
                payload: MetricEventPayload::IntegerValue(10 * 1000),
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
            &[MetricEvent {
                metric_id: mos_registry::UPDATE_CHECK_INTERVAL_MIGRATED_METRIC_ID,
                event_codes: (
                    mos_registry::UpdateCheckIntervalMigratedMetricDimensionResult::Success,
                    mos_registry::UpdateCheckIntervalMigratedMetricDimensionClock::Monotonic,
                    mos_registry::UpdateCheckIntervalMigratedMetricDimensionInitiator::OnDemand,
                )
                    .as_event_codes(),
                payload: MetricEventPayload::IntegerValue(10 * 1000),
            }],
        )
        .await;

        assert_metrics(
            Metrics::UpdateCheckInterval {
                interval: Duration::from_millis(20),
                clock: ClockType::Wall,
                install_source: InstallSource::ScheduledTask,
            },
            &[MetricEvent {
                metric_id: mos_registry::UPDATE_CHECK_INTERVAL_MIGRATED_METRIC_ID,
                event_codes: (
                    mos_registry::UpdateCheckIntervalMigratedMetricDimensionResult::Success,
                    mos_registry::UpdateCheckIntervalMigratedMetricDimensionClock::Wall,
                    mos_registry::UpdateCheckIntervalMigratedMetricDimensionInitiator::ScheduledTask,
                )
                    .as_event_codes(),
                payload: MetricEventPayload::IntegerValue(20 * 1000),
            }],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_update_check_failure_reason() {
        assert_metrics(
            Metrics::UpdateCheckFailureReason(UpdateCheckFailureReason::Configuration),
            &[
                MetricEvent {
                    metric_id: mos_registry::UPDATE_CHECK_FAILURE_MIGRATED_METRIC_ID,
                    event_codes:
                        (mos_registry::UpdateCheckFailureMigratedMetricDimensionReason::Configuration)
                            .as_event_codes(),
                    payload: MetricEventPayload::Count(1),
                },
                MetricEvent {
                    metric_id: mos_registry::UPDATE_CHECK_FAILURE_COUNT_MIGRATED_METRIC_ID,
                    event_codes:
                        (mos_registry::UpdateCheckFailureMigratedMetricDimensionReason::Configuration)
                            .as_event_codes(),
                    payload: MetricEventPayload::Count(1)
                },
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_requests_per_check() {
        assert_metrics(
            Metrics::RequestsPerCheck { count: 3, successful: true },
            &[MetricEvent {
                metric_id: mos_registry::REQUESTS_PER_CHECK_MIGRATED_METRIC_ID,
                event_codes:
                    (mos_registry::RequestsPerCheckMigratedMetricDimensionResult::Success,)
                        .as_event_codes(),
                payload: MetricEventPayload::IntegerValue(3),
            }],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_attempts_to_successful_check() {
        assert_metrics(
            Metrics::AttemptsToSuccessfulCheck(3),
            &[MetricEvent {
                metric_id: mos_registry::ATTEMPTS_TO_SUCCESSFUL_CHECK_MIGRATED_METRIC_ID,
                event_codes: (
                    mos_registry::AttemptsToSuccessfulCheckMigratedMetricDimensionResult::Success,
                )
                    .as_event_codes(),
                payload: MetricEventPayload::IntegerValue(3),
            }],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_report_attempts_to_successful_install() {
        assert_metrics(
            Metrics::AttemptsToSuccessfulInstall { count: 3, successful: true },
            &[
                MetricEvent {
                    metric_id: mos_registry::ATTEMPTS_PER_DEVICE_DAY_MIGRATED_METRIC_ID,
                    event_codes: (
                        mos_registry::AttemptsPerDeviceDayMigratedMetricDimensionResult::Success,
                    )
                        .as_event_codes(),
                    payload: MetricEventPayload::Count(1),
                },
                MetricEvent {
                    metric_id: mos_registry::ATTEMPTS_TO_REACH_SUCCESS_MIGRATED_METRIC_ID,
                    event_codes: (
                        mos_registry::AttemptsToReachSuccessMigratedMetricDimensionResult::Success,
                    )
                        .as_event_codes(),
                    payload: MetricEventPayload::Count(3),
                },
            ],
        )
        .await;

        assert_metrics(
            Metrics::AttemptsToSuccessfulInstall { count: 3, successful: false },
            &[
                MetricEvent {
                    metric_id: mos_registry::ATTEMPTS_PER_DEVICE_DAY_MIGRATED_METRIC_ID,
                    event_codes: (
                        mos_registry::AttemptsPerDeviceDayMigratedMetricDimensionResult::Failed,
                    )
                        .as_event_codes(),
                    payload: MetricEventPayload::Count(1),
                },
                MetricEvent {
                    metric_id: mos_registry::ATTEMPTS_TO_REACH_SUCCESS_MIGRATED_METRIC_ID,
                    event_codes: (
                        mos_registry::AttemptsToReachSuccessMigratedMetricDimensionResult::Failed,
                    )
                        .as_event_codes(),
                    payload: MetricEventPayload::Count(3),
                },
            ],
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_failed_boot_attempts() {
        assert_metrics(
            Metrics::FailedBootAttempts(42),
            &[MetricEvent {
                metric_id: mos_registry::FAILED_BOOT_ATTEMPTS_MIGRATED_METRIC_ID,
                event_codes: vec![
                    mos_registry::FailedBootAttemptsMigratedMetricDimensionResult::Success,
                ]
                .as_event_codes(),
                payload: MetricEventPayload::IntegerValue(42),
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
                    &[MetricEvent {
                        metric_id: mos_registry::OMAHA_EVENT_LOST_MIGRATED_METRIC_ID,
                        event_codes: (
                            mos_registry::OmahaEventLostMigratedMetricDimensionEventType::$typeId,
                            mos_registry::OmahaEventLostMigratedMetricDimensionEventResult::$resId,
                        )
                            .as_event_codes(),
                        payload: MetricEventPayload::Count(1),
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
