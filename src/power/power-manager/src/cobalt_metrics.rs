// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::{Celsius, Microseconds, Nanoseconds};
use crate::utils::{CobaltIntHistogram, CobaltIntHistogramConfig};
use fuchsia_async as fasync;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use lazy_static::lazy_static;
use parking_lot::Mutex;
use power_manager_metrics::power_manager_metrics as power_metrics_registry;
use power_metrics_registry::ThermalLimitResultMetricDimensionResult as thermal_limit_result;
use std::sync::Arc;

/// The CobaltMetricsImpl struct is implemented as a singleton accessible via the public
/// `get_cobalt_metrics_instance` function. This is in contrast to the node structure seen in most
/// other parts of power_manager. While it would likely be very natural to implement CobaltMetrics
/// as a node, there are some considerations that favor the current approach.
/// 1) The Cobalt metrics structure is largely a logging mechanism and as such, benefits nicely
///    global access. By providing access to the CobaltMetricsImpl instance via the public function,
///    it means that any node can be easily updated to contribute information to the instance
///    without requiring major changes to that node (adding fields to the struct, updating the
///    constructor, changing the node config file and schema, updating all affected tests, etc.).
/// 2) One of the great benefits of the node architecture is the ability to configure the node on a
///    per-product basis. For the Cobalt metrics, there is (currently) no such configuration
///    required. The CobaltMetricsImpl singleton is created without any parameters and is the same
///    without considering the product. Should the need arise for per-product configration of the
///    Cobalt metrics, it may be best to convert the structure to using a node format.

/// Functions provided by CobaltMetricsImpl.
pub trait CobaltMetrics {
    fn log_raw_temperature(&self, temperature: Celsius);
    fn log_throttle_start(&self, timestamp: Nanoseconds);
    fn log_throttle_end_mitigated(&self, timestamp: Nanoseconds);
    fn log_throttle_end_shutdown(&self, timestamp: Nanoseconds);
}

lazy_static! {
    /// Global instance for Cobalt metrics.
    static ref COBALT_METRICS: CobaltMetricsImpl = CobaltMetricsImpl::new();
}

/// Gets a clone of the CobaltMetricsImpl instance.
pub fn get_cobalt_metrics_instance() -> CobaltMetricsImpl {
    COBALT_METRICS.clone()
}

/// Stores and dispatches Cobalt metric data.
#[derive(Clone)]
pub struct CobaltMetricsImpl {
    /// Metric data with interior mutability and reference counting.
    inner: Arc<Mutex<CobaltMetricsInner>>,
}

/// Metric data for CobaltMetricsImpl.
struct CobaltMetricsInner {
    /// Sends Cobalt events to the Cobalt FIDL service.
    cobalt_sender: CobaltSender,

    /// Timestamp of the start of a throttling duration.
    throttle_start_time: Option<Nanoseconds>,

    /// Histogram of raw temperature readings.
    temperature_histogram: CobaltIntHistogram,
}

impl CobaltMetricsInner {
    /// Creates a new CobaltMetricsInner and connects to the Cobalt FIDL service.
    fn new() -> Self {
        let (cobalt_sender, sender_future) = CobaltConnector::default()
            .serve(ConnectionType::project_id(power_metrics_registry::PROJECT_ID));

        // Spawn the future that handles sending data to Cobalt
        fasync::Task::local(sender_future).detach();

        Self::new_with_cobalt_sender(cobalt_sender)
    }

    /// Creates a new CobaltMetricsInner without connecting to the Cobalt FIDL service. Returns a
    /// channel to receive the Cobalt events that would have otherwise been delivered to the Cobalt
    /// service.
    fn new_with_cobalt_sender(cobalt_sender: CobaltSender) -> Self {
        Self {
            cobalt_sender: cobalt_sender,
            throttle_start_time: None,
            temperature_histogram: CobaltIntHistogram::new(CobaltIntHistogramConfig {
                floor: power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_FLOOR,
                num_buckets: power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_NUM_BUCKETS,
                step_size: power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_STEP_SIZE,
            }),
        }
    }
}

