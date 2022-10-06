// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::MIN_INTERVAL_FOR_SYSLOG_MS,
    anyhow::{format_err, Result},
    fidl_fuchsia_boot as fboot, fidl_fuchsia_kernel as fkernel,
    fidl_fuchsia_metricslogger_test as fmetrics, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_zbi_abi::ZbiType,
    fuchsia_zbi_abi::{ZbiTopologyEntityType, ZbiTopologyNode},
    fuchsia_zircon as zx,
    futures::stream::StreamExt,
    num_traits::FromPrimitive,
    std::{
        cmp::max,
        collections::{hash_map::DefaultHasher, BTreeMap},
        hash::{Hash, Hasher},
        iter::FromIterator,
        mem,
        rc::Rc,
    },
    tracing::{error, info},
    zerocopy::FromBytes,
};

pub const ZBI_TOPOLOGY_NODE_SIZE: usize = mem::size_of::<ZbiTopologyNode>();

pub async fn generate_cpu_stats_driver() -> Result<CpuStatsDriver> {
    let items_proxy = connect_to_protocol::<fboot::ItemsMarker>()?;
    let proxy = connect_to_protocol::<fkernel::StatsMarker>()?;

    match items_proxy.get(ZbiType::CpuTopology as u32, ZBI_TOPOLOGY_NODE_SIZE as u32).await {
        Ok((Some(vmo), length)) => match vmo_to_topology(vmo, length) {
            Ok(topology) => Ok(CpuStatsDriver { proxy, topology: Some(topology) }),
            Err(err) => Err(format_err!("Parsing VMO failed with error {}", err)),
        },
        Ok((None, _)) => {
            info!("Query Zbi with ZbiType::CpuTopology returned None");
            Ok(CpuStatsDriver { proxy, topology: None })
        }
        Err(err) => Err(format_err!("ItemsProxy IPC failed with error {}", err)),
    }
}

pub struct CpuStatsDriver {
    /// CPU topology represented by a list of Cluster. None if it's not available.
    pub topology: Option<Vec<Cluster>>,
    pub proxy: fkernel::StatsProxy,
}

#[derive(Clone, PartialEq, Debug)]
pub struct Cluster {
    pub max_perf_scale: f64,
    pub cpu_indexes: Vec<u16>,
}

// TODO (fxbug.dev/102987): Support CPU stats logging by performance groups for X86.
pub fn vmo_to_topology(vmo: zx::Vmo, length: u32) -> Result<Vec<Cluster>> {
    let mut max_performance_class = 0;
    let mut clusters_to_cpus = BTreeMap::new();
    let mut performance_classes = Vec::new();
    let mut buffer: [u8; ZBI_TOPOLOGY_NODE_SIZE] = [0; ZBI_TOPOLOGY_NODE_SIZE];
    for index in 0..length / ZBI_TOPOLOGY_NODE_SIZE as u32 {
        // In case of a reading error, break out of loop and return as CPU topology isn't available.
        vmo.read(&mut buffer, (index * ZBI_TOPOLOGY_NODE_SIZE as u32) as u64).map_err(|e| {
            format_err!(
                "Failed to read VMO (offset {:?}) into buffer with err: {:?}",
                index * ZBI_TOPOLOGY_NODE_SIZE as u32,
                e
            )
        })?;
        let node = ZbiTopologyNode::read_from(&buffer as &[u8]).ok_or(format_err!(
            "Reads a copy of ZbiTopologyNode (index {:?}) from VMO bytes failed.",
            index
        ))?;
        match ZbiTopologyEntityType::from_u8(node.entity_type) {
            Some(ZbiTopologyEntityType::ZbiTopologyEntityCluster) => {
                let performance_class: u8 = unsafe { node.entity.cluster.performance_class };
                max_performance_class = max(max_performance_class, performance_class);
                clusters_to_cpus.insert(index as u16, Vec::new());
                performance_classes.push(performance_class);
            }
            Some(ZbiTopologyEntityType::ZbiTopologyEntityProcessor) => {
                let logical_id = unsafe { node.entity.processor.logical_ids[0] };
                clusters_to_cpus.get_mut(&node.parent_index).map(|c| c.push(logical_id));
            }
            _ => (),
        };
    }
    Ok(clusters_to_cpus
        .into_iter()
        .enumerate()
        .map(|(i, (_, cpu_indexes))| Cluster {
            max_perf_scale: (performance_classes[i] as f64 + 1.0)
                / (max_performance_class as f64 + 1.0),
            cpu_indexes,
        })
        .collect())
}

