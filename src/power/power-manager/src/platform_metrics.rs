// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::{Celsius, Microseconds, Nanoseconds, Seconds};
use crate::utils::{get_current_timestamp, CobaltIntHistogram, CobaltIntHistogramConfig};
use anyhow::{format_err, Result};
use async_trait::async_trait;
use fuchsia_async as fasync;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use fuchsia_inspect as inspect;
use futures::future::LocalBoxFuture;
use futures::stream::FuturesUnordered;
use futures::{FutureExt, StreamExt};
use power_manager_metrics::power_manager_metrics as power_metrics_registry;
use power_metrics_registry::ThermalLimitResultMetricDimensionResult as thermal_limit_result;
use serde_derive::Deserialize;
use serde_json as json;
use std::cell::RefCell;
use std::collections::{HashMap, VecDeque};
use std::rc::Rc;

/// Node: PlatformMetrics
///
/// Summary: This node collects and publishes metric data that is interesting to report and monitor
///   in the platform. Initially, this includes things like CPU temperature and system throttling
///   state. The data that is published by this node may be load-bearing in dashboard and metrics
///   pipelines.
///
/// Handles Messages:
///     - LogThrottleStart
///     - LogThrottleEndMitigated
///     - LogThrottleEndShutdown
///
/// Sends Messages:
///     - ReadTemperature
///
/// FIDL dependencies: N/A

pub struct PlatformMetricsBuilder<'a> {
    cpu_temperature_poll_interval: Seconds,
    cobalt_sender: Option<CobaltSender>,
    inspect_root: Option<&'a inspect::Node>,
    cpu_temperature_handler_node: Rc<dyn Node>,
}

impl<'a> PlatformMetricsBuilder<'a> {
    #[cfg(test)]
    fn new(cpu_temperature_handler_node: Rc<dyn Node>) -> Self {
        Self {
            cpu_temperature_handler_node,
            cpu_temperature_poll_interval: Seconds(1.0),
            cobalt_sender: None,
            inspect_root: None,
        }
    }

    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Config {
            cpu_temperature_poll_interval_s: f64,
        }

        #[derive(Deserialize)]
        struct Dependencies {
            cpu_temperature_handler_node: String,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
            dependencies: Dependencies,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        Self {
            cpu_temperature_poll_interval: Seconds(data.config.cpu_temperature_poll_interval_s),
            cpu_temperature_handler_node: nodes[&data.dependencies.cpu_temperature_handler_node]
                .clone(),
            cobalt_sender: None,
            inspect_root: None,
        }
    }

    #[cfg(test)]
    fn with_cobalt_sender(mut self, cobalt_sender: CobaltSender) -> Self {
        self.cobalt_sender = Some(cobalt_sender);
        self
    }

    #[cfg(test)]
    fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    pub fn build<'b>(
        self,
        futures_out: &FuturesUnordered<LocalBoxFuture<'b, ()>>,
    ) -> Result<Rc<PlatformMetrics>> {
        let cobalt_sender = self.cobalt_sender.unwrap_or_else(|| {
            let (cobalt_sender, sender_future) = CobaltConnector::default()
                .serve(ConnectionType::project_id(power_metrics_registry::PROJECT_ID));

            // Spawn a task to handle sending data to the Cobalt service
            fasync::Task::local(sender_future).detach();

            cobalt_sender
        });

        let historical_max_cpu_temperature = {
            let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());
            let platform_metrics_node = inspect_root.create_child("platform_metrics");
            let historical_max_cpu_temperature =
                HistoricalMaxCpuTemperature::new(&platform_metrics_node);

            inspect_root.record(platform_metrics_node);
            historical_max_cpu_temperature
        };

        let node = Rc::new(PlatformMetrics {
            inner: RefCell::new(PlatformMetricsInner {
                cobalt: CobaltPlatformMetrics {
                    sender: cobalt_sender,
                    throttle_start_time: None,
                    temperature_histogram: CobaltIntHistogram::new(CobaltIntHistogramConfig {
                        floor: power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_FLOOR,
                        num_buckets:
                            power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_NUM_BUCKETS,
                        step_size: power_metrics_registry::RAW_TEMPERATURE_INT_BUCKETS_STEP_SIZE,
                    }),
                },
                inspect: InspectPlatformMetrics { historical_max_cpu_temperature },
            }),
            cpu_temperature_handler: self.cpu_temperature_handler_node,
            cpu_temperature_poll_interval: self.cpu_temperature_poll_interval,
        });

        futures_out.push(node.clone().cpu_temperature_polling_loop());
        Ok(node)
    }
}

