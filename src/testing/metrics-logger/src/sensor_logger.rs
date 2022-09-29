// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::driver_utils::{connect_proxy, map_topo_paths_to_class_paths, Driver},
    crate::MIN_INTERVAL_FOR_SYSLOG_MS,
    anyhow::{format_err, Error, Result},
    async_trait::async_trait,
    fidl_fuchsia_hardware_power_sensor as fpower,
    fidl_fuchsia_hardware_temperature as ftemperature, fidl_fuchsia_metricslogger_test as fmetrics,
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, ArrayProperty, Property},
    fuchsia_zircon as zx,
    futures::{stream::FuturesUnordered, StreamExt},
    std::{collections::HashMap, rc::Rc},
    tracing::{error, info},
};

// The fuchsia.hardware.temperature.Device is composed into fuchsia.hardware.thermal.Device, so
// drivers are found in two directories.
const TEMPERATURE_SERVICE_DIRS: [&str; 2] = ["/dev/class/temperature", "/dev/class/thermal"];
const POWER_SERVICE_DIRS: [&str; 1] = ["/dev/class/power-sensor"];

// Type aliases for convenience.
pub type TemperatureDriver = Driver<ftemperature::DeviceProxy>;
pub type PowerDriver = Driver<fpower::DeviceProxy>;
pub type TemperatureLogger = SensorLogger<ftemperature::DeviceProxy>;
pub type PowerLogger = SensorLogger<fpower::DeviceProxy>;

pub async fn generate_temperature_drivers(
    driver_aliases: HashMap<String, String>,
) -> Result<Vec<Driver<ftemperature::DeviceProxy>>> {
    generate_sensor_drivers::<ftemperature::DeviceMarker>(&TEMPERATURE_SERVICE_DIRS, driver_aliases)
        .await
}

pub async fn generate_power_drivers(
    driver_aliases: HashMap<String, String>,
) -> Result<Vec<Driver<fpower::DeviceProxy>>> {
    generate_sensor_drivers::<fpower::DeviceMarker>(&POWER_SERVICE_DIRS, driver_aliases).await
}

/// Generates a list of `Driver` from driver paths and aliases.
async fn generate_sensor_drivers<T: fidl::endpoints::ProtocolMarker>(
    service_dirs: &[&str],
    driver_aliases: HashMap<String, String>,
) -> Result<Vec<Driver<T::Proxy>>> {
    let topo_to_class = map_topo_paths_to_class_paths(service_dirs).await?;

    // For each driver path, create a proxy for the service.
    let mut drivers = Vec::new();
    for (topological_path, class_path) in topo_to_class {
        let proxy: T::Proxy = connect_proxy::<T>(&class_path)?;
        let alias = driver_aliases.get(&topological_path).map(|c| c.to_string());
        drivers.push(Driver { alias, topological_path, proxy });
    }
    Ok(drivers)
}

pub enum SensorType {
    Temperature,
    Power,
}

#[async_trait(?Send)]
pub trait Sensor<T> {
    fn sensor_type() -> SensorType;
    fn unit() -> String;
    async fn read_data(sensor: &T) -> Result<f32, Error>;
}

#[async_trait(?Send)]
impl Sensor<ftemperature::DeviceProxy> for ftemperature::DeviceProxy {
    fn sensor_type() -> SensorType {
        SensorType::Temperature
    }

    fn unit() -> String {
        String::from("°C")
    }

    async fn read_data(sensor: &ftemperature::DeviceProxy) -> Result<f32, Error> {
        match sensor.get_temperature_celsius().await {
            Ok((zx_status, temperature)) => match zx::Status::ok(zx_status) {
                Ok(()) => Ok(temperature),
                Err(e) => Err(format_err!("get_temperature_celsius returned an error: {}", e)),
            },
            Err(e) => Err(format_err!("get_temperature_celsius IPC failed: {}", e)),
        }
    }
}

#[async_trait(?Send)]
impl Sensor<fpower::DeviceProxy> for fpower::DeviceProxy {
    fn sensor_type() -> SensorType {
        SensorType::Power
    }

    fn unit() -> String {
        String::from("W")
    }

    async fn read_data(sensor: &fpower::DeviceProxy) -> Result<f32, Error> {
        match sensor.get_power_watts().await {
            Ok(result) => match result {
                Ok(power) => Ok(power),
                Err(e) => Err(format_err!("get_power_watts returned an error: {}", e)),
            },
            Err(e) => Err(format_err!("get_power_watts IPC failed: {}", e)),
        }
    }
}

