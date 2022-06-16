// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error, Result},
    async_trait::async_trait,
    fidl_fuchsia_device as fdevice, fidl_fuchsia_hardware_power_sensor as fpower,
    fidl_fuchsia_hardware_temperature as ftemperature, fidl_fuchsia_kernel as fkernel,
    fidl_fuchsia_metricslogger_test::{self as fmetrics, MetricsLoggerRequest},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{self as inspect, ArrayProperty, Property},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::{
        future::join_all,
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
        task::Context,
        FutureExt, TryFutureExt,
    },
    serde_derive::Deserialize,
    serde_json as json,
    std::{
        cell::RefCell,
        collections::{hash_map::DefaultHasher, HashMap, HashSet},
        hash::{Hash, Hasher},
        iter::FromIterator,
        pin::Pin,
        rc::Rc,
    },
};

// Max number of clients that can log concurrently. This limit is chosen mostly arbitrarily to allow
// a fixed number clients to keep memory use bounded.
const MAX_CONCURRENT_CLIENTS: usize = 20;

// Minimum interval for logging to syslog.
const MIN_INTERVAL_FOR_SYSLOG_MS: u32 = 500;

const CONFIG_PATH: &'static str = "/config/data/config.json";

// The fuchsia.hardware.temperature.Device is composed into fuchsia.hardware.thermal.Device, so
// drivers are found in two directories.
const TEMPERATURE_SERVICE_DIRS: [&str; 2] = ["/dev/class/temperature", "/dev/class/thermal"];
const POWER_SERVICE_DIRS: [&str; 1] = ["/dev/class/power-sensor"];

pub fn connect_proxy<T: fidl::endpoints::ProtocolMarker>(path: &str) -> Result<T::Proxy> {
    let (proxy, server) = fidl::endpoints::create_proxy::<T>()
        .map_err(|e| format_err!("Failed to create proxy: {}", e))?;

    fdio::service_connect(path, server.into_channel())
        .map_err(|s| format_err!("Failed to connect to service at {}: {}", path, s))?;
    Ok(proxy)
}

/// Maps from devices' topological paths to their class paths in the provided directory.
async fn map_topo_paths_to_class_paths(
    dir_path: &str,
    path_map: &mut HashMap<String, String>,
) -> Result<()> {
    let drivers = list_drivers(dir_path).await;
    for driver in drivers.iter() {
        let class_path = format!("{}/{}", dir_path, driver);
        let topo_path = get_driver_topological_path(&class_path).await?;
        path_map.insert(topo_path, class_path);
    }
    Ok(())
}

async fn get_driver_topological_path(path: &str) -> Result<String> {
    let proxy = connect_proxy::<fdevice::ControllerMarker>(path)?;
    proxy
        .get_topological_path()
        .await?
        .map_err(|raw| format_err!("zx error: {}", zx::Status::from_raw(raw)))
}

async fn list_drivers(path: &str) -> Vec<String> {
    let dir = match fuchsia_fs::open_directory_in_namespace(
        path,
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    ) {
        Ok(s) => s,
        Err(e) => {
            fx_log_info!(
                "Service directory {} doesn't exist or NodeProxy failed with error: {}",
                path,
                e
            );
            return Vec::new();
        }
    };
    match fuchsia_fs::directory::readdir(&dir).await {
        Ok(s) => s.iter().map(|dir_entry| dir_entry.name.clone()).collect(),
        Err(e) => {
            fx_log_err!("Read service directory {} failed with error: {}", path, e);
            Vec::new()
        }
    }
}

/// Generates a list of `SensorDriver` from driver paths and aliases.
async fn generate_sensor_drivers<T: fidl::endpoints::ProtocolMarker>(
    service_dirs: &[&str],
    driver_aliases: HashMap<String, String>,
) -> Result<Vec<SensorDriver<T::Proxy>>> {
    // Determine topological paths for devices in service directories.
    let mut topo_to_class = HashMap::new();
    for dir in service_dirs {
        map_topo_paths_to_class_paths(dir, &mut topo_to_class).await?;
    }

    // For each driver path, create a proxy for the service.
    let mut drivers = Vec::new();
    for (topological_path, class_path) in topo_to_class {
        let proxy: T::Proxy = connect_proxy::<T>(&class_path)?;
        let alias = driver_aliases.get(&topological_path).map(|c| c.to_string());
        drivers.push(SensorDriver { alias, topological_path, proxy });
    }
    Ok(drivers)
}

// Type aliases for convenience.
type TemperatureDriver = SensorDriver<ftemperature::DeviceProxy>;
type PowerDriver = SensorDriver<fpower::DeviceProxy>;
type TemperatureLogger = SensorLogger<ftemperature::DeviceProxy>;
type PowerLogger = SensorLogger<fpower::DeviceProxy>;

// Representation of an actively-used driver.
struct SensorDriver<T> {
    alias: Option<String>,

    topological_path: String,

    proxy: T,
}

impl<T> SensorDriver<T> {
    fn name(&self) -> &str {
        &self.alias.as_ref().unwrap_or(&self.topological_path)
    }
}

enum SensorType {
    Temperature,
    Power,
}

#[async_trait(?Send)]
trait Sensor<T> {
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
            // TODO (didis): Remove temperature_logger category after the e2e test is transitioned.
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

struct SensorLogger<T> {
    /// List of sensor drivers.
    drivers: Rc<Vec<SensorDriver<T>>>,

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
    fn new(
        drivers: Rc<Vec<SensorDriver<T>>>,
        sampling_interval_ms: u32,
        statistics_interval_ms: Option<u32>,
        duration_ms: Option<u32>,
        client_inspect: &inspect::Node,
        driver_names: Vec<String>,
        client_id: String,
        output_samples_to_syslog: bool,
        output_stats_to_syslog: bool,
    ) -> Self {
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

        SensorLogger {
            drivers,
            sampling_interval,
            start_time,
            end_time,
            client_id,
            statistics_tracker,
            inspect,
            output_samples_to_syslog,
            output_stats_to_syslog,
        }
    }