pub struct PlatformMetrics {
    /// CPU temperature handler and poll interval for reporting historical CPU temperature in Cobalt
    /// and Inspect.
    cpu_temperature_handler: Rc<dyn Node>,
    cpu_temperature_poll_interval: Seconds,

    inner: RefCell<PlatformMetricsInner>,
}

struct PlatformMetricsInner {
    cobalt: CobaltPlatformMetrics,
    inspect: InspectPlatformMetrics,
}

struct CobaltPlatformMetrics {
    /// Sends Cobalt events to the Cobalt FIDL service.
    sender: CobaltSender,

    /// Timestamp of the start of a throttling duration.
    throttle_start_time: Option<Nanoseconds>,

    /// Histogram of raw CPU temperature readings.
    temperature_histogram: CobaltIntHistogram,
}

struct InspectPlatformMetrics {
    /// Tracks the max CPU temperature observed over the last 60 seconds and writes the most recent
    /// two values to Inspect.
    historical_max_cpu_temperature: HistoricalMaxCpuTemperature,
}

impl PlatformMetrics {
    /// Number of temperature readings before dispatching a Cobalt event.
    const NUM_TEMPERATURE_READINGS: u32 = 100;

    /// Returns a future that calls `poll_cpu_temperature` at `cpu_temperature_poll_interval`.
    fn cpu_temperature_polling_loop<'a>(self: Rc<Self>) -> LocalBoxFuture<'a, ()> {
        let mut periodic_timer = fasync::Interval::new(self.cpu_temperature_poll_interval.into());

        async move {
            while let Some(()) = periodic_timer.next().await {
                log_if_err!(self.poll_cpu_temperature().await, "Error while polling temperature");
            }
        }
        .boxed_local()
    }

    /// Polls `cpu_temperature_handler` for temperature and logs it into Cobalt and Inspect.
    async fn poll_cpu_temperature(&self) -> Result<()> {
        match self.send_message(&self.cpu_temperature_handler, &Message::ReadTemperature).await {
            Ok(MessageReturn::ReadTemperature(temperature)) => {
                self.log_raw_cpu_temperature(temperature);
                Ok(())
            }
            e => Err(format_err!("Error polling CPU temperature: {:?}", e)),
        }
    }

    /// Logs the provided temperature value into Cobalt and Inspect.
    fn log_raw_cpu_temperature(&self, temperature: Celsius) {
        let mut data = self.inner.borrow_mut();
        data.cobalt.temperature_histogram.add_data(temperature.0 as i64);
        if data.cobalt.temperature_histogram.count() == Self::NUM_TEMPERATURE_READINGS {
            let hist_data = data.cobalt.temperature_histogram.get_data();
            data.cobalt.sender.log_int_histogram(
                power_metrics_registry::RAW_TEMPERATURE_METRIC_ID,
                (),
                hist_data,
            );
            data.cobalt.temperature_histogram.clear();
        }

        data.inspect.historical_max_cpu_temperature.log_raw_cpu_temperature(temperature);
    }

    /// Log the start of a thermal throttling duration. Since the timestamp is recorded and used to
    /// track duration of throttling events, it is invalid to call this function consecutively
    /// without first ending the existing throttling duration by calling
    /// `handle_log_throttle_end_[mitigated|shutdown]` first. If the invalid scenario is
    /// encountered, the function will panic on debug builds or have no effect otherwise.
    fn handle_log_throttle_start(
        &self,
        timestamp: Nanoseconds,
    ) -> Result<MessageReturn, PowerManagerError> {
        let mut data = self.inner.borrow_mut();
        crate::log_if_false_and_debug_assert!(
            data.cobalt.throttle_start_time.is_none(),
            "handle_log_throttle_start called before ending previous throttle"
        );

        // Record the new timestamp only if it was previously None
        data.cobalt.throttle_start_time = data.cobalt.throttle_start_time.or(Some(timestamp));

        Ok(MessageReturn::LogThrottleStart)
    }