macro_rules! log_trace {
    ( $sensor_type:expr, $trace_args:expr) => {
        match $sensor_type {
            // TODO (fxbug.dev/100797): Remove temperature_logger category after the e2e test is
            // transitioned.
            SensorType::Temperature => {
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("temperature_logger"),
                    fuchsia_trace::cstr!("temperature"),
                    0,
                    $trace_args,
                );
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("metrics_logger"),
                    fuchsia_trace::cstr!("temperature"),
                    0,
                    $trace_args,
                );
            }
            SensorType::Power => {
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("metrics_logger"),
                    fuchsia_trace::cstr!("power"),
                    0,
                    $trace_args,
                );
            }
        }
    };
}

macro_rules! log_trace_statistics {
    ( $sensor_type:expr, $trace_args:expr) => {
        match $sensor_type {
            SensorType::Temperature => {
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("metrics_logger"),
                    fuchsia_trace::cstr!("temperature_min"),
                    0,
                    &$trace_args[Statistics::Min as usize],
                );
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("metrics_logger"),
                    fuchsia_trace::cstr!("temperature_max"),
                    0,
                    &$trace_args[Statistics::Max as usize],
                );
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("metrics_logger"),
                    fuchsia_trace::cstr!("temperature_avg"),
                    0,
                    &$trace_args[Statistics::Avg as usize],
                );
            }
            SensorType::Power => {
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("metrics_logger"),
                    fuchsia_trace::cstr!("power_min"),
                    0,
                    &$trace_args[Statistics::Min as usize],
                );
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("metrics_logger"),
                    fuchsia_trace::cstr!("power_max"),
                    0,
                    &$trace_args[Statistics::Max as usize],
                );
                fuchsia_trace::counter(
                    fuchsia_trace::cstr!("metrics_logger"),
                    fuchsia_trace::cstr!("power_avg"),
                    0,
                    &$trace_args[Statistics::Avg as usize],
                );
            }
        }
    };
}

struct StatisticsTracker {
    /// Interval for summarizing statistics.
    statistics_interval: zx::Duration,

    /// List of samples polled from all the sensors during `statistics_interval` starting from
    /// `statistics_start_time`. Data is cleared at the end of each `statistics_interval`.
    /// For each sensor, samples are stored in `Vec<f32>` in chronological order.
    samples: Vec<Vec<f32>>,

    /// Start time for a new statistics period.
    /// This is an exclusive start.
    statistics_start_time: fasync::Time,
}

pub struct SensorLogger<T> {
    /// List of sensor drivers.
    drivers: Rc<Vec<Driver<T>>>,

    /// Polling interval from the sensors.
    sampling_interval: zx::Duration,

    /// Start time for the logger; used to calculate elapsed time.
    /// This is an exclusive start.
    start_time: fasync::Time,

    /// Time at which the logger will stop.
    /// This is an exclusive end.
    end_time: fasync::Time,

    /// Client associated with this logger.
    client_id: String,

    statistics_tracker: Option<StatisticsTracker>,

    inspect: InspectData,

    output_samples_to_syslog: bool,

    output_stats_to_syslog: bool,
}

impl<T: Sensor<T>> SensorLogger<T> {
    pub async fn new(
        drivers: Rc<Vec<Driver<T>>>,
        sampling_interval_ms: u32,
        statistics_interval_ms: Option<u32>,
        duration_ms: Option<u32>,
        client_inspect: &inspect::Node,
        client_id: String,
        output_samples_to_syslog: bool,
        output_stats_to_syslog: bool,
    ) -> Result<Self, fmetrics::MetricsLoggerError> {
        if let Some(interval) = statistics_interval_ms {
            if sampling_interval_ms > interval
                || duration_ms.map_or(false, |d| d <= interval)
                || output_stats_to_syslog && interval < MIN_INTERVAL_FOR_SYSLOG_MS
            {
                return Err(fmetrics::MetricsLoggerError::InvalidStatisticsInterval);
            }
        }
        if sampling_interval_ms == 0
            || output_samples_to_syslog && sampling_interval_ms < MIN_INTERVAL_FOR_SYSLOG_MS
            || duration_ms.map_or(false, |d| d <= sampling_interval_ms)
        {
            return Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval);
        }
        if drivers.len() == 0 {
            return Err(fmetrics::MetricsLoggerError::NoDrivers);
        }

        let driver_names: Vec<String> = drivers.iter().map(|c| c.name().to_string()).collect();

