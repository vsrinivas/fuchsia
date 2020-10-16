// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error, Result},
    fidl_fuchsia_device as fdevice, fidl_fuchsia_hardware_temperature as ftemperature,
    fidl_fuchsia_thermal_test::{self as fthermal, TemperatureLoggerRequest},
    fuchsia_async as fasync,
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
    std::{cell::RefCell, collections::HashMap, rc::Rc, task::Poll},
};

const CONFIG_PATH: &'static str = "/config/data/config.json";

// The fuchsia.hardware.temperature.Device is composed into fuchsia.hardware.thermal.Device, so
// drivers are found in two directories.
const SERVICE_DIRS: [&str; 2] = ["/dev/class/temperature", "/dev/class/thermal"];

// Configuration for a temperature driver, used in the building process.
#[derive(Deserialize)]
struct DriverConfig {
    /// Human-readable name.
    name: String,

    /// Topological path.
    topological_path: String,
}

// Representation of an actively-used temperature driver.
struct Driver {
    /// Human-readable name.
    name: String,

    /// Topological path.
    topological_path: String,

    proxy: ftemperature::DeviceProxy,
}

pub fn connect_proxy<T: fidl::endpoints::ServiceMarker>(path: &str) -> Result<T::Proxy> {
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
    let drivers = list_drivers(dir_path).await?;
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

async fn list_drivers(path: &str) -> Result<Vec<String>> {
    let dir = io_util::open_directory_in_namespace(path, io_util::OPEN_RIGHT_READABLE)?;
    Ok(files_async::readdir(&dir).await?.iter().map(|dir_entry| dir_entry.name.clone()).collect())
}

/// Builds a TemperatureLoggerServer.
pub struct ServerBuilder<'a> {
    /// Paths to temperature sensor drivers.
    driver_configs: Vec<DriverConfig>,

    /// Optional proxies for test usage.
    driver_proxies: Option<Vec<ftemperature::DeviceProxy>>,

    /// Optional inspect root for test usage.
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> ServerBuilder<'a> {
    /// Constructs a new ServerBuilder from a JSON configuration.
    fn new_from_json(json_data: json::Value) -> Self {
        #[derive(Deserialize)]
        struct Config {
            drivers: Vec<DriverConfig>,
        }

        let config: Config = json::from_value(json_data).unwrap();

        ServerBuilder { driver_configs: config.drivers, driver_proxies: None, inspect_root: None }
    }

    /// For testing purposes, proxies may be provided directly to the Server builder. Each element
    /// of `proxies` will be paired with the corresponding element of `self.driver_configs`.
    #[cfg(test)]
    fn with_proxies(mut self, proxies: Vec<ftemperature::DeviceProxy>) -> Self {
        assert_eq!(proxies.len(), self.driver_configs.len());
        self.driver_proxies = Some(proxies);
        self
    }

    /// Injects an Inspect root for use in tests.
    #[cfg(test)]
    fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    /// Builds a TemperatureLoggerServer.
    async fn build(self) -> Result<Rc<TemperatureLoggerServer>> {
        // Clone driver names for InspectData before consuming self.driver_configs.
        let driver_names = self.driver_configs.iter().map(|c| c.name.clone()).collect();

        let drivers = match self.driver_proxies {
            // If no proxies are provided, create proxies based on driver paths.
            None => {
                // Determine topological paths for devices in SERVICE_DIRS.
                let mut topo_to_class = HashMap::new();
                for dir in &SERVICE_DIRS {
                    map_topo_paths_to_class_paths(dir, &mut topo_to_class).await?;
                }

                // For each driver path, create a proxy for the temperature service.
                let mut drivers = Vec::new();
                for config in self.driver_configs.into_iter() {
                    let class_path =
                        topo_to_class.get(&config.topological_path).ok_or(format_err!(
                            "Topological path {} missing from topo-to-class mapping: {:?}",
                            &config.topological_path,
                            topo_to_class
                        ))?;
                    let proxy = connect_proxy::<ftemperature::DeviceMarker>(class_path)?;
                    drivers.push(Driver {
                        name: config.name,
                        topological_path: config.topological_path,
                        proxy,
                    });
                }
                drivers
            }
            // If proxies were provided, match them to the driver configs.
            Some(proxies) => self
                .driver_configs
                .into_iter()
                .zip(proxies.into_iter())
                .map(|(config, proxy)| Driver {
                    name: config.name,
                    topological_path: config.topological_path,
                    proxy,
                })
                .collect(),
        };

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        Ok(TemperatureLoggerServer::new(
            Rc::new(drivers),
            Rc::new(InspectData::new(inspect_root, driver_names)),
        ))
    }
}