    /// Logs data from all provided sensors.
    async fn log_data(mut self) {
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
                format!("{:?}({:?})", alias, topological_path)
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
                        fx_log_info!(
                            "Reading sensor {:?} [{:?}]: {:?}",
                            &sensor_names[index],
                            T::unit(),
                            value
                        );
                    }
                }
                // In case of a polling error, the previous value from this sensor will not be
                // updated. We could do something fancier like exposing an error count, but this
                // sample will be missing from the trace counter as is, and any serious analysis
                // should be performed on the trace. This sample will also be missing for
                // calculating statistics.
                Err(e) => fx_log_err!(
                    "Error reading sensor {:?}: {:?}",
                    self.drivers[index].topological_path,
                    e
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
                        fx_log_info!(
                            "Sensor {:?} statistics [{:?}]:\n\
                            max: {:?}, min: {:?}, avg: {:?};",
                            &sensor_names[index],
                            T::unit(),
                            max,
                            min,
                            avg,
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

/// Builds a MetricsLoggerServer.
pub struct ServerBuilder<'a> {
    /// Aliases for temperature sensor drivers. Empty if no aliases are provided.
    temperature_driver_aliases: HashMap<String, String>,

    /// Optional drivers for test usage.
    temperature_drivers: Option<Vec<TemperatureDriver>>,

    /// Aliases for power sensor drivers. Empty if no aliases are provided.
    power_driver_aliases: HashMap<String, String>,

    /// Optional drivers for test usage.
    power_drivers: Option<Vec<PowerDriver>>,

    // Optional proxy for test usage.
    cpu_stats_proxy: Option<fkernel::StatsProxy>,

    /// Optional inspect root for test usage.
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> ServerBuilder<'a> {
    /// Constructs a new ServerBuilder from a JSON configuration.
    fn new_from_json(json_data: Option<json::Value>) -> Self {
        #[derive(Deserialize)]
        struct DriverAlias {
            /// Human-readable alias.
            name: String,
            /// Topological path.
            topological_path: String,
        }
        #[derive(Deserialize)]
        struct Config {
            temperature_drivers: Option<Vec<DriverAlias>>,
            power_drivers: Option<Vec<DriverAlias>>,
        }
        let config: Option<Config> = json_data.map(|d| json::from_value(d).unwrap());

        let (temperature_driver_aliases, power_driver_aliases) = match config {
            None => (HashMap::new(), HashMap::new()),
            Some(c) => (
                c.temperature_drivers.map_or_else(
                    || HashMap::new(),
                    |d| d.into_iter().map(|m| (m.topological_path, m.name)).collect(),
                ),
                c.power_drivers.map_or_else(
                    || HashMap::new(),
                    |d| d.into_iter().map(|m| (m.topological_path, m.name)).collect(),
                ),
            ),
        };

        ServerBuilder {
            temperature_driver_aliases,
            temperature_drivers: None,
            power_driver_aliases,
            power_drivers: None,
            cpu_stats_proxy: None,
            inspect_root: None,
        }
    }

    /// For testing purposes, proxies may be provided directly to the Server builder.
    #[cfg(test)]
    fn with_temperature_drivers(mut self, temperature_drivers: Vec<TemperatureDriver>) -> Self {
        self.temperature_drivers = Some(temperature_drivers);
        self
    }

    #[cfg(test)]
    fn with_power_drivers(mut self, power_drivers: Vec<PowerDriver>) -> Self {
        self.power_drivers = Some(power_drivers);
        self
    }

    #[cfg(test)]
    fn with_cpu_stats_proxy(mut self, cpu_stats_proxy: fkernel::StatsProxy) -> Self {
        self.cpu_stats_proxy = Some(cpu_stats_proxy);
        self
    }

    /// Injects an Inspect root for use in tests.
    #[cfg(test)]
    fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    /// Builds a MetricsLoggerServer.
    async fn build(self) -> Result<Rc<MetricsLoggerServer>> {
        // If no proxies are provided, create proxies based on driver paths.
        let temperature_drivers: Vec<TemperatureDriver> = match self.temperature_drivers {
            None => {
                generate_sensor_drivers::<ftemperature::DeviceMarker>(
                    &TEMPERATURE_SERVICE_DIRS,
                    self.temperature_driver_aliases,
                )
                .await?
            }
            Some(drivers) => drivers,
        };

        // If no proxies are provided, create proxies based on driver paths.
        let power_drivers = match self.power_drivers {
            None => {
                generate_sensor_drivers::<fpower::DeviceMarker>(
                    &POWER_SERVICE_DIRS,
                    self.power_driver_aliases,
                )
                .await?
            }
            Some(drivers) => drivers,
        };

        // If no proxy is provided, create proxy for polling CPU stats
        let cpu_stats_proxy = match &self.cpu_stats_proxy {
            Some(proxy) => proxy.clone(),
            None => connect_to_protocol::<fkernel::StatsMarker>()?,
        };

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        Ok(MetricsLoggerServer::new(
            Rc::new(temperature_drivers),
            Rc::new(power_drivers),
            Rc::new(cpu_stats_proxy),
            inspect_root.create_child("MetricsLogger"),
        ))
    }
}

struct CpuLoadLogger {
    interval: zx::Duration,
    end_time: fasync::Time,
    last_sample: Option<(fasync::Time, fkernel::CpuStats)>,
    stats_proxy: Rc<fkernel::StatsProxy>,
    client_id: String,
    output_samples_to_syslog: bool,
}

impl CpuLoadLogger {
    fn new(
        interval: zx::Duration,
        duration: Option<zx::Duration>,
        stats_proxy: Rc<fkernel::StatsProxy>,
        client_id: String,
        output_samples_to_syslog: bool,
    ) -> Self {
        let end_time = duration.map_or(fasync::Time::INFINITE, |d| fasync::Time::now() + d);
        CpuLoadLogger {
            interval,
            end_time,
            last_sample: None,
            stats_proxy,
            client_id,
            output_samples_to_syslog,
        }
    }

    async fn log_cpu_usages(mut self) {
        let mut interval = fasync::Interval::new(self.interval);

        while let Some(()) = interval.next().await {
            let now = fasync::Time::now();
            if now >= self.end_time {
                break;
            }
            self.log_cpu_usage(now).await;
        }
    }

    async fn log_cpu_usage(&mut self, now: fasync::Time) {
        let mut hasher = DefaultHasher::new();
        self.client_id.hash(&mut hasher);
        let trace_counter_id = hasher.finish();

        match self.stats_proxy.get_cpu_stats().await {
            Ok(cpu_stats) => {
                if let Some((last_sample_time, last_cpu_stats)) = self.last_sample.take() {
                    let elapsed = now - last_sample_time;
                    let mut cpu_percentage_sum: f64 = 0.0;
                    for (i, per_cpu_stats) in
                        cpu_stats.per_cpu_stats.as_ref().unwrap().iter().enumerate()
                    {
                        let last_per_cpu_stats = &last_cpu_stats.per_cpu_stats.as_ref().unwrap()[i];
                        let delta_idle_time = zx::Duration::from_nanos(
                            per_cpu_stats.idle_time.unwrap()
                                - last_per_cpu_stats.idle_time.unwrap(),
                        );
                        let busy_time = elapsed - delta_idle_time;
                        cpu_percentage_sum +=
                            100.0 * busy_time.into_nanos() as f64 / elapsed.into_nanos() as f64;
                    }

                    let cpu_usage = cpu_percentage_sum / cpu_stats.actual_num_cpus as f64;

                    if self.output_samples_to_syslog {
                        fx_log_info!("CpuUsage: {:?}", cpu_usage);
                    }

                    // TODO (didis): Remove system_metrics_logger category after the e2e test is
                    // transitioned.
                    fuchsia_trace::counter!(
                        "system_metrics_logger",
                        "cpu_usage",
                        0,
                        "cpu_usage" => cpu_usage
                    );
                    fuchsia_trace::counter!(
                        "metrics_logger",
                        "cpu_usage",
                        trace_counter_id,
                        "client_id" => self.client_id.as_str(),
                        "cpu_usage" => cpu_usage
                    );
                }

                self.last_sample.replace((now, cpu_stats));
            }
            Err(e) => fx_log_err!("get_cpu_stats IPC failed: {}", e),
        }
    }
}

struct MetricsLoggerServer {
    /// List of temperature sensor drivers for polling temperatures.
    temperature_drivers: Rc<Vec<TemperatureDriver>>,

    /// List of power sensor drivers for polling powers.
    power_drivers: Rc<Vec<PowerDriver>>,

    /// Proxy for polling CPU stats.
    cpu_stats_proxy: Rc<fkernel::StatsProxy>,

    /// Root node for MetricsLogger
    inspect_root: inspect::Node,

    /// Map that stores the logging task for all clients. Once a logging request is received
    /// with a new client_id, a task is lazily inserted into the map using client_id as the key.
    client_tasks: RefCell<HashMap<String, fasync::Task<()>>>,
}

impl MetricsLoggerServer {
    fn new(
        temperature_drivers: Rc<Vec<TemperatureDriver>>,
        power_drivers: Rc<Vec<PowerDriver>>,
        cpu_stats_proxy: Rc<fkernel::StatsProxy>,
        inspect_root: inspect::Node,
    ) -> Rc<Self> {
        Rc::new(Self {
            temperature_drivers,
            power_drivers,
            cpu_stats_proxy,
            inspect_root,
            client_tasks: RefCell::new(HashMap::new()),
        })
    }

    fn handle_new_service_connection(
        self: Rc<Self>,
        mut stream: fmetrics::MetricsLoggerRequestStream,
    ) -> fasync::Task<()> {
        fasync::Task::local(
            async move {
                while let Some(request) = stream.try_next().await? {
                    self.clone().handle_metrics_logger_request(request).await?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("{:?}", e)),
        )
    }

    async fn handle_metrics_logger_request(
        self: &Rc<Self>,
        request: MetricsLoggerRequest,
    ) -> Result<()> {
        self.purge_completed_tasks();

        match request {
            MetricsLoggerRequest::StartLogging {
                client_id,
                metrics,
                duration_ms,
                output_samples_to_syslog,
                output_stats_to_syslog,
                responder,
            } => {
                let mut result = self
                    .start_logging(
                        &client_id,
                        metrics,
                        output_samples_to_syslog,
                        output_stats_to_syslog,
                        Some(duration_ms),
                    )
                    .await;
                responder.send(&mut result)?;
            }
            MetricsLoggerRequest::StartLoggingForever {
                client_id,
                metrics,
                output_samples_to_syslog,
                output_stats_to_syslog,
                responder,
            } => {
                let mut result = self
                    .start_logging(
                        &client_id,
                        metrics,
                        output_samples_to_syslog,
                        output_stats_to_syslog,
                        None,
                    )
                    .await;
                responder.send(&mut result)?;
            }
            MetricsLoggerRequest::StopLogging { client_id, responder } => {
                responder.send(self.client_tasks.borrow_mut().remove(&client_id).is_some())?;
            }
        }

        Ok(())
    }

    async fn start_logging(
        &self,
        client_id: &str,
        metrics: Vec<fmetrics::Metric>,
        output_samples_to_syslog: bool,
        output_stats_to_syslog: bool,
        duration_ms: Option<u32>,
    ) -> fmetrics::MetricsLoggerStartLoggingResult {
        if self.client_tasks.borrow_mut().contains_key(client_id) {
            return Err(fmetrics::MetricsLoggerError::AlreadyLogging);
        }

        if self.client_tasks.borrow().len() >= MAX_CONCURRENT_CLIENTS {
            return Err(fmetrics::MetricsLoggerError::TooManyActiveClients);
        }

        let incoming_metric_types: HashSet<_> =
            HashSet::from_iter(metrics.iter().map(|m| std::mem::discriminant(m)));
        if incoming_metric_types.len() != metrics.len() {
            return Err(fmetrics::MetricsLoggerError::DuplicatedMetric);
        }

        for metric in metrics.iter() {
            match metric {
                fmetrics::Metric::CpuLoad(fmetrics::CpuLoad { interval_ms }) => {
                    if *interval_ms == 0
                        || output_samples_to_syslog && *interval_ms < MIN_INTERVAL_FOR_SYSLOG_MS
                        || duration_ms.map_or(false, |d| d <= *interval_ms)
                    {
                        return Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval);
                    }
                }
                fmetrics::Metric::Temperature(fmetrics::Temperature {
                    sampling_interval_ms,
                    statistics_args,
                }) => {
                    if self.temperature_drivers.len() == 0 {
                        return Err(fmetrics::MetricsLoggerError::NoDrivers);
                    }
                    if let Some(args) = statistics_args {
                        if *sampling_interval_ms > args.statistics_interval_ms
                            || duration_ms.map_or(false, |d| d <= args.statistics_interval_ms)
                            || output_stats_to_syslog
                                && args.statistics_interval_ms < MIN_INTERVAL_FOR_SYSLOG_MS
                        {
                            return Err(fmetrics::MetricsLoggerError::InvalidStatisticsInterval);
                        }
                    }
                    if *sampling_interval_ms == 0
                        || output_samples_to_syslog
                            && *sampling_interval_ms < MIN_INTERVAL_FOR_SYSLOG_MS
                        || duration_ms.map_or(false, |d| d <= *sampling_interval_ms)
                    {
                        return Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval);
                    }
                }
                fmetrics::Metric::Power(fmetrics::Power {
                    sampling_interval_ms,
                    statistics_args,
                }) => {
                    if self.power_drivers.len() == 0 {
                        return Err(fmetrics::MetricsLoggerError::NoDrivers);
                    }
                    if let Some(args) = statistics_args {
                        if *sampling_interval_ms > args.statistics_interval_ms
                            || duration_ms.map_or(false, |d| d <= args.statistics_interval_ms)
                            || output_stats_to_syslog
                                && args.statistics_interval_ms < MIN_INTERVAL_FOR_SYSLOG_MS
                        {
                            return Err(fmetrics::MetricsLoggerError::InvalidStatisticsInterval);
                        }
                    }
                    if *sampling_interval_ms == 0
                        || output_samples_to_syslog
                            && *sampling_interval_ms < MIN_INTERVAL_FOR_SYSLOG_MS
                        || duration_ms.map_or(false, |d| d <= *sampling_interval_ms)
                    {
                        return Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval);
                    }
                }
            }
        }

        self.client_tasks.borrow_mut().insert(
            client_id.to_string(),
            self.spawn_client_tasks(
                client_id.to_string(),
                metrics,
                duration_ms,
                output_samples_to_syslog,
                output_stats_to_syslog,
            ),
        );

        Ok(())
    }

