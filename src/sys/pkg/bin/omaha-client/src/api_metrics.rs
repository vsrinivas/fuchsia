// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cobalt_sw_delivery_registry as registry;
use fidl_contrib::{protocol_connector::ProtocolSender, ProtocolConnector};
use fidl_fuchsia_metrics::MetricEvent;
use fidl_fuchsia_update::CheckNotStartedReason;
use fuchsia_cobalt_builders::MetricEventExt as _;
use std::future::Future;
use tracing::info;

#[derive(Debug)]
#[allow(clippy::enum_variant_names)]
pub enum ApiEvent {
    UpdateChannelControlSetTarget,
    UpdateManagerConnection,
    UpdateManagerCheckNowResult(Result<(), CheckNotStartedReason>),
}

pub trait ApiMetricsReporter {
    fn emit_event(&mut self, metrics: ApiEvent);
}

pub struct CobaltApiMetricsReporter {
    cobalt_sender: ProtocolSender<MetricEvent>,
}

impl CobaltApiMetricsReporter {
    pub fn new() -> (Self, impl Future<Output = ()>) {
        let (cobalt_sender, fut) =
            ProtocolConnector::new(crate::metrics::CobaltConnectedService(registry::PROJECT_ID))
                .serve_and_log_errors();
        (CobaltApiMetricsReporter { cobalt_sender }, fut)
    }

    #[cfg(test)]
    fn new_mock() -> (Self, futures::channel::mpsc::Receiver<MetricEvent>) {
        let (sender, receiver) = futures::channel::mpsc::channel(1);
        let cobalt_sender = ProtocolSender::new(sender);
        (CobaltApiMetricsReporter { cobalt_sender }, receiver)
    }
}

impl ApiMetricsReporter for CobaltApiMetricsReporter {
    fn emit_event(&mut self, metrics: ApiEvent) {
        info!("Reporting metrics to Cobalt: {:?}", metrics);
        match metrics {
            ApiEvent::UpdateChannelControlSetTarget => {
                self.cobalt_sender.send(
                    MetricEvent::builder(
                        registry::UPDATE_CHANNEL_CONTROL_SET_TARGET_MIGRATED_METRIC_ID,
                    )
                    .with_event_codes(
                        registry::UpdateChannelControlSetTargetMigratedMetricDimensionResult::Success,
                    )
                    .as_occurrence(1),
                );
            }
            ApiEvent::UpdateManagerConnection => {
                self.cobalt_sender.send(
                    MetricEvent::builder(registry::UPDATE_MANAGER_CONNECTION_MIGRATED_METRIC_ID)
                        .with_event_codes(
                            registry::UpdateManagerConnectionMigratedMetricDimensionResult::Success,
                        )
                        .as_occurrence(1),
                );
            }
            ApiEvent::UpdateManagerCheckNowResult(result) => {
                self.cobalt_sender.send(
                    MetricEvent::builder(registry::UPDATE_MANAGER_CHECK_NOW_MIGRATED_METRIC_ID)
                        .with_event_codes(match result {
                            Ok(()) => registry::UpdateManagerCheckNowMigratedMetricDimensionResult::Success,
                            Err(CheckNotStartedReason::Internal) => {
                                registry::UpdateManagerCheckNowMigratedMetricDimensionResult::Internal
                            }
                            Err(CheckNotStartedReason::InvalidOptions) => {
                                registry::UpdateManagerCheckNowMigratedMetricDimensionResult::InvalidOptions
                            }
                            Err(CheckNotStartedReason::AlreadyInProgress) => {
                                registry::UpdateManagerCheckNowMigratedMetricDimensionResult::AlreadyInProgress
                            }
                            Err(CheckNotStartedReason::Throttled) => {
                                registry::UpdateManagerCheckNowMigratedMetricDimensionResult::Throttled
                            }
                        })
                        .as_occurrence(1),
                );
            }
        }
    }
}

#[cfg(test)]
pub struct StubApiMetricsReporter;

#[cfg(test)]
impl ApiMetricsReporter for StubApiMetricsReporter {
    fn emit_event(&mut self, metrics: ApiEvent) {
        info!("Received request to report API metrics: {:?}", metrics);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_metrics::MetricEventPayload;

    #[test]
    fn test_report_api_update_manager_connection() {
        let (mut reporter, mut receiver) = CobaltApiMetricsReporter::new_mock();
        reporter.emit_event(ApiEvent::UpdateManagerConnection);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            MetricEvent {
                metric_id: registry::UPDATE_MANAGER_CONNECTION_MIGRATED_METRIC_ID,
                event_codes: vec![
                    registry::UpdateManagerConnectionMigratedMetricDimensionResult::Success as u32,
                ],
                payload: MetricEventPayload::Count(1)
            }
        )
    }

    #[test]
    fn test_report_api_update_channel_control_set_target() {
        let (mut reporter, mut receiver) = CobaltApiMetricsReporter::new_mock();
        reporter.emit_event(ApiEvent::UpdateChannelControlSetTarget);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            MetricEvent {
                metric_id: registry::UPDATE_CHANNEL_CONTROL_SET_TARGET_MIGRATED_METRIC_ID,
                event_codes: vec![
                    registry::UpdateChannelControlSetTargetMigratedMetricDimensionResult::Success
                        as u32,
                ],
                payload: MetricEventPayload::Count(1)
            }
        )
    }

    #[test]
    fn test_report_api_update_manager_check_now() {
        let (mut reporter, mut receiver) = CobaltApiMetricsReporter::new_mock();
        reporter.emit_event(ApiEvent::UpdateManagerCheckNowResult(Ok(())));
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            MetricEvent {
                metric_id: registry::UPDATE_MANAGER_CHECK_NOW_MIGRATED_METRIC_ID,
                event_codes: vec![
                    registry::UpdateManagerCheckNowMigratedMetricDimensionResult::Success as u32,
                ],
                payload: MetricEventPayload::Count(1)
            }
        )
    }
}
