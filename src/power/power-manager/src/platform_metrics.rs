// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::ok_or_default_err;
use crate::types::{Celsius, Seconds, ThermalLoad, Watts};
use crate::utils::{get_current_timestamp, CobaltIntHistogram, CobaltIntHistogramConfig};
use anyhow::{format_err, Context, Error, Result};
use async_trait::async_trait;
use fidl_contrib::{protocol_connector::ProtocolSender, ProtocolConnector};
use fidl_fuchsia_metrics::MetricEvent;
use fuchsia_async as fasync;
use fuchsia_cobalt_builders::MetricEventExt;
use fuchsia_inspect::{self as inspect, HistogramProperty, LinearHistogramParams, Property};
use futures::future::{self, LocalBoxFuture};
use futures::stream::FuturesUnordered;
use futures::{FutureExt, StreamExt};
use log::*;
use power_manager_metrics::power_manager_metrics as power_metrics_registry;
use power_metrics_registry::ThermalLimitResultMigratedMetricDimensionResult as thermal_limit_result;
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
///     - LogPlatformMetric
///
/// Sends Messages:
///     - ReadTemperature
///     - FileCrashReport
///
/// FIDL dependencies: N/A

#[derive(Default)]
pub struct PlatformMetricsBuilder<'a> {
    cpu_temperature_poll_interval: Seconds,
    cobalt_sender: Option<ProtocolSender<MetricEvent>>,
    inspect_root: Option<&'a inspect::Node>,
    cpu_temperature_handler: Option<Rc<dyn Node>>,
    crash_report_handler: Option<Rc<dyn Node>>,
    throttle_debounce_timeout: Seconds,
}

pub struct CobaltConnectedService;
impl fidl_contrib::protocol_connector::ConnectedProtocol for CobaltConnectedService {
    type Protocol = fidl_fuchsia_metrics::MetricEventLoggerProxy;
    type ConnectError = Error;
    type Message = fidl_fuchsia_metrics::MetricEvent;
    type SendError = Error;

    fn get_protocol<'a>(
        &'a mut self,
    ) -> future::BoxFuture<'a, Result<fidl_fuchsia_metrics::MetricEventLoggerProxy, Error>> {
        async {
            let (logger_proxy, server_end) =
                fidl::endpoints::create_proxy().context("failed to create proxy endpoints")?;
            let metric_event_logger_factory = fuchsia_component::client::connect_to_protocol::<
                fidl_fuchsia_metrics::MetricEventLoggerFactoryMarker,
            >()
            .context("Failed to connect to fuchsia::metrics::MetricEventLoggerFactory")?;

            metric_event_logger_factory
                .create_metric_event_logger(
                    fidl_fuchsia_metrics::ProjectSpec {
                        project_id: Some(power_metrics_registry::PROJECT_ID),
                        ..fidl_fuchsia_metrics::ProjectSpec::EMPTY
                    },
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
        protocol: &'a fidl_fuchsia_metrics::MetricEventLoggerProxy,
        mut msg: fidl_fuchsia_metrics::MetricEvent,
    ) -> future::BoxFuture<'a, Result<(), Error>> {
        async move {
            let fut = protocol.log_metric_events(&mut std::iter::once(&mut msg));
            fut.await?.map_err(|e| format_err!("Failed to log metric {e:?}"))?;
            Ok(())
        }
        .boxed()
    }
}

impl<'a> PlatformMetricsBuilder<'a> {
    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Config {
            cpu_temperature_poll_interval_s: f64,
            throttle_debounce_timeout_s: f64,
        }

        #[derive(Deserialize)]
        struct Dependencies {
            cpu_temperature_handler_node: String,
            crash_report_handler_node: String,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
            dependencies: Dependencies,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        Self {
            cpu_temperature_poll_interval: Seconds(data.config.cpu_temperature_poll_interval_s),
            cpu_temperature_handler: Some(
                nodes[&data.dependencies.cpu_temperature_handler_node].clone(),
            ),
            cobalt_sender: None,
            inspect_root: None,
            crash_report_handler: Some(nodes[&data.dependencies.crash_report_handler_node].clone()),
            throttle_debounce_timeout: Seconds(data.config.throttle_debounce_timeout_s),
        }
    }