struct TemperatureLogger {
    /// List of temperature sensor drivers.
    drivers: Rc<Vec<Driver>>,

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
        drivers: Rc<Vec<Driver>>,
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

                    let name = &self.drivers[index].name;
                    trace_args.push(fuchsia_trace::ArgValue::of(name, temperature as f64));
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

/// Server for the TemperatureLogger protocol.
struct TemperatureLoggerServer {
    /// List of temperature sensor drivers to provie the logged temperatures.
    drivers: Rc<Vec<Driver>>,

    /// Task that asynchronously executes the polling and logging.
    logging_task: RefCell<Option<fasync::Task<()>>>,

    inspect: Rc<InspectData>,
}

impl TemperatureLoggerServer {
    fn new(drivers: Rc<Vec<Driver>>, inspect: Rc<InspectData>) -> Rc<Self> {
        Rc::new(Self { drivers, inspect, logging_task: RefCell::new(None) })
    }

    // Creates a Task to handle the request stream from a new client.
    fn handle_new_service_connection(
        self: Rc<Self>,
        mut stream: fthermal::TemperatureLoggerRequestStream,
    ) -> fasync::Task<()> {
        fasync::Task::local(
            async move {
                while let Some(request) = stream.try_next().await? {
                    self.handle_temperature_logger_request(request).await?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("{:?}", e)),
        )
    }

    // Handles a single TemperatureLoggerRequest.
    async fn handle_temperature_logger_request(
        self: &Rc<Self>,
        request: TemperatureLoggerRequest,
    ) -> Result<()> {
        match request {
            TemperatureLoggerRequest::StartLogging { interval_ms, duration_ms, responder } => {
                let mut result = self.start_logging(interval_ms, Some(duration_ms)).await;
                responder.send(&mut result)?;
            }
            TemperatureLoggerRequest::StartLoggingForever { interval_ms, responder } => {
                let mut result = self.start_logging(interval_ms, None).await;
                responder.send(&mut result)?;
            }
            fthermal::TemperatureLoggerRequest::StopLogging { .. } => {
                *self.logging_task.borrow_mut() = None
            }
        }

        Ok(())
    }

    async fn start_logging(
        &self,
        interval_ms: u32,
        duration_ms: Option<u32>,
    ) -> fthermal::TemperatureLoggerStartLoggingResult {
        // If self.logging_task is None, then the server has never logged. If the task exists and
        // is Pending then logging is already active, and an error is returned. If the task is
        // Ready, then a previous logging session has ended naturally, and we proceed to create a
        // new task.
        if let Some(task) = self.logging_task.borrow_mut().as_mut() {
            if let Poll::Pending = futures::poll!(task) {
                return Err(fthermal::TemperatureLoggerError::AlreadyLogging);
            }
        }

        if interval_ms == 0 || duration_ms.map_or(false, |d| d <= interval_ms) {
            return Err(fthermal::TemperatureLoggerError::InvalidArgument);
        }

        let logger = TemperatureLogger::new(
            self.drivers.clone(),
            zx::Duration::from_millis(interval_ms as i64),
            duration_ms.map(|ms| zx::Duration::from_millis(ms as i64)),
            self.inspect.clone(),
        );
        self.logging_task.borrow_mut().replace(logger.spawn_logging_task());

        Ok(())
    }
}

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
    fuchsia_syslog::init_with_tags(&["temperature-logger"]).expect("failed to initialize logger");

