// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cpu_load_logger;
mod driver_utils;
mod gpu_usage_logger;
mod sensor_logger;

use {
    crate::cpu_load_logger::{generate_cpu_stats_driver, CpuLoadLogger, CpuStatsDriver},
    crate::driver_utils::Config,
    crate::gpu_usage_logger::{generate_gpu_drivers, GpuDriver, GpuUsageLogger},
    crate::sensor_logger::{
        generate_power_drivers, generate_temperature_drivers, PowerDriver, PowerLogger,
        TemperatureDriver, TemperatureLogger,
    },
    anyhow::{Error, Result},
    fidl_fuchsia_metricslogger_test::{self as fmetrics, MetricsLoggerRequest},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    futures::{
        future::join_all,
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
        task::Context,
        FutureExt, TryFutureExt,
    },
    serde_json as json,
    std::{
        cell::RefCell,
        collections::{HashMap, HashSet},
        iter::FromIterator,
        pin::Pin,
        rc::Rc,
    },
    tracing::{error, info},
};

// Max number of clients that can log concurrently. This limit is chosen mostly arbitrarily to allow
// a fixed number clients to keep memory use bounded.
const MAX_CONCURRENT_CLIENTS: usize = 20;

// Minimum interval for logging to syslog.
const MIN_INTERVAL_FOR_SYSLOG_MS: u32 = 500;

const CONFIG_PATH: &'static str = "/config/data/config.json";

/// Builds a MetricsLoggerServer.
pub struct ServerBuilder<'a> {
    /// Optional drivers for test usage.
    temperature_drivers: Option<Vec<TemperatureDriver>>,

    /// Optional drivers for test usage.
    power_drivers: Option<Vec<PowerDriver>>,

    /// Optional drivers for test usage.
    gpu_drivers: Option<Vec<GpuDriver>>,

    /// Optional proxy for test usage.
    cpu_stats_driver: Option<CpuStatsDriver>,

    /// Optional inspect root for test usage.
    inspect_root: Option<&'a inspect::Node>,

    /// Config for driver aliases.
    config: Option<Config>,
}