    pub fn build<'b>(
        self,
        futures_out: &FuturesUnordered<LocalBoxFuture<'b, ()>>,
    ) -> Result<Rc<PlatformMetrics>> {
        let cpu_temperature_handler = ok_or_default_err!(self.cpu_temperature_handler)?;
        let crash_report_handler = ok_or_default_err!(self.crash_report_handler)?;

        let cobalt_sender = self.cobalt_sender.unwrap_or_else(|| {
            let (cobalt_sender, sender_future) =
                ProtocolConnector::new(CobaltConnectedService).serve_and_log_errors();

            // Spawn a task to handle sending data to the Cobalt service
            fasync::Task::local(sender_future).detach();

            cobalt_sender
        });

        let inspect_platform_metrics = {
            let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());
            let platform_metrics_node = inspect_root.create_child("platform_metrics");
            let historical_max_cpu_temperature =
                HistoricalMaxCpuTemperature::new(&platform_metrics_node);
            let throttle_history = InspectThrottleHistory::new(&platform_metrics_node);
            let throttling_state = InspectThrottlingState::new(&platform_metrics_node);
            inspect_root.record(platform_metrics_node);

            InspectPlatformMetrics {
                historical_max_cpu_temperature,
                throttle_history,
                throttling_state,
            }
        };

        let node = Rc::new(PlatformMetrics {
            inner: Rc::new(RefCell::new(PlatformMetricsInner {
                cobalt: CobaltPlatformMetrics {
                    sender: cobalt_sender,
                    temperature_histogram: CobaltIntHistogram::new(CobaltIntHistogramConfig {
                        floor: power_metrics_registry::RAW_TEMPERATURE_MIGRATED_INT_BUCKETS_FLOOR,
                        num_buckets:
                            power_metrics_registry::RAW_TEMPERATURE_MIGRATED_INT_BUCKETS_NUM_BUCKETS,
                        step_size:
                            power_metrics_registry::RAW_TEMPERATURE_MIGRATED_INT_BUCKETS_STEP_SIZE,
                    }),
                },
                inspect: inspect_platform_metrics,
                throttle_debounce_task: None,
            })),
            cpu_temperature_handler,
            cpu_temperature_poll_interval: self.cpu_temperature_poll_interval,
            crash_report_handler,
            throttle_debounce_timeout: self.throttle_debounce_timeout,
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

    /// The node used for filing a crash report. It is expected that this node responds to the
    /// `FileCrashReport` message.
    crash_report_handler: Rc<dyn Node>,

    /// Time to wait after receiving the `ThrottlingResultMitigated` event before updating metrics.
    /// Used to reduce noise when a device is hovering around the throttling threshold.
    throttle_debounce_timeout: Seconds,

    /// Mutable inner state.
    inner: Rc<RefCell<PlatformMetricsInner>>,
}

struct PlatformMetricsInner {
    /// Holds structures for tracking/recording Cobalt metrics.
    cobalt: CobaltPlatformMetrics,

    /// Holds structures for tracking/recording Inspect metrics.
    inspect: InspectPlatformMetrics,

    /// Holds the debounce timer task, if one is active.
    throttle_debounce_task: Option<fasync::Task<()>>,
}

struct CobaltPlatformMetrics {
    /// Sends Cobalt events to the Cobalt FIDL service.
    sender: ProtocolSender<MetricEvent>,

    /// Histogram of raw CPU temperature readings.
    temperature_histogram: CobaltIntHistogram,
}

struct InspectPlatformMetrics {
    /// Tracks the max CPU temperature observed over the last 60 seconds and writes the most recent
    /// two values to Inspect.
    historical_max_cpu_temperature: HistoricalMaxCpuTemperature,

    /// Tracks the current throttling state and writes it into Inspect.
    throttling_state: InspectThrottlingState,

    /// Tracks the last 10 periods of thermal throttling (start/end times and a histogram of thermal
    /// loads) and writes them into Inspect.
    throttle_history: InspectThrottleHistory,
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
            data.cobalt.sender.send(
                MetricEvent::builder(power_metrics_registry::RAW_TEMPERATURE_MIGRATED_METRIC_ID)
                    .as_integer_histogram(hist_data),
            );
            data.cobalt.temperature_histogram.clear();
        }

        data.inspect.historical_max_cpu_temperature.log_raw_cpu_temperature(temperature);
    }

    fn handle_log_platform_metric(
        &self,
        metric: &PlatformMetric,
    ) -> Result<MessageReturn, PowerManagerError> {
        match metric {
            PlatformMetric::ThrottlingActive => self.handle_log_throttling_active(),
            PlatformMetric::ThrottlingResultMitigated => {
                self.handle_log_throttling_result_mitigated()
            }
            PlatformMetric::ThrottlingResultShutdown => {
                self.handle_log_throttling_result_shutdown()
            }
            PlatformMetric::ThermalLoad(thermal_load, driver_path) => {
                self.handle_log_thermal_load(*thermal_load, &driver_path)
            }

            // TODO(fxbug.dev/98245): log these metrics in a useful way
            PlatformMetric::AvailablePower(_) | PlatformMetric::CpuPowerUsage(_, _) => {}
        };

        Ok(MessageReturn::LogPlatformMetric)
    }

    /// Log the start of thermal throttling.
    ///
    /// The expectation is that this function should not be called consecutively without first
    /// ending the previous throttling period by calling `handle_log_throttling_result_mitigated` or
    /// `handle_log_throttling_result_shutdown`.
    fn handle_log_throttling_active(&self) {
        info!("Throttling active");

        let mut data = self.inner.borrow_mut();
        data.throttle_debounce_task = None;
        data.inspect.throttling_state.set_active();
        data.inspect.throttle_history.set_active();
    }

    /// Log the end of thermal throttling due to successful mitigation.
    ///
    /// Calling this function updates the current throttling state to "debounce" and starts a new
    /// `throttle_debounce_task` which will further update metrics and file a crash report after
    /// `throttle_debounce_timeout` has elapsed.
    fn handle_log_throttling_result_mitigated(&self) {
        info!("Throttling result: mitigated");
        self.inner.borrow_mut().inspect.throttling_state.set_debounce();

        // Clone the required data to move into the new `throttle_debounce_task`
        let throttle_debounce_timeout = self.throttle_debounce_timeout;
        let crash_report_handler = self.crash_report_handler.clone();
        let inner = self.inner.clone();

        self.inner.borrow_mut().throttle_debounce_task = Some(fasync::Task::local(async move {
            info!("Starting throttle debounce timer ({:?})", throttle_debounce_timeout);
            fasync::Timer::new(fasync::Time::after(throttle_debounce_timeout.into())).await;
            info!("Throttling debounce timer expired");

            // File a crash report with the signature "fuchsia-thermal-throttle".
            log_if_err!(
                crash_report_handler
                    .handle_message(&Message::FileCrashReport(
                        "fuchsia-thermal-throttle".to_string()
                    ))
                    .await,
                "Failed to file crash report"
            );

            Self::log_throttling_result(inner, thermal_limit_result::Mitigated);
        }));
    }

    /// Log the end of thermal throttling due to a shutdown.
    fn handle_log_throttling_result_shutdown(&self) {
        info!("Throttling result: shutdown");
        Self::log_throttling_result(self.inner.clone(), thermal_limit_result::Shutdown);
    }

    /// Log the end of thermal throttling with a specified reason.
    ///
    /// Calling this function updates the throttling properties in Inspect and dispatches a new
    /// Cobalt event for the `thermal_limit_result` metric.
    fn log_throttling_result(
        inner: Rc<RefCell<PlatformMetricsInner>>,
        result: thermal_limit_result,
    ) {
        let mut data = inner.borrow_mut();
        data.inspect.throttling_state.set_inactive();
        data.inspect.throttle_history.set_inactive();
        data.cobalt.sender.send(
            MetricEvent::builder(power_metrics_registry::THERMAL_LIMIT_RESULT_MIGRATED_METRIC_ID)
                .with_event_codes(result)
                .as_occurrence(1),
        );
    }

    /// Log a thermal load value.
    ///
    /// Calling this function updates the thermal load histogram being tracked by `throttle_history`
    /// while thermal throttling is active.
    ///
    /// `driver_path` is provided to eventually record multiple thermal load histograms, but it is
    /// not currently used.
    fn handle_log_thermal_load(&self, thermal_load: ThermalLoad, _driver_path: &str) {
        self.inner.borrow_mut().inspect.throttle_history.record_thermal_load(thermal_load);
    }
}