    // Set up tracing
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let mut fs = ServiceFs::new_local();

    // Allow our services to be discovered.
    fs.take_and_serve_directory_handle()?;

    // Required call to serve the inspect tree
    let inspector = inspect::component::inspector();
    inspector.serve(&mut fs)?;

    // Construct the server, and begin serving.
    let config: json::Value =
        json::from_reader(std::io::BufReader::new(std::fs::File::open(CONFIG_PATH)?))?;
    let server = ServerBuilder::new_from_json(config).build().await?;
    fs.dir("svc").add_fidl_service(move |stream: fthermal::TemperatureLoggerRequestStream| {
        TemperatureLoggerServer::handle_new_service_connection(server.clone(), stream).detach();
    });

    // This future never completes.
    fs.collect::<()>().await;

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        futures::{FutureExt, TryStreamExt},
        inspect::assert_inspect_tree,
        matches::assert_matches,
        std::cell::Cell,
    };

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

    // Helper struct for managing test state.
    struct Runner {
        executor: fasync::Executor,
        server_task: fasync::Task<()>,
        proxy: fthermal::TemperatureLoggerProxy,

        cpu_temperature: Rc<Cell<f32>>,
        gpu_temperature: Rc<Cell<f32>>,

        inspector: inspect::Inspector,

        // Driver tasks are never used but must remain in scope.
        _driver_tasks: Vec<fasync::Task<()>>,
    }

    impl Runner {
        fn new() -> Self {
            let mut executor = fasync::Executor::new_with_fake_time().unwrap();
            executor.set_fake_time(fasync::Time::from_nanos(0));

            let inspector = inspect::Inspector::new();

            let config = json::json!({
                "drivers": [
                    {
                        "name": "cpu",
                        "topological_path": "/dev/fake/cpu_temperature"
                    },
                    {
                        "name": "gpu",
                        "topological_path": "/dev/fake/gpu_temperature"
                    }
                ]
            });

            // Create two fake temperature drivers.
            let mut driver_tasks = Vec::new();
            let cpu_temperature = Rc::new(Cell::new(0.0));
            let cpu_temperature_clone = cpu_temperature.clone();
            let (cpu_temperature_proxy, task) =
                setup_fake_driver(move || cpu_temperature_clone.get());
            driver_tasks.push(task);
            let gpu_temperature = Rc::new(Cell::new(0.0));
            let gpu_temperature_clone = gpu_temperature.clone();
            let (gpu_temperature_proxy, task) =
                setup_fake_driver(move || gpu_temperature_clone.get());
            driver_tasks.push(task);

            // Build the server.
            let builder = ServerBuilder::new_from_json(config)
                .with_proxies(vec![cpu_temperature_proxy, gpu_temperature_proxy])
                .with_inspect_root(inspector.root());
            let poll = executor.run_until_stalled(&mut builder.build().boxed_local());
            let server = match poll {
                Poll::Ready(Ok(server)) => server,
                _ => panic!("Failed to build TemperatureLoggerServer"),
            };

            // Construct the server task.
            let (proxy, stream) =
                fidl::endpoints::create_proxy_and_stream::<fthermal::TemperatureLoggerMarker>()
                    .unwrap();
            let server_task = server.handle_new_service_connection(stream);

            Self {
                executor,
                server_task,
                proxy,
                cpu_temperature,
                gpu_temperature,
                inspector,
                _driver_tasks: driver_tasks,
            }
        }