        let start_time = fasync::Time::now();
        let end_time = duration_ms
            .map_or(fasync::Time::INFINITE, |d| start_time + zx::Duration::from_millis(d as i64));
        let sampling_interval = zx::Duration::from_millis(sampling_interval_ms as i64);

        let statistics_tracker = statistics_interval_ms.map(|i| StatisticsTracker {
            statistics_interval: zx::Duration::from_millis(i as i64),
            statistics_start_time: fasync::Time::now(),
            samples: vec![Vec::new(); drivers.len()],
        });

        let logger_name = match T::sensor_type() {
            SensorType::Temperature => "TemperatureLogger",
            SensorType::Power => "PowerLogger",
        };
        let inspect = InspectData::new(client_inspect, logger_name, driver_names, T::unit());

        Ok(SensorLogger {
            drivers,
            sampling_interval,
            start_time,
            end_time,
            client_id,
            statistics_tracker,
            inspect,
            output_samples_to_syslog,
            output_stats_to_syslog,
        })
    }

    /// Logs data from all provided sensors.
    pub async fn log_data(mut self) {
        let mut interval = fasync::Interval::new(self.sampling_interval);

        while let Some(()) = interval.next().await {
            // If we're interested in very high-rate polling in the future, it might be worth
            // comparing the elapsed time to the intended polling interval and logging any
            // anomalies.
            let now = fasync::Time::now();
            if now >= self.end_time {
                break;
            }
            self.log_single_data(now).await;
        }
    }

    async fn log_single_data(&mut self, time_stamp: fasync::Time) {
        // Execute a query to each sensor driver.
        let queries = FuturesUnordered::new();
        for (index, driver) in self.drivers.iter().enumerate() {
            let query = async move {
                let result = T::read_data(&driver.proxy).await;
                (index, result)
            };

            queries.push(query);
        }
        let results = queries.collect::<Vec<(usize, Result<f32, Error>)>>().await;

        // Current statistics interval is (self.statistics_start_time,
        // self.statistics_start_time + self.statistics_interval]. Check if current sample
        // is the last sample of the current statistics interval.
        let is_last_sample_for_statistics = self
            .statistics_tracker
            .as_ref()
            .map_or(false, |t| time_stamp - t.statistics_start_time >= t.statistics_interval);

        let mut trace_args = Vec::new();
        let mut trace_args_statistics = vec![Vec::new(), Vec::new(), Vec::new()];

        let mut sensor_names = Vec::new();
        for driver in self.drivers.iter() {
            let topological_path = &driver.topological_path;
            let sensor_name = driver.alias.as_ref().map_or(topological_path.to_string(), |alias| {
                format!("{}({})", alias, topological_path)
            });
            sensor_names.push(sensor_name);
        }

        for (index, result) in results.into_iter() {
            match result {
                Ok(value) => {
                    // Save the current sample for calculating statistics.
                    if let Some(tracker) = &mut self.statistics_tracker {
                        tracker.samples[index].push(value);
                    }

                    // Log data to Inspect.
                    self.inspect.log_data(
                        index,
                        value,
                        (time_stamp - self.start_time).into_millis(),
                    );

                    trace_args
                        .push(fuchsia_trace::ArgValue::of(&sensor_names[index], value as f64));

                    if self.output_samples_to_syslog {
                        info!(
                            name = sensor_names[index].as_str(),
                            unit = T::unit().as_str(),
                            value,
                            "Reading sensor"
                        );
                    }
                }
                // In case of a polling error, the previous value from this sensor will not be
                // updated. We could do something fancier like exposing an error count, but this
                // sample will be missing from the trace counter as is, and any serious analysis
                // should be performed on the trace. This sample will also be missing for
                // calculating statistics.
                Err(err) => error!(
                    ?err,
                    path = self.drivers[index].topological_path.as_str(),
                    "Error reading sensor",
                ),
            };

            if is_last_sample_for_statistics {
                if let Some(tracker) = &mut self.statistics_tracker {
                    let mut min = f32::MAX;
                    let mut max = f32::MIN;
                    let mut sum: f32 = 0.0;
                    for sample in &tracker.samples[index] {
                        min = f32::min(min, *sample);
                        max = f32::max(max, *sample);
                        sum += *sample;
                    }
                    let avg = sum / tracker.samples[index].len() as f32;

                    self.inspect.log_statistics(
                        index,
                        (tracker.statistics_start_time - self.start_time).into_millis(),
                        (time_stamp - self.start_time).into_millis(),
                        min,
                        max,
                        avg,
                    );

                    trace_args_statistics[Statistics::Min as usize]
                        .push(fuchsia_trace::ArgValue::of(&sensor_names[index], min as f64));
                    trace_args_statistics[Statistics::Max as usize]
                        .push(fuchsia_trace::ArgValue::of(&sensor_names[index], max as f64));
                    trace_args_statistics[Statistics::Avg as usize]
                        .push(fuchsia_trace::ArgValue::of(&sensor_names[index], avg as f64));

                    if self.output_stats_to_syslog {
                        info!(
                            name = sensor_names[index].as_str(),
                            max,
                            min,
                            avg,
                            unit = T::unit().as_str(),
                            "Sensor statistics",
                        );
                    }

                    // Empty samples for this sensor.
                    tracker.samples[index].clear();
                }
            }
        }

        trace_args.push(fuchsia_trace::ArgValue::of("client_id", self.client_id.as_str()));
        log_trace!(T::sensor_type(), &trace_args);

        if is_last_sample_for_statistics {
            for t in trace_args_statistics.iter_mut() {
                t.push(fuchsia_trace::ArgValue::of("client_id", self.client_id.as_str()));
            }
            log_trace_statistics!(T::sensor_type(), trace_args_statistics);

            // Reset timestamp to the calculated theoretical start time of next cycle.
            self.statistics_tracker
                .as_mut()
                .map(|t| t.statistics_start_time += t.statistics_interval);
        }
    }
}