#[derive(Debug, PartialEq)]
pub enum PlatformMetric {
    /// Marks the start of thermal throttling.
    ThrottlingActive,

    /// Marks the end of thermal throttling due to successful mitigation.
    ThrottlingResultMitigated,

    /// Marks the end of thermal throttling due to critical shutdown.
    ThrottlingResultShutdown,

    /// Records a thermal load value for the given sensor path.
    ThermalLoad(crate::types::ThermalLoad, String),

    /// Records the current available power.
    AvailablePower(Watts),

    /// Records an amount of power used by the given CPU domain.
    CpuPowerUsage(String, Watts),
}

#[async_trait(?Send)]
impl Node for PlatformMetrics {
    fn name(&self) -> String {
        "PlatformMetrics".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::LogPlatformMetric(metric) => self.handle_log_platform_metric(metric),
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

/// Tracks the current thermal throttling state and writes it into Inspect.
struct InspectThrottlingState {
    throttling_state: inspect::StringProperty,
}

impl InspectThrottlingState {
    const THROTTLING_ACTIVE: &'static str = "throttled";
    const THROTTLING_INACTIVE: &'static str = "not throttled";
    const THROTTLING_DEBOUNCE: &'static str = "debounce";

    fn new(platform_metrics_root: &inspect::Node) -> Self {
        let throttling_state =
            platform_metrics_root.create_string("throttling_state", Self::THROTTLING_INACTIVE);
        Self { throttling_state }
    }

    fn set_active(&self) {
        self.throttling_state.set(Self::THROTTLING_ACTIVE);
    }

    fn set_inactive(&self) {
        self.throttling_state.set(Self::THROTTLING_INACTIVE);
    }

    fn set_debounce(&self) {
        self.throttling_state.set(Self::THROTTLING_DEBOUNCE);
    }
}

/// Captures and retains data from previous throttling events in a rolling buffer.
struct InspectThrottleHistory {
    /// The Inspect node that will be used as the parent for throttle event child nodes.
    root_node: inspect::Node,

    /// A running count of the number of throttle events ever captured in `throttle_history_list`.
    /// The count is always increasing, even when older throttle events are removed from the list.
    entry_count: usize,

    /// The maximum number of throttling events to keep in `throttle_history_list`.
    capacity: usize,

    /// State to track if throttling is currently active (used to ignore readings when throttling
    /// isn't active).
    throttling_active: bool,

    /// List to store the throttle entries.
    throttle_history_list: VecDeque<InspectThrottleHistoryEntry>,
}

impl InspectThrottleHistory {
    /// Rolling number of throttle events to store.
    const NUM_THROTTLE_EVENTS: usize = 10;

    fn new(platform_metrics_root: &inspect::Node) -> Self {
        Self::new_with_throttle_event_count(platform_metrics_root, Self::NUM_THROTTLE_EVENTS)
    }

    fn new_with_throttle_event_count(
        platform_metrics_root: &inspect::Node,
        throttle_event_count: usize,
    ) -> Self {
        Self {
            root_node: platform_metrics_root.create_child("throttle_history"),
            entry_count: 0,
            capacity: throttle_event_count,
            throttling_active: false,
            throttle_history_list: VecDeque::with_capacity(throttle_event_count),
        }
    }

    /// Mark the start of throttling.
    fn set_active(&mut self) {
        // Must have ended previous throttling
        if self.throttling_active {
            debug_assert!(false, "Must end previous throttling before setting active again");
            return;
        }

        // Begin a new throttling entry
        self.new_entry();

        self.throttling_active = true;
        self.throttle_history_list
            .back()
            .unwrap()
            .throttle_start_time
            .set(Seconds::from(get_current_timestamp()).0 as i64);
    }

    /// Mark the end of throttling.
    fn set_inactive(&mut self) {
        if self.throttling_active {
            self.throttle_history_list
                .back()
                .unwrap()
                .throttle_end_time
                .set(Seconds::from(get_current_timestamp()).0 as i64);
            self.throttling_active = false
        }
    }

    /// Begin a new throttling entry. Removes the oldest entry once we've reached
    /// InspectData::NUM_THROTTLE_EVENTS number of entries.
    fn new_entry(&mut self) {
        if self.throttle_history_list.len() >= self.capacity {
            self.throttle_history_list.pop_front();
        }

        let node = self.root_node.create_child(&self.entry_count.to_string());
        let entry = InspectThrottleHistoryEntry::new(node);
        self.throttle_history_list.push_back(entry);
        self.entry_count += 1;
    }

    /// Record the current thermal load. No-op unless throttling has been set active.
    fn record_thermal_load(&self, thermal_load: ThermalLoad) {
        if self.throttling_active {
            self.throttle_history_list
                .back()
                .unwrap()
                .thermal_load_hist
                .insert(thermal_load.0.into());
        }
    }
}

/// Stores data for a single throttle event.
struct InspectThrottleHistoryEntry {
    _node: inspect::Node,
    throttle_start_time: inspect::IntProperty,
    throttle_end_time: inspect::IntProperty,
    thermal_load_hist: inspect::UintLinearHistogramProperty,
}

impl InspectThrottleHistoryEntry {
    /// Creates a new InspectThrottleHistoryEntry which creates new properties under `node`.
    fn new(node: inspect::Node) -> Self {
        Self {
            throttle_start_time: node.create_int("throttle_start_time", 0),
            throttle_end_time: node.create_int("throttle_end_time", 0),
            thermal_load_hist: node.create_uint_linear_histogram(
                "thermal_load_hist",
                LinearHistogramParams { floor: 0, step_size: 5, buckets: 19 },
            ),
            _node: node,
        }
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
mod inspect_throttle_history_tests {
    use super::*;
    use fuchsia_inspect::assert_data_tree;

    /// Verifies that `InspectThrottleHistory` correctly rolls old entries out of its buffer.
    #[test]
    fn test_inspect_throttle_history_window() {
        // Need an executor for the `get_current_timestamp()` calls
        let executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Create a InspectThrottleHistory with capacity for only one throttling entry
        let inspector = inspect::Inspector::new();
        let mut throttle_history =
            InspectThrottleHistory::new_with_throttle_event_count(inspector.root(), 1);

        // Add a throttling entry
        executor.set_fake_time(Seconds(0.0).into());
        throttle_history.set_active();
        executor.set_fake_time(Seconds(1.0).into());
        throttle_history.set_inactive();

        assert_data_tree!(
            inspector,
            root: {
                throttle_history: {
                    "0": contains {
                        throttle_start_time: 0i64,
                        throttle_end_time: 1i64,
                    },
                }
            }
        );

        // Add a new throttling entry -- the old one should now be rolled out
        executor.set_fake_time(Seconds(2.0).into());
        throttle_history.set_active();
        executor.set_fake_time(Seconds(3.0).into());
        throttle_history.set_inactive();

        assert_data_tree!(
            inspector,
            root: {
                throttle_history: {
                    "1": contains {
                        throttle_start_time: 2i64,
                        throttle_end_time: 3i64,
                    },
                }
            }
        );
    }

    /// Tests that calling `set_active` twice without first calling `set_inactive` causes a panic in
    /// the debug configuration.
    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "Must end previous throttling before setting active again")]
    #[cfg(debug_assertions)]
    async fn test_debug_panic_double_set_active() {
        let mut throttle_history = InspectThrottleHistory::new(&inspect::Inspector::new().root());
        throttle_history.set_active();
        throttle_history.set_active();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::mock_node::{create_dummy_node, MessageMatcher, MockNodeMaker};
    use crate::types::Seconds;
    use crate::utils::run_all_tasks_until_stalled::run_all_tasks_until_stalled;
    use crate::{msg_eq, msg_ok_return};
    use assert_matches::assert_matches;
    use async_utils::PollExt as _;
    use fidl_contrib::protocol_connector::ProtocolSender;
    use fidl_fuchsia_metrics::{MetricEvent, MetricEventPayload};
    use fuchsia_inspect::{assert_data_tree, HistogramAssertion};

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "PlatformMetrics",
            "name": "platform_metrics",
            "config": {
              "cpu_temperature_poll_interval_s": 1,
              "throttle_debounce_timeout_s": 60
            },
            "dependencies": {
              "cpu_temperature_handler_node": "soc_pll_thermal",
              "crash_report_handler_node": "crash_report_handler"
            }
        });

        let mut nodes: HashMap<String, Rc<dyn Node>> = HashMap::new();
        nodes.insert("soc_pll_thermal".to_string(), create_dummy_node());
        nodes.insert("crash_report_handler".to_string(), create_dummy_node());
        let _ = PlatformMetricsBuilder::new_from_json(json_data, &nodes);
    }

