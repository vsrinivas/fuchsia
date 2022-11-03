// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::driver_utils::{connect_proxy, list_drivers},
    crate::MIN_INTERVAL_FOR_SYSLOG_MS,
    anyhow::{format_err, Result},
    fidl_fuchsia_hardware_network as fhwnet, fidl_fuchsia_metricslogger_test as fmetrics,
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_zircon as zx,
    futures::{stream::FuturesUnordered, StreamExt},
    std::{cell::RefCell, collections::HashMap, rc::Rc},
    tracing::{error, info},
};

// TODO (didis): Use netstack API after fxbug.dev/111228 is implemented.
const NETWORK_SERVICE_DIR: &str = "/dev/class/network";

// TODO (didis): This assumes the wifi device instance is already there. We should not depend on
// implicit ordering as it might cause racing issues if this code is triggered too early.
pub async fn generate_network_devices() -> Result<Vec<fhwnet::DeviceProxy>> {
    let mut proxies = Vec::new();
    for driver in list_drivers(NETWORK_SERVICE_DIR).await {
        let filepath = format!("{}/{}", NETWORK_SERVICE_DIR, driver);
        let device_instance_proxy = connect_proxy::<fhwnet::DeviceInstanceMarker>(&filepath)?;

        // Get client proxy from device instance.
        let (device, device_server_end) =
            fidl::endpoints::create_endpoints::<fhwnet::DeviceMarker>()?;
        device_instance_proxy.get_device(device_server_end)?;

        proxies.push(device.into_proxy()?);
    }
    Ok(proxies)
}

async fn keep_ports_updated(
    devices: Rc<Vec<fhwnet::DeviceProxy>>,
    ports: Rc<RefCell<HashMap<u8, fhwnet::PortProxy>>>,
) {
    let futures = FuturesUnordered::new();

    for device in devices.iter() {
        futures.push(watch_and_update_ports(&device, ports.clone()));
    }
    let _: Vec<_> = futures.collect().await;
}

async fn watch_and_update_ports(
    device: &fhwnet::DeviceProxy,
    ports: Rc<RefCell<HashMap<u8, fhwnet::PortProxy>>>,
) {
    // TODO (fxbug.dev/110111): Return `MetricsLoggerError::INTERNAL` instead of unwrap.
    let (port_watcher, port_watcher_server_end) =
        fidl::endpoints::create_proxy::<fhwnet::PortWatcherMarker>().unwrap();
    device.get_port_watcher(port_watcher_server_end).unwrap();

    match port_watcher.watch().await.unwrap() {
        fhwnet::DevicePortEvent::Removed(port_id) => {
            // Remove port if a port was removed from the device.
            if let Some(_) = ports.borrow_mut().remove(&port_id.base) {
                info!(port_id.base, port_id.salt, "Removed a port");
            }
        }
        // When watcher was created, ports might already exist.
        fhwnet::DevicePortEvent::Added(mut port_id)
        | fhwnet::DevicePortEvent::Existing(mut port_id) => {
            let (port, port_server_end) =
                fidl::endpoints::create_proxy::<fhwnet::PortMarker>().unwrap();
            device.get_port(&mut port_id, port_server_end).unwrap();

            match port.get_info().await {
                Ok(fhwnet::PortInfo { id: _, class: port_class, .. }) => {
                    // Add port if a new port was a Wlan/WlanAp port.
                    if let Some(class) = port_class {
                        if class == fhwnet::DeviceClass::WlanAp
                            || class == fhwnet::DeviceClass::Wlan
                        {
                            if ports.borrow_mut().insert(port_id.base, port).is_none() {
                                info!(port_id.base, port_id.salt, "Added a new/existing port");
                            }
                        }
                    } else {
                        error!("Port with port id {:?} is missing device type", port_id);
                    }
                }
                Err(err) => {
                    error!("Querying port ({:?}) info failed with error: {:?}", port_id, err)
                }
            }
        }
        _ => (),
    };
}

