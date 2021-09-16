// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::message::{Message, MessageResult, MessageReturn};
use crate::node::Node;
use crate::types::{CpuPerformanceInfo, CpuPerformanceScale, NormPerfs, PState, Watts};
use anyhow::{bail, format_err, Error};
use async_trait::async_trait;
use fuchsia_inspect::{self as inspect, ArrayProperty as _, Property as _};
use serde_derive::Deserialize;
use serde_json as json;
use std::cell::Cell;
use std::collections::HashMap;
use std::convert::TryInto as _;
use std::fmt::Debug;
use std::rc::Rc;

/// Node: CpuManager
///
/// Summary: Provides high-level management of all CPU domains in the system, coordinating both
///          driver- and kernel-level activity. Currently only administers CPU throttling, but in
///          the longer term is meant to become a standalone component that provides a FIDL
///          interface for managing DVFS.
///
/// Handles Messages:
///     - SetMaxPowerConsumption
///
/// Sends Messages:
///     - GetCpuLoads
///     - GetCpuPerformanceStates
///     - GetPerformanceState
///     - SetPerformanceState
///     - GetSetCpuPerformanceInfo
///
/// FIDL dependencies: No direct dependencies

#[derive(Clone, Copy, Debug)]
struct Range<T: Clone + Copy + Debug> {
    upper: T,
    lower: T,
}

/// Describes a value that, if not known exactly, can be confined to a range.
#[derive(Clone, Copy, Debug)]
enum RangedValue<T: Clone + Copy + Debug> {
    Known(T),
    InRange(Range<T>),
}

// The `lower()` and `upper()` methods allow a Known value to be treated as a singleton range
// without adding extra complexity at the call site.
impl<T: Clone + Copy + Debug> RangedValue<T> {
    fn lower(&self) -> T {
        match self {
            &Self::Known(value) => value,
            &Self::InRange(range) => range.lower,
        }
    }

    fn upper(&self) -> T {
        match self {
            &Self::Known(value) => value,
            &Self::InRange(range) => range.upper,
        }
    }
}

/// Runtime representation of a CPU cluster.
struct CpuCluster {
    /// Name of the cluster, for logging purposes only.
    name: String,

    /// This cluster's index in CpuManager's ordering of all clusters.
    cluster_index: usize,

    /// Handler that manages the corresponding CPU driver. Must respond to:
    ///  - GetPerformanceState
    ///  - SetPerformanceState
    ///  - GetCpuPerformanceStates
    handler: Rc<dyn Node>,

    /// Logical CPU numbers of the CPUs in this cluster.
    logical_cpu_numbers: Vec<u32>,

    /// Normalized performance of this cluster per GHz of CPU speed.
    performance_per_ghz: NormPerfs,

    /// All P-states supported by this cluster. The handler guarantees that they are sorted
    /// primairly by frequency and secondarily by voltage.
    pstates: Vec<PState>,

    /// Index of this cluster's current P-state. If an update to the P-state fails, we will assume
    /// that it is between the previous and desired states (inclusive), so that pessimistic guesses
    /// of the P-state may be used accordingly.
    // TODO(fxbug.dev/84685): Look into richer specification of failure modes in the CPU device
    // protocols.
    current_pstate: Cell<RangedValue<usize>>,
}

impl CpuCluster {
    /// Given fractional loads for all system CPUs, gives this cluster's corresponding load and its
    /// estimated normalized performance. If the current P-state is unknown, the highest-possible
    /// frequency will be used to ensure that performance (and thus contribution to thermals) is
    /// not underestimated.
    fn process_fractional_loads(&self, all_cpu_loads: &Vec<f32>) -> (f32, NormPerfs) {
        let cluster_load: f32 =
            self.logical_cpu_numbers.iter().map(|i| all_cpu_loads[*i as usize]).sum();

        // P-states are sorted with frequency as primary key, so the lowest-possible index has the
        // highest-possible frequency.
        let pstate_index = self.current_pstate.get().lower();
        let frequency = self.pstates[pstate_index].frequency;

        let performance =
            self.performance_per_ghz.mul_scalar(frequency.0 / 1e9 * cluster_load as f64);
        (cluster_load, performance)
    }

    /// Gets the performance capacity of the indicated P-state.
    fn get_performance_capacity(&self, pstate_index: usize) -> NormPerfs {
        let pstate = &self.pstates[pstate_index];
        let num_cores = self.logical_cpu_numbers.len() as f64;
        self.performance_per_ghz.mul_scalar(num_cores * pstate.frequency.0 / 1e9)
    }

    // Updates the kernel's CPU performance info to match the provided P-state.
    async fn update_kernel_performance_info(
        &self,
        syscall_handler: &Rc<dyn Node>,
        target_pstate: &PState,
    ) -> Result<(), PowerManagerError> {
        let performance_scale: CpuPerformanceScale =
            self.performance_per_ghz.mul_scalar(target_pstate.frequency.0 / 1e9).try_into()?;

        let performance_info = self
            .logical_cpu_numbers
            .iter()
            .map(|n| CpuPerformanceInfo { logical_cpu_number: *n, performance_scale })
            .collect::<Vec<_>>();

        let msg = Message::GetSetCpuPerformanceInfo(performance_info);
        match syscall_handler.handle_message(&msg).await {
            Ok(MessageReturn::GetSetCpuPerformanceInfo) => Ok(()),
            Ok(other) => panic!("Unexpected GetSetCpuPerformanceInfo result: {:?}", other),
            Err(e) => Err(e),
        }
    }