    fn purge_completed_tasks(&self) {
        self.client_tasks.borrow_mut().retain(|_n, task| {
            task.poll_unpin(&mut Context::from_waker(futures::task::noop_waker_ref())).is_pending()
        });
    }

    fn spawn_client_tasks(
        &self,
        client_id: String,
        metrics: Vec<fmetrics::Metric>,
        duration_ms: Option<u32>,
        output_samples_to_syslog: bool,
        output_stats_to_syslog: bool,
    ) -> fasync::Task<()> {
        let cpu_stats_proxy = self.cpu_stats_proxy.clone();
        let temperature_drivers = self.temperature_drivers.clone();
        let power_drivers = self.power_drivers.clone();
        let client_inspect = self.inspect_root.create_child(&client_id);

        fasync::Task::local(async move {
            let mut futures: Vec<Box<dyn futures::Future<Output = ()>>> = Vec::new();

            for metric in metrics {
                match metric {
                    fmetrics::Metric::CpuLoad(fmetrics::CpuLoad { interval_ms }) => {
                        let cpu_load_logger = CpuLoadLogger::new(
                            zx::Duration::from_millis(interval_ms as i64),
                            duration_ms.map(|ms| zx::Duration::from_millis(ms as i64)),
                            cpu_stats_proxy.clone(),
                            String::from(&client_id),
                            output_samples_to_syslog,
                        );
                        futures.push(Box::new(cpu_load_logger.log_cpu_usages()));
                    }
                    fmetrics::Metric::Temperature(fmetrics::Temperature {
                        sampling_interval_ms,
                        statistics_args,
                    }) => {
                        let temperature_driver_names: Vec<String> =
                            temperature_drivers.iter().map(|c| c.name().to_string()).collect();

                        let temperature_logger = TemperatureLogger::new(
                            temperature_drivers.clone(),
                            sampling_interval_ms,
                            statistics_args.map(|i| i.statistics_interval_ms),
                            duration_ms,
                            &client_inspect,
                            temperature_driver_names,
                            String::from(&client_id),
                            output_samples_to_syslog,
                            output_stats_to_syslog,
                        );
                        futures.push(Box::new(temperature_logger.log_data()));
                    }
                    fmetrics::Metric::Power(fmetrics::Power {
                        sampling_interval_ms,
                        statistics_args,
                    }) => {
                        let power_driver_names: Vec<String> =
                            power_drivers.iter().map(|c| c.name().to_string()).collect();

                        let power_logger = PowerLogger::new(
                            power_drivers.clone(),
                            sampling_interval_ms,
                            statistics_args.map(|i| i.statistics_interval_ms),
                            duration_ms,
                            &client_inspect,
                            power_driver_names,
                            String::from(&client_id),
                            output_samples_to_syslog,
                            output_stats_to_syslog,
                        );
                        futures.push(Box::new(power_logger.log_data()));
                    }
                }
            }
            join_all(futures.into_iter().map(|f| Pin::from(f))).await;
        })
    }
}

