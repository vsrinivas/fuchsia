// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    fidl_fuchsia_kernel as fkernel, fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
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
    zerocopy::FromBytes,
};

pub const ZBI_TOPOLOGY_NODE_SIZE: usize = mem::size_of::<ZbiTopologyNode>();

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
    cpu_topology: Option<Vec<Cluster>>,
    interval: zx::Duration,
    end_time: fasync::Time,
    last_sample: Option<CpuLoadSample>,
    stats_proxy: Rc<fkernel::StatsProxy>,
    client_id: String,
    output_samples_to_syslog: bool,
}

impl CpuLoadLogger {
    pub fn new(
        cpu_topology: Option<Vec<Cluster>>,
        interval: zx::Duration,
        duration: Option<zx::Duration>,
        stats_proxy: Rc<fkernel::StatsProxy>,
        client_id: String,
        output_samples_to_syslog: bool,
    ) -> Self {
        let end_time = duration.map_or(fasync::Time::INFINITE, |d| fasync::Time::now() + d);
        CpuLoadLogger {
            cpu_topology,
            interval,
            end_time,
            last_sample: None,
            stats_proxy,
            client_id,
            output_samples_to_syslog,
        }
    }

    pub async fn log_cpu_usages(mut self) {
        let mut interval = fasync::Interval::new(self.interval);

        while let Some(()) = interval.next().await {
            let now = fasync::Time::now();
            if now >= self.end_time {
                break;
            }
            self.log_cpu_usage(now).await;
        }
    }

    // TODO (fxbug.dev/92320): Populate CPU Usage info into Inspect.
    async fn log_cpu_usage(&mut self, now: fasync::Time) {
        let mut hasher = DefaultHasher::new();
        self.client_id.hash(&mut hasher);
        let trace_counter_id = hasher.finish();

        match self.stats_proxy.get_cpu_stats().await {
            Ok(cpu_stats) => {
                let cpu_num = cpu_stats.actual_num_cpus;
                let current_sample = CpuLoadSample { time_stamp: now, cpu_stats };

                if let Some(last_sample) = self.last_sample.take() {
                    if let Some(cpu_topology) = self.cpu_topology.as_ref() {
                        for cluster in cpu_topology {
                            let cpu_usage = calculate_cpu_usage(
                                cluster.cpu_indexes.clone(),
                                &last_sample,
                                &current_sample,
                            );
                            if self.output_samples_to_syslog {
                                fx_log_info!(
                                    "Max perf scale: {:?} CpuUsage: {:?}",
                                    cluster.max_perf_scale,
                                    cpu_usage
                                );
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

                        if self.output_samples_to_syslog {
                            fx_log_info!("CpuUsage: {:?}", cpu_usage);
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
            Err(e) => fx_log_err!("get_cpu_stats IPC failed: {}", e),
        }
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