    // Carries out a P-state change for this cluster.
    async fn update_pstate(
        &self,
        syscall_handler: &Rc<dyn Node>,
        index: usize,
    ) -> Result<(), PowerManagerError> {
        // If the current P-state is known and equal to the new one, no update is needed.
        if let RangedValue::Known(current) = self.current_pstate.get() {
            if current == index {
                return Ok(());
            }
        }

        // If the P-state is unknown, the lowest-possible frequency (highest-possible P-state index)
        // is what was used to inform the last `update_kernel_performance_info` call.
        let current_frequency = self.pstates[self.current_pstate.get().upper()].frequency;

        let target_pstate = &self.pstates[index];

        // If lowering frequency, we update the kernel before changing P-states. Otherwise, the
        // kernel will be updated after the change.
        let kernel_updated = if target_pstate.frequency < current_frequency {
            self.update_kernel_performance_info(&syscall_handler, target_pstate).await?;
            true
        } else {
            false
        };

        // If the current P-state is unknown or not equal to the new one, attempt an update.
        match self.handler.handle_message(&Message::SetPerformanceState(index as u32)).await {
            Ok(MessageReturn::SetPerformanceState) => {
                self.current_pstate.set(RangedValue::Known(index));

                if !kernel_updated {
                    self.update_kernel_performance_info(&syscall_handler, target_pstate).await?;
                }
                Ok(())
            }
            Ok(r) => {
                // Programming error
                panic!("Wrong response type for SetPerformanceState: {:?}", r);
            }
            Err(e) => {
                log::error!("SetPerformanceState failed: {:?}", e);

                // If the update failed, query the value, so at least the current state is known. If
                // that fails, too, record a range of possible values based on the previous and
                // desired values. Regardless, propagate the error from SetPerformanceState.
                match self.handler.handle_message(&Message::GetPerformanceState).await {
                    Ok(MessageReturn::GetPerformanceState(i)) => {
                        self.current_pstate.set(RangedValue::Known(i as usize));
                    }
                    result => {
                        log::error!("Unexpected result from GetPerformanceState: {:?}", result);
                        let range = Range {
                            lower: std::cmp::min(self.current_pstate.get().lower(), index),
                            upper: std::cmp::max(self.current_pstate.get().upper(), index),
                        };
                        self.current_pstate.set(RangedValue::InRange(range));
                    }
                }

                // If we already updated the kernel, make a new update using the lowest-possible
                // CPU frequency (highest-possbile P-state index) to provide a pessimistic estimate
                // of CPU performance.
                if kernel_updated {
                    let pstate = &self.pstates[self.current_pstate.get().upper()];
                    self.update_kernel_performance_info(&syscall_handler, pstate).await?;
                }

                Err(e)
            }
        }
    }
}

/// Cross-cluster CPU thermal state
///
/// The power of a thermal state for a given performance in NormPerfs is modeled as
//      power = static_power + dynamic_power_per_normperf * performance.
#[derive(Clone, Debug)]
struct ThermalState {
    /// Index of the P-state to be used for each CPU cluster.
    cluster_pstates: Vec<usize>,

    /// Minimum performance at which this thermal state will be used. At low performance values,
    /// it is common for different thermal states to have very similar power requirements. The
    /// minimum performance is used to force a particular choice between such states.
    min_performance: NormPerfs,

    /// Static (fixed) power draw of this thermal state.
    static_power: Watts,

    /// Power draw per unit of normalized performance, assuming that load is perfectly balanced
    /// across CPUs.
    ///
    /// If there is only one cluster, and it is modeled using the standard switching power model
    ///     switching_power = capacitance * voltage**2 * operation_rate,
    /// then dynamic_power_per_normperf would be
    ///     operation_rate * performance_per_ghz * capacitance * voltage**2.
    /// The multi-cluster case is somewhat more complicated, and we furthermore don't require use
    /// of the switching power model. But the voltage term captures a typical way that this value
    /// depends on the underlying P-states.
    dynamic_power_per_normperf: Watts,

    /// Maximum performance that this thermal state can provide, i.e. the performance that will be
    /// achieved when all CPUs are saturated. This value is derived directly from the P-states
    /// specified by this thermal state.
    ///
    /// The term "capacity" is used in agreement with the kernel scheduler.
    performance_capacity: NormPerfs,
}

/// Validates that thermal states are in order of decreasing power use for any input performance.
/// Power is modeled as
///     power = static_power + dynamic_power_per_normperf * min(performance, performance_capacity).
/// It can be shown that power of the thermal states is strictly decreasing if:
///  - static_power is non-increasing
///  - dynamic_power_per_normperf is strictly decreasing
///  - performance_capacity is non-increasing.
fn validate_thermal_states(states: &Vec<ThermalState>) -> Result<(), Error> {
    for pair in states.as_slice().windows(2) {
        let (s0, s1) = (&pair[0], &pair[1]);
        if s1.static_power > s0.static_power {
            bail!(
                "Thermal states' static_power must be non-increasing; violated by {:?} and {:?}",
                s0,
                s1
            );
        } else if s1.dynamic_power_per_normperf >= s0.dynamic_power_per_normperf {
            bail!(
                "Thermal states' dynamic_power_per_normperf must be strictly decreasing; \
                violated by {:?} and {:?}",
                s0,
                s1
            );
        } else if s1.performance_capacity >= s0.performance_capacity {
            bail!(
                "Thermal states' performance_capacity must be non-increasing \
                violated by {:?} and {:?}",
                s0,
                s1
            );
        }
    }
    Ok(())
}

// Configuration structs for CpuManagerBuilder.
#[derive(Clone, Deserialize)]
struct ClusterConfig {
    name: String,
    cluster_index: usize,
    handler: String,
    logical_cpu_numbers: Vec<u32>,
    normperfs_per_ghz: f64,
}

#[derive(Clone, Deserialize)]
struct ThermalStateConfig {
    cluster_pstates: Vec<usize>,
    min_performance_normperfs: f64,
    static_power_w: f64,
    dynamic_power_per_normperf_w: f64,
}