    /// Log the end of a thermal throttling duration due to successful mitigation. To track the
    /// elapsed time of a throttling event, it is expected that `handle_log_throttle_start` is
    /// called first. Failure to do so results in a panic on debug builds or missed Cobalt events
    /// for the elapsed throttling time metric otherwise.
    fn handle_log_throttle_end_mitigated(
        &self,
        timestamp: Nanoseconds,
    ) -> Result<MessageReturn, PowerManagerError> {
        crate::log_if_false_and_debug_assert!(
            self.inner.borrow().cobalt.throttle_start_time.is_some(),
            "handle_log_throttle_end_mitigated called without 
            first calling handle_log_throttle_start"
        );
        self.log_throttle_end_with_result(thermal_limit_result::Mitigated, timestamp);
        Ok(MessageReturn::LogThrottleEndMitigated)
    }

    /// Log the end of a thermal throttling duration due to a shutdown. To track the elapsed time of
    /// a throttling event, it is expected that `handle_log_throttle_start` is called first.
    /// However, this is not strictly enforced because in a theoretical scenario we could see a
    /// LogThrottleEnd* message from the ThermalShutdown node without the ThermalPolicy node having
    /// first sent a LogThrottleStart message.
    fn handle_log_throttle_end_shutdown(
        &self,
        timestamp: Nanoseconds,
    ) -> Result<MessageReturn, PowerManagerError> {
        self.log_throttle_end_with_result(thermal_limit_result::Shutdown, timestamp);
        Ok(MessageReturn::LogThrottleEndShutdown)
    }

    /// Log the end of a thermal throttling duration with a specified reason. To track the elapsed
    /// time of a throttling event, it is expected that `handle_log_throttle_start` is called first.
    /// However, this is not strictly enforced because of a special case described in
    /// `handle_log_throttle_end_shutdown`.
    fn log_throttle_end_with_result(&self, result: thermal_limit_result, timestamp: Nanoseconds) {
        let mut data = self.inner.borrow_mut();

        if let Some(time) = data.cobalt.throttle_start_time.take() {
            let elapsed_time = timestamp - time;
            if elapsed_time >= Nanoseconds(0) {
                data.cobalt.sender.log_elapsed_time(
                    power_metrics_registry::THERMAL_LIMITING_ELAPSED_TIME_METRIC_ID,
                    (),
                    Microseconds::from(elapsed_time).0,
                );
            } else {
                crate::log_if_false_and_debug_assert!(false, "Elapsed time must not be negative");
            }
        }

        data.cobalt
            .sender
            .log_event(power_metrics_registry::THERMAL_LIMIT_RESULT_METRIC_ID, vec![result as u32]);
    }
}