impl CobaltMetrics for CobaltMetricsImpl {
    /// Log the start of a thermal throttling duration. Since the timestamp is recorded and used to
    /// track duration of throttling events, it is invalid to call this function consecutively
    /// without first ending the existing throttling duration by calling
    /// `log_throttle_end_[mitigated|shutdown]` first. If the invalid scenario is encountered, the
    /// function will panic on debug builds or have no effect otherwise.
    fn log_throttle_start(&self, timestamp: Nanoseconds) {
        let mut data = self.inner.lock();
        debug_assert!(
            data.throttle_start_time.is_none(),
            "throttle_start called before ending previous throttle"
        );

        // Record the new timestamp only if it was previously None
        data.throttle_start_time = data.throttle_start_time.or(Some(timestamp));
    }

    /// Log the end of a thermal throttling duration due to successful mitigation. To track the
    /// elapsed time of a throttling event, it is expected that `log_throttle_start` is called
    /// first. Failure to do so results in a panic on debug builds or missed Cobalt events for the
    /// elapsed throttling time metric otherwise.
    fn log_throttle_end_mitigated(&self, timestamp: Nanoseconds) {
        debug_assert!(self.inner.lock().throttle_start_time.is_some());
        self.log_throttle_end_with_result(thermal_limit_result::Mitigated, timestamp)
    }

    /// Log the end of a thermal throttling duration due to a shutdown. To track the elapsed time of
    /// a throttling event, it is expected that `log_throttle_start` is called first. However, this
    /// is not strictly enforced because in a theoretical scenario we could see a call to this
    /// function from the ThermalShutdown node without the ThermalPolicy node having first called
    /// the `log_throttle_start` function.
    fn log_throttle_end_shutdown(&self, timestamp: Nanoseconds) {
        self.log_throttle_end_with_result(thermal_limit_result::Shutdown, timestamp)
    }

    /// Log a raw temperature reading. After `CobaltMetricsImpl::NUM_TEMPERATURE_READINGS` calls, a
    /// Cobalt event is dispatched and the local data is cleared.
    fn log_raw_temperature(&self, temperature: Celsius) {
        let mut data = self.inner.lock();
        data.temperature_histogram.add_data(temperature.0 as i64);
        if data.temperature_histogram.count() == Self::NUM_TEMPERATURE_READINGS {
            let hist_data = data.temperature_histogram.get_data();
            data.cobalt_sender.log_int_histogram(
                power_metrics_registry::RAW_TEMPERATURE_METRIC_ID,
                (),
                hist_data,
            );
            data.temperature_histogram.clear();
        }
    }
}

impl CobaltMetricsImpl {
    /// Number of temperature readings before dispatching a Cobalt event.
    const NUM_TEMPERATURE_READINGS: u32 = 100;

    fn new() -> Self {
        Self { inner: Arc::new(Mutex::new(CobaltMetricsInner::new())) }
    }

    #[cfg(test)]
    fn new_with_cobalt_sender(cobalt_sender: CobaltSender) -> Self {
        Self {
            inner: Arc::new(Mutex::new(CobaltMetricsInner::new_with_cobalt_sender(cobalt_sender))),
        }
    }

    /// Log the end of a thermal throttling duration with a specified reason. To track the elapsed
    /// time of a throttling event, it is expected that `log_throttle_start` is called first.
    /// However, this is not strictly enforced because of a special case described in
    /// `log_throttle_end_shutdown`.
    fn log_throttle_end_with_result(&self, result: thermal_limit_result, timestamp: Nanoseconds) {
        let mut data = self.inner.lock();

        if let Some(time) = data.throttle_start_time.take() {
            let elapsed_time = timestamp - time;
            if elapsed_time >= Nanoseconds(0) {
                data.cobalt_sender.log_elapsed_time(
                    power_metrics_registry::THERMAL_LIMITING_ELAPSED_TIME_METRIC_ID,
                    (),
                    Microseconds::from(elapsed_time).0,
                );
            } else {
                debug_assert!(false, "Elapsed time must not be negative");
            }
        }

        data.cobalt_sender
            .log_event(power_metrics_registry::THERMAL_LIMIT_RESULT_METRIC_ID, vec![result as u32]);
    }
}

#[cfg(test)]
mod cobalt_metrics_impl_test {
    use super::*;
    use crate::types::Seconds;
    use fidl_fuchsia_cobalt::{CobaltEvent, Event, EventPayload};

