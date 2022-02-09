// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error, Result},
    fidl_fuchsia_device as fdevice, fidl_fuchsia_hardware_temperature as ftemperature,
    fidl_fuchsia_kernel as fkernel,
    fidl_fuchsia_metricslogger_test::{self as fmetrics, MetricsLoggerRequest},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::{
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
        TryFutureExt,
    },
    serde_derive::Deserialize,
    serde_json as json,
    std::{
        cell::RefCell, collections::HashMap, collections::HashSet, iter::FromIterator, rc::Rc,
        task::Poll,
    },
};

const CONFIG_PATH: &'static str = "/config/data/config.json";

// The fuchsia.hardware.temperature.Device is composed into fuchsia.hardware.thermal.Device, so
// drivers are found in two directories.
const TEMPERATURE_SERVICE_DIRS: [&str; 2] = ["/dev/class/temperature", "/dev/class/thermal"];

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
    let dir = match io_util::open_directory_in_namespace(path, io_util::OPEN_RIGHT_READABLE) {
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
    match files_async::readdir(&dir).await {
        Ok(s) => s.iter().map(|dir_entry| dir_entry.name.clone()).collect(),
        Err(e) => {
            fx_log_err!("Read service directory {} failed with error: {}", path, e);
            Vec::new()
        }
    }
}

// Representation of an actively-used temperature driver.
struct TemperatureDriver {
    alias: Option<String>,

    topological_path: String,

    proxy: ftemperature::DeviceProxy,
}

impl TemperatureDriver {
    fn name(&self) -> &str {
        &self.alias.as_ref().unwrap_or(&self.topological_path)
    }
}

/// Builds a MetricsLoggerServer.
pub struct ServerBuilder<'a> {
    /// Optional configs for temperature sensor drivers.
    temperature_driver_aliases: HashMap<String, String>,

    /// Optional drivers for test usage.
    temperature_drivers: Option<Vec<TemperatureDriver>>,

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
            drivers: Vec<DriverAlias>,
        }
        let config: Option<Config> = json_data.map(|d| json::from_value(d).unwrap());

        let topo_to_alias: HashMap<String, String> = config.map_or(HashMap::new(), |c| {
            c.drivers.into_iter().map(|m| (m.topological_path, m.name)).collect()
        });

        ServerBuilder {
            temperature_driver_aliases: topo_to_alias,
            temperature_drivers: None,
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
        let drivers = match self.temperature_drivers {
            // If no proxies are provided, create proxies based on driver paths.
            None => {
                // Determine topological paths for devices in TEMPERATURE_SERVICE_DIRS.
                let mut topo_to_class = HashMap::new();
                for dir in &TEMPERATURE_SERVICE_DIRS {
                    map_topo_paths_to_class_paths(dir, &mut topo_to_class).await?;
                }

                // For each driver path, create a proxy for the service.
                let mut drivers = Vec::new();
                for (topological_path, class_path) in topo_to_class {
                    let proxy = connect_proxy::<ftemperature::DeviceMarker>(&class_path)?;
                    let alias = self
                        .temperature_driver_aliases
                        .get(&topological_path)
                        .map(|c| c.to_string());
                    drivers.push(TemperatureDriver { alias, topological_path, proxy });
                }
                drivers
            }

            Some(drivers) => drivers,
        };

        // Create proxy for polling CPU stats
        let cpu_stats_proxy = match &self.cpu_stats_proxy {
            Some(proxy) => proxy.clone(),
            None => connect_to_protocol::<fkernel::StatsMarker>()?,
        };

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        let driver_names = drivers.iter().map(|c| c.name().to_string()).collect();

        Ok(MetricsLoggerServer::new(
            Rc::new(drivers),
            Rc::new(InspectData::new(inspect_root, driver_names)),
            Rc::new(cpu_stats_proxy),
        ))
    }
}

struct TemperatureLogger {
    /// List of temperature sensor drivers.
    drivers: Rc<Vec<TemperatureDriver>>,

    /// Logging interval.
    interval: zx::Duration,

    /// Start time for the logger; used to calculate elapsed time.
    start_time: fasync::Time,

    /// Time at which the logger will stop.
    end_time: fasync::Time,

