// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::driver_utils::{connect_proxy, list_drivers},
    crate::MIN_INTERVAL_FOR_SYSLOG_MS,
    anyhow::{format_err, Result},
    fidl::endpoints::Proxy,
    fidl_fuchsia_device as fdev, fidl_fuchsia_hardware_network as fhwnet,
    fidl_fuchsia_metricslogger_test as fmetrics, fuchsia_async as fasync,
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
        let controller = connect_proxy::<fdev::ControllerMarker>(&filepath)?;

        // The same channel is expected to implement `fhwnet::DeviceInstanceMarker`.
        let channel = controller
            .into_channel()
            .map_err(|_| format_err!("failed to get controller's channel"))?
            .into_zx_channel();
        let device_instance =
            fidl::endpoints::ClientEnd::<fhwnet::DeviceInstanceMarker>::new(channel)
                .into_proxy()?;

        // Get device proxy from device instance.
        let (device, device_server_end) =
            fidl::endpoints::create_endpoints::<fhwnet::DeviceMarker>()?;
        device_instance.get_device(device_server_end)?;

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

struct NetworkActivitySample {
    time_stamp: fasync::Time,
    rx_bytes: u64,
    tx_bytes: u64,
    rx_frames: u64,
    tx_frames: u64,
}

impl NetworkActivityLogger {
    pub async fn new(
        devices: Rc<Vec<fhwnet::DeviceProxy>>,
        interval_ms: u32,
        duration_ms: Option<u32>,
        client_inspect: &inspect::Node,
        client_id: String,
        output_samples_to_syslog: bool,
    ) -> Result<Self, fmetrics::MetricsLoggerError> {
        if interval_ms == 0
            || output_samples_to_syslog && interval_ms < MIN_INTERVAL_FOR_SYSLOG_MS
            || duration_ms.map_or(false, |d| d <= interval_ms)
        {
            return Err(fmetrics::MetricsLoggerError::InvalidSamplingInterval);
        }
        if devices.len() == 0 {
            return Err(fmetrics::MetricsLoggerError::NoDrivers);
        }

        // Keep a task running in the background to listen to the port events to keep `ports`
        // updated.
        let ports = Rc::new(RefCell::new(HashMap::new()));
        fasync::Task::local(keep_ports_updated(devices.clone(), ports.clone())).detach();

        let start_time = fasync::Time::now();
        let end_time = duration_ms.map_or(fasync::Time::INFINITE, |ms| {
            fasync::Time::now() + zx::Duration::from_millis(ms as i64)
        });

        let inspect = InspectData::new(client_inspect);

        Ok(NetworkActivityLogger {
            ports,
            last_sample: None,
            interval: zx::Duration::from_millis(interval_ms as i64),
            client_id,
            inspect,
            output_samples_to_syslog,
            start_time,
            end_time,
        })
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