impl<'a> ServerBuilder<'a> {
    /// Constructs a new ServerBuilder from a JSON configuration.
    fn new_from_json(json_data: Option<json::Value>) -> Self {
        let config: Option<Config> = json_data.map(|d| json::from_value(d).unwrap());

        ServerBuilder {
            temperature_drivers: None,
            power_drivers: None,
            gpu_drivers: None,
            cpu_stats_driver: None,
            inspect_root: None,
            config,
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
    fn with_gpu_drivers(mut self, gpu_drivers: Vec<GpuDriver>) -> Self {
        self.gpu_drivers = Some(gpu_drivers);
        self
    }

    #[cfg(test)]
    fn with_cpu_stats_driver(mut self, cpu_stats_driver: CpuStatsDriver) -> Self {
        self.cpu_stats_driver = Some(cpu_stats_driver);
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
        let temperature_drivers = match self.temperature_drivers {
            None => RefCell::new(None),
            Some(drivers) => RefCell::new(Some(Rc::new(drivers))),
        };

        let power_drivers = match self.power_drivers {
            None => RefCell::new(None),
            Some(drivers) => RefCell::new(Some(Rc::new(drivers))),
        };

        let cpu_stats_driver = match self.cpu_stats_driver {
            None => RefCell::new(None),
            Some(proxy) => RefCell::new(Some(Rc::new(proxy))),
        };

        let gpu_drivers = match self.gpu_drivers {
            None => RefCell::new(None),
            Some(drivers) => RefCell::new(Some(Rc::new(drivers))),
        };

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        Ok(MetricsLoggerServer::new(
            temperature_drivers,
            power_drivers,
            gpu_drivers,
            cpu_stats_driver,
            self.config,
            inspect_root.create_child("MetricsLogger"),
        ))
    }
}

struct MetricsLoggerServer {
    /// List of temperature sensor drivers for polling temperatures.
    temperature_drivers: RefCell<Option<Rc<Vec<TemperatureDriver>>>>,

    /// List of power sensor drivers for polling powers.
    power_drivers: RefCell<Option<Rc<Vec<PowerDriver>>>>,

    /// List of gpu drivers for polling GPU stats.
    gpu_drivers: RefCell<Option<Rc<Vec<GpuDriver>>>>,

    /// Proxy for polling CPU stats.
    cpu_stats_driver: RefCell<Option<Rc<CpuStatsDriver>>>,

    /// Root node for MetricsLogger
    inspect_root: inspect::Node,

    /// Config for driver aliases.
    config: Option<Config>,

    /// Map that stores the logging task for all clients. Once a logging request is received
    /// with a new client_id, a task is lazily inserted into the map using client_id as the key.
    client_tasks: RefCell<HashMap<String, fasync::Task<()>>>,
}

impl MetricsLoggerServer {
    fn new(
        temperature_drivers: RefCell<Option<Rc<Vec<TemperatureDriver>>>>,
        power_drivers: RefCell<Option<Rc<Vec<PowerDriver>>>>,
        gpu_drivers: RefCell<Option<Rc<Vec<GpuDriver>>>>,
        cpu_stats_driver: RefCell<Option<Rc<CpuStatsDriver>>>,
        config: Option<Config>,
        inspect_root: inspect::Node,
    ) -> Rc<Self> {
        Rc::new(Self {
            temperature_drivers,
            power_drivers,
            gpu_drivers,
            cpu_stats_driver,
            inspect_root,
            config,
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
            .unwrap_or_else(|e: Error| error!("{:?}", e)),
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

        let client_inspect = self.inspect_root.create_child(&client_id.to_string());
        let futures: FuturesUnordered<Box<dyn futures::Future<Output = ()>>> =
            FuturesUnordered::new();

        for metric in metrics {
            match metric {
                fmetrics::Metric::CpuLoad(fmetrics::CpuLoad { interval_ms }) => {
                    let driver = self.get_cpu_driver().await?;
                    let cpu_load_logger = CpuLoadLogger::new(
                        driver,
                        interval_ms,
                        duration_ms,
                        &client_inspect,
                        String::from(&client_id.to_string()),
                        output_samples_to_syslog,
                    )
                    .await?;
                    futures.push(Box::new(cpu_load_logger.log_cpu_usages()));
                }
                fmetrics::Metric::GpuUsage(fmetrics::GpuUsage { interval_ms }) => {
                    let drivers = self.get_gpu_drivers().await?;
                    let gpu_usage_logger = GpuUsageLogger::new(
                        drivers,
                        interval_ms,
                        duration_ms,
                        &client_inspect,
                        String::from(&client_id.to_string()),
                        output_samples_to_syslog,
                    )
                    .await?;
                    futures.push(Box::new(gpu_usage_logger.log_gpu_usages()));
                }
                fmetrics::Metric::Temperature(fmetrics::Temperature {
                    sampling_interval_ms,
                    statistics_args,
                }) => {
                    let drivers = self.get_temperature_drivers().await?;
                    let temperature_logger = TemperatureLogger::new(
                        drivers,
                        sampling_interval_ms,
                        statistics_args.map(|i| i.statistics_interval_ms),
                        duration_ms,
                        &client_inspect,
                        String::from(&client_id.to_string()),
                        output_samples_to_syslog,
                        output_stats_to_syslog,
                    )
                    .await?;
                    futures.push(Box::new(temperature_logger.log_data()));
                }
                fmetrics::Metric::Power(fmetrics::Power {
                    sampling_interval_ms,
                    statistics_args,
                }) => {
                    let drivers = self.get_power_drivers().await?;
                    let power_logger = PowerLogger::new(
                        drivers,
                        sampling_interval_ms,
                        statistics_args.map(|i| i.statistics_interval_ms),
                        duration_ms,
                        &client_inspect,
                        String::from(&client_id.to_string()),
                        output_samples_to_syslog,
                        output_stats_to_syslog,
                    )
                    .await?;
                    futures.push(Box::new(power_logger.log_data()));
                }
            }
        }

        self.client_tasks.borrow_mut().insert(
            client_id.to_string(),
            fasync::Task::local(async move {
                // Move ownership of `client_inspect` to the client task.
                let _inspect = client_inspect;
                join_all(futures.into_iter().map(|f| Pin::from(f))).await;
            }),
        );

        Ok(())
    }

    fn purge_completed_tasks(&self) {
        self.client_tasks.borrow_mut().retain(|_n, task| {
            task.poll_unpin(&mut Context::from_waker(futures::task::noop_waker_ref())).is_pending()
        });
    }

    async fn get_temperature_drivers(
        &self,
    ) -> Result<Rc<Vec<TemperatureDriver>>, fmetrics::MetricsLoggerError> {
        match self.temperature_drivers.borrow().as_ref() {
            Some(drivers) => return Ok(drivers.clone()),
            _ => (),
        }

        let driver_aliases = match &self.config {
            None => HashMap::new(),
            Some(c) => c.temperature_drivers.as_ref().map_or_else(
                || HashMap::new(),
                |d| d.into_iter().map(|m| (m.topological_path.clone(), m.name.clone())).collect(),
            ),
        };

        let drivers =
            Rc::new(generate_temperature_drivers(driver_aliases).await.map_err(|err| {
                error!(%err, "Request failed with internal error");
                fmetrics::MetricsLoggerError::Internal
            })?);
        self.temperature_drivers.replace(Some(drivers.clone()));
        Ok(drivers)
    }

    async fn get_power_drivers(
        &self,
    ) -> Result<Rc<Vec<PowerDriver>>, fmetrics::MetricsLoggerError> {
        match self.power_drivers.borrow().as_ref() {
            Some(drivers) => return Ok(drivers.clone()),
            _ => (),
        }

        let driver_aliases = match &self.config {
            None => HashMap::new(),
            Some(c) => c.power_drivers.as_ref().map_or_else(
                || HashMap::new(),
                |d| d.into_iter().map(|m| (m.topological_path.clone(), m.name.clone())).collect(),
            ),
        };

        let drivers = Rc::new(generate_power_drivers(driver_aliases).await.map_err(|err| {
            error!(%err, "Request failed with internal error");
            fmetrics::MetricsLoggerError::Internal
        })?);
        self.power_drivers.replace(Some(drivers.clone()));
        Ok(drivers)
    }

    async fn get_gpu_drivers(&self) -> Result<Rc<Vec<GpuDriver>>, fmetrics::MetricsLoggerError> {
        match self.gpu_drivers.borrow().as_ref() {
            Some(drivers) => return Ok(drivers.clone()),
            _ => (),
        }

        let driver_aliases = match &self.config {
            None => HashMap::new(),
            Some(c) => c.gpu_drivers.as_ref().map_or_else(
                || HashMap::new(),
                |d| d.into_iter().map(|m| (m.topological_path.clone(), m.name.clone())).collect(),
            ),
        };

        let drivers = Rc::new(generate_gpu_drivers(driver_aliases).await.map_err(|err| {
            error!(%err, "Request failed with internal error");
            fmetrics::MetricsLoggerError::Internal
        })?);
        self.gpu_drivers.replace(Some(drivers.clone()));
        Ok(drivers)
    }

    async fn get_cpu_driver(&self) -> Result<Rc<CpuStatsDriver>, fmetrics::MetricsLoggerError> {
        match self.cpu_stats_driver.borrow().as_ref() {
            Some(driver) => return Ok(driver.clone()),
            _ => (),
        }

        let driver = Rc::new(generate_cpu_stats_driver().await.map_err(|err| {
            error!(%err, "Request failed with internal error");
            fmetrics::MetricsLoggerError::Internal
        })?);
        self.cpu_stats_driver.replace(Some(driver.clone()));
        Ok(driver)
    }
}

#[fuchsia::main(logging_tags = ["metrics-logger"])]
async fn main() {
    // v2 components can't surface stderr yet, so we need to explicitly log errors.
    match inner_main().await {
        Err(err) => error!(%err, "Terminated with error"),
        Ok(()) => info!("Terminated with Ok(())"),
    }
}

async fn inner_main() -> Result<()> {
    info!("Starting metrics logger");

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
        crate::cpu_load_logger::{
            tests::{
                create_vmo_from_topology_nodes, generate_cluster_node, generate_processor_node,
            },
            vmo_to_topology,
        },
        crate::gpu_usage_logger::tests::create_magma_total_time_query_result_vmo,
        assert_matches::assert_matches,
        fidl_fuchsia_gpu_magma as fgpu, fidl_fuchsia_hardware_power_sensor as fpower,
        fidl_fuchsia_hardware_temperature as ftemperature, fidl_fuchsia_kernel as fkernel,
        fidl_fuchsia_kernel::{CpuStats, PerCpuStats},
        fmetrics::{CpuLoad, GpuUsage, Metric, Power, StatisticsArgs, Temperature},
        fuchsia_zircon as zx,
        futures::{task::Poll, FutureExt, TryStreamExt},
        inspect::assert_data_tree,
        std::cell::Cell,
    };

    fn setup_fake_cpu_stats_service(
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

    fn setup_fake_gpu_driver(
        mut query: impl FnMut(fgpu::QueryId) -> fgpu::DeviceQueryResult + 'static,
    ) -> (fgpu::DeviceProxy, fasync::Task<()>) {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fgpu::DeviceMarker>().unwrap();
        let task = fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fgpu::DeviceRequest::Query { query_id, responder }) => {
                        let _ = responder.send(&mut query(query_id));
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

        gpu_time_ns: Rc<Cell<u64>>,
        monotonic_time_ns: Rc<Cell<u64>>,

        cpu_idle_time: Rc<Cell<[i64; 5]>>,

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

            let power_1 = Rc::new(Cell::new(0.0));
            let power_1_clone = power_1.clone();
            let (power_1_proxy, task) = setup_fake_power_driver(move || power_1_clone.get());
            tasks.push(task);
            let power_2 = Rc::new(Cell::new(0.0));
            let power_2_clone = power_2.clone();
            let (power_2_proxy, task) = setup_fake_power_driver(move || power_2_clone.get());
            tasks.push(task);

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

            let gpu_time_ns = Rc::new(Cell::new(0 as u64));
            let gpu_time_ns_clone = gpu_time_ns.clone();
            let monotonic_time_ns = Rc::new(Cell::new(0 as u64));
            let monotonic_time_ns_clone = monotonic_time_ns.clone();
            let (gpu_proxy, task) = setup_fake_gpu_driver(move |query_id| match query_id {
                fgpu::QueryId::MagmaQueryTotalTime => {
                    let vmo = create_magma_total_time_query_result_vmo(
                        gpu_time_ns_clone.get(),
                        monotonic_time_ns_clone.get(),
                    )
                    .unwrap();
                    Ok(fgpu::DeviceQueryResponse::BufferResult(vmo))
                }
                _ => panic!("Unexpected query ID {:?} (expected MagmaQueryTotalTime)", query_id),
            });
            tasks.push(task);

            let gpu_drivers = vec![GpuDriver {
                alias: None,
                topological_path: "/dev/fake/gpu".to_string(),
                proxy: gpu_proxy,
            }];

            let cpu_idle_time = Rc::new(Cell::new([0, 0, 0, 0, 0]));
            let cpu_idle_time_clone = cpu_idle_time.clone();
            let (proxy, task) = setup_fake_cpu_stats_service(move || CpuStats {
                actual_num_cpus: 5,
                per_cpu_stats: Some(vec![
                    PerCpuStats {
                        idle_time: Some(cpu_idle_time_clone.get()[0]),
                        ..PerCpuStats::EMPTY
                    },
                    PerCpuStats {
                        idle_time: Some(cpu_idle_time_clone.get()[1]),
                        ..PerCpuStats::EMPTY
                    },
                    PerCpuStats {
                        idle_time: Some(cpu_idle_time_clone.get()[2]),
                        ..PerCpuStats::EMPTY
                    },
                    PerCpuStats {
                        idle_time: Some(cpu_idle_time_clone.get()[3]),
                        ..PerCpuStats::EMPTY
                    },
                    PerCpuStats {
                        idle_time: Some(cpu_idle_time_clone.get()[4]),
                        ..PerCpuStats::EMPTY
                    },
                ]),
            });
            tasks.push(task);

            let (vmo, size) = create_vmo_from_topology_nodes(vec![
                generate_cluster_node(/*performance_class*/ 0),
                generate_processor_node(/*parent_index*/ 0, /*logical_id*/ 0),
                generate_processor_node(/*parent_index*/ 0, /*logical_id*/ 1),
                generate_cluster_node(/*performance_class*/ 1),
                generate_processor_node(/*parent_index*/ 3, /*logical_id*/ 2),
                generate_processor_node(/*parent_index*/ 3, /*logical_id*/ 3),
                generate_processor_node(/*parent_index*/ 3, /*logical_id*/ 4),
            ])
            .unwrap();
            let topology = vmo_to_topology(vmo, size as u32).unwrap();

            // Build the server.
            let builder = ServerBuilder::new_from_json(None)
                .with_temperature_drivers(temperature_drivers)
                .with_power_drivers(power_drivers)
                .with_gpu_drivers(gpu_drivers)
                .with_cpu_stats_driver(CpuStatsDriver { proxy, topology: Some(topology) })
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
                gpu_time_ns,
                monotonic_time_ns,
                cpu_idle_time,
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
        let mut _query = runner.proxy.start_logging(
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

        // Run `server_task` until stalled to create futures for logging temperatures and CpuLoads.
        runner.run_server_task_until_stalled();

        // Check the Inspect nodes for the loggers are created.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        CpuLoadLogger: {
                        },
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

        // Check data is logged to Inspect.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        CpuLoadLogger: {
                            "elapsed time (ms)": 100i64,
                            "Cluster 0": {
                                "Max perf scale": 0.5,
                                "CPU usage (%)": 100.0,
                            },
                            "Cluster 1": {
                                "Max perf scale": 1.0,
                                "CPU usage (%)": 100.0,
                            }
                        },
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

        // Check data is logged to Inspect.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        CpuLoadLogger: {
                            "elapsed time (ms)": 900i64,
                            "Cluster 0": {
                                "Max perf scale": 0.5,
                                "CPU usage (%)": 100.0,
                            },
                            "Cluster 1": {
                                "Max perf scale": 1.0,
                                "CPU usage (%)": 100.0,
                            }
                        },
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
                        CpuLoadLogger: {
                        },
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
                        CpuLoadLogger: {
                        },
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

        assert_eq!(runner.iterate_logging_task(), true);

        // Check `test1` data remaining the same, `test2` data updated.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test1: {
                        CpuLoadLogger: {
                        },
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

        // Check `test2` data remaining the same, `test1` data updated.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test1: {
                        CpuLoadLogger: {
                            "elapsed time (ms)": 300i64,
                            "Cluster 0": {
                                "Max perf scale": 0.5,
                                "CPU usage (%)": 100.0,
                            },
                            "Cluster 1": {
                                "Max perf scale": 1.0,
                                "CPU usage (%)": 100.0,
                            }
                        },
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
                        CpuLoadLogger: {
                            "elapsed time (ms)": 300i64,
                            "Cluster 0": {
                                "Max perf scale": 0.5,
                                "CPU usage (%)": 100.0,
                            },
                            "Cluster 1": {
                                "Max perf scale": 1.0,
                                "CPU usage (%)": 100.0,
                            }
                        },
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
    fn test_logging_cpu_stats() {
        let mut runner = Runner::new();

        runner.cpu_idle_time.set([0, 0, 0, 0, 0]);

        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::CpuLoad(CpuLoad { interval_ms: 100 })].into_iter(),
            350,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Check CpuLoadLogger added before first query.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        CpuLoadLogger: {
                        }
                    }
                }
            }
        );

        runner.cpu_idle_time.set([50_000_000, 0, 0, 0, 0]);
        // Run the first logging task.
        runner.iterate_logging_task();
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        CpuLoadLogger: {
                            "elapsed time (ms)": 100i64,
                            "Cluster 0": {
                                "Max perf scale": 0.5,
                                "CPU usage (%)": 75.0,
                            },
                            "Cluster 1": {
                                "Max perf scale": 1.0,
                                "CPU usage (%)": 100.0,
                            }
                        }
                    }
                }
            }
        );

        // Run the second logging task.
        runner.iterate_logging_task();
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        CpuLoadLogger: {
                            "elapsed time (ms)": 200i64,
                            "Cluster 0": {
                                "Max perf scale": 0.5,
                                "CPU usage (%)": 100.0,
                            },
                            "Cluster 1": {
                                "Max perf scale": 1.0,
                                "CPU usage (%)": 100.0,
                            }
                        }
                    }
                }
            }
        );

        runner.cpu_idle_time.set([50_000_000, 0, 30_000_000, 0, 0]);
        // Run the third logging task.
        runner.iterate_logging_task();
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        CpuLoadLogger: {
                            "elapsed time (ms)": 300i64,
                            "Cluster 0": {
                                "Max perf scale": 0.5,
                                "CPU usage (%)": 100.0,
                            },
                            "Cluster 1": {
                                "Max perf scale": 1.0,
                                "CPU usage (%)": 90.0,
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

    #[test]
    fn test_logging_gpu_usage() {
        let mut runner = Runner::new();

        // Starting logging for 1 second at 100ms intervals. When the query stalls, the logging task
        // will be waiting on its timer.
        let _query = runner.proxy.start_logging(
            "test",
            &mut vec![&mut Metric::GpuUsage(GpuUsage { interval_ms: 100 })].into_iter(),
            1_000,
            false,
            false,
        );
        runner.run_server_task_until_stalled();

        // Check GpuUsageLogger added before first temperature poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        GpuUsageLogger: {
                        }
                    }
                }
            }
        );

        // For the first 9 steps, GPU usage is logged to Insepct.
        for i in 0..5 {
            runner.gpu_time_ns.set((i + 1) * 50);
            runner.monotonic_time_ns.set((i + 1) * 100);
            runner.iterate_logging_task();
            assert_data_tree!(
                runner.inspector,
                root: {
                    MetricsLogger: {
                        test: {
                            GpuUsageLogger: {
                                "elapsed time (ms)": 100 * (1 + i as i64),
                                "/dev/fake/gpu": {
                                    "GPU usage (%)": 50 as f64,
                                }
                            }
                        }
                    }
                }
            );
        }
        for i in 5..9 {
            runner.monotonic_time_ns.set((i + 1) * 100);
            runner.iterate_logging_task();
            assert_data_tree!(
                runner.inspector,
                root: {
                    MetricsLogger: {
                        test: {
                            GpuUsageLogger: {
                                "elapsed time (ms)": 100 * (1 + i as i64),
                                "/dev/fake/gpu": {
                                    "GPU usage (%)": 0 as f64,
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
}