    inspect: Rc<InspectData>,
}

impl TemperatureLogger {
    fn new(
        drivers: Rc<Vec<TemperatureDriver>>,
        interval: zx::Duration,
        duration: Option<zx::Duration>,
        inspect: Rc<InspectData>,
    ) -> Self {
        let start_time = fasync::Time::now();
        let end_time = duration.map_or(fasync::Time::INFINITE, |d| start_time + d);
        TemperatureLogger { drivers, interval, start_time, end_time, inspect }
    }

    /// Constructs a Task that will log temperatures from all provided sensors.
    fn spawn_logging_task(self) -> fasync::Task<()> {
        let mut interval = fasync::Interval::new(self.interval);

        fasync::Task::local(async move {
            while let Some(()) = interval.next().await {
                // If we're interested in very high-rate polling in the future, it might be worth
                // comparing the elapsed time to the intended polling interval and logging any
                // anomalies.
                let now = fasync::Time::now();
                if now >= self.end_time {
                    break;
                }
                self.log_temperatures(now - self.start_time).await;
            }
        })
    }

    /// Polls temperature sensors and logs the resulting data to Inspect and trace.
    async fn log_temperatures(&self, elapsed: zx::Duration) {
        // Execute a query to each temperature sensor driver.
        let queries = FuturesUnordered::new();
        for (index, driver) in self.drivers.iter().enumerate() {
            let query = async move {
                let result = match driver.proxy.get_temperature_celsius().await {
                    Ok((zx_status, temperature)) => match zx::Status::ok(zx_status) {
                        Ok(()) => Ok(temperature),
                        Err(e) => Err(format_err!(
                            "{}: get_temperature_celsius returned an error: {}",
                            driver.topological_path,
                            e
                        )),
                    },
                    Err(e) => Err(format_err!(
                        "{}: get_temperature_celsius IPC failed: {}",
                        driver.topological_path,
                        e
                    )),
                };
                (index, result)
            };
            queries.push(query);
        }
        let results = queries.collect::<Vec<(usize, Result<f32, Error>)>>().await;

        // Log the temperatures to Inspect and to a trace counter.
        let mut trace_args = Vec::new();
        for (index, result) in results.into_iter() {
            match result {
                Ok(temperature) => {
                    self.inspect.log_temperature(index, temperature, elapsed.into_millis());

                    trace_args.push(fuchsia_trace::ArgValue::of(
                        self.drivers[index].name(),
                        temperature as f64,
                    ));
                }
                // In case of a polling error, the previous value will from this sensor will not be
                // updated. We could do something fancier like exposing an error count, but this
                // sample will be missing from the trace counter as is, and any serious analysis
                // should be performed on the trace.
                Err(e) => fx_log_err!("Error reading temperature: {:?}", e),
            };
        }

        fuchsia_trace::counter(
            fuchsia_trace::cstr!("temperature_logger"),
            fuchsia_trace::cstr!("temperature"),
            0,
            &trace_args,
        );
    }
}

struct CpuLoadLogger {
    interval: zx::Duration,
    end_time: fasync::Time,
    last_sample: Option<(fasync::Time, fkernel::CpuStats)>,
    stats_proxy: Rc<fkernel::StatsProxy>,
}

impl CpuLoadLogger {
    fn new(
        interval: zx::Duration,
        duration: Option<zx::Duration>,
        stats_proxy: Rc<fkernel::StatsProxy>,
    ) -> Self {
        let end_time = duration.map_or(fasync::Time::INFINITE, |d| fasync::Time::now() + d);
        CpuLoadLogger { interval, end_time, last_sample: None, stats_proxy }
    }

    fn spawn_logging_task(mut self) -> fasync::Task<()> {
        let mut interval = fasync::Interval::new(self.interval);

        fasync::Task::local(async move {
            while let Some(()) = interval.next().await {
                let now = fasync::Time::now();
                if now >= self.end_time {
                    break;
                }
                self.log_cpu_usage(now).await;
            }
        })
    }