    /// Tests for the correct behavior when the `ThrottlingActive` metric is received:
    ///     - update `throttling_state`
    ///     - update `throttle_history`
    #[test]
    fn test_log_throttling_active() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let inspector = inspect::Inspector::new();
        let platform_metrics = PlatformMetricsBuilder {
            cpu_temperature_handler: Some(create_dummy_node()),
            crash_report_handler: Some(create_dummy_node()),
            inspect_root: Some(inspector.root()),
            ..Default::default()
        }
        .build(&FuturesUnordered::new())
        .unwrap();

        executor.set_fake_time(Seconds(10.0).into());
        assert_matches!(
            executor
                .run_until_stalled(
                    &mut platform_metrics.handle_message(&Message::LogPlatformMetric(
                        PlatformMetric::ThrottlingActive
                    ))
                )
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        // Verify `throttle_history` entry has `throttle_start_time` populated and
        // `throttling_state` shows "active"
        assert_data_tree!(
            inspector,
            root: {
                platform_metrics: {
                    throttle_history: {
                        "0": contains {
                            throttle_start_time: 10i64,
                            throttle_end_time: 0i64,
                        },
                    },
                    throttling_state: "throttled",
                    historical_max_cpu_temperature_c: {}
                }
            }
        );
    }

    /// Tests for the correct behavior when the `ThrottlingResultMitigated` metric is received:
    ///     - throttle debounce works as expected
    ///     - dispatch a Cobalt event for the `thermal_limit_result` metric
    ///     - update `throttling_state`
    ///     - update `throttle_history`
    ///     - crash report filed
    #[test]
    fn test_log_throttling_result_mitigated() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let mut mock_maker = MockNodeMaker::new();
        let crash_report_handler = mock_maker.make(
            "MockCrashReportHandler",
            vec![(
                msg_eq!(FileCrashReport("fuchsia-thermal-throttle".to_string())),
                msg_ok_return!(FileCrashReport),
            )],
        );