enum Statistics {
    Min = 0,
    Max,
    Avg,
}

// TODO (fxbug.dev/92320): Populate CPU Usageinfo into Inspect.
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
            data: Vec::new(),
            statistics: Vec::new(),
            statistics_periods: Vec::new(),
            statistics_nodes: Vec::new(),
            elapsed_millis: None,
            sensor_nodes: Vec::new(),
            sensor_names,
            unit,
            logger_root: parent.create_child(logger_name),
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

#[fasync::run_singlethreaded]
async fn main() {
    // v2 components can't surface stderr yet, so we need to explicitly log errors.
    match inner_main().await {
        Err(e) => fx_log_err!("Terminated with error: {}", e),
        Ok(()) => fx_log_info!("Terminated with Ok(())"),
    }
}

async fn inner_main() -> Result<()> {
    fuchsia_syslog::init_with_tags(&["metrics-logger"]).expect("failed to initialize logger");

    fx_log_info!("Starting metrics logger");

    // Set up tracing
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let mut fs = ServiceFs::new_local();

    // Allow our services to be discovered.
    fs.take_and_serve_directory_handle()?;

    // Required call to serve the inspect tree
    let inspector = inspect::component::inspector();
    inspect_runtime::serve(inspector, &mut fs)?;

    // Construct the server, and begin serving.
    let config: Option<json::Value> = std::fs::File::open(CONFIG_PATH)
        .ok()
        .and_then(|file| json::from_reader(std::io::BufReader::new(file)).ok());
    let server = ServerBuilder::new_from_json(config).build().await?;
    fs.dir("svc").add_fidl_service(move |stream: fmetrics::MetricsLoggerRequestStream| {
        MetricsLoggerServer::handle_new_service_connection(server.clone(), stream).detach();
    });

    // This future never completes.
    fs.collect::<()>().await;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_kernel::{CpuStats, PerCpuStats},
        fmetrics::{CpuLoad, Metric, Power, StatisticsArgs, Temperature},
        futures::{task::Poll, FutureExt, TryStreamExt},
        inspect::assert_data_tree,
        std::cell::{Cell, RefCell},
    };

    fn setup_fake_stats_service(
        mut get_cpu_stats: impl FnMut() -> CpuStats + 'static,
    ) -> (fkernel::StatsProxy, fasync::Task<()>) {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fkernel::StatsMarker>().unwrap();
        let task = fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fkernel::StatsRequest::GetCpuStats { responder }) => {
                        let _ = responder.send(&mut get_cpu_stats());
                    }
                    _ => assert!(false),
                }
            }
        });

        (proxy, task)
    }

    fn setup_fake_temperature_driver(
        mut get_temperature: impl FnMut() -> f32 + 'static,
    ) -> (ftemperature::DeviceProxy, fasync::Task<()>) {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<ftemperature::DeviceMarker>().unwrap();
        let task = fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(ftemperature::DeviceRequest::GetTemperatureCelsius { responder }) => {
                        let _ = responder.send(zx::Status::OK.into_raw(), get_temperature());
                    }
                    _ => assert!(false),
                }
            }
        });

        (proxy, task)
    }

    fn setup_fake_power_driver(
        mut get_power: impl FnMut() -> f32 + 'static,
    ) -> (fpower::DeviceProxy, fasync::Task<()>) {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fpower::DeviceMarker>().unwrap();
        let task = fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fpower::DeviceRequest::GetPowerWatts { responder }) => {
                        let _ = responder.send(&mut Ok(get_power()));
                    }
                    _ => assert!(false),
                }
            }
        });

        (proxy, task)
    }

    struct Runner {
        server_task: fasync::Task<()>,
        proxy: fmetrics::MetricsLoggerProxy,

        cpu_temperature: Rc<Cell<f32>>,
        gpu_temperature: Rc<Cell<f32>>,

        power_1: Rc<Cell<f32>>,
        power_2: Rc<Cell<f32>>,

        inspector: inspect::Inspector,

        _tasks: Vec<fasync::Task<()>>,

        // Fields are dropped in declaration order. Always drop executor last because we hold other
        // zircon objects tied to the executor in this struct, and those can't outlive the executor.
        //
        // See
        // - https://fuchsia-docs.firebaseapp.com/rust/fuchsia_async/struct.TestExecutor.html
        // - https://doc.rust-lang.org/reference/destructors.html.
        executor: fasync::TestExecutor,
    }

    impl Runner {
        fn new() -> Self {
            let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
            executor.set_fake_time(fasync::Time::from_nanos(0));

            let inspector = inspect::Inspector::new();

            let mut tasks = Vec::new();

            let cpu_temperature = Rc::new(Cell::new(0.0));
            let cpu_temperature_clone = cpu_temperature.clone();
            let (cpu_temperature_proxy, task) =
                setup_fake_temperature_driver(move || cpu_temperature_clone.get());
            tasks.push(task);
            let gpu_temperature = Rc::new(Cell::new(0.0));
            let gpu_temperature_clone = gpu_temperature.clone();
            let (gpu_temperature_proxy, task) =
                setup_fake_temperature_driver(move || gpu_temperature_clone.get());
            tasks.push(task);

            let power_1 = Rc::new(Cell::new(0.0));
            let power_1_clone = power_1.clone();
            let (power_1_proxy, task) = setup_fake_power_driver(move || power_1_clone.get());
            tasks.push(task);
            let power_2 = Rc::new(Cell::new(0.0));
            let power_2_clone = power_2.clone();
            let (power_2_proxy, task) = setup_fake_power_driver(move || power_2_clone.get());
            tasks.push(task);
            let temperature_drivers = vec![
                TemperatureDriver {
                    alias: Some("cpu".to_string()),
                    topological_path: "/dev/fake/cpu_temperature".to_string(),
                    proxy: cpu_temperature_proxy,
                },
                TemperatureDriver {
                    alias: None,
                    topological_path: "/dev/fake/gpu_temperature".to_string(),
                    proxy: gpu_temperature_proxy,
                },
            ];
            let power_drivers = vec![
                PowerDriver {
                    alias: Some("power_1".to_string()),
                    topological_path: "/dev/fake/power_1".to_string(),
                    proxy: power_1_proxy,
                },
                PowerDriver {
                    alias: None,
                    topological_path: "/dev/fake/power_2".to_string(),
                    proxy: power_2_proxy,
                },
            ];

            let cpu_stats = Rc::new(RefCell::new(CpuStats {
                actual_num_cpus: 1,
                per_cpu_stats: Some(vec![PerCpuStats { idle_time: Some(0), ..PerCpuStats::EMPTY }]),
            }));
            let (cpu_stats_proxy, task) =
                setup_fake_stats_service(move || cpu_stats.borrow().clone());
            tasks.push(task);

            // Build the server.
            let builder = ServerBuilder::new_from_json(None)
                .with_temperature_drivers(temperature_drivers)
                .with_power_drivers(power_drivers)
                .with_cpu_stats_proxy(cpu_stats_proxy)
                .with_inspect_root(inspector.root());
            let poll = executor.run_until_stalled(&mut builder.build().boxed_local());
            let server = match poll {
                Poll::Ready(Ok(server)) => server,
                _ => panic!("Failed to build MetricsLoggerServer"),
            };

            // Construct the server task.
            let (proxy, stream) =
                fidl::endpoints::create_proxy_and_stream::<fmetrics::MetricsLoggerMarker>()
                    .unwrap();
            let server_task = server.handle_new_service_connection(stream);

            Self {
                executor,
                server_task,
                proxy,
                cpu_temperature,
                gpu_temperature,
                inspector,
                power_1,
                power_2,
                _tasks: tasks,
            }
        }

        // If the server has an active logging task, run until the next log and return true.
        // Otherwise, return false.
        fn iterate_logging_task(&mut self) -> bool {
            let wakeup_time = match self.executor.wake_next_timer() {
                Some(t) => t,
                None => return false,
            };
            self.executor.set_fake_time(wakeup_time);
            assert_eq!(
                futures::task::Poll::Pending,
                self.executor.run_until_stalled(&mut self.server_task)
            );
            true
        }

        fn run_server_task_until_stalled(&mut self) {
            assert_matches!(self.executor.run_until_stalled(&mut self.server_task), Poll::Pending);
        }
    }

    #[test]
    fn test_spawn_client_tasks() {
        let mut runner = Runner::new();

        // Check the root Inspect node for MetricsLogger is created.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                }
            }
        );

        // Create a logging request.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![
                &mut Metric::CpuLoad(CpuLoad { interval_ms: 100 }),
                &mut Metric::Temperature(Temperature {
                    sampling_interval_ms: 100,
                    statistics_args: None,
                }),
            ]
            .into_iter(),
            1000,
            false,
            false,
        );

        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Check client Inspect node is added.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {}
                }
            }
        );

        // Run  `server_task`  until stalled to create futures for logging
        // temperatures and CpuLoads.
        runner.run_server_task_until_stalled();

        // Check the Inspect node for TemperatureLogger is created.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        TemperatureLogger: {
                        }
                    }
                }
            }
        );

        runner.cpu_temperature.set(35.0);
        runner.gpu_temperature.set(45.0);

        // Run the initial logging tasks.
        for _ in 0..2 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        // Check data is logged to TemperatureLogger Inspect node.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        TemperatureLogger: {
                            "elapsed time (ms)": 100i64,
                            "cpu": {
                                "data (°C)": 35.0,
                            },
                            "/dev/fake/gpu_temperature": {
                                "data (°C)": 45.0,
                            }
                        }
                    }
                }
            }
        );

        // Run the remaining logging tasks (8 CpuLoad tasks + 8 Temperature tasks).
        for _ in 0..16 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        // Check data is logged to TemperatureLogger Inspect node.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        TemperatureLogger: {
                            "elapsed time (ms)": 900i64,
                            "cpu": {
                                "data (°C)": 35.0,
                            },
                            "/dev/fake/gpu_temperature": {
                                "data (°C)": 45.0,
                            }
                        }
                    }
                }
            }
        );

        // Run the last 2 tasks which hits `now >= self.end_time` and ends the logging.
        for _ in 0..2 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        // Check Inspect node for the client is removed.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                }
            }
        );

        assert_eq!(runner.iterate_logging_task(), false);
    }

    /// Tests that well-formed alias JSON does not panic the `new_from_json` function.
    #[test]
    fn test_new_from_json() {
        // Test config file for one sensor.
        let json_data = json::json!({
            "power_drivers": [{
                "name": "power_1",
                "topological_path": "/dev/sys/platform/power_1"
            }]
        });
        let _ = ServerBuilder::new_from_json(Some(json_data));

        // Test config file for two sensors.
        let json_data = json::json!({
            "temperature_drivers": [{
                "name": "temp_1",
                "topological_path": "/dev/sys/platform/temp_1"
            }],
            "power_drivers": [{
                "name": "power_1",
                "topological_path": "/dev/sys/platform/power_1"
            }]
        });
        let _ = ServerBuilder::new_from_json(Some(json_data));
    }

    #[test]
    fn test_logging_duration() {
        let mut runner = Runner::new();

        // Start logging every 100ms for a total of 2000ms.
        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            2000,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Ensure that we get exactly 20 samples.
        for _ in 0..20 {
            assert_eq!(runner.iterate_logging_task(), true);
        }
        assert_eq!(runner.iterate_logging_task(), false);
    }

    #[test]
    fn test_logging_duration_too_short() {
        let mut runner = Runner::new();

        // Attempt to start logging with an interval of 100ms but a duration of 50ms. The request
        // should fail as the logging session would not produce any samples.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            50,
            false,
            false,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval)))
        );

        // Check client node is not added in Inspect.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {}
            }
        );
    }

    #[test]
    fn test_duplicated_metrics_in_one_request() {
        let mut runner = Runner::new();

        // Attempt to start logging CPU Load twice. The request should fail as the logging request
        // contains duplicated metric type.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![
                &mut Metric::CpuLoad(CpuLoad { interval_ms: 100 }),
                &mut Metric::CpuLoad(CpuLoad { interval_ms: 100 }),
            ]
            .into_iter(),
            200,
            false,
            false,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::DuplicatedMetric)))
        );
    }

    #[test]
    fn test_logging_forever() {
        let mut runner = Runner::new();

        // Start logging every 100ms with no predetermined end time.
        let _query = runner.proxy.start_logging_forever(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Samples should continue forever. Obviously we can't check infinitely many samples, but
        // we can check that they don't stop for a relatively large number of iterations.
        for _ in 0..1000 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        let mut query = runner.proxy.stop_logging("test");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(true)));

        // Check that we can start another request for the same client_id after
        // `stop_logging` is called.
        let mut query = runner.proxy.start_logging_forever(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            false,
            false,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
    }

    #[test]
    fn test_concurrent_logging() {
        let mut runner = Runner::new();

        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![
                &mut Metric::CpuLoad(CpuLoad { interval_ms: 100 }),
                &mut Metric::Temperature(Temperature {
                    sampling_interval_ms: 200,
                    statistics_args: None,
                }),
            ]
            .into_iter(),
            600,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Check logger added to client before first temperature poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        TemperatureLogger: {
                        }
                    }
                }
            }
        );

        runner.cpu_temperature.set(35.0);
        runner.gpu_temperature.set(45.0);

        // Run existing tasks to completion (6 CpuLoad tasks + 3 Temperature tasks).
        for _ in 0..9 {
            assert_eq!(runner.iterate_logging_task(), true);
        }
        assert_eq!(runner.iterate_logging_task(), false);

        // Check temperature logger removed in Inspect.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {}
            }
        );
    }

    #[test]
    fn test_stop_logging() {
        let mut runner = Runner::new();

        let _query = runner.proxy.start_logging_forever(
            "test",
            &mut vec![&mut Metric::Temperature(Temperature {
                sampling_interval_ms: 100,
                statistics_args: None,
            })]
            .into_iter(),
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Check logger added to client before first temperature poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        TemperatureLogger: {
                        }
                    }
                }
            }
        );

        runner.cpu_temperature.set(35.0);
        runner.gpu_temperature.set(45.0);

        // Run a few logging tasks to populate Inspect node before we test `stop_logging`.
        for _ in 0..10 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        // Checked data populated to Inspect node.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        TemperatureLogger: {
                            "elapsed time (ms)": 1_000i64,
                            "cpu": {
                                "data (°C)": 35.0,
                            },
                            "/dev/fake/gpu_temperature": {
                                "data (°C)": 45.0,
                            }
                        }
                    }
                }
            }
        );

        let mut query = runner.proxy.stop_logging("test");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(true)));
        runner.run_server_task_until_stalled();

        assert_eq!(runner.iterate_logging_task(), false);

        // Check temperature logger removed in Inspect.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {}
            }
        );
    }

    #[test]
    fn test_multi_clients() {
        let mut runner = Runner::new();

        // Create a request for logging CPU load and Temperature.
        let _query1 = runner.proxy.start_logging(
            "test1",
            &mut vec![
                &mut Metric::CpuLoad(CpuLoad { interval_ms: 300 }),
                &mut Metric::Temperature(Temperature {
                    sampling_interval_ms: 200,
                    statistics_args: None,
                }),
            ]
            .into_iter(),
            500,
            false,
            false,
        );

        // Create a request for logging Temperature.
        let _query2 = runner.proxy.start_logging(
            "test2",
            &mut vec![&mut Metric::Temperature(Temperature {
                sampling_interval_ms: 200,
                statistics_args: None,
            })]
            .into_iter(),
            300,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Check TemperatureLogger added before first temperature poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test1: {
                        TemperatureLogger: {
                        }
                    },
                    test2: {
                        TemperatureLogger: {
                        }
                    }
                }
            }
        );

        runner.cpu_temperature.set(35.0);
        runner.gpu_temperature.set(45.0);

        // Run the first task which is the first logging task for client `test1`.
        assert_eq!(runner.iterate_logging_task(), true);

        // Check temperature data in Inspect.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test1: {
                        TemperatureLogger: {
                            "elapsed time (ms)": 200i64,
                            "cpu": {
                                "data (°C)": 35.0,
                            },
                            "/dev/fake/gpu_temperature": {
                                "data (°C)": 45.0,
                            }
                        }
                    },
                    test2: {
                        TemperatureLogger: {
                        }
                    }
                }
            }
        );

        // Set new temperature data.
        runner.cpu_temperature.set(36.0);
        runner.gpu_temperature.set(46.0);

        for _ in 0..2 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        // Check `test1` data remaining the same, `test2` data updated.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test1: {
                        TemperatureLogger: {
                            "elapsed time (ms)": 200i64,
                            "cpu": {
                                "data (°C)": 35.0,
                            },
                            "/dev/fake/gpu_temperature": {
                                "data (°C)": 45.0,
                            }
                        }
                    },
                    test2: {
                        TemperatureLogger: {
                            "elapsed time (ms)": 200i64,
                            "cpu": {
                                "data (°C)": 36.0,
                            },
                            "/dev/fake/gpu_temperature": {
                                "data (°C)": 46.0,
                            }
                        }
                    }
                }
            }
        );

        assert_eq!(runner.iterate_logging_task(), true);

        // Check `test1` data updated, `test2` data remaining the same.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test1: {
                        TemperatureLogger: {
                            "elapsed time (ms)": 400i64,
                            "cpu": {
                                "data (°C)": 36.0,
                            },
                            "/dev/fake/gpu_temperature": {
                                "data (°C)": 46.0,
                            }
                        }
                    },
                    test2: {
                        TemperatureLogger: {
                            "elapsed time (ms)": 200i64,
                            "cpu": {
                                "data (°C)": 36.0,
                            },
                            "/dev/fake/gpu_temperature": {
                                "data (°C)": 46.0,
                            }
                        }
                    }
                }
            }
        );

        // Run the remaining 3 tasks.
        for _ in 0..3 {
            assert_eq!(runner.iterate_logging_task(), true);
        }
        assert_eq!(runner.iterate_logging_task(), false);

        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {}
            }
        );
    }

    #[test]
    fn test_large_number_of_clients() {
        let mut runner = Runner::new();

        // Create MAX_CONCURRENT_CLIENTS clients.
        for i in 0..MAX_CONCURRENT_CLIENTS {
            let mut query = runner.proxy.start_logging_forever(
                &(i as u32).to_string(),
                &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 300 })].into_iter(),
                false,
                false,
            );
            assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
            runner.run_server_task_until_stalled();
        }

        // Check new client logging request returns TOO_MANY_ACTIVE_CLIENTS error.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            400,
            false,
            false,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::TooManyActiveClients)))
        );

        // Remove one active client.
        let mut query = runner.proxy.stop_logging("3");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(true)));

        // Check we can add another client.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            400,
            false,
            false,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
    }

    #[test]
    fn test_already_logging() {
        let mut runner = Runner::new();

        // Start the first logging task.
        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            400,
            false,
            false,
        );

        runner.run_server_task_until_stalled();

        assert_eq!(runner.iterate_logging_task(), true);

        // Attempt to start another task for logging the same metric while the first one is still
        // running. The request to start should fail.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            400,
            false,
            false,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::AlreadyLogging)))
        );

        // Attempt to start another task for logging a different metric while the first one is
        // running. The request to start should fail.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Temperature(Temperature {
                sampling_interval_ms: 100,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 100 })),
            })]
            .into_iter(),
            200,
            false,
            false,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::AlreadyLogging)))
        );

        // Starting a new logging task of a different client should succeed.
        let mut query = runner.proxy.start_logging(
            "test2",
            &mut vec![&mut Metric::Temperature(Temperature {
                sampling_interval_ms: 500,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 500 })),
            })]
            .into_iter(),
            1000,
            false,
            false,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Run logging tasks of the first client to completion.
        for _ in 0..4 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        // Starting a new logging task of the first client should succeed now.
        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Temperature(Temperature {
                sampling_interval_ms: 100,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 100 })),
            })]
            .into_iter(),
            200,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Starting a new logging task of the second client should still fail.
        let mut query = runner.proxy.start_logging(
            "test2",
            &mut vec![&mut Metric::Temperature(Temperature {
                sampling_interval_ms: 100,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 100 })),
            })]
            .into_iter(),
            200,
            false,
            false,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::AlreadyLogging)))
        );
    }

    #[test]
    fn test_invalid_argument() {
        let mut runner = Runner::new();

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 0 })].into_iter(),
            200,
            false,
            false,
        );
        // Check `InvalidSamplingInterval` is returned when interval_ms is 0.
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval)))
        );

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 200 })].into_iter(),
            1_000,
            true,
            false,
        );
        // Check `InvalidSamplingInterval` is returned when logging samples to syslog at an interval
        // smaller than MIN_INTERVAL_FOR_SYSLOG_MS.
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval)))
        );

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 200 })].into_iter(),
            100,
            false,
            false,
        );
        // Check `InvalidSamplingInterval` is returned when logging samples to syslog at an interval
        // larger than `duration_ms`.
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval)))
        );
    }

    #[test]
    fn test_invalid_statistics_interval() {
        let mut runner = Runner::new();

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Power(Power {
                sampling_interval_ms: 500,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 500 })),
            })]
            .into_iter(),
            300,
            false,
            false,
        );

        // Check `InvalidStatisticsInterval` is returned when statistics is enabled and
        // `statistics_interval_ms` is larger than `duration_ms`.
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidStatisticsInterval)))
        );

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Power(Power {
                sampling_interval_ms: 600,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 500 })),
            })]
            .into_iter(),
            800,
            false,
            false,
        );

        // Check `InvalidStatisticsInterval` is returned when statistics is enabled and
        // `statistics_interval_ms` is less than `sampling_interval_ms`.
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidStatisticsInterval)))
        );

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Power(Power {
                sampling_interval_ms: 200,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 200 })),
            })]
            .into_iter(),
            800,
            false,
            true,
        );

        // Check `InvalidStatisticsInterval` is returned when statistics is enabled and
        // `statistics_interval_ms` is less than MIN_INTERVAL_FOR_SYSLOG_MS.
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidStatisticsInterval)))
        );
    }

    #[test]
    fn test_multiple_stops_ok() {
        let mut runner = Runner::new();

        let mut query = runner.proxy.stop_logging("test");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(false)));

        let mut query = runner.proxy.stop_logging("test");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(false)));

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            200,
            false,
            false,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
        runner.run_server_task_until_stalled();

        let mut query = runner.proxy.stop_logging("test");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(true)));
        let mut query = runner.proxy.stop_logging("test");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(false)));
        let mut query = runner.proxy.stop_logging("test");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(false)));
    }

    #[test]
    fn test_logging_temperature() {
        let mut runner = Runner::new();

        // Starting logging for 1 second at 100ms intervals. When the query stalls, the logging task
        // will be waiting on its timer.
        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Temperature(Temperature {
                sampling_interval_ms: 100,
                statistics_args: None,
            })]
            .into_iter(),
            1_000,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Check TemperatureLogger added before first temperature poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        TemperatureLogger: {
                        }
                    }
                }
            }
        );

        // For the first 9 steps, CPU and GPU temperature are logged to Insepct.
        for i in 0..9 {
            runner.cpu_temperature.set(30.0 + i as f32);
            runner.gpu_temperature.set(40.0 + i as f32);
            runner.iterate_logging_task();
            assert_data_tree!(
                runner.inspector,
                root: {
                    MetricsLogger: {
                        test: {
                            TemperatureLogger: {
                                "elapsed time (ms)": 100 * (1 + i as i64),
                                "cpu": {
                                    "data (°C)": runner.cpu_temperature.get() as f64,
                                },
                                "/dev/fake/gpu_temperature": {
                                    "data (°C)": runner.gpu_temperature.get() as f64,
                                }
                            }
                        }
                    }
                }
            );
        }

        // With one more time step, the end time has been reached, the client is removed from
        // Inspect.
        runner.iterate_logging_task();
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {}
            }
        );
    }

    #[test]
    fn test_logging_statistics() {
        let mut runner = Runner::new();

        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Temperature(Temperature {
                sampling_interval_ms: 100,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 300 })),
            })]
            .into_iter(),
            1_000,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        for i in 0..9 {
            runner.cpu_temperature.set(30.0 + i as f32);
            runner.gpu_temperature.set(40.0 + i as f32);
            runner.iterate_logging_task();

            if i < 2 {
                // Check statistics data is not available for the first 200 ms.
                assert_data_tree!(
                    runner.inspector,
                    root: {
                        MetricsLogger: {
                            test: {
                                TemperatureLogger: {
                                    "elapsed time (ms)": 100 * (1 + i as i64),
                                    "cpu": {
                                        "data (°C)": runner.cpu_temperature.get() as f64,
                                    },
                                    "/dev/fake/gpu_temperature": {
                                        "data (°C)": runner.gpu_temperature.get() as f64,
                                    }
                                }
                            }
                        }
                    }
                );
            } else {
                // Check statistics data is updated every 300 ms.
                assert_data_tree!(
                    runner.inspector,
                    root: {
                        MetricsLogger: {
                            test: {
                                TemperatureLogger: {
                                    "elapsed time (ms)": 100 * (i + 1 as i64),
                                    "cpu": {
                                        "data (°C)": (30 + i) as f64,
                                        "statistics": {
                                            "(start ms, end ms]":
                                                vec![100 * (i - 2 - (i + 1) % 3 as i64),
                                                     100 * (i + 1 - (i + 1) % 3 as i64)],
                                            "max (°C)": (30 + i - (i + 1) % 3) as f64,
                                            "min (°C)": (28 + i - (i + 1) % 3) as f64,
                                            "average (°C)": (29 + i - (i + 1) % 3) as f64,
                                        }
                                    },
                                    "/dev/fake/gpu_temperature": {
                                        "data (°C)": (40 + i) as f64,
                                        "statistics": {
                                            "(start ms, end ms]":
                                                vec![100 * (i - 2 - (i + 1) % 3 as i64),
                                                     100 * (i + 1 - (i + 1) % 3 as i64)],
                                            "max (°C)": (40 + i - (i + 1) % 3) as f64,
                                            "min (°C)": (38 + i - (i + 1) % 3) as f64,
                                            "average (°C)": (39 + i - (i + 1) % 3) as f64,
                                        }
                                    }
                                }
                            }
                        }
                    }
                );
            }
        }

        // With one more time step, the end time has been reached, the client is removed from
        // Inspect.
        runner.iterate_logging_task();
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {}
            }
        );
    }

    #[test]
    fn test_logging_power() {
        let mut runner = Runner::new();

        runner.power_1.set(2.0);
        runner.power_2.set(5.0);

        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Power(Power {
                sampling_interval_ms: 100,
                statistics_args: Some(Box::new(StatisticsArgs { statistics_interval_ms: 100 })),
            })]
            .into_iter(),
            200,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Check PowerLogger added before first power sensor poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        PowerLogger: {
                        }
                    }
                }
            }
        );

        // Run 1 logging task.
        runner.iterate_logging_task();
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        PowerLogger: {
                            "elapsed time (ms)": 100i64,
                            "power_1": {
                                "data (W)":2.0,
                                "statistics": {
                                    "(start ms, end ms]": vec![0i64, 100i64],
                                    "max (W)": 2.0,
                                    "min (W)": 2.0,
                                    "average (W)": 2.0,
                                }
                            },
                            "/dev/fake/power_2": {
                                "data (W)": 5.0,
                                "statistics": {
                                    "(start ms, end ms]": vec![0i64, 100i64],
                                    "max (W)": 5.0,
                                    "min (W)": 5.0,
                                    "average (W)": 5.0,
                                }
                            }
                        }
                    }
                }
            }
        );

        // Finish the remaining task.
        runner.iterate_logging_task();

        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {}
            }
        );
    }
}