    async fn log_cpu_usage(&mut self, now: fasync::Time) {
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
                    fuchsia_trace::counter!(
                        "system_metrics_logger",
                        "cpu_usage",
                        0,
                        "cpu_usage" => cpu_percentage_sum / cpu_stats.actual_num_cpus as f64
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

    /// Task that asynchronously executes the polling and logging of temperature.
    temperature_logging_task: RefCell<Option<fasync::Task<()>>>,

    // Inspect node for logging temperature data.
    temperature_inspect: Rc<InspectData>,

    /// Task that asynchronously executes the polling and logging of cpu stats.
    cpu_logging_task: RefCell<Option<fasync::Task<()>>>,

    /// Proxy for polling CPU stats.
    cpu_stats_proxy: Rc<fkernel::StatsProxy>,
}

impl MetricsLoggerServer {
    fn new(
        temperature_drivers: Rc<Vec<TemperatureDriver>>,
        temperature_inspect: Rc<InspectData>,
        cpu_stats_proxy: Rc<fkernel::StatsProxy>,
    ) -> Rc<Self> {
        Rc::new(Self {
            temperature_drivers,
            temperature_inspect,
            cpu_stats_proxy,
            temperature_logging_task: RefCell::new(None),
            cpu_logging_task: RefCell::new(None),
        })
    }

    fn handle_new_service_connection(
        self: Rc<Self>,
        mut stream: fmetrics::MetricsLoggerRequestStream,
    ) -> fasync::Task<()> {
        fasync::Task::local(
            async move {
                while let Some(request) = stream.try_next().await? {
                    self.handle_metrics_logger_request(request).await?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("{:?}", e)),
        )
    }

    async fn start_logging(
        &self,
        metrics: Vec<fmetrics::Metric>,
        duration_ms: Option<u32>,
    ) -> fmetrics::MetricsLoggerStartLoggingResult {
        if self.already_logging().await {
            return Err(fmetrics::MetricsLoggerError::AlreadyLogging);
        }

        let incoming_metric_types: HashSet<_> =
            HashSet::from_iter(metrics.iter().map(|m| std::mem::discriminant(m)));
        if incoming_metric_types.len() != metrics.len() {
            return Err(fmetrics::MetricsLoggerError::DuplicatedMetric);
        }

        for metric in metrics {
            match metric {
                fmetrics::Metric::CpuLoad(fmetrics::CpuLoad { interval_ms }) => {
                    self.start_logging_cpu_usage(interval_ms, duration_ms).await?
                }
                fmetrics::Metric::Temperature(fmetrics::Temperature { interval_ms }) => {
                    self.start_logging_temperature(interval_ms, duration_ms).await?
                }
            }
        }
        Ok(())
    }

    async fn start_logging_temperature(
        &self,
        interval_ms: u32,
        duration_ms: Option<u32>,
    ) -> fmetrics::MetricsLoggerStartLoggingResult {
        if self.temperature_drivers.len() == 0 {
            return Err(fmetrics::MetricsLoggerError::NoDrivers);
        }

        if interval_ms == 0 || duration_ms.map_or(false, |d| d <= interval_ms) {
            return Err(fmetrics::MetricsLoggerError::InvalidArgument);
        }

        let temperature_logger = TemperatureLogger::new(
            self.temperature_drivers.clone(),
            zx::Duration::from_millis(interval_ms as i64),
            duration_ms.map(|ms| zx::Duration::from_millis(ms as i64)),
            self.temperature_inspect.clone(),
        );
        self.temperature_logging_task.borrow_mut().replace(temperature_logger.spawn_logging_task());

        Ok(())
    }

    async fn start_logging_cpu_usage(
        &self,
        interval_ms: u32,
        duration_ms: Option<u32>,
    ) -> fmetrics::MetricsLoggerStartLoggingResult {
        if interval_ms == 0 || duration_ms.map_or(false, |d| d <= interval_ms) {
            return Err(fmetrics::MetricsLoggerError::InvalidArgument);
        }

        let cpu_logger = CpuLoadLogger::new(
            zx::Duration::from_millis(interval_ms as i64),
            duration_ms.map(|ms| zx::Duration::from_millis(ms as i64)),
            self.cpu_stats_proxy.clone(),
        );
        self.cpu_logging_task.borrow_mut().replace(cpu_logger.spawn_logging_task());

        Ok(())
    }

    async fn already_logging(&self) -> bool {
        // If any task exists and is Pending then logging is already active.
        if let Some(task) = self.temperature_logging_task.borrow_mut().as_mut() {
            if let Poll::Pending = futures::poll!(task) {
                return true;
            }
        }
        if let Some(task) = self.cpu_logging_task.borrow_mut().as_mut() {
            if let Poll::Pending = futures::poll!(task) {
                return true;
            }
        }
        false
    }

    fn stop_logging(&self) {
        *self.cpu_logging_task.borrow_mut() = None;
        *self.temperature_logging_task.borrow_mut() = None;
    }

    async fn handle_metrics_logger_request(
        self: &Rc<Self>,
        request: MetricsLoggerRequest,
    ) -> Result<()> {
        // TODO (fxbug.dev/92142): Use client_id to support concurrent clients.
        match request {
            MetricsLoggerRequest::StartLogging {
                client_id: _,
                metrics,
                duration_ms,
                responder,
            } => {
                let mut result = self.start_logging(metrics, Some(duration_ms)).await;
                // If the current request contains an error, clear all tasks and respond.
                // If Logger is already active, directly respond.
                if let Err(e) = result {
                    if e != fmetrics::MetricsLoggerError::AlreadyLogging {
                        self.stop_logging();
                    }
                }
                responder.send(&mut result)?;
            }
            MetricsLoggerRequest::StartLoggingForever { client_id: _, metrics, responder } => {
                let mut result = self.start_logging(metrics, None).await;
                // If the current request contains an error, clear all tasks and respond.
                // If Logger is already active, directly respond.
                if let Err(e) = result {
                    if e != fmetrics::MetricsLoggerError::AlreadyLogging {
                        self.stop_logging();
                    }
                }
                responder.send(&mut result)?;
            }
            MetricsLoggerRequest::StopLogging { client_id: _, responder } => {
                let already_logging = self.already_logging().await;
                self.stop_logging();
                responder.send(already_logging)?;
            }
        }

        Ok(())
    }
}

// TODO (fxbug.dev/92320): Populate CPU Usage and active client info into Inspect.
struct InspectData {
    temperatures: Vec<inspect::DoubleProperty>,
    elapsed_millis: inspect::IntProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, sensor_names: Vec<String>) -> Self {
        let root = parent.create_child("TemperatureLogger");
        let temperatures = sensor_names
            .into_iter()
            .map(|name| root.create_double(name + " (°C)", f64::MIN))
            .collect();
        let elapsed_millis = root.create_int("elapsed time (ms)", std::i64::MIN);
        parent.record(root);
        Self { temperatures, elapsed_millis: elapsed_millis }
    }

    fn log_temperature(&self, index: usize, value: f32, elapsed_millis: i64) {
        self.temperatures[index].set(value as f64);
        self.elapsed_millis.set(elapsed_millis);
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
        fidl_fuchsia_kernel::{CpuStats, PerCpuStats},
        fmetrics::{CpuLoad, Metric, Temperature},
        futures::{FutureExt, TryStreamExt},
        inspect::assert_data_tree,
        matches::assert_matches,
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

    fn setup_fake_driver<'a>(
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

    struct Runner {
        server_task: fasync::Task<()>,
        proxy: fmetrics::MetricsLoggerProxy,

        cpu_temperature: Rc<Cell<f32>>,
        gpu_temperature: Rc<Cell<f32>>,

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
                setup_fake_driver(move || cpu_temperature_clone.get());
            tasks.push(task);
            let gpu_temperature = Rc::new(Cell::new(0.0));
            let gpu_temperature_clone = gpu_temperature.clone();
            let (gpu_temperature_proxy, task) =
                setup_fake_driver(move || gpu_temperature_clone.get());
            tasks.push(task);
            let drivers = vec![
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

            let cpu_stats = Rc::new(RefCell::new(CpuStats {
                actual_num_cpus: 1,
                per_cpu_stats: Some(vec![PerCpuStats { idle_time: Some(0), ..PerCpuStats::EMPTY }]),
            }));
            let (cpu_stats_proxy, task) =
                setup_fake_stats_service(move || cpu_stats.borrow().clone());
            tasks.push(task);

            // Build the server.
            let builder = ServerBuilder::new_from_json(None)
                .with_temperature_drivers(drivers)
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
    }

    #[test]
    fn test_logging_duration() {
        let mut runner = Runner::new();

        // Start logging every 100ms for a total of 2000ms.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            2000,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

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
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidArgument)))
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
        let mut query = runner.proxy.start_logging_forever(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Samples should continue forever. Obviously we can't check infinitely many samples, but
        // we can check that they don't stop for a relatively large number of iterations.
        for _ in 0..1000 {
            assert_eq!(runner.iterate_logging_task(), true);
        }

        let mut query = runner.proxy.stop_logging("test");
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(true)));
        let mut query = runner.proxy.start_logging_forever(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
    }

    #[test]
    fn test_concurrent_logging() {
        let mut runner = Runner::new();

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![
                &mut Metric::CpuLoad(CpuLoad { interval_ms: 100 }),
                &mut Metric::Temperature(Temperature { interval_ms: 200 }),
            ]
            .into_iter(),
            600,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Check default values before first temperature poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                TemperatureLogger: {
                    "cpu (°C)": f64::MIN,
                    "/dev/fake/gpu_temperature (°C)": f64::MIN,
                    "elapsed time (ms)": std::i64::MIN
                }
            }
        );