    /// Tests that we can call `throttle_start` a second time if we first call `throttle_end`.
    #[fasync::run_singlethreaded(test)]
    async fn test_throttle_restart() {
        let cobalt_metrics = CobaltMetricsImpl::new();
        cobalt_metrics.log_throttle_start(Nanoseconds(0));
        cobalt_metrics.log_throttle_end_mitigated(Nanoseconds(1000));
        cobalt_metrics.log_throttle_start(Nanoseconds(2000));
    }

    /// Tests that calling CobaltMetrics `throttle_start` twice without first calling `throttle_end`
    /// causes a panic in the debug configuration.
    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "throttle_start called before ending previous throttle")]
    #[cfg(debug_assertions)]
    async fn test_double_start_panic() {
        let cobalt_metrics = CobaltMetricsImpl::new();
        cobalt_metrics.log_throttle_start(Nanoseconds(0));
        cobalt_metrics.log_throttle_start(Nanoseconds(0));
    }

    /// Tests that a negative throttling elapsed time metric panics in the debug configuration.
    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "Elapsed time must not be negative")]
    #[cfg(debug_assertions)]
    async fn test_negative_elapsed_time() {
        let cobalt_metrics = CobaltMetricsImpl::new();
        cobalt_metrics.log_throttle_start(Nanoseconds(10));
        cobalt_metrics.log_throttle_end_mitigated(Nanoseconds(9));
    }

    /// Tests that two events for thermal_limiting_elapsed_time and thermal_limit_result are
    /// dispatched after log calls corresponding to a single throttling event.
    #[test]
    fn test_throttle_elapsed_time() {
        let (sender, mut receiver) = futures::channel::mpsc::channel(10);

        let cobalt_metrics = CobaltMetricsImpl::new_with_cobalt_sender(CobaltSender::new(sender));

        cobalt_metrics.log_throttle_start(Seconds(0.0).into());
        cobalt_metrics.log_throttle_end_mitigated(Seconds(1.0).into());

        // Verify the expected Cobalt event for the thermal_limiting_elapsed_time metric
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: power_metrics_registry::THERMAL_LIMITING_ELAPSED_TIME_METRIC_ID,
                event_codes: vec![],
                component: None,
                payload: EventPayload::ElapsedMicros(Microseconds::from(Seconds(1.0)).0)
            }
        );

        // Verify the expected Cobalt event for the thermal_limit_result metric
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: power_metrics_registry::THERMAL_LIMIT_RESULT_METRIC_ID,
                event_codes: vec![thermal_limit_result::Mitigated as u32],
                component: None,
                payload: EventPayload::Event(Event),
            }
        );

        // Verify there were no more dispatched Cobalt events
        assert!(receiver.try_next().is_err());
    }

    /// Tests that an event for thermal_limit_result is dispatched after a log call corresponding to
    /// a thermal shutdown.
    #[test]
    fn test_throttle_end_shutdown() {
        let (sender, mut receiver) = futures::channel::mpsc::channel(10);
        let cobalt_metrics = CobaltMetricsImpl::new_with_cobalt_sender(CobaltSender::new(sender));

        cobalt_metrics.log_throttle_end_shutdown(Nanoseconds(0));

        // Verify the expected Cobalt event for the thermal_limit_result metric
        assert_eq!(
            receiver.try_next().unwrap().unwrap(),
            CobaltEvent {
                metric_id: power_metrics_registry::THERMAL_LIMIT_RESULT_METRIC_ID,
                event_codes: vec![thermal_limit_result::Shutdown as u32],
                component: None,
                payload: EventPayload::Event(Event),
            }
        );

        // Verify there were no more dispatched Cobalt events
        assert!(receiver.try_next().is_err());
    }

    /// Tests that a Cobalt event for raw_temperature is dispatched after log calls for the expected
    /// number of raw temperature samples.
    #[test]
    fn test_raw_temperature() {
        let (sender, mut receiver) = futures::channel::mpsc::channel(10);
        let cobalt_metrics = CobaltMetricsImpl::new_with_cobalt_sender(CobaltSender::new(sender));
        let num_temperature_readings = CobaltMetricsImpl::NUM_TEMPERATURE_READINGS + 1;

        for _ in 0..num_temperature_readings {
            cobalt_metrics.log_raw_temperature(Celsius(50.0));
        }

        // Generate the expected raw_temperature Cobalt event
        let mut expected_histogram = CobaltIntHistogram::new(CobaltIntHistogramConfig {
            floor: power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_FLOOR,
            num_buckets: power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_NUM_BUCKETS,
            step_size: power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_STEP_SIZE,
        });
        for _ in 0..num_temperature_readings - 1 {
            expected_histogram.add_data(Celsius(50.0).0 as i64);
        }

        let expected_cobalt_event = CobaltEvent {
            metric_id: power_metrics_registry::RAW_TEMPERATURE_METRIC_ID,
            event_codes: vec![],
            component: None,
            payload: EventPayload::IntHistogram(expected_histogram.get_data()),
        };

        // Verify that the expected Cobalt event was received, and there were no extra events
        assert_eq!(receiver.try_next().unwrap().unwrap(), expected_cobalt_event);
        assert!(receiver.try_next().is_err());
    }
}