#[derive(Deserialize)]
struct Config {
    clusters: Vec<ClusterConfig>,
    thermal_states: Vec<ThermalStateConfig>,
}

#[derive(Deserialize)]
struct Dependencies {
    cpu_device_handlers: Vec<String>,
    cpu_stats_handler: String,
    syscall_handler: String,
}

#[derive(Deserialize)]
struct JsonData {
    config: Config,
    dependencies: Dependencies,
}

/// Builder for `CpuManager`
pub struct CpuManagerBuilder<'a> {
    cluster_configs: Vec<ClusterConfig>,

    /// Parallel to `cluster_configs`; contains one `CpuDeviceHandler` node (or equivalent) for each
    /// CPU cluster.
    cluster_handlers: Vec<Rc<dyn Node>>,

    thermal_state_configs: Vec<ThermalStateConfig>,
    syscall_handler: Rc<dyn Node>,
    cpu_stats_handler: Rc<dyn Node>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> CpuManagerBuilder<'a> {
    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        let data: JsonData = json::from_value(json_data).unwrap();
        assert_eq!(
            data.config.clusters.iter().map(|c| &c.handler).collect::<Vec<_>>(),
            data.dependencies.cpu_device_handlers.iter().collect::<Vec<_>>()
        );

        let cluster_handlers =
            data.config.clusters.iter().map(|c| nodes[&c.handler].clone()).collect();
        let cpu_stats_handler = nodes[&data.dependencies.cpu_stats_handler].clone();
        let syscall_handler = nodes[&data.dependencies.syscall_handler].clone();

        Self::new(
            data.config.clusters,
            cluster_handlers,
            data.config.thermal_states,
            syscall_handler,
            cpu_stats_handler,
        )
    }

    fn new(
        cluster_configs: Vec<ClusterConfig>,
        cluster_handlers: Vec<Rc<dyn Node>>,
        thermal_state_configs: Vec<ThermalStateConfig>,
        syscall_handler: Rc<dyn Node>,
        cpu_stats_handler: Rc<dyn Node>,
    ) -> Self {
        Self {
            cluster_configs,
            cluster_handlers,
            thermal_state_configs,
            cpu_stats_handler,
            syscall_handler,
            inspect_root: None,
        }
    }

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    pub async fn build(self) -> Result<Rc<CpuManager>, Error> {
        let mut clusters = Vec::new();
        for (cluster_config, handler) in
            self.cluster_configs.into_iter().zip(self.cluster_handlers.into_iter())
        {
            let pstates = match handler.handle_message(&Message::GetCpuPerformanceStates).await {
                Ok(MessageReturn::GetCpuPerformanceStates(pstates)) => pstates,
                Ok(r) => {
                    bail!("GetCpuPerformanceStates returned unexpected value: {:?}", r)
                }
                Err(e) => bail!("Error fetching performance states: {}", e),
            };

            // The current P-state will be set when CpuManager's thermal state is initialized below,
            // so initialize it to a range of all possible values for now.
            let pstate_range = Range { lower: 0, upper: pstates.len() - 1 };
            let current_pstate = Cell::new(RangedValue::InRange(pstate_range));

            clusters.push(CpuCluster {
                name: cluster_config.name,
                cluster_index: cluster_config.cluster_index,
                handler,
                logical_cpu_numbers: cluster_config.logical_cpu_numbers,
                performance_per_ghz: NormPerfs(cluster_config.normperfs_per_ghz),
                pstates,
                current_pstate,
            });
        }

        let get_performance_capacity = |thermal_state_config: &ThermalStateConfig| {
            clusters
                .iter()
                .map(|cluster| {
                    let pstate_index = thermal_state_config.cluster_pstates[cluster.cluster_index];
                    cluster.get_performance_capacity(pstate_index)
                })
                .sum()
        };

        let thermal_states = self
            .thermal_state_configs
            .into_iter()
            .map(|t| {
                let performance_capacity = get_performance_capacity(&t);
                ThermalState {
                    cluster_pstates: t.cluster_pstates,
                    min_performance: NormPerfs(t.min_performance_normperfs),
                    static_power: Watts(t.static_power_w),
                    dynamic_power_per_normperf: Watts(t.dynamic_power_per_normperf_w),
                    performance_capacity,
                }
            })
            .collect();

        validate_thermal_states(&thermal_states)?;

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        let cluster_names = clusters.iter().map(|c| c.name.as_str()).collect();
        let inspect_data = InspectData::new(inspect_root, "CpuManager", cluster_names);
        inspect_data.set_thermal_states(&thermal_states);

        // Retrieve the total number of CPUs, and confirm that it is consistent with the logical
        // CPU numbers for each cluster.
        let num_cpus = match self.syscall_handler.handle_message(&Message::GetNumCpus).await {
            Ok(MessageReturn::GetNumCpus(n)) => n,
            other => bail!("Unexpected GetNumCpus response: {:?}", other),
        };
        for cluster in &clusters {
            if num_cpus <= *cluster.logical_cpu_numbers.iter().max().unwrap_or(&0) {
                bail!(
                    "Cluster {}'s logical CPU numbers {:?} are inconsistent with num CPUs {}",
                    cluster.name,
                    cluster.logical_cpu_numbers,
                    num_cpus
                );
            }
        }

        let cpu_manager = Rc::new(CpuManager {
            clusters,
            num_cpus,
            thermal_states,
            cpu_stats_handler: self.cpu_stats_handler,
            syscall_handler: self.syscall_handler,
            current_thermal_state: Cell::new(None),
            inspect: inspect_data,
        });

        // Update cluster P-states to match the highest power operating condition.
        cpu_manager.update_thermal_state(0).await?;

        Ok(cpu_manager)
    }
}