/// Returns:
/// - Err if querying the port returned an error.
/// - Ok(None) if no port is instantiated.
/// - Ok(Some(rx_bytes, tx_bytes, rx_frames, tx_frames))) if successfully queried all instantiated
///   ports. The result is the the sum of queried results from all ports.
async fn query_network_activity(
    ports: Vec<fhwnet::PortProxy>,
) -> Result<Option<(u64, u64, u64, u64)>> {
    if ports.len() == 0 {
        return Ok(None);
    }

    let queries = FuturesUnordered::new();
    for port in ports {
        let query = async move {
            let result = port.get_counters().await;
            result
        };
        queries.push(query);
    }
    let results =
        queries.collect::<Vec<Result<fhwnet::PortGetCountersResponse, fidl::Error>>>().await;

    let mut rx_bytes_sum = 0;
    let mut tx_bytes_sum = 0;
    let mut rx_frames_sum = 0;
    let mut tx_frames_sum = 0;

    for result in results.into_iter() {
        match result {
            Ok(fhwnet::PortGetCountersResponse {
                rx_bytes,
                tx_bytes,
                rx_frames,
                tx_frames,
                ..
            }) => {
                if let Some(value) = rx_bytes {
                    rx_bytes_sum += value;
                } else {
                    return Err(format_err!("Query Port for rx_bytes returned None"));
                }
                if let Some(value) = tx_bytes {
                    tx_bytes_sum += value;
                } else {
                    return Err(format_err!("Query Port for tx_bytes returned None"));
                }
                if let Some(value) = rx_frames {
                    rx_frames_sum += value;
                } else {
                    return Err(format_err!("Query Port for rx_frames returned None"));
                }
                if let Some(value) = tx_frames {
                    tx_frames_sum += value;
                } else {
                    return Err(format_err!("Query Port for tx_frames returned None"));
                }
            }
            Err(err) => {
                return Err(format_err!("Error querying tx/rx data from port: {:?}", err));
            }
        }
    }
    Ok(Some((rx_bytes_sum, tx_bytes_sum, rx_frames_sum, tx_frames_sum)))
}

struct NetworkActivitySample {
    time_stamp: fasync::Time,
    rx_bytes: u64,
    tx_bytes: u64,
    rx_frames: u64,
    tx_frames: u64,
}

pub struct NetworkActivityLoggerBuilder<'a> {
    ports: Option<Rc<RefCell<HashMap<u8, fhwnet::PortProxy>>>>,
    client_inspect: &'a inspect::Node,
    client_id: String,
    devices: Rc<Vec<fhwnet::DeviceProxy>>,
    interval_ms: u32,
    duration_ms: Option<u32>,
    output_samples_to_syslog: bool,
}

impl<'a> NetworkActivityLoggerBuilder<'a> {
    pub fn new(
        devices: Rc<Vec<fhwnet::DeviceProxy>>,
        interval_ms: u32,
        duration_ms: Option<u32>,
        client_inspect: &'a inspect::Node,
        client_id: String,
        output_samples_to_syslog: bool,
    ) -> Self {
        NetworkActivityLoggerBuilder {
            ports: None,
            client_inspect,
            client_id,
            devices,
            interval_ms,
            duration_ms,
            output_samples_to_syslog,
        }
    }

    /// For testing purposes, port proxies may be provided directly to the builder.
    #[cfg(test)]
    fn with_network_ports(mut self, ports: Rc<RefCell<HashMap<u8, fhwnet::PortProxy>>>) -> Self {
        self.ports = Some(ports);
        self
    }

    pub async fn build(self) -> Result<NetworkActivityLogger, fmetrics::MetricsLoggerError> {
        if self.interval_ms == 0
            || self.output_samples_to_syslog && self.interval_ms < MIN_INTERVAL_FOR_SYSLOG_MS
            || self.duration_ms.map_or(false, |d| d <= self.interval_ms)
        {
            return Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval);
        }