#[async_trait(?Send)]
impl Node for PlatformMetrics {
    fn name(&self) -> String {
        "PlatformMetrics".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::LogThrottleStart(timestamp) => self.handle_log_throttle_start(*timestamp),
            Message::LogThrottleEndMitigated(timestamp) => {
                self.handle_log_throttle_end_mitigated(*timestamp)
            }
            Message::LogThrottleEndShutdown(timestamp) => {
                self.handle_log_throttle_end_shutdown(*timestamp)
            }
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

/// Tracks the max CPU temperature observed over the last 60 seconds and writes the most recent two
/// values to Inspect.
struct HistoricalMaxCpuTemperature {
    /// Stores the two max temperature values.
    inspect_node: inspect::Node,
    entries: VecDeque<inspect::IntProperty>,

    /// Number of samples before recording the max temperature to Inspect.
    max_sample_count: usize,

    /// Current number of samples observed. Resets back to zero after reaching `max_sample_count`.
    sample_count: usize,

    /// Current observed max temperature.
    max_temperature: Celsius,
}

impl HistoricalMaxCpuTemperature {
    /// Number of historical max CPU temperature values to maintain in Inspect.
    const RECORD_COUNT: usize = 2;

    /// Default number of temperature samples to process with `log_raw_cpu_temperature` before
    /// publishing a new max value into Inspect.
    const DEFAULT_MAX_SAMPLE_COUNT: usize = 60;

    const INVALID_TEMPERATURE: Celsius = Celsius(f64::NEG_INFINITY);

    fn new(platform_metrics_root: &inspect::Node) -> Self {
        Self::new_with_max_sample_count(platform_metrics_root, Self::DEFAULT_MAX_SAMPLE_COUNT)
    }

    fn new_with_max_sample_count(
        platform_metrics_root: &inspect::Node,
        max_sample_count: usize,
    ) -> Self {
        Self {
            inspect_node: platform_metrics_root.create_child("historical_max_cpu_temperature_c"),
            entries: VecDeque::with_capacity(Self::RECORD_COUNT),
            max_sample_count,
            sample_count: 0,
            max_temperature: Self::INVALID_TEMPERATURE,
        }
    }

    /// Logs a raw CPU temperature reading and updates the max temperature observed. After
    /// `max_sample_count` times, records the max temperature to Inspect and resets the sample count
    /// and max values.
    fn log_raw_cpu_temperature(&mut self, temperature: Celsius) {
        if temperature > self.max_temperature {
            self.max_temperature = temperature
        }

        self.sample_count += 1;
        if self.sample_count == self.max_sample_count {
            self.record_temperature_entry(self.max_temperature);
            self.sample_count = 0;
            self.max_temperature = Self::INVALID_TEMPERATURE;
        }
    }

    /// Records the temperature to Inspect while removing stale records according to `RECORD_COUNT`.
    /// The current timestamp in seconds after system boot is used as the property key, and
    /// temperature is recorded in Celsius as an integer.
    fn record_temperature_entry(&mut self, temperature: Celsius) {
        while self.entries.len() >= Self::RECORD_COUNT {
            self.entries.pop_front();
        }

        let time = Seconds::from(get_current_timestamp()).0 as i64;
        let temperature = temperature.0 as i64;
        self.entries.push_back(self.inspect_node.create_int(time.to_string(), temperature));
    }
}

#[cfg(test)]
mod historical_max_cpu_temperature_tests {
    use super::*;
    use inspect::assert_data_tree;

    /// Tests that after each max temperature recording, the max temperature is reset for the next
    /// round. The test would fail if HistoricalMaxCpuTemperature was not resetting the previous max
    /// temperature at the end of each N samples.
    #[test]
    fn test_reset_max_temperature_after_sample_count() {
        let executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let inspector = inspect::Inspector::new();
        let mut max_temperatures =
            HistoricalMaxCpuTemperature::new_with_max_sample_count(inspector.root(), 10);

        // Log 10 samples to dispatch the first max temperature reading
        executor.set_fake_time(Seconds(0.0).into());
        for _ in 0..10 {
            max_temperatures.log_raw_cpu_temperature(Celsius(50.0));
        }

        // Log 10 more samples to disaptch the second max temperature reading (with a lower max
        // temperature)
        executor.set_fake_time(Seconds(1.0).into());
        for _ in 0..10 {
            max_temperatures.log_raw_cpu_temperature(Celsius(40.0));
        }

        assert_data_tree!(
            inspector,
            root: {
                historical_max_cpu_temperature_c: {
                    "0": 50i64,
                    "1": 40i64
                }
            }
        );
    }

    /// Tests that the max CPU temperature isn't logged until after the specified number of
    /// temperature samples are observed.
    #[test]
    fn test_dispatch_reading_after_n_samples() {
        let executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let inspector = inspect::Inspector::new();
        let mut max_temperatures =
            HistoricalMaxCpuTemperature::new_with_max_sample_count(inspector.root(), 10);

        executor.set_fake_time(Seconds(0.0).into());

        // Tree is initially empty
        assert_data_tree!(
            inspector,
            root: {
                historical_max_cpu_temperature_c: {}
            }
        );

        // Observe n-1 temperature samples
        for _ in 0..9 {
            max_temperatures.log_raw_cpu_temperature(Celsius(50.0));
        }

        // Tree is still empty
        assert_data_tree!(
            inspector,
            root: {
                historical_max_cpu_temperature_c: {}
            }
        );

        // After one more temperature sample, the max temperature should be logged
        max_temperatures.log_raw_cpu_temperature(Celsius(50.0));
        assert_data_tree!(
            inspector,
            root: {
                historical_max_cpu_temperature_c: {
                    "0": 50i64
                }
            }
        );
    }

    /// Tests that there are never more than the two most recent max temperature recordings logged
    /// into Inspect.
    #[test]
    fn test_max_record_count() {
        let executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let inspector = inspect::Inspector::new();
        let mut max_temperatures =
            HistoricalMaxCpuTemperature::new_with_max_sample_count(inspector.root(), 2);

        executor.set_fake_time(Seconds(0.0).into());
        for _ in 0..2 {
            max_temperatures.log_raw_cpu_temperature(Celsius(50.0));
        }

        executor.set_fake_time(Seconds(1.0).into());
        for _ in 0..2 {
            max_temperatures.log_raw_cpu_temperature(Celsius(50.0));
        }

        executor.set_fake_time(Seconds(2.0).into());
        for _ in 0..2 {
            max_temperatures.log_raw_cpu_temperature(Celsius(50.0));
        }

        assert_data_tree!(
            inspector,
            root: {
                historical_max_cpu_temperature_c: {
                    "1": 50i64,
                    "2": 50i64
                }
            }
        );
    }

    /// Tests that the actual max value is recorded after varying temperature values were logged.
    #[test]
    fn test_max_temperature_selection() {
        let executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        let inspector = inspect::Inspector::new();
        let mut max_temperatures =
            HistoricalMaxCpuTemperature::new_with_max_sample_count(inspector.root(), 3);

        executor.set_fake_time(Seconds(0.0).into());
        max_temperatures.log_raw_cpu_temperature(Celsius(10.0));
        max_temperatures.log_raw_cpu_temperature(Celsius(30.0));
        max_temperatures.log_raw_cpu_temperature(Celsius(20.0));

        assert_data_tree!(
            inspector,
            root: {
                historical_max_cpu_temperature_c: {
                    "0": 30i64
                }
            }
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::mock_node::{create_dummy_node, MessageMatcher, MockNodeMaker};
    use crate::types::Seconds;
    use crate::{msg_eq, msg_ok_return};
    use fidl_fuchsia_cobalt::{CobaltEvent, Event, EventPayload};
    use fuchsia_inspect::assert_data_tree;

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "PlatformMetrics",
            "name": "platform_metrics",
            "config": {
              "cpu_temperature_poll_interval_s": 1
            },
            "dependencies": {
              "cpu_temperature_handler_node": "soc_pll_thermal"
            }
        });

        let mut nodes: HashMap<String, Rc<dyn Node>> = HashMap::new();
        nodes.insert("soc_pll_thermal".to_string(), create_dummy_node());
        let _ = PlatformMetricsBuilder::new_from_json(json_data, &nodes);
    }

    /// Tests that we can send `LogThrottleStart` a second time if we first send `LogThrottleEnd`.
    #[fasync::run_singlethreaded(test)]
    async fn test_throttle_restart() {
        let platform_metrics = PlatformMetricsBuilder::new(create_dummy_node())
            .build(&FuturesUnordered::new())
            .unwrap();

        assert!(platform_metrics
            .handle_message(&Message::LogThrottleStart(Nanoseconds(1000)))
            .await
            .is_ok());
        assert!(platform_metrics
            .handle_message(&Message::LogThrottleEndMitigated(Nanoseconds(2000)))
            .await
            .is_ok());
        assert!(platform_metrics
            .handle_message(&Message::LogThrottleStart(Nanoseconds(3000)))
            .await
            .is_ok());
    }

    /// Tests that sending `LogThrottleStart` twice without first calling `LogThrottleEnd` causes a
    /// panic in the debug configuration.
    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "throttle_start called before ending previous throttle")]
    #[cfg(debug_assertions)]
    async fn test_double_start_panic() {
        let platform_metrics = PlatformMetricsBuilder::new(create_dummy_node())
            .build(&FuturesUnordered::new())
            .unwrap();

        assert!(platform_metrics
            .handle_message(&Message::LogThrottleStart(Nanoseconds(0)))
            .await
            .is_ok());

        // This second consecutive LogThrottleStart should cause the expected panic
        let _ = platform_metrics.handle_message(&Message::LogThrottleStart(Nanoseconds(0))).await;
    }

    /// Tests that a negative throttling elapsed time metric panics in the debug configuration.
    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "Elapsed time must not be negative")]
    #[cfg(debug_assertions)]
    async fn test_negative_elapsed_time() {
        let platform_metrics = PlatformMetricsBuilder::new(create_dummy_node())
            .build(&FuturesUnordered::new())
            .unwrap();

        assert!(platform_metrics
            .handle_message(&Message::LogThrottleStart(Nanoseconds(1000)))
            .await
            .is_ok());

        // Sending an earlier time here should cause the expected panic
        let _ = platform_metrics
            .handle_message(&Message::LogThrottleEndMitigated(Nanoseconds(900)))
            .await;
    }

    /// Tests that two Cobalt events for thermal_limiting_elapsed_time and thermal_limit_result are
    /// dispatched after log calls corresponding to a single throttling duration.
    #[fasync::run_singlethreaded(test)]
    async fn test_throttle_elapsed_time() {
        let (sender, mut receiver) = futures::channel::mpsc::channel(10);
        let platform_metrics = PlatformMetricsBuilder::new(create_dummy_node())
            .with_cobalt_sender(CobaltSender::new(sender))
            .build(&FuturesUnordered::new())
            .unwrap();

        platform_metrics
            .handle_message(&Message::LogThrottleStart(Seconds(0.0).into()))
            .await
            .unwrap();
        platform_metrics
            .handle_message(&Message::LogThrottleEndMitigated(Seconds(1.0).into()))
            .await
            .unwrap();

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

    /// Tests that a Cobalt event for thermal_limit_result is dispatched after the node receives a
    /// LogThrottleEndShutdown message.
    #[fasync::run_singlethreaded(test)]
    async fn test_throttle_end_shutdown() {
        let (sender, mut receiver) = futures::channel::mpsc::channel(10);
        let platform_metrics = PlatformMetricsBuilder::new(create_dummy_node())
            .with_cobalt_sender(CobaltSender::new(sender))
            .build(&FuturesUnordered::new())
            .unwrap();

        platform_metrics
            .handle_message(&Message::LogThrottleEndShutdown(Seconds(1.0).into()))
            .await
            .unwrap();

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
    #[fasync::run_singlethreaded(test)]
    async fn test_raw_temperature() {
        let mut mock_maker = MockNodeMaker::new();
        let (sender, mut receiver) = futures::channel::mpsc::channel(10);

        let mock_temperature_handler = mock_maker.make("MockTemperatureHandler", vec![]);
        let platform_metrics = PlatformMetricsBuilder::new(mock_temperature_handler.clone())
            .with_cobalt_sender(CobaltSender::new(sender))
            .build(&FuturesUnordered::new())
            .unwrap();
        let num_temperature_readings = PlatformMetrics::NUM_TEMPERATURE_READINGS + 1;

        for _ in 0..num_temperature_readings {
            mock_temperature_handler.add_msg_response_pair((
                msg_eq!(ReadTemperature),
                msg_ok_return!(ReadTemperature(Celsius(50.0))),
            ));
            platform_metrics.poll_cpu_temperature().await.unwrap();
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

    /// Tests that the "platform_metrics" Inspect node is published along with the expected
    /// "historical_max_cpu_temperature_c" entry.
    #[test]
    fn test_inspect_data() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(Seconds(10.0).into());

        let mut mock_maker = MockNodeMaker::new();
        let mock_cpu_temperature = mock_maker.make("MockCpuTemperature", vec![]);
        let inspector = inspect::Inspector::new();
        let platform_metrics = PlatformMetricsBuilder::new(mock_cpu_temperature.clone())
            .with_inspect_root(inspector.root())
            .build(&FuturesUnordered::new())
            .unwrap();

        for _ in 0..60 {
            mock_cpu_temperature.add_msg_response_pair((
                msg_eq!(ReadTemperature),
                msg_ok_return!(ReadTemperature(Celsius(40.0))),
            ));

            assert!(executor
                .run_until_stalled(&mut Box::pin(platform_metrics.poll_cpu_temperature()))
                .is_ready());
        }

        assert_data_tree!(
            inspector,
            root: {
                platform_metrics: {
                    historical_max_cpu_temperature_c: {
                        "10": 40i64
                    }
                },
            }
        );
    }
}