struct CpuLoadSample {
    time_stamp: fasync::Time,
    cpu_stats: fkernel::CpuStats,
}

fn calculate_cpu_usage(
    cpu_indexes: Vec<u16>,
    last_sample: &CpuLoadSample,
    current_sample: &CpuLoadSample,
) -> f64 {
    let elapsed = current_sample.time_stamp - last_sample.time_stamp;
    let num_cpus = cpu_indexes.len() as f64;
    let mut cpu_percentage_sum: f64 = 0.0;
    for cpu_index in cpu_indexes {
        // TODO (fxbug.dev/110111): Return `MetricsLoggerError::INTERNAL` instead of unwrap.
        let current_per_cpu_stats =
            &current_sample.cpu_stats.per_cpu_stats.as_ref().unwrap()[cpu_index as usize];
        let last_per_cpu_stats =
            &last_sample.cpu_stats.per_cpu_stats.as_ref().unwrap()[cpu_index as usize];
        let delta_idle_time = zx::Duration::from_nanos(
            current_per_cpu_stats.idle_time.unwrap() - last_per_cpu_stats.idle_time.unwrap(),
        );
        let busy_time = elapsed - delta_idle_time;
        cpu_percentage_sum += 100.0 * busy_time.into_nanos() as f64 / elapsed.into_nanos() as f64;
    }

    cpu_percentage_sum / num_cpus
}

pub struct CpuLoadLogger {
    interval: zx::Duration,
    last_sample: Option<CpuLoadSample>,
    cpu_stats_driver: Rc<CpuStatsDriver>,
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

impl CpuLoadLogger {
    pub async fn new(
        cpu_stats_driver: Rc<CpuStatsDriver>,
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

        let start_time = fasync::Time::now();
        let end_time = duration_ms.map_or(fasync::Time::INFINITE, |ms| {
            fasync::Time::now() + zx::Duration::from_millis(ms as i64)
        });
        let inspect = InspectData::new(client_inspect, cpu_stats_driver.topology.clone());

        Ok(CpuLoadLogger {
            cpu_stats_driver,
            interval: zx::Duration::from_millis(interval_ms as i64),
            last_sample: None,
            client_id,
            inspect,
            output_samples_to_syslog,
            start_time,
            end_time,
        })
    }