        let ports = match self.ports {
            None => {
                if self.devices.len() == 0 {
                    return Err(fmetrics::MetricsLoggerError::NoDrivers);
                }
                let network_ports = Rc::new(RefCell::new(HashMap::new()));
                // Keep a task running in the background to listen to the port events to keep
                // `ports` updated.
                fasync::Task::local(keep_ports_updated(
                    self.devices.clone(),
                    network_ports.clone(),
                ))
                .detach();
                network_ports
            }
            Some(network_ports) => network_ports,
        };

        Ok(NetworkActivityLogger::new(
            ports,
            self.interval_ms,
            self.duration_ms,
            self.client_inspect,
            self.client_id,
            self.output_samples_to_syslog,
        ))
    }
}

pub struct NetworkActivityLogger {
    /// Map from the port ID (u8) to the port proxy. The ID identifies a port instance in hardware,
    /// which doesn't change on different instantiation of the identified port.
    ports: Rc<RefCell<HashMap<u8, fhwnet::PortProxy>>>,

    last_sample: Option<NetworkActivitySample>,
    interval: zx::Duration,
    client_id: String,
    inspect: InspectData,
    output_samples_to_syslog: bool,

    /// Start time for the logger; used to calculate elapsed time.
    /// This is an exclusive start.
    start_time: fasync::Time,

    /// Time at which the logger will stop.
    /// This is an exclusive end.
    end_time: fasync::Time,
}

impl NetworkActivityLogger {
    fn new(
        ports: Rc<RefCell<HashMap<u8, fhwnet::PortProxy>>>,
        interval_ms: u32,
        duration_ms: Option<u32>,
        client_inspect: &inspect::Node,
        client_id: String,
        output_samples_to_syslog: bool,
    ) -> Self {
        let start_time = fasync::Time::now();
        let end_time = duration_ms.map_or(fasync::Time::INFINITE, |ms| {
            fasync::Time::now() + zx::Duration::from_millis(ms as i64)
        });

        let inspect = InspectData::new(client_inspect);

        NetworkActivityLogger {
            ports,
            last_sample: None,
            interval: zx::Duration::from_millis(interval_ms as i64),
            client_id,
            inspect,
            output_samples_to_syslog,
            start_time,
            end_time,
        }
    }

    pub async fn log_network_activities(mut self) {
        let mut interval = fasync::Interval::new(self.interval);
        // Start querying network ports. Logging will start at the next interval.
        self.log_network_activity(fasync::Time::now()).await;

        while let Some(()) = interval.next().await {
            let now = fasync::Time::now();
            if now >= self.end_time {
                break;
            }
            self.log_network_activity(now).await;
        }
    }

    async fn log_network_activity(&mut self, now: fasync::Time) {
        match query_network_activity(
            self.ports.borrow().values().cloned().collect::<Vec<fhwnet::PortProxy>>(),
        )
        .await
        {
            Ok(Some((rx_bytes, tx_bytes, rx_frames, tx_frames))) => {
                if let Some(last_sample) = self.last_sample.take() {
                    // Calculate elapsed time in seconds with millisecond resolution.
                    let elapsed_time = (now - last_sample.time_stamp).into_millis() as f64 / 1000.0;

                    let rx_bytes_per_sec = (rx_bytes - last_sample.rx_bytes) as f64 / elapsed_time;
                    let tx_bytes_per_sec = (tx_bytes - last_sample.tx_bytes) as f64 / elapsed_time;
                    let rx_frames_per_sec =
                        (rx_frames - last_sample.rx_frames) as f64 / elapsed_time;
                    let tx_frames_per_sec =
                        (tx_frames - last_sample.tx_frames) as f64 / elapsed_time;

                    self.inspect.log_network_activity(
                        rx_bytes_per_sec,
                        tx_bytes_per_sec,
                        rx_frames_per_sec,
                        tx_frames_per_sec,
                        (now - self.start_time).into_millis(),
                    );

                    if self.output_samples_to_syslog {
                        info!(
                            rx_bytes_per_sec,
                            tx_bytes_per_sec,
                            rx_frames_per_sec,
                            tx_frames_per_sec,
                            "Network activity"
                        );
                    }

                    fuchsia_trace::counter!(
                        "metrics_logger",
                        "network_activity",
                        0,
                        "client_id" => self.client_id.as_str(),
                        "rx_bytes_per_sec" => rx_bytes_per_sec,
                        "tx_bytes_per_sec" => tx_bytes_per_sec,
                        "rx_frames_per_sec" => rx_frames_per_sec,
                        "tx_frames_per_sec" => tx_frames_per_sec
                    );
                }

                self.last_sample = Some(NetworkActivitySample {
                    time_stamp: now,
                    rx_bytes,
                    tx_bytes,
                    rx_frames,
                    tx_frames,
                });
            }
            Ok(None) => info!("No ports available"),
            // In case of a polling error, the previous sample should not be updated.
            Err(err) => error!("Polling tx/rx byte cnt failed with error: {:?}", err),
        }
    }
}

