// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use fidl_fuchsia_cobalt::CobaltEvent;
use fidl_fuchsia_update::CheckNotStartedReason;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use log::info;
use std::future::Future;

#[derive(Debug)]
pub enum ApiEvent {
    UpdateChannelControlSetTarget,
    UpdateManagerConnection,
    UpdateManagerCheckNowResult(Result<(), CheckNotStartedReason>),
}

pub trait ApiMetricsReporter {
    fn emit_event(&mut self, metrics: ApiEvent);
}

pub struct CobaltApiMetricsReporter {
    cobalt_sender: CobaltSender,
}

impl CobaltApiMetricsReporter {
    pub fn new() -> (Self, impl Future<Output = ()>) {
        let (cobalt_sender, fut) = CobaltConnector::default()
            .serve(ConnectionType::project_id(cobalt_sw_delivery_registry::PROJECT_ID));
        (CobaltApiMetricsReporter { cobalt_sender }, fut)
    }

    #[cfg(test)]
    fn new_mock() -> (Self, futures::channel::mpsc::Receiver<CobaltEvent>) {
        let (sender, receiver) = futures::channel::mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);
        (CobaltApiMetricsReporter { cobalt_sender }, receiver)
    }
}

impl ApiMetricsReporter for CobaltApiMetricsReporter {
    fn emit_event(&mut self, metrics: ApiEvent) {
        info!("Reporting metrics to Cobalt: {:?}", metrics);
        match metrics {
            ApiEvent::UpdateChannelControlSetTarget => {
                self.cobalt_sender.log_event_count(
                    cobalt_sw_delivery_registry::UPDATE_CHANNEL_CONTROL_SET_TARGET_METRIC_ID,
                    cobalt_sw_delivery_registry::UpdateChannelControlSetTargetMetricDimensionResult::Success,
                    0,
                    1,
                );
            }
            ApiEvent::UpdateManagerConnection => {
                self.cobalt_sender.log_event_count(
                    cobalt_sw_delivery_registry::UPDATE_MANAGER_CONNECTION_METRIC_ID,
                    cobalt_sw_delivery_registry::UpdateManagerConnectionMetricDimensionResult::Success,
                    0,
                    1,
                );
            }
            ApiEvent::UpdateManagerCheckNowResult(result) => {
                self.cobalt_sender.log_event_count(
                    cobalt_sw_delivery_registry::UPDATE_MANAGER_CHECK_NOW_METRIC_ID,
                    match result {
                        Ok(()) => {
                            cobalt_sw_delivery_registry::UpdateManagerCheckNowMetricDimensionResult::Success
                        }
                        Err(CheckNotStartedReason::Internal) => {
                            cobalt_sw_delivery_registry::UpdateManagerCheckNowMetricDimensionResult::Internal
                        }
                        Err(CheckNotStartedReason::InvalidOptions) => {
                            cobalt_sw_delivery_registry::UpdateManagerCheckNowMetricDimensionResult::InvalidOptions
                        }
                        Err(CheckNotStartedReason::AlreadyInProgress) => {
                            cobalt_sw_delivery_registry::UpdateManagerCheckNowMetricDimensionResult::AlreadyInProgress
                        }
                        Err(CheckNotStartedReason::Throttled) => {
                            cobalt_sw_delivery_registry::UpdateManagerCheckNowMetricDimensionResult::Throttled
                        }
                    },
                    0,
                    1,
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
    use fidl_fuchsia_cobalt::{CountEvent, EventPayload};

    #[test]
    fn test_report_api_update_manager_connection() {
        let (mut reporter, mut receiver) = CobaltApiMetricsReporter::new_mock();
        reporter.emit_event(ApiEvent::UpdateManagerConnection);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: cobalt_sw_delivery_registry::UPDATE_MANAGER_CONNECTION_METRIC_ID,
                event_codes: vec![
                    cobalt_sw_delivery_registry::UpdateManagerConnectionMetricDimensionResult::Success
                        as u32,
                ],
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 1,
                }),
            }
        )
    }

    #[test]
    fn test_report_api_update_channel_control_set_target() {
        let (mut reporter, mut receiver) = CobaltApiMetricsReporter::new_mock();
        reporter.emit_event(ApiEvent::UpdateChannelControlSetTarget);
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: cobalt_sw_delivery_registry::UPDATE_CHANNEL_CONTROL_SET_TARGET_METRIC_ID,
                event_codes: vec![
                    cobalt_sw_delivery_registry::UpdateChannelControlSetTargetMetricDimensionResult::Success as u32,
                ],
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 1,
                }),
            }
        )
    }

    #[test]
    fn test_report_api_update_manager_check_now() {
        let (mut reporter, mut receiver) = CobaltApiMetricsReporter::new_mock();
        reporter.emit_event(ApiEvent::UpdateManagerCheckNowResult(Ok(())));
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: cobalt_sw_delivery_registry::UPDATE_MANAGER_CHECK_NOW_METRIC_ID,
                event_codes: vec![
                    cobalt_sw_delivery_registry::UpdateManagerCheckNowMetricDimensionResult::Success
                        as u32,
                ],
                component: None,
                payload: EventPayload::EventCount(CountEvent {
                    period_duration_micros: 0,
                    count: 1,
                }),
            }
        )
    }
}
