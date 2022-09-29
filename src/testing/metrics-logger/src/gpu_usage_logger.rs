// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::driver_utils::{connect_proxy, map_topo_paths_to_class_paths, Driver},
    crate::MIN_INTERVAL_FOR_SYSLOG_MS,
    anyhow::{format_err, Error, Result},
    fidl_fuchsia_gpu_magma as fgpu, fidl_fuchsia_metricslogger_test as fmetrics,
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_zircon as zx,
    futures::{stream::FuturesUnordered, StreamExt},
    magma::magma_total_time_query_result,
    std::{collections::HashMap, mem, rc::Rc},
    tracing::{error, info},
    zerocopy::FromBytes,
};

const GPU_SERVICE_DIRS: [&str; 1] = ["/dev/class/gpu"];

// Type aliases for convenience.
pub type GpuDriver = Driver<fgpu::DeviceProxy>;

const MAGMA_TOTAL_TIME_QUERY_RESULT_SIZE: usize = mem::size_of::<magma_total_time_query_result>();

/// Generates a list of `GpuDriver` from driver paths and aliases.
pub async fn generate_gpu_drivers(
    driver_aliases: HashMap<String, String>,
) -> Result<Vec<GpuDriver>> {
    let topo_to_class = map_topo_paths_to_class_paths(&GPU_SERVICE_DIRS).await?;

    let mut drivers = Vec::new();
    for (topological_path, class_path) in topo_to_class {
        let proxy = connect_proxy::<fgpu::DeviceMarker>(&class_path)?;
        let alias = driver_aliases.get(&topological_path).map(|c| c.to_string());
        // Add driver if querying `MagmaQueryTotalTime` is supported.
        if is_total_time_supported(&proxy).await? {
            drivers.push(Driver { alias, topological_path, proxy });
        } else {
            info!("GPU driver {:?}: `MagmaQueryTotalTime` is not supported", alias);
        }
    }
    Ok(drivers)
}

async fn is_total_time_supported(proxy: &fgpu::DeviceProxy) -> Result<bool, Error> {
    match proxy.query(fgpu::QueryId::IsTotalTimeSupported).await {
        Ok(result) => match result {
            Ok(response) => match response {
                fgpu::DeviceQueryResponse::SimpleResult(simple_result) => Ok(simple_result != 0),
                fgpu::DeviceQueryResponse::BufferResult(_) => {
                    error!(
                        "Query fuchsia_gpu_magma (IsTotalTimeSupported) returned a buffer result"
                    );
                    Ok(false)
                }
            },
            Err(err) => Err(format_err!("Query fuchsia_gpu_magma returned an error: {:?}", err)),
        },
        Err(err) => Err(format_err!("Query fuchsia_gpu_magma IPC failed: {:?}", err)),
    }
}

fn vmo_to_magma_total_time_query_result(vmo: zx::Vmo) -> Result<magma_total_time_query_result> {
    let mut buffer: [u8; MAGMA_TOTAL_TIME_QUERY_RESULT_SIZE] =
        [0; MAGMA_TOTAL_TIME_QUERY_RESULT_SIZE];
    vmo.read(&mut buffer, 0)
        .map_err(|e| format_err!("Failed to read VMO into buffer with err: {:?}", e))?;
    let result = magma_total_time_query_result::read_from(&buffer as &[u8]).ok_or(format_err!(
        "Reads a copy of magma_total_time_query_result from VMO bytes failed."
    ))?;
    Ok(result)
}

async fn query_total_time(
    proxy: &fgpu::DeviceProxy,
) -> Result<magma_total_time_query_result, Error> {
    match proxy.query(fgpu::QueryId::MagmaQueryTotalTime).await {
        Ok(result) => match result {
            Ok(response) => match response {
                fgpu::DeviceQueryResponse::BufferResult(buffer_result) => {
                    vmo_to_magma_total_time_query_result(buffer_result)
                }
                fgpu::DeviceQueryResponse::SimpleResult(simple_result) => Err(format_err!(
                    "Query fuchsia_gpu_magma (MagmaQueryTotalTime) returned a single result: {:?}",
                    simple_result
                )),
            },
            Err(err) => Err(format_err!("Query fuchsia_gpu_magma returned an error: {:?}", err)),
        },
        Err(err) => Err(format_err!("Query fuchsia_gpu_magma IPC failed: {:?}", err)),
    }
}

pub struct GpuUsageLogger {
    drivers: Rc<Vec<GpuDriver>>,
    interval: zx::Duration,
    last_samples: Vec<Option<magma_total_time_query_result>>,
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

impl GpuUsageLogger {
    pub async fn new(
        drivers: Rc<Vec<GpuDriver>>,
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
        if drivers.len() == 0 {
            return Err(fmetrics::MetricsLoggerError::NoDrivers);
        }

        let driver_names: Vec<String> = drivers.iter().map(|c| c.name().to_string()).collect();
        let start_time = fasync::Time::now();
        let end_time = duration_ms.map_or(fasync::Time::INFINITE, |ms| {
            fasync::Time::now() + zx::Duration::from_millis(ms as i64)
        });
        let inspect = InspectData::new(client_inspect, driver_names);

        Ok(GpuUsageLogger {
            last_samples: vec![None; drivers.len()],
            drivers,
            interval: zx::Duration::from_millis(interval_ms as i64),
            client_id,
            inspect,
            output_samples_to_syslog,
            start_time,
            end_time,
        })
    }