struct InspectData {
    logger_root: inspect::Node,
    elapsed_millis: Option<inspect::IntProperty>,
    rx_bytes_per_sec: Option<inspect::DoubleProperty>,
    tx_bytes_per_sec: Option<inspect::DoubleProperty>,
    rx_frames_per_sec: Option<inspect::DoubleProperty>,
    tx_frames_per_sec: Option<inspect::DoubleProperty>,
}

impl InspectData {
    fn new(parent: &inspect::Node) -> Self {
        Self {
            logger_root: parent.create_child("NetworkActivityLogger"),
            elapsed_millis: None,
            rx_bytes_per_sec: None,
            tx_bytes_per_sec: None,
            rx_frames_per_sec: None,
            tx_frames_per_sec: None,
        }
    }

    fn init_nodes(&mut self) {
        self.elapsed_millis = Some(self.logger_root.create_int("elapsed time (ms)", std::i64::MIN));
        self.rx_bytes_per_sec = Some(self.logger_root.create_double("rx_bytes_per_sec", f64::MIN));
        self.tx_bytes_per_sec = Some(self.logger_root.create_double("tx_bytes_per_sec", f64::MIN));
        self.rx_frames_per_sec =
            Some(self.logger_root.create_double("rx_frames_per_sec", f64::MIN));
        self.tx_frames_per_sec =
            Some(self.logger_root.create_double("tx_frames_per_sec", f64::MIN));
    }

    fn log_network_activity(
        &mut self,
        rx_bytes_per_sec: f64,
        tx_bytes_per_sec: f64,
        rx_frames_per_sec: f64,
        tx_frames_per_sec: f64,
        elapsed_millis: i64,
    ) {
        if self.elapsed_millis.is_none() {
            self.init_nodes();
        }
        self.elapsed_millis.as_ref().map(|e| e.set(elapsed_millis));
        self.rx_bytes_per_sec.as_ref().map(|r| r.set(rx_bytes_per_sec));
        self.tx_bytes_per_sec.as_ref().map(|r| r.set(tx_bytes_per_sec));
        self.rx_frames_per_sec.as_ref().map(|r| r.set(rx_frames_per_sec));
        self.tx_frames_per_sec.as_ref().map(|r| r.set(tx_frames_per_sec));
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        futures::{task::Poll, FutureExt, TryStreamExt},
        inspect::assert_data_tree,
        std::{cell::Cell, pin::Pin},
    };