    pub async fn log_cpu_usages(mut self) {
        let mut interval = fasync::Interval::new(self.interval);
        // Start polling stats proxy. Logging will start at the next interval.
        self.log_cpu_usage(fasync::Time::now()).await;

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

        match self.cpu_stats_driver.proxy.get_cpu_stats().await {
            Ok(cpu_stats) => {
                let cpu_num = cpu_stats.actual_num_cpus;
                let current_sample = CpuLoadSample { time_stamp: now, cpu_stats };

                if let Some(last_sample) = self.last_sample.take() {
                    if let Some(cpu_topology) = self.cpu_stats_driver.topology.as_ref() {
                        for (index, cluster) in cpu_topology.iter().enumerate() {
                            let cpu_usage = calculate_cpu_usage(
                                cluster.cpu_indexes.clone(),
                                &last_sample,
                                &current_sample,
                            );

                            self.inspect.log_cpu_load_by_cluster(
                                index,
                                cpu_usage,
                                (current_sample.time_stamp - self.start_time).into_millis(),
                            );

                            if self.output_samples_to_syslog {
                                info!(max_perf_scale = cluster.max_perf_scale, cpu_usage);
                            }

                            fuchsia_trace::counter!(
                                "metrics_logger",
                                "cpu_usage",
                                trace_counter_id,
                                "client_id" => self.client_id.as_str(),
                                "max_perf_scale" => cluster.max_perf_scale,
                                "cpu_usage" => cpu_usage
                            );
                        }
                        // TODO (fxbug.dev/100797): Remove system_metrics_logger category after the
                        // e2e test is transitioned.
                        fuchsia_trace::counter!(
                            "system_metrics_logger",
                            "cpu_usage",
                            0,
                            "cpu_usage" => calculate_cpu_usage(
                                Vec::from_iter(0..cpu_num as u16), &last_sample, &current_sample)
                        );
                    } else {
                        let cpu_usage = calculate_cpu_usage(
                            Vec::from_iter(0..cpu_num as u16),
                            &last_sample,
                            &current_sample,
                        );

                        self.inspect.log_total_cpu_load(
                            cpu_usage,
                            (current_sample.time_stamp - self.start_time).into_millis(),
                        );

                        if self.output_samples_to_syslog {
                            info!(cpu_usage);
                        }

                        fuchsia_trace::counter!(
                            "metrics_logger",
                            "cpu_usage",
                            trace_counter_id,
                            "client_id" => self.client_id.as_str(),
                            "cpu_usage" => cpu_usage
                        );
                    }
                }

                self.last_sample.replace(current_sample);
            }
            Err(err) => error!(%err, "get_cpu_stats IPC failed"),
        }
    }
}

struct InspectData {
    logger_root: inspect::Node,
    cpu_topology: Option<Vec<Cluster>>,
    elapsed_millis: Option<inspect::IntProperty>,

    // List of nodes for tracking CPU load by cluster if topology exists.
    cluster_nodes: Vec<inspect::Node>,
    cluster_loads: Vec<inspect::DoubleProperty>,

    // Node for tracking total CPU load if topology doesn't exist.
    total_load: Option<inspect::DoubleProperty>,
}

impl InspectData {
    fn new(parent: &inspect::Node, cpu_topology: Option<Vec<Cluster>>) -> Self {
        Self {
            logger_root: parent.create_child("CpuLoadLogger"),
            cpu_topology,
            elapsed_millis: None,
            cluster_nodes: Vec::new(),
            cluster_loads: Vec::new(),
            total_load: None,
        }
    }

    fn init_elapsed_time(&mut self) {
        self.elapsed_millis = Some(self.logger_root.create_int("elapsed time (ms)", std::i64::MIN));
    }

    fn init_total_load(&mut self) {
        self.total_load = Some(self.logger_root.create_double("CPU usage (%)", f64::MIN));
    }

    fn init_cluster_nodes(&mut self) {
        if let Some(cpu_topology) = self.cpu_topology.as_ref() {
            for (index, cluster) in cpu_topology.iter().enumerate() {
                let cluster_node = self.logger_root.create_child(format!("Cluster {}", index));
                cluster_node.record_double("Max perf scale", cluster.max_perf_scale);
                self.cluster_loads.push(cluster_node.create_double("CPU usage (%)", f64::MIN));

                self.cluster_nodes.push(cluster_node);
            }
        }
    }

    fn log_total_cpu_load(&mut self, value: f64, elapsed_millis: i64) {
        if self.total_load.is_none() {
            self.init_total_load();
            self.init_elapsed_time();
        }
        self.elapsed_millis.as_ref().map(|e| e.set(elapsed_millis));
        self.total_load.as_ref().map(|l| l.set(value));
    }