        // Runs the next iteration of the logging task.
        fn iterate_logging_task(&mut self) {
            let wakeup_time = self.executor.wake_next_timer().unwrap();
            self.executor.set_fake_time(wakeup_time);
            assert_eq!(
                futures::task::Poll::Pending,
                self.executor.run_until_stalled(&mut self.server_task)
            );
        }
    }
    #[test]
    fn test_logging_duration() {
        let mut runner = Runner::new();

        // Starting logging for 1 second at 100ms intervals. When the query stalls, the logging task
        // will be waiting on its timer.
        let mut query = runner.proxy.start_logging(100, 1_000);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Check default values before first temperature poll.
        assert_inspect_tree!(
            runner.inspector,
            root: {
                TemperatureLogger: {
                    "cpu (°C)": f64::MIN,
                    "gpu (°C)": f64::MIN,
                    "elapsed time (ms)": std::i64::MIN
                }
            }
        );

        // For the first 9 steps, CPU and GPU temperature are logged to Insepct.
        for i in 0..9 {
            runner.cpu_temperature.set(30.0 + i as f32);
            runner.gpu_temperature.set(40.0 + i as f32);
            runner.iterate_logging_task();
            assert_inspect_tree!(
                runner.inspector,
                root: {
                    TemperatureLogger: {
                        "cpu (°C)": runner.cpu_temperature.get() as f64,
                        "gpu (°C)": runner.gpu_temperature.get() as f64,
                        "elapsed time (ms)": 100 * (1 + i as i64)
                    }
                }
            );
        }

        // With one more time step, the end time has been reached, and Inspect data is not updated.
        runner.cpu_temperature.set(77.0);
        runner.gpu_temperature.set(77.0);
        runner.iterate_logging_task();
        assert_inspect_tree!(
            runner.inspector,
            root: {
                TemperatureLogger: {
                    "cpu (°C)": 38.0,
                    "gpu (°C)": 48.0,
                    "elapsed time (ms)": 900i64
                }
            }
        );
    }

    #[test]
    fn test_log_forever() {
        let mut runner = Runner::new();

        // We can't actually that the logger never stops, but we'll run it through a decent number
        // of iterations to exercise the code path.
        let mut query = runner.proxy.start_logging_forever(1_000);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
        for i in 0..1000 {
            runner.cpu_temperature.set(i as f32);
            runner.gpu_temperature.set(i as f32);
            runner.iterate_logging_task();
            assert_inspect_tree!(
                runner.inspector,
                root: {
                    TemperatureLogger: {
                        "cpu (°C)": runner.cpu_temperature.get() as f64,
                        "gpu (°C)": runner.gpu_temperature.get() as f64,
                        "elapsed time (ms)": 1000 * (1 + i as i64)
                    }
                }
            );
        }
    }

    #[test]
    fn test_already_logging_error() {
        let mut runner = Runner::new();

        // Start logging.
        let mut query = runner.proxy.start_logging(100, 200);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Attempting to start logging a second time should fail.
        let mut query = runner.proxy.start_logging(100, 500);
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fthermal::TemperatureLoggerError::AlreadyLogging)))
        );

        // Stop logging.
        assert!(runner.proxy.stop_logging().is_ok());

        // Now starting logging will succeed.
        let mut query = runner.proxy.start_logging(100, 200);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Logging stops on the second wakeup.
        runner.iterate_logging_task();
        runner.iterate_logging_task();

        // Starting logging again succeeds.
        let mut query = runner.proxy.start_logging(100, 200);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));
    }

    #[test]
    fn test_invalid_arguments() {
        let mut runner = Runner::new();

        // Interval must be positive.
        let mut query = runner.proxy.start_logging(0, 200);
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fthermal::TemperatureLoggerError::InvalidArgument)))
        );

        // Duration must be greater than the interval length.
        let mut query = runner.proxy.start_logging(100, 100);
        assert_matches!(
            runner.executor.run_until_stalled(&mut query),
            Poll::Ready(Ok(Err(fthermal::TemperatureLoggerError::InvalidArgument)))
        );
    }

    #[test]
    fn test_multiple_stops_ok() {
        let mut runner = Runner::new();

        // Start logging.
        let mut query = runner.proxy.start_logging(100, 200);
        assert_matches!(runner.executor.run_until_stalled(&mut query), Poll::Ready(Ok(Ok(()))));

        // Stop logging multiple times.
        assert!(runner.proxy.stop_logging().is_ok());
        assert!(runner.proxy.stop_logging().is_ok());
    }
}