pub struct CpuManager {
    /// All CPU clusters governed by the `CpuManager`.
    clusters: Vec<CpuCluster>,

    /// Number of CPUs in the system; confirmed to be greater than the max logical CPU number of any
    /// cluster.
    num_cpus: u32,

    /// All supported thermal states for the CPU subsystem.
    thermal_states: Vec<ThermalState>,

    /// Must service GetNumCpus and GetSetPerformanceInfo messages.
    syscall_handler: Rc<dyn Node>,

    /// The node that will provide CPU load information. It is expected that this node responds to
    /// the GetCpuLoads message.
    cpu_stats_handler: Rc<dyn Node>,

    /// The current thermal state of the CPU subsystem. The CPU will be put into its highest-power
    /// state on startup.
    current_thermal_state: Cell<Option<usize>>,

    inspect: InspectData,
}

impl CpuManager {
    // Returns a Vec of all CPU loads as fractional utilizations.
    async fn get_cpu_loads(&self) -> Result<Vec<f32>, Error> {
        fuchsia_trace::duration!("power_manager", "CpuManager::get_cpu_loads");

        // Get load for all CPUs in the system
        match self.send_message(&self.cpu_stats_handler, &Message::GetCpuLoads).await {
            Ok(MessageReturn::GetCpuLoads(loads)) => Ok(loads),
            Ok(r) => Err(format_err!("GetCpuLoads had unexpected return value: {:?}", r)),
            Err(e) => Err(format_err!("GetCpuLoads failed: {:?}", e)),
        }
    }

    /// Estimates the power that the provided thermal state will consume given its capacity and the
    /// desired performance.
    fn estimate_power(state: &ThermalState, desired_performance: NormPerfs) -> Watts {
        let performance = NormPerfs::min(state.performance_capacity, desired_performance);
        let dynamic_power = state.dynamic_power_per_normperf.mul_scalar(performance.0);
        state.static_power + dynamic_power
    }

    // Determines the thermal state that should be used for the given available power and projected
    // performance, also returning the power we expect to dissipate at that state.
    fn select_thermal_state_and_power(
        &self,
        available_power: Watts,
        projected_performance: NormPerfs,
    ) -> (usize, Watts) {
        // Track the maximum index allowed by minimum performance constraints, i.e. the index of the
        // lowest-power state allowed, to serve as a fallback if no states meet the power
        // constraint.
        let mut max_allowed_index = 0;

        for (i, thermal_state) in self.thermal_states.iter().enumerate() {
            if thermal_state.min_performance > projected_performance {
                continue;
            }

            max_allowed_index = i;

            let power = Self::estimate_power(thermal_state, projected_performance);
            if power < available_power {
                return (i, power);
            }
        }

        (
            max_allowed_index,
            Self::estimate_power(&self.thermal_states[max_allowed_index], projected_performance),
        )
    }

    // Updates the current thermal state.
    async fn update_thermal_state(&self, index: usize) -> Result<(), PowerManagerError> {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuManager::update_thermal_state",
            "index" => index as u32
        );

        // Return early if no update is required. We're assuming that P-states have not changed.
        if self.current_thermal_state.get() == Some(index) {
            return Ok(());
        }

        let pstate_indices = &self.thermal_states[index].cluster_pstates;
        let cluster_update_futures: Vec<_> = self
            .clusters
            .iter()
            .map(|cluster| {
                cluster.update_pstate(&self.syscall_handler, pstate_indices[cluster.cluster_index])
            })
            .collect();

        // Aggregate any errors that may have occurred when setting P-states.
        let errors: Vec<_> = futures::future::join_all(cluster_update_futures)
            .await
            .into_iter()
            .filter_map(|r| r.err())
            .collect();

        // Update the thermal state index.
        if errors.is_empty() {
            self.current_thermal_state.set(Some(index));
            self.inspect.thermal_state_index.set(&index.to_string());
            Ok(())
        } else {
            self.current_thermal_state.set(None);

            let msg = format!("P-state update(s) failed: {:?}", errors);
            self.inspect.thermal_state_index.set(&format!("Unknown; {}", msg));
            Err(format_err!(msg).into())
        }
    }

    /// Handles a SetMaxPowerConsumption message. If an error is encountered in the execution of
    /// this method, CpuManager will keep itself in a usable state by using pessimistic estimates of
    /// any value that it cannot determine and then propagate the error to the caller.
    async fn handle_set_max_power_consumption(&self, available_power: Watts) -> MessageResult {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuManager::handle_set_max_power_consumption",
            "available_power" => available_power.0
        );
        self.inspect.available_power.set(available_power.0);

        // Gather CPU loads over the last time interval. In the unlikely event of an error, use the
        // worst-case CPU load of 1.0 for all CPUs and throttle accordingly before propagating the
        // error.
        let (cpu_loads, load_query_error) = match self.get_cpu_loads().await {
            Ok(loads) => (loads, None),
            Err(e) => {
                log::error!(
                    "Error querying CPU loads: {}\nWill throttle assuming maximal load.",
                    e
                );
                self.inspect.last_error.set(&e.to_string());
                (vec![1.0; self.num_cpus as usize], Some(e))
            }
        };

        // Determine the normalized performance over the last interval.
        let mut last_performance = NormPerfs(0.0);
        for cluster in self.clusters.iter() {
            let (load, performance) = cluster.process_fractional_loads(&cpu_loads);
            last_performance += performance;

            fuchsia_trace::counter!(
                "power_manager",
                "cluster load",
                cluster.cluster_index as u64,
                &cluster.name => load as f64
            );
            self.inspect.last_cluster_loads[cluster.cluster_index].set(load as f64);
        }
        self.inspect.last_performance.set(last_performance.0);

        // Determine the next thermal state, updating if needed. We use the performance over the
        // last interval as an estimate of performance over the next interval; in principle a more
        // sophisticated estimate could be used.
        let (new_thermal_state_index, power) =
            self.select_thermal_state_and_power(available_power, last_performance);

        if let Err(e) = self.update_thermal_state(new_thermal_state_index).await {
            self.inspect.last_error.set(&e.to_string());
            return Err(e);
        }

        self.inspect.projected_power.set(power.0);

        // Bubble up any error that may have occurred while querying CPU load.
        match load_query_error {
            None => Ok(MessageReturn::SetMaxPowerConsumption(power)),
            Some(e) => Err(e.into()),
        }
    }
}