        let inspector = inspect::Inspector::new();
        let (cobalt_sender, mut cobalt_receiver) = futures::channel::mpsc::channel(10);
        let platform_metrics = PlatformMetricsBuilder {
            cpu_temperature_handler: Some(create_dummy_node()),
            crash_report_handler: Some(crash_report_handler),
            throttle_debounce_timeout: Seconds(5.0),
            cobalt_sender: Some(ProtocolSender::new(cobalt_sender)),
            inspect_root: Some(inspector.root()),
            ..Default::default()
        }
        .build(&FuturesUnordered::new())
        .unwrap();

        executor.set_fake_time(Seconds(10.0).into());
        assert_matches!(
            executor
                .run_until_stalled(
                    &mut platform_metrics.handle_message(&Message::LogPlatformMetric(
                        PlatformMetric::ThrottlingActive
                    ))
                )
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        executor.set_fake_time(Seconds(11.0).into());
        assert_matches!(
            executor
                .run_until_stalled(&mut platform_metrics.handle_message(
                    &Message::LogPlatformMetric(PlatformMetric::ThrottlingResultMitigated)
                ))
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        assert_data_tree!(
            inspector,
            root: {
                platform_metrics: {
                    throttle_history: {
                        "0": contains {
                            throttle_start_time: 10i64,
                            throttle_end_time: 0i64,
                        },
                    },
                    throttling_state: "debounce",
                    historical_max_cpu_temperature_c: {}
                }
            }
        );

        // Run `throttle_debounce_task`, which stalls at the timer
        run_all_tasks_until_stalled(&mut executor);

        // Wake the `throttle_debounce_task` timer
        let expected_debounce_finish_time = Seconds(16.0).into();
        assert_eq!(executor.wake_next_timer().unwrap(), expected_debounce_finish_time);
        executor.set_fake_time(expected_debounce_finish_time);

        // Continue running `throttle_debounce_task`, which will now complete
        run_all_tasks_until_stalled(&mut executor);

        assert_data_tree!(
            inspector,
            root: {
                platform_metrics: {
                    throttle_history: {
                        "0": contains {
                            throttle_start_time: 10i64,
                            throttle_end_time: 16i64,
                        },
                    },
                    throttling_state: "not throttled",
                    historical_max_cpu_temperature_c: {}
                }
            }
        );

        // Verify the expected Cobalt event for the `thermal_limit_result` metric
        assert_eq!(
            cobalt_receiver.try_next().unwrap().unwrap(),
            MetricEvent {
                metric_id: power_metrics_registry::THERMAL_LIMIT_RESULT_MIGRATED_METRIC_ID,
                event_codes: vec![thermal_limit_result::Mitigated as u32],
                payload: MetricEventPayload::Count(1)
            }
        );

        // Verify there were no more dispatched Cobalt events
        assert!(cobalt_receiver.try_next().is_err());
    }

    /// Tests for the correct behavior when the `ThrottlingResultShutdown` metric is received:
    ///     - dispatch a Cobalt event for the `thermal_limit_result` metric
    ///     - update `throttling_state`
    ///     - update `throttle_history`
    #[test]
    fn test_log_throttling_result_shutdown() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let inspector = inspect::Inspector::new();
        let (cobalt_sender, mut cobalt_receiver) = futures::channel::mpsc::channel(10);
        let platform_metrics = PlatformMetricsBuilder {
            cpu_temperature_handler: Some(create_dummy_node()),
            crash_report_handler: Some(create_dummy_node()),
            cobalt_sender: Some(ProtocolSender::new(cobalt_sender)),
            inspect_root: Some(inspector.root()),
            ..Default::default()
        }
        .build(&FuturesUnordered::new())
        .unwrap();

        executor.set_fake_time(Seconds(0.0).into());
        assert_matches!(
            executor
                .run_until_stalled(
                    &mut platform_metrics.handle_message(&Message::LogPlatformMetric(
                        PlatformMetric::ThrottlingActive
                    ))
                )
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        executor.set_fake_time(Seconds(1.0).into());
        assert_matches!(
            executor
                .run_until_stalled(&mut platform_metrics.handle_message(
                    &Message::LogPlatformMetric(PlatformMetric::ThrottlingResultShutdown)
                ))
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        // Cobalt
        {
            // Verify the expected Cobalt event for the `thermal_limit_result` metric
            assert_eq!(
                cobalt_receiver.try_next().unwrap().unwrap(),
                MetricEvent {
                    metric_id: power_metrics_registry::THERMAL_LIMIT_RESULT_MIGRATED_METRIC_ID,
                    event_codes: vec![thermal_limit_result::Shutdown as u32],
                    payload: MetricEventPayload::Count(1),
                }
            );

            // Verify there were no more dispatched Cobalt events
            assert!(cobalt_receiver.try_next().is_err());
        }

        // Inspect
        {
            // Verify `throttle_history` entry has ended and throttling state is "not throttled"
            assert_data_tree!(
                inspector,
                root: {
                    platform_metrics: {
                        throttle_history: {
                            "0": contains {
                                throttle_start_time: 0i64,
                                throttle_end_time: 1i64,
                            },
                        },
                        throttling_state: "not throttled",
                        historical_max_cpu_temperature_c: {}
                    }
                }
            );
        }
    }

    /// Tests that the PlatformMetrics node correctly polls CPU temperature to:
    ///     - report historical max values in Inspect
    ///     - dispatch a Cobalt event for the `raw_temperature` metric
    #[test]
    fn test_cpu_temperature_logging_task() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        // Initialize current time
        let mut current_time = Seconds(0.0);
        executor.set_fake_time(current_time.into());

        let mut mock_maker = MockNodeMaker::new();
        let mock_cpu_temperature = mock_maker.make("MockCpuTemperature", vec![]);
        let inspector = inspect::Inspector::new();
        let (cobalt_sender, mut cobalt_receiver) = futures::channel::mpsc::channel(10);
        let futures_out = FuturesUnordered::new();
        let _platform_metrics = PlatformMetricsBuilder {
            cpu_temperature_handler: Some(mock_cpu_temperature.clone()),
            crash_report_handler: Some(create_dummy_node()),
            cpu_temperature_poll_interval: Seconds(1.0),
            cobalt_sender: Some(ProtocolSender::new(cobalt_sender)),
            inspect_root: Some(inspector.root()),
            ..Default::default()
        }
        .build(&futures_out)
        .unwrap();

        // Resolve to a single future to provide to the executor
        let mut futures_out = futures_out.collect::<()>();

        let mut iterate_polling_loop = |temperature| {
            mock_cpu_temperature.add_msg_response_pair((
                msg_eq!(ReadTemperature),
                msg_ok_return!(ReadTemperature(temperature)),
            ));

            assert_eq!(executor.wake_next_timer().unwrap(), (current_time + Seconds(1.0)).into());
            current_time += Seconds(1.0);
            executor.set_fake_time(current_time.into());
            assert!(executor.run_until_stalled(&mut futures_out).is_pending());
        };

        // Iterate 60 times to trigger the 1-minute historical temperature publish into Inspect
        for _ in 0..60 {
            iterate_polling_loop(Celsius(40.0));
        }

        // Repeat CPU temperature polling for one more "minute" with a different temperature
        for _ in 0..60 {
            iterate_polling_loop(Celsius(60.0));
        }

        // Verify the `historical_max_cpu_temperature_c` property is published
        assert_data_tree!(
            inspector,
            root: {
                platform_metrics: contains {
                    historical_max_cpu_temperature_c: {
                        "60": 40i64,
                        "120": 60i64
                    }
                },
            }
        );

        // Repeat CPU temperature polling for one more "minute" with a different temperature
        for _ in 0..60 {
            iterate_polling_loop(Celsius(80.0));
        }

        // Verify the first "minute" is rolled out
        assert_data_tree!(
            inspector,
            root: {
                platform_metrics: contains {
                    historical_max_cpu_temperature_c: {
                        "120": 60i64,
                        "180": 80i64
                    }
                },
            }
        );

        // Build the expected raw temperature histogram. PlatformMetrics dispatches the Cobalt event
        // after 100 temperature readings, so only populate the histogram with that many readings
        let mut expected_histogram = CobaltIntHistogram::new(CobaltIntHistogramConfig {
            floor: power_metrics_registry::RAW_TEMPERATURE_MIGRATED_INT_BUCKETS_FLOOR,
            num_buckets: power_metrics_registry::RAW_TEMPERATURE_MIGRATED_INT_BUCKETS_NUM_BUCKETS,
            step_size: power_metrics_registry::RAW_TEMPERATURE_MIGRATED_INT_BUCKETS_STEP_SIZE,
        });
        for _ in 0..60 {
            expected_histogram.add_data(Celsius(40.0).0 as i64);
        }
        for _ in 0..40 {
            expected_histogram.add_data(Celsius(60.0).0 as i64);
        }

        // Verify the expected Cobalt event for the `raw_temperature` metric
        assert_eq!(
            cobalt_receiver.try_next().unwrap().unwrap(),
            MetricEvent {
                metric_id: power_metrics_registry::RAW_TEMPERATURE_MIGRATED_METRIC_ID,
                event_codes: vec![],
                payload: MetricEventPayload::Histogram(expected_histogram.get_data()),
            }
        );

        // Verify there were no more dispatched Cobalt events
        assert!(cobalt_receiver.try_next().is_err());
    }

    /// Tests for the correct behavior when the `ThermalLoad` metric is received: record thermal
    /// load into the current `throttle_history` entry histogram if throttling is active, or no-op
    /// otherwise.
    #[test]
    fn test_log_thermal_load() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let inspector = inspect::Inspector::new();
        let platform_metrics = PlatformMetricsBuilder {
            cpu_temperature_handler: Some(create_dummy_node()),
            crash_report_handler: Some(create_dummy_node()),
            throttle_debounce_timeout: Seconds(10.0),
            inspect_root: Some(inspector.root()),
            ..Default::default()
        }
        .build(&FuturesUnordered::new())
        .unwrap();

        // Set arbitrary start time
        executor.set_fake_time(Seconds(10.0).into());

        // Log a thermal load value. Since throttling is not active, this value will be ignored
        // (verified via `assert_data_tree` later)
        assert_matches!(
            executor
                .run_until_stalled(&mut platform_metrics.handle_message(
                    &Message::LogPlatformMetric(PlatformMetric::ThermalLoad(
                        ThermalLoad(20),
                        "sensor1".to_string()
                    ))
                ))
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        // Log throttling active
        assert_matches!(
            executor
                .run_until_stalled(
                    &mut platform_metrics.handle_message(&Message::LogPlatformMetric(
                        PlatformMetric::ThrottlingActive
                    ))
                )
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        // Log a thermal load value. Since throttling is now active, it will be added to the thermal
        // load histogram.
        assert_matches!(
            executor
                .run_until_stalled(&mut platform_metrics.handle_message(
                    &Message::LogPlatformMetric(PlatformMetric::ThermalLoad(
                        ThermalLoad(40),
                        "sensor1".to_string()
                    ))
                ))
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        // Bump the fake time before we end throttling
        executor.set_fake_time(Seconds(12.0).into());

        // Log throttling ended
        assert_matches!(
            executor
                .run_until_stalled(&mut platform_metrics.handle_message(
                    &Message::LogPlatformMetric(PlatformMetric::ThrottlingResultShutdown)
                ))
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        // Log a thermal load value. Since throttling is no longer active, this value will be
        // ignored (verified via `assert_data_tree` later)
        assert_matches!(
            executor
                .run_until_stalled(&mut platform_metrics.handle_message(
                    &Message::LogPlatformMetric(PlatformMetric::ThermalLoad(
                        ThermalLoad(60),
                        "sensor1".to_string()
                    ))
                ))
                .unwrap(),
            Ok(MessageReturn::LogPlatformMetric)
        );

        // Build the expected thermal load histogram containing just the one thermal load value
        let mut expected_thermal_load_hist = HistogramAssertion::linear(LinearHistogramParams {
            floor: 0u64,
            step_size: 5,
            buckets: 19,
        });
        expected_thermal_load_hist.insert_values(vec![40]);

        assert_data_tree!(
            inspector,
            root: contains {
                platform_metrics: contains {
                    throttle_history: {
                        "0": {
                            throttle_start_time: 10i64,
                            throttle_end_time: 12i64,
                            thermal_load_hist: expected_thermal_load_hist,
                        }
                    }
                }
            }
        )
    }
}