    fn log_cpu_load_by_cluster(&mut self, index: usize, value: f64, elapsed_millis: i64) {
        if self.cluster_nodes.is_empty() {
            self.init_cluster_nodes();
            self.init_elapsed_time();
        }
        self.elapsed_millis.as_ref().map(|e| e.set(elapsed_millis));
        self.cluster_loads[index].set(value);
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        anyhow::{format_err, Error},
        fuchsia_zbi_abi::{
            ArchitectureInfo, Entity, ZbiTopologyArchitecture, ZbiTopologyArmInfo,
            ZbiTopologyCluster, ZbiTopologyEntityType, ZbiTopologyNode, ZbiTopologyProcessor,
        },
    };

    pub fn generate_cluster_node(performance_class: u8) -> ZbiTopologyNode {
        const ZBI_TOPOLOGY_NO_PARENT: u16 = 0xFFFF;
        ZbiTopologyNode {
            entity_type: ZbiTopologyEntityType::ZbiTopologyEntityCluster as u8,
            parent_index: ZBI_TOPOLOGY_NO_PARENT,
            entity: Entity { cluster: ZbiTopologyCluster { performance_class } },
        }
    }

    pub fn generate_processor_node(parent_index: u16, logical_id: u16) -> ZbiTopologyNode {
        ZbiTopologyNode {
            entity_type: ZbiTopologyEntityType::ZbiTopologyEntityProcessor as u8,
            parent_index: parent_index,
            entity: Entity {
                processor: ZbiTopologyProcessor {
                    logical_ids: [logical_id, 0, 0, 0],
                    logical_id_count: 1,
                    flags: 0,
                    architecture: ZbiTopologyArchitecture::ZbiTopologyArchArm as u8,
                    architecture_info: ArchitectureInfo {
                        arm: ZbiTopologyArmInfo {
                            cluster_1_id: 0,
                            cluster_2_id: 0,
                            cluster_3_id: 0,
                            cpu_id: 0,
                            gic_id: 0,
                        },
                    },
                },
            },
        }
    }

    // Write ZbiTopologyNode into a VMO buffer.
    pub fn create_vmo_from_topology_nodes(
        nodes: Vec<ZbiTopologyNode>,
    ) -> Result<(zx::Vmo, u64), Error> {
        let vmo_size = (ZBI_TOPOLOGY_NODE_SIZE * nodes.len()) as u64;
        let vmo = zx::Vmo::create(vmo_size).unwrap();
        for (i, node) in nodes.into_iter().enumerate() {
            let bytes = unsafe {
                std::mem::transmute::<ZbiTopologyNode, [u8; ZBI_TOPOLOGY_NODE_SIZE]>(node)
            };
            vmo.write(&bytes, (i * ZBI_TOPOLOGY_NODE_SIZE) as u64)
                .map_err(|e| format_err!("Failed to write data to vmo: {}", e))?;
        }
        Ok((vmo, vmo_size))
    }

    #[test]
    fn test_vmo_to_topology() {
        let (vmo, size) = create_vmo_from_topology_nodes(vec![
            generate_cluster_node(/*performance_class*/ 0),
            generate_processor_node(/*parent_index*/ 0, /*logical_id*/ 0),
            generate_processor_node(/*parent_index*/ 0, /*logical_id*/ 1),
            generate_processor_node(/*parent_index*/ 0, /*logical_id*/ 2),
            generate_processor_node(/*parent_index*/ 0, /*logical_id*/ 3),
        ])
        .unwrap();
        let cpu_topology = vmo_to_topology(vmo, size as u32).unwrap();
        assert_eq!(
            cpu_topology,
            vec![Cluster { max_perf_scale: 1.0, cpu_indexes: vec![0, 1, 2, 3] }]
        );

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
        let cpu_topology = vmo_to_topology(vmo, size as u32).unwrap();
        assert_eq!(
            cpu_topology,
            vec![
                Cluster { max_perf_scale: 0.5, cpu_indexes: vec![0, 1] },
                Cluster { max_perf_scale: 1.0, cpu_indexes: vec![2, 3, 4] }
            ]
        );
    }
}