#[cfg(test)]
pub mod mock_cobalt_metrics {
    use super::*;
    use anyhow::Context;
    use std::cell::RefCell;
    use std::cell::RefMut;
    use std::collections::VecDeque;
    use std::rc::Rc;

    /// Mock implementation of CobaltMetrics that is used for verifying a user of CobaltMetricsImpl
    /// calls the expected logging functions.
    #[derive(Clone)]
    pub struct MockCobaltMetrics {
        expected_logs: Rc<RefCell<VecDeque<LogType>>>,
    }

    /// Log types that the mock supports.
    #[derive(PartialEq, Debug)]
    pub enum LogType {
        LogRawTemperature(Celsius),
        LogThrottleStart(Nanoseconds),
        LogThrottleEndMitigated(Nanoseconds),
        LogThrottleEndShutdown(Nanoseconds),
    }

    impl MockCobaltMetrics {
        pub fn new() -> Self {
            Self { expected_logs: Rc::new(RefCell::new(VecDeque::new())) }
        }

        /// Returns a mutable borrow of `expected_logs`.
        fn expected_logs_mut(&self) -> RefMut<'_, VecDeque<LogType>> {
            self.expected_logs.borrow_mut()
        }

        /// Asserts that all expected log calls have been made.
        pub fn verify(&self, err_str: &str) {
            assert!(
                self.expected_logs.borrow().is_empty(),
                "{}. Leftover expected calls: {:?}",
                err_str,
                self.expected_logs.borrow()
            );
        }

        pub fn expect_log_raw_temperature(&self, temperature: Celsius) {
            self.expected_logs_mut().push_back(LogType::LogRawTemperature(temperature));
        }

        pub fn expect_log_throttle_start(&self, timestamp: Nanoseconds) {
            self.expected_logs_mut().push_back(LogType::LogThrottleStart(timestamp));
        }

        pub fn expect_log_throttle_end_mitigated(&self, timestamp: Nanoseconds) {
            self.expected_logs_mut().push_back(LogType::LogThrottleEndMitigated(timestamp));
        }

        pub fn expect_log_throttle_end_shutdown(&self, timestamp: Nanoseconds) {
            self.expected_logs_mut().push_back(LogType::LogThrottleEndShutdown(timestamp));
        }
    }

    impl CobaltMetrics for MockCobaltMetrics {
        fn log_raw_temperature(&self, temperature: Celsius) {
            assert_eq!(
                self.expected_logs_mut()
                    .pop_front()
                    .context("Received unexpected call to `log_raw_temperature`")
                    .unwrap(),
                LogType::LogRawTemperature(temperature),
            );
        }

        fn log_throttle_start(&self, timestamp: Nanoseconds) {
            assert_eq!(
                self.expected_logs_mut()
                    .pop_front()
                    .context("Received unexpected call to `log_throttle_start`")
                    .unwrap(),
                LogType::LogThrottleStart(timestamp)
            );
        }

        fn log_throttle_end_mitigated(&self, timestamp: Nanoseconds) {
            assert_eq!(
                self.expected_logs_mut()
                    .pop_front()
                    .context("Received unexpected call to `log_throttle_end_mitigated`")
                    .unwrap(),
                LogType::LogThrottleEndMitigated(timestamp)
            );
        }

        fn log_throttle_end_shutdown(&self, timestamp: Nanoseconds) {
            assert_eq!(
                self.expected_logs_mut()
                    .pop_front()
                    .context("Received unexpected call to `log_throttle_end_shutdown`")
                    .unwrap(),
                LogType::LogThrottleEndShutdown(timestamp)
            );
        }
    }
}