        runner.cpu_temperature.set(35.0 as f32);
        runner.gpu_temperature.set(45.0 as f32);

        // Run existing logging tasks to completion (6 CPU Load tasks + 3 Temperature tasks).
        for _ in 0..9 {
            assert_eq!(runner.iterate_logging_task(), true);
        }
        // Two tasks stop at the same time.
        assert_eq!(runner.iterate_logging_task(), false);

        // Check temperature data in Inspect.
        assert_data_tree!(
            runner.inspector,
            root: {
                TemperatureLogger: {
                    "cpu (°C)": 35.0,
                    "/dev/fake/gpu_temperature (°C)": 45.0,
                    "elapsed time (ms)": 400i64
                }
            }
        );
    }

    #[test]
    fn test_already_logging() {
        let mut runner = Runner::new();

        // Start the first logging task.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            400,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        assert_eq!(runner.iterate_logging_task(), true);

        // Attempt to start another task for logging the same metric while the first one is still
        // running. The request to start should fail.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            400,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::AlreadyLogging)))
        );

        // Attempt to start another task for logging a different metric while the first one is
        // running. The request to start should fail.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Temperature(Temperature { interval_ms: 100 })].into_iter(),
            200,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::AlreadyLogging)))
        );

        // Run existing logging tasks to completion.
        for _ in 0..3 {
            assert_eq!(runner.iterate_logging_task(), true);
        }
        assert_eq!(runner.iterate_logging_task(), false);

        // Starting a new logging task should succeed now.
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Temperature(Temperature { interval_ms: 100 })].into_iter(),
            200,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
    }

    #[test]
    fn test_invalid_argument() {
        let mut runner = Runner::new();

        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 0 })].into_iter(),
            200,
        );
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fmetrics::MetricsLoggerError::InvalidArgument)))
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
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

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
        let mut query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::Temperature(Temperature { interval_ms: 100 })].into_iter(),
            1_000,
        );
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Check default values before first temperature poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                TemperatureLogger: {
                    "cpu (°C)": f64::MIN,
                    "/dev/fake/gpu_temperature (°C)": f64::MIN,
                    "elapsed time (ms)": std::i64::MIN
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
                    TemperatureLogger: {
                        "cpu (°C)": runner.cpu_temperature.get() as f64,
                        "/dev/fake/gpu_temperature (°C)": runner.gpu_temperature.get() as f64,
                        "elapsed time (ms)": 100 * (1 + i as i64)
                    }
                }
            );
        }

        // With one more time step, the end time has been reached, and Inspect data is not updated.
        runner.cpu_temperature.set(77.0);
        runner.gpu_temperature.set(77.0);
        runner.iterate_logging_task();
        assert_data_tree!(
            runner.inspector,
            root: {
                TemperatureLogger: {
                    "cpu (°C)": 38.0,
                    "/dev/fake/gpu_temperature (°C)": 48.0,
                    "elapsed time (ms)": 900i64
                }
            }
        );
    }
}