    fn setup_fake_network_port(
        mut get_counter: impl FnMut() -> fhwnet::PortGetCountersResponse + 'static,
    ) -> (fhwnet::PortProxy, fasync::Task<()>) {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fhwnet::PortMarker>().unwrap();
        let task = fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fhwnet::PortRequest::GetCounters { responder }) => {
                        let _ = responder.send(get_counter());
                    }
                    _ => assert!(false),
                }
            }
        });

        (proxy, task)
    }

    struct Runner {
        inspector: inspect::Inspector,
        inspect_root: inspect::Node,

        _tasks: Vec<fasync::Task<()>>,

        network_counters: Rc<Cell<[u64; 4]>>,

        ports: Rc<RefCell<HashMap<u8, fhwnet::PortProxy>>>,

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
            let executor = fasync::TestExecutor::new_with_fake_time().unwrap();
            executor.set_fake_time(fasync::Time::from_nanos(0));

            let inspector = inspect::Inspector::new();
            let inspect_root = inspector.root().create_child("MetricsLogger");

            let mut tasks = Vec::new();
            let network_counters = Rc::new(Cell::new([0, 0, 0, 0]));
            let network_counters_clone = network_counters.clone();
            let (proxy, task) = setup_fake_network_port(move || fhwnet::PortGetCountersResponse {
                rx_bytes: Some(network_counters_clone.get()[0]),
                tx_bytes: Some(network_counters_clone.get()[1]),
                rx_frames: Some(network_counters_clone.get()[2]),
                tx_frames: Some(network_counters_clone.get()[3]),
                unknown_data: None,
                ..fhwnet::PortGetCountersResponse::EMPTY
            });
            tasks.push(task);

            let ports = Rc::new(RefCell::new(HashMap::from([(0, proxy)])));

            Self { executor, inspector, inspect_root, network_counters, ports, _tasks: tasks }
        }

        fn iterate_task(&mut self, task: &mut Pin<Box<dyn futures::Future<Output = ()>>>) -> bool {
            let wakeup_time = match self.executor.wake_next_timer() {
                Some(t) => t,
                None => return false,
            };
            self.executor.set_fake_time(wakeup_time);
            let _ = self.executor.run_until_stalled(task);
            true
        }
    }

    #[test]
    fn test_logging_network_activiy_to_inspect() {
        let mut runner = Runner::new();

        let client_id = "test".to_string();
        let client_inspect = runner.inspect_root.create_child(&client_id);
        let poll = runner.executor.run_until_stalled(
            &mut NetworkActivityLoggerBuilder::new(
                Rc::new(vec![]),
                100,
                Some(1_000),
                &client_inspect,
                client_id,
                false,
            )
            .with_network_ports(runner.ports.clone())
            .build()
            .boxed_local(),
        );

        let network_activity_logger = match poll {
            Poll::Ready(Ok(network_activity_logger)) => network_activity_logger,
            _ => panic!("Failed to create NetworkActivityLogger"),
        };

        // Starting logging for 1 second at 100ms intervals. When the query stalls, the logging task
        // will be waiting on its timer.
        let mut task = network_activity_logger.log_network_activities().boxed_local();
        assert_matches!(runner.executor.run_until_stalled(&mut task), Poll::Pending);

        // Check NetworkActivityLogger added before first poll.
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                        NetworkActivityLogger: {
                        }
                    }
                }
            }
        );

        // For the first 9 steps, network activity is logged to Insepct.
        for i in 0..5 {
            runner.network_counters.set([i + 1, (i + 1) * 2, (i + 1) * 3, (i + 1) * 4]);
            runner.iterate_task(&mut task);
            assert_data_tree!(
                runner.inspector,
                root: {
                    MetricsLogger: {
                        test: {
                            NetworkActivityLogger: {
                                "elapsed time (ms)": 100 * (1 + i as i64),
                                "rx_bytes_per_sec": 10.0,
                                "tx_bytes_per_sec": 20.0,
                                "rx_frames_per_sec": 30.0,
                                "tx_frames_per_sec": 40.0,
                            }
                        }
                    }
                }
            );
        }
        for i in 5..9 {
            runner.network_counters.set([5, 10, 15, 20]);
            runner.iterate_task(&mut task);
            assert_data_tree!(
                runner.inspector,
                root: {
                    MetricsLogger: {
                        test: {
                            NetworkActivityLogger: {
                                "elapsed time (ms)": 100 * (1 + i as i64),
                                "rx_bytes_per_sec": 0.0,
                                "tx_bytes_per_sec": 0.0,
                                "rx_frames_per_sec": 0.0,
                                "tx_frames_per_sec": 0.0,
                            }
                        }
                    }
                }
            );
        }

        // With one more time step, the end time has been reached.
        runner.iterate_task(&mut task);
        assert_data_tree!(
            runner.inspector,
            root: {
                MetricsLogger: {
                    test: {
                    }
                }
            }
        );
    }
}