enum Statistics {
    Min = 0,
    Max,
    Avg,
}

struct InspectData {
    data: Vec<inspect::DoubleProperty>,
    statistics: Vec<Vec<inspect::DoubleProperty>>,
    statistics_periods: Vec<inspect::IntArrayProperty>,
    elapsed_millis: Option<inspect::IntProperty>,
    sensor_nodes: Vec<inspect::Node>,
    statistics_nodes: Vec<inspect::Node>,
    logger_root: inspect::Node,
    sensor_names: Vec<String>,
    unit: String,
}

impl InspectData {
    fn new(
        parent: &inspect::Node,
        logger_name: &str,
        sensor_names: Vec<String>,
        unit: String,
    ) -> Self {
        Self {
            logger_root: parent.create_child(logger_name),
            data: Vec::new(),
            statistics: Vec::new(),
            statistics_periods: Vec::new(),
            statistics_nodes: Vec::new(),
            elapsed_millis: None,
            sensor_nodes: Vec::new(),
            sensor_names,
            unit,
        }
    }

    fn init_nodes_for_logging_data(&mut self) {
        self.elapsed_millis = Some(self.logger_root.create_int("elapsed time (ms)", std::i64::MIN));
        self.sensor_nodes =
            self.sensor_names.iter().map(|name| self.logger_root.create_child(name)).collect();
        for node in self.sensor_nodes.iter() {
            self.data.push(node.create_double(format!("data ({})", self.unit), f64::MIN));
        }
    }

    fn init_stats_nodes(&mut self) {
        for node in self.sensor_nodes.iter() {
            let statistics_node = node.create_child("statistics");

            let statistics_period = statistics_node.create_int_array("(start ms, end ms]", 2);
            statistics_period.set(0, std::i64::MIN);
            statistics_period.set(1, std::i64::MIN);
            self.statistics_periods.push(statistics_period);

            // The indices of the statistics child nodes match the sequence defined in
            // `Statistics`.
            self.statistics.push(vec![
                statistics_node.create_double(format!("min ({})", self.unit), f64::MIN),
                statistics_node.create_double(format!("max ({})", self.unit), f64::MIN),
                statistics_node.create_double(format!("average ({})", self.unit), f64::MIN),
            ]);
            self.statistics_nodes.push(statistics_node);
        }
    }

    fn log_data(&mut self, index: usize, value: f32, elapsed_millis: i64) {
        if self.data.is_empty() {
            self.init_nodes_for_logging_data();
        }
        self.elapsed_millis.as_ref().map(|e| e.set(elapsed_millis));
        self.data[index].set(value as f64);
    }

    fn log_statistics(
        &mut self,
        index: usize,
        start_time: i64,
        end_time: i64,
        min: f32,
        max: f32,
        avg: f32,
    ) {
        if self.statistics_nodes.is_empty() {
            self.init_stats_nodes();
        }
        self.statistics_periods[index].set(0, start_time);
        self.statistics_periods[index].set(1, end_time);
        self.statistics[index][Statistics::Min as usize].set(min as f64);
        self.statistics[index][Statistics::Max as usize].set(max as f64);
        self.statistics[index][Statistics::Avg as usize].set(avg as f64);
    }
}