#[async_trait(?Send)]
impl Node for CpuManager {
    fn name(&self) -> String {
        "CpuManager".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> MessageResult {
        match msg {
            &Message::SetMaxPowerConsumption(p) => self.handle_set_max_power_consumption(p).await,
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

// TODO(fxbug.dev/84727): Determine whether it would be useful to track histories of any of these
// signals.
struct InspectData {
    root_node: inspect::Node,

    // Properties
    thermal_state_index: inspect::StringProperty,
    last_performance: inspect::DoubleProperty,
    last_cluster_loads: Vec<inspect::DoubleProperty>,
    available_power: inspect::DoubleProperty,
    projected_power: inspect::DoubleProperty,
    last_error: inspect::StringProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, node_name: &str, cluster_names: Vec<&str>) -> Self {
        // Create a local root node and properties
        let root_node = parent.create_child(node_name);

        let state_node = root_node.create_child("state");
        let thermal_state_index = state_node.create_string("thermal_state_index", "initializing");
        let last_performance = state_node.create_double("last_performance (NormPerfs)", 0.0);

        let last_loads_node = state_node.create_child("last_cluster_loads (#cores)");
        let last_cluster_loads =
            cluster_names.into_iter().map(|n| last_loads_node.create_double(n, 0.0)).collect();

        let available_power = state_node.create_double("available_power (W)", 0.0);
        let projected_power = state_node.create_double("projected_power (W)", 0.0);

        let last_error = state_node.create_string("last_error", "<None>");

        state_node.record(last_loads_node);
        root_node.record(state_node);

        Self {
            root_node,
            thermal_state_index,
            last_performance,
            last_cluster_loads,
            available_power,
            projected_power,
            last_error,
        }
    }

    fn set_thermal_states(&self, states: &Vec<ThermalState>) {
        let states_node = self.root_node.create_child("thermal_states");

        // Iterate over `states` in reverse order so that the Inspect nodes appear in the same
        // order as the vector (`create_child` inserts nodes at the head).
        for (i, state) in states.iter().enumerate().rev() {
            let node = states_node.create_child(format!("thermal_state_{:02}", i));

            let pstates = node.create_uint_array("cluster_pstates", state.cluster_pstates.len());
            state.cluster_pstates.iter().enumerate().for_each(|(i, p)| pstates.set(i, *p as u64));
            node.record(pstates);

            node.record_double("min_performance (NormPerfs)", state.min_performance.0);
            node.record_double("static_power (W)", state.static_power.0);
            node.record_double(
                "dynamic_power_per_normperf (W)",
                state.dynamic_power_per_normperf.0,
            );
            node.record_double("performance_capacity (NormPerfs)", state.performance_capacity.0);

            // Pass ownership of the new node to the parent.
            states_node.record(node);
        }

        // Pass ownership of the new `states_node` to the root node
        self.root_node.record(states_node);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::mock_node::{MessageMatcher, MockNode, MockNodeMaker};
    use crate::types::{Hertz, Volts};
    use crate::{msg_eq, msg_ok_return};
    use fuchsia_async as fasync;
    use inspect::assert_data_tree;
    use matches::assert_matches;

    // Common test configurations for big and little clusters.
    static BIG_CPU_NUMBERS: [u32; 2] = [0, 1];
    static BIG_PSTATES: [PState; 3] = [
        PState { frequency: Hertz(2.0e9), voltage: Volts(1.0) },
        PState { frequency: Hertz(1.9e9), voltage: Volts(0.9) },
        PState { frequency: Hertz(1.8e9), voltage: Volts(0.8) },
    ];
    static BIG_PERFORMANCE_PER_GHZ: NormPerfs = NormPerfs(1.0);

    static LITTLE_CPU_NUMBERS: [u32; 2] = [2, 3];
    static LITTLE_PSTATES: [PState; 3] = [
        PState { frequency: Hertz(1.0e9), voltage: Volts(0.5) },
        PState { frequency: Hertz(0.9e9), voltage: Volts(0.4) },
        PState { frequency: Hertz(0.8e9), voltage: Volts(0.3) },
    ];
    static LITTLE_PERFORMANCE_PER_GHZ: NormPerfs = NormPerfs(0.5);

    // Convenience struct to manage mocks of the handlers on which CpuManager depends.
    struct Handlers {
        big_cluster: Rc<MockNode>,
        little_cluster: Rc<MockNode>,
        syscall: Rc<MockNode>,
        cpu_stats: Rc<MockNode>,

        // The MockMaker comes last, so it is dropped after the MockNodes.
        _mock_maker: MockNodeMaker,
    }

    impl Handlers {
        fn new() -> Self {
            let mut mock_maker = MockNodeMaker::new();

            // The big and little cluster handlers are initially queried for all performance states.
            let big_cluster = mock_maker.make(
                "big_cluster_handler",
                vec![(
                    msg_eq!(GetCpuPerformanceStates),
                    msg_ok_return!(GetCpuPerformanceStates(Vec::from(&BIG_PSTATES[..]))),
                )],
            );
            let little_cluster = mock_maker.make(
                "little_cluster_handler",
                vec![(
                    msg_eq!(GetCpuPerformanceStates),
                    msg_ok_return!(GetCpuPerformanceStates(Vec::from(&LITTLE_PSTATES[..]))),
                )],
            );

            // The syscall handler provides the number of CPUs during initialization.
            let num_cpus = BIG_CPU_NUMBERS.len() + LITTLE_CPU_NUMBERS.len();
            let syscall = mock_maker.make(
                "syscall_handler",
                vec![(msg_eq!(GetNumCpus), msg_ok_return!(GetNumCpus(num_cpus as u32)))],
            );

            let cpu_stats = mock_maker.make("cpu_stats_handler", Vec::new());

            let handlers =
                Self { big_cluster, little_cluster, syscall, cpu_stats, _mock_maker: mock_maker };

            // During initialization, CpuManager configures the highest-power thermal state, with
            // both clusters at their respective 0th P-states.
            handlers.expect_big_pstate(0);
            handlers.expect_little_pstate(0);

            handlers
        }

        // Tells the syscall handler to expect a GetSetPerformanceInfo call for the provided
        // collection of CPUs and performance scale.
        fn expect_performance_scale(&self, logical_cpu_numbers: &[u32], float_scale: f64) {
            let scale = NormPerfs(float_scale).try_into().unwrap();
            let info = logical_cpu_numbers
                .iter()
                .map(|n| CpuPerformanceInfo { logical_cpu_number: *n, performance_scale: scale })
                .collect::<Vec<_>>();
            self.syscall.add_msg_response_pair((
                msg_eq!(GetSetCpuPerformanceInfo(info)),
                msg_ok_return!(GetSetCpuPerformanceInfo),
            ));
        }

        // Updates the handlers with expectations for a big cluster P-state change.
        fn expect_big_pstate(&self, pstate_index: u32) {
            self.big_cluster.add_msg_response_pair((
                msg_eq!(SetPerformanceState(pstate_index)),
                msg_ok_return!(SetPerformanceState),
            ));
            let frequency = &BIG_PSTATES[pstate_index as usize].frequency;
            let float_scale = BIG_PERFORMANCE_PER_GHZ.0 * frequency.0 / 1e9;
            self.expect_performance_scale(&BIG_CPU_NUMBERS, float_scale);
        }

        // Updates the handlers with expectations for a little cluster P-state change.
        fn expect_little_pstate(&self, pstate_index: u32) {
            self.little_cluster.add_msg_response_pair((
                msg_eq!(SetPerformanceState(pstate_index)),
                msg_ok_return!(SetPerformanceState),
            ));
            let frequency = &LITTLE_PSTATES[pstate_index as usize].frequency;
            let float_scale = LITTLE_PERFORMANCE_PER_GHZ.0 * frequency.0 / 1e9;
            self.expect_performance_scale(&LITTLE_CPU_NUMBERS, float_scale);
        }

        // Prepares the stats handler for a CPU load query.
        fn enqueue_cpu_loads(&self, loads: Vec<f32>) {
            self.cpu_stats
                .add_msg_response_pair((msg_eq!(GetCpuLoads), msg_ok_return!(GetCpuLoads(loads))));
        }
    }

    // Verify that a node is successfully constructed from JSON configuration.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let handlers = Handlers::new();

        let mut nodes = HashMap::<String, Rc<dyn Node>>::new();
        nodes.insert("big_cluster_node".to_string(), handlers.big_cluster.clone());
        nodes.insert("little_cluster_node".to_string(), handlers.little_cluster.clone());
        nodes.insert("syscall_handler_node".to_string(), handlers.syscall.clone());
        nodes.insert("stats_handler_node".to_string(), handlers.cpu_stats.clone());

        let json_data = json::json!({
            "type": "CpuManager",
            "name": "cpu_manager",
            "config": {
                "clusters": [
                      {
                          "name": "big_cluster",
                          "cluster_index": 0,
                          "handler": "big_cluster_node",
                          "logical_cpu_numbers": [0, 1],
                          "normperfs_per_ghz": BIG_PERFORMANCE_PER_GHZ.0
                      },
                      {
                          "name": "little_cluster",
                          "cluster_index": 1,
                          "handler": "little_cluster_node",
                          "logical_cpu_numbers": [2, 3],
                          "normperfs_per_ghz": LITTLE_PERFORMANCE_PER_GHZ.0
                      }
                ],
                "thermal_states": [
                    {
                      "cluster_pstates": [0, 0],
                      "min_performance_normperfs": 1.0,
                      "static_power_w": 0.9,
                      "dynamic_power_per_normperf_w": 0.6
                    }
                ]
            },
            "dependencies": {
                "cpu_device_handlers": [
                    "big_cluster_node",
                    "little_cluster_node"
                ],
                "cpu_stats_handler": "stats_handler_node",
                "syscall_handler": "syscall_handler_node"
            }
        });

        let builder = CpuManagerBuilder::new_from_json(json_data, &nodes);
        assert!(builder.build().await.is_ok());
    }

    // Verifies that thermal states are properly validated.
    #[fasync::run_singlethreaded(test)]
    async fn test_thermal_state_validation() {
        // Since CpuManagerBuilder::build() exits early, we need a custom constructor for Handlers
        // that omits expectations for messages that are never sent.
        impl Handlers {
            fn new_for_failed_validation() -> Self {
                let mut mock_maker = MockNodeMaker::new();

                // The big and little cluster handlers are initially queried for all performance
                // states.
                let big_cluster = mock_maker.make(
                    "big_cluster_handler",
                    vec![(
                        msg_eq!(GetCpuPerformanceStates),
                        msg_ok_return!(GetCpuPerformanceStates(Vec::from(&BIG_PSTATES[..]))),
                    )],
                );
                let little_cluster = mock_maker.make(
                    "little_cluster_handler",
                    vec![(
                        msg_eq!(GetCpuPerformanceStates),
                        msg_ok_return!(GetCpuPerformanceStates(Vec::from(&LITTLE_PSTATES[..]))),
                    )],
                );

                Self {
                    big_cluster,
                    little_cluster,
                    syscall: mock_maker.make("syscall_handler", Vec::new()),
                    cpu_stats: mock_maker.make("cpu_stats_handler", Vec::new()),
                    _mock_maker: mock_maker,
                }
            }
        }

        let cluster_configs = vec![
            ClusterConfig {
                name: "big_cluster".to_string(),
                cluster_index: 0,
                handler: "<unused>".to_string(),
                logical_cpu_numbers: BIG_CPU_NUMBERS[..].to_vec(),
                normperfs_per_ghz: 1.0,
            },
            ClusterConfig {
                name: "little_cluster".to_string(),
                cluster_index: 1,
                handler: "<unused>".to_string(),
                logical_cpu_numbers: LITTLE_CPU_NUMBERS[..].to_vec(),
                normperfs_per_ghz: 0.5,
            },
        ];

        // Increasing static power
        let handlers = Handlers::new_for_failed_validation();
        let thermal_state_configs = vec![
            ThermalStateConfig {
                cluster_pstates: vec![0, 0],
                min_performance_normperfs: 0.0,
                static_power_w: 2.0,
                dynamic_power_per_normperf_w: 1.0,
            },
            ThermalStateConfig {
                cluster_pstates: vec![2, 2],
                min_performance_normperfs: 0.0,
                static_power_w: 2.1,
                dynamic_power_per_normperf_w: 0.8,
            },
        ];
        let builder = CpuManagerBuilder::new(
            cluster_configs.clone(),
            vec![handlers.big_cluster.clone(), handlers.little_cluster.clone()],
            thermal_state_configs.clone(),
            handlers.syscall.clone(),
            handlers.cpu_stats.clone(),
        );
        assert!(builder.build().await.is_err());

        // Non-decreasing dynamic power
        let handlers = Handlers::new_for_failed_validation();
        let thermal_state_configs = vec![
            ThermalStateConfig {
                cluster_pstates: vec![0, 0],
                min_performance_normperfs: 0.0,
                static_power_w: 2.0,
                dynamic_power_per_normperf_w: 1.0,
            },
            ThermalStateConfig {
                cluster_pstates: vec![2, 2],
                min_performance_normperfs: 0.0,
                static_power_w: 1.5,
                dynamic_power_per_normperf_w: 1.0,
            },
        ];
        let builder = CpuManagerBuilder::new(
            cluster_configs.clone(),
            vec![handlers.big_cluster.clone(), handlers.little_cluster.clone()],
            thermal_state_configs.clone(),
            handlers.syscall.clone(),
            handlers.cpu_stats.clone(),
        );
        assert!(builder.build().await.is_err());

        // Increasing performance capacity
        let handlers = Handlers::new_for_failed_validation();
        let thermal_state_configs = vec![
            ThermalStateConfig {
                cluster_pstates: vec![2, 2],
                min_performance_normperfs: 0.0,
                static_power_w: 2.0,
                dynamic_power_per_normperf_w: 1.0,
            },
            ThermalStateConfig {
                cluster_pstates: vec![2, 1],
                min_performance_normperfs: 0.0,
                static_power_w: 1.5,
                dynamic_power_per_normperf_w: 0.8,
            },
        ];
        let builder = CpuManagerBuilder::new(
            cluster_configs,
            vec![handlers.big_cluster.clone(), handlers.little_cluster.clone()],
            thermal_state_configs,
            handlers.syscall.clone(),
            handlers.cpu_stats.clone(),
        );
        assert!(builder.build().await.is_err());
    }

    // Verify that CpuManager responds as expected to SetMaxPowerConsumption messages.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_max_power_consumption() {
        let handlers = Handlers::new();

        let cluster_configs = vec![
            ClusterConfig {
                name: "big_cluster".to_string(),
                cluster_index: 0,
                handler: "<unused>".to_string(),
                logical_cpu_numbers: BIG_CPU_NUMBERS[..].to_vec(),
                normperfs_per_ghz: 1.0,
            },
            ClusterConfig {
                name: "little_cluster".to_string(),
                cluster_index: 1,
                handler: "<unused>".to_string(),
                logical_cpu_numbers: LITTLE_CPU_NUMBERS[..].to_vec(),
                normperfs_per_ghz: 0.5,
            },
        ];

        let thermal_state_configs = vec![
            ThermalStateConfig {
                cluster_pstates: vec![0, 0],
                min_performance_normperfs: 0.0,
                static_power_w: 2.0,
                dynamic_power_per_normperf_w: 1.0,
            },
            ThermalStateConfig {
                cluster_pstates: vec![0, 1],
                min_performance_normperfs: 0.2,
                static_power_w: 1.5,
                dynamic_power_per_normperf_w: 0.8,
            },
            ThermalStateConfig {
                cluster_pstates: vec![1, 2],
                min_performance_normperfs: 0.4,
                static_power_w: 1.0,
                dynamic_power_per_normperf_w: 0.6,
            },
        ];

        let node = CpuManagerBuilder::new(
            cluster_configs,
            vec![handlers.big_cluster.clone(), handlers.little_cluster.clone()],
            thermal_state_configs,
            handlers.syscall.clone(),
            handlers.cpu_stats.clone(),
        )
        .build()
        .await
        .unwrap();

        // The current P-state is 0, so with 0.1 fractional utililzation per core, we have:
        //   Big cluster: 0.2 cores load -> 0.4GHz utilized -> 0.4 NormPerfs
        //   Little cluster: 0.2 cores load -> 0.2GHz utilized -> 0.1 NormPerfs
        // At thermal state 0, the projected power use at 0.5 NormPerfs is
        //   2W static + 0.5W dynamic = 2.5W
        // This is within the 3W budget, so there are no P-state changes.
        handlers.enqueue_cpu_loads(vec![0.1; 4]);
        let result = node.handle_message(&Message::SetMaxPowerConsumption(Watts(3.0))).await;
        assert!(result.is_ok());

        // The current P-state is 0, so with 0.25 fractional utililzation per core, we have:
        //   Big cluster: 0.5 cores load -> 1GHz utilized -> 1 NormPerfs
        //   Little cluster: 0.5 cores load -> 0.5GHz utilized -> 0.25 NormPerfs
        // Projected power usage at 1.25 NormPerfs is:
        //   Thermal state 0: 2W static + 1.25 dynamic = 3.25W => over 3W budget
        //   Thermal state 1: 1.5W static + 1W dynamic = 2.5W => within 3W budget
        // So the new thermal state is 1, for which the little cluster changes to P-state 1.
        handlers.enqueue_cpu_loads(vec![0.25; 4]);
        handlers.expect_little_pstate(1);
        let result = node.handle_message(&Message::SetMaxPowerConsumption(Watts(3.0))).await;
        assert!(result.is_ok());

        // CPU load stays the same, but the power budget drops to 2.4W, below allocation for thermal
        // state 1. This pushes us to thermal state 2, with big P-state 1 and little P-state 2.
        handlers.enqueue_cpu_loads(vec![0.25; 4]);
        handlers.expect_big_pstate(1);
        handlers.expect_little_pstate(2);
        let result = node.handle_message(&Message::SetMaxPowerConsumption(Watts(2.4))).await;
        assert!(result.is_ok());

        // The power budget is 1.4W, which is below the static power for thermal state 1. However,
        // at 0.05 fractional utilization per core, the projected performance is 0.25 Perfs, which
        // makes thermal state 2 inadmissible. Thus, we fall back to thermal state 1.
        handlers.enqueue_cpu_loads(vec![0.05; 4]);
        handlers.expect_big_pstate(0);
        handlers.expect_little_pstate(1);
        let result = node.handle_message(&Message::SetMaxPowerConsumption(Watts(1.4))).await;
        assert!(result.is_ok());

        // At 0.01 fractional utilization per core, the projected performance is 0.05 Perfs, so now
        // thermal state 1 is inadmissible. This drives us to thermal state 0.
        handlers.enqueue_cpu_loads(vec![0.01; 4]);
        handlers.expect_little_pstate(0);
        let result = node.handle_message(&Message::SetMaxPowerConsumption(Watts(1.4))).await;
        assert!(result.is_ok());
    }

    // Verify that Inspect data is populated as expected.
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let handlers = Handlers::new();

        let cluster_configs = vec![
            ClusterConfig {
                name: "big_cluster".to_string(),
                cluster_index: 0,
                handler: "<unused>".to_string(),
                logical_cpu_numbers: BIG_CPU_NUMBERS[..].to_vec(),
                normperfs_per_ghz: BIG_PERFORMANCE_PER_GHZ.0,
            },
            ClusterConfig {
                name: "little_cluster".to_string(),
                cluster_index: 1,
                handler: "<unused>".to_string(),
                logical_cpu_numbers: LITTLE_CPU_NUMBERS[..].to_vec(),
                normperfs_per_ghz: LITTLE_PERFORMANCE_PER_GHZ.0,
            },
        ];

        let thermal_states = vec![
            ThermalStateConfig {
                cluster_pstates: vec![0, 0],
                min_performance_normperfs: 0.0,
                static_power_w: 2.0,
                dynamic_power_per_normperf_w: 1.0,
            },
            ThermalStateConfig {
                cluster_pstates: vec![1, 2],
                min_performance_normperfs: 0.2,
                static_power_w: 1.5,
                dynamic_power_per_normperf_w: 0.8,
            },
        ];

        let inspector = inspect::Inspector::new();
        let builder = CpuManagerBuilder::new(
            cluster_configs,
            vec![handlers.big_cluster.clone(), handlers.little_cluster.clone()],
            thermal_states,
            handlers.syscall.clone(),
            handlers.cpu_stats.clone(),
        )
        .with_inspect_root(inspector.root());
        let node = builder.build().await.unwrap();

        // The power budget of 1W exceeds the static power of thermal state 0, so we are pushed
        // to thermal state 1.
        handlers.enqueue_cpu_loads(vec![1.0; 4]);
        handlers.expect_big_pstate(1);
        handlers.expect_little_pstate(2);
        let result = node.handle_message(&Message::SetMaxPowerConsumption(Watts(1.0))).await;
        assert_matches!(result, Ok(_));

        assert_data_tree!(
            inspector,
            root: {
                "CpuManager": {
                    "state": {
                        "thermal_state_index": "1",
                        "last_performance (NormPerfs)": 5.0,
                        "last_cluster_loads (#cores)": {
                            "big_cluster": 2.0,
                            "little_cluster": 2.0
                        },
                        "available_power (W)": 1.0,
                        "projected_power (W)": 5.18,
                        "last_error": "<None>",
                    },
                    "thermal_states": {
                        "thermal_state_00": {
                            "cluster_pstates": vec![0u64, 0],
                            "min_performance (NormPerfs)": 0.0,
                            "static_power (W)": 2.0,
                            "dynamic_power_per_normperf (W)": 1.0,
                            "performance_capacity (NormPerfs)": 5.0,
                        },
                        "thermal_state_01": {
                            "cluster_pstates": vec![1u64, 2],
                            "min_performance (NormPerfs)": 0.2,
                            "static_power (W)": 1.5,
                            "dynamic_power_per_normperf (W)": 0.8,
                            "performance_capacity (NormPerfs)": 4.6,
                        },
                    }
                }
            }
        );
    }
}