    pub async fn log_gpu_usages(mut self) {
        let mut interval = fasync::Interval::new(self.interval);
        // Start polling gpu proxy. Logging will start at the next interval.
        self.log_gpu_usage(fasync::Time::now()).await;

        while let Some(()) = interval.next().await {
            let now = fasync::Time::now();
            if now >= self.end_time {
                break;
            }
            self.log_gpu_usage(now).await;
        }
    }

    async fn log_gpu_usage(&mut self, now: fasync::Time) {
        // Execute a query to each driver.
        let queries = FuturesUnordered::new();
        let mut driver_names = Vec::new();

        for (index, driver) in self.drivers.iter().enumerate() {
            let topological_path = &driver.topological_path;
            let driver_name = driver.alias.as_ref().map_or(topological_path.to_string(), |alias| {
                format!("{:?}({:?})", alias, topological_path)
            });
            driver_names.push(driver_name);

            let query = async move {
                let result = query_total_time(&driver.proxy).await;
                (index, result)
            };
            queries.push(query);
        }
        let results =
            queries.collect::<Vec<(usize, Result<magma_total_time_query_result, Error>)>>().await;

        let mut current_samples = Vec::new();
        let mut trace_args = Vec::new();

        for (index, result) in results.into_iter() {
            let mut current_sample = None;
            match result {
                Ok(value) => {
                    if let Some(Some(last_sample)) = self.last_samples.get(index) {
                        let gpu_usage = 100.0
                            * (value.gpu_time_ns - last_sample.gpu_time_ns) as f64
                            / (value.monotonic_time_ns - last_sample.monotonic_time_ns) as f64;

                        self.inspect.log_gpu_usage(
                            index,
                            gpu_usage,
                            (now - self.start_time).into_millis(),
                        );

                        if self.output_samples_to_syslog {
                            info!(name = driver_names[index].as_str(), gpu_usage);
                        }

                        trace_args.push(fuchsia_trace::ArgValue::of(
                            &driver_names[index],
                            gpu_usage as f64,
                        ));
                    }
                    current_sample = Some(value);
                }
                Err(err) => error!(
                    ?err,
                    path = self.drivers[index].topological_path.as_str(),
                    "Error reading GPU stats",
                ),
            }
            current_samples.push(current_sample);
        }

        self.last_samples = current_samples;

        trace_args.push(fuchsia_trace::ArgValue::of("client_id", self.client_id.as_str()));
        fuchsia_trace::counter(
            fuchsia_trace::cstr!("metrics_logger"),
            fuchsia_trace::cstr!("gpu"),
            0,
            &trace_args,
        );
    }
}

struct InspectData {
    logger_root: inspect::Node,
    elapsed_millis: Option<inspect::IntProperty>,
    driver_nodes: Vec<inspect::Node>,
    data: Vec<inspect::DoubleProperty>,
    driver_names: Vec<String>,
}

impl InspectData {
    fn new(parent: &inspect::Node, driver_names: Vec<String>) -> Self {
        Self {
            logger_root: parent.create_child("GpuUsageLogger"),
            elapsed_millis: None,
            driver_nodes: Vec::new(),
            data: Vec::new(),
            driver_names,
        }
    }

    fn init_nodes(&mut self) {
        self.elapsed_millis = Some(self.logger_root.create_int("elapsed time (ms)", std::i64::MIN));
        self.driver_nodes =
            self.driver_names.iter().map(|name| self.logger_root.create_child(name)).collect();
        for node in self.driver_nodes.iter() {
            self.data.push(node.create_double("GPU usage (%)", f64::MIN));
        }
    }

    fn log_gpu_usage(&mut self, index: usize, value: f64, elapsed_millis: i64) {
        if self.data.is_empty() {
            self.init_nodes();
        }
        self.elapsed_millis.as_ref().map(|e| e.set(elapsed_millis));
        self.data[index].set(value);
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    // Write magma_total_time_query_result into a VMO buffer.
    pub fn create_magma_total_time_query_result_vmo(
        gpu_time_ns: u64,
        monotonic_time_ns: u64,
    ) -> Result<zx::Vmo, Error> {
        let result = magma_total_time_query_result { gpu_time_ns, monotonic_time_ns };
        let vmo_size = MAGMA_TOTAL_TIME_QUERY_RESULT_SIZE as u64;
        let vmo = zx::Vmo::create(vmo_size).unwrap();
        let bytes = unsafe {
            std::mem::transmute::<
                magma_total_time_query_result,
                [u8; MAGMA_TOTAL_TIME_QUERY_RESULT_SIZE],
            >(result)
        };
        vmo.write(&bytes, 0).map_err(|e| format_err!("Failed to write data to vmo: {}", e))?;
        Ok(vmo)
    }

    #[test]
    fn test_vmo_to_magma_total_time_query_result() {
        let vmo = create_magma_total_time_query_result_vmo(1221, 11333).unwrap();
        let result = vmo_to_magma_total_time_query_result(vmo).unwrap();
        assert_eq!(result.gpu_time_ns, 1221);
        assert_eq!(result.monotonic_time_ns, 11333);
    }
}
