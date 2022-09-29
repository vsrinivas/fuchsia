// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::platform_metrics::PlatformMetric;
use crate::shutdown_request::{RebootReason, ShutdownRequest};
use crate::temperature_handler::TemperatureFilter;
use crate::types::{Celsius, Nanoseconds, Seconds, ThermalLoad, Watts};
use crate::utils::get_current_timestamp;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, Property};
use futures::{
    future::{FutureExt, LocalBoxFuture},
    stream::FuturesUnordered,
    StreamExt,
};
use log::*;
use serde_derive::Deserialize;
use serde_json as json;
use std::cell::Cell;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: ThermalPolicy
///
/// Summary: Implements the closed loop thermal control policy for the system
///
/// Handles Messages: N/A
///
/// Sends Messages:
///     - ReadTemperature
///     - SetMaxPowerConsumption
///     - SystemShutdown
///     - UpdateThermalLoad
///     - LogPlatformMetric
///     - GetDriverPath
///
/// FIDL dependencies: N/A

pub struct ThermalPolicyBuilder<'a> {
    config: ThermalConfig,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> ThermalPolicyBuilder<'a> {
    pub fn new(config: ThermalConfig) -> Self {
        Self { config, inspect_root: None }
    }

    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct ControllerConfig {
            sample_interval: f64,
            filter_time_constant: f64,
            target_temperature: f64,
            e_integral_min: f64,
            e_integral_max: f64,
            sustainable_power: f64,
            proportional_gain: f64,
            integral_gain: f64,
        }

        #[derive(Deserialize)]
        struct NodeConfig {
            thermal_shutdown_temperature: f64,
            controller_params: ControllerConfig,
        }

        #[derive(Deserialize)]
        struct Dependencies {
            cpu_control_nodes: Vec<String>,
            system_power_handler_node: String,
            temperature_handler_node: String,
            thermal_load_notify_nodes: Vec<String>,
            platform_metrics_node: String,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: NodeConfig,
            dependencies: Dependencies,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        let thermal_config = ThermalConfig {
            temperature_node: nodes[&data.dependencies.temperature_handler_node].clone(),
            cpu_control_nodes: data
                .dependencies
                .cpu_control_nodes
                .iter()
                .map(|node| nodes[node].clone())
                .collect(),
            sys_pwr_handler: nodes[&data.dependencies.system_power_handler_node].clone(),
            thermal_load_notify_nodes: data
                .dependencies
                .thermal_load_notify_nodes
                .iter()
                .map(|node_name| nodes[node_name].clone())
                .collect(),
            platform_metrics_node: nodes[&data.dependencies.platform_metrics_node].clone(),
            policy_params: ThermalPolicyParams {
                controller_params: ThermalControllerParams {
                    sample_interval: Seconds(data.config.controller_params.sample_interval),
                    filter_time_constant: Seconds(
                        data.config.controller_params.filter_time_constant,
                    ),
                    target_temperature: Celsius(data.config.controller_params.target_temperature),
                    e_integral_min: data.config.controller_params.e_integral_min,
                    e_integral_max: data.config.controller_params.e_integral_max,
                    sustainable_power: Watts(data.config.controller_params.sustainable_power),
                    proportional_gain: data.config.controller_params.proportional_gain,
                    integral_gain: data.config.controller_params.integral_gain,
                },
                thermal_shutdown_temperature: Celsius(data.config.thermal_shutdown_temperature),
            },
        };

        Self::new(thermal_config)
    }

    #[cfg(test)]
    fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    pub async fn build<'b>(
        self,
        futures_out: &FuturesUnordered<LocalBoxFuture<'b, ()>>,
    ) -> Result<Rc<ThermalPolicy>, Error> {
        // Create default values
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        // Query the TemperatureHandler node for its associated driver path
        let driver_path = query_driver_path(&self.config.temperature_node).await;

        let node = Rc::new(ThermalPolicy {
            driver_path,
            state: ThermalState {
                temperature_filter: TemperatureFilter::new(
                    self.config.temperature_node.clone(),
                    self.config.policy_params.controller_params.filter_time_constant,
                ),
                prev_timestamp: Cell::new(Nanoseconds(0)),
                max_time_delta: Cell::new(Seconds(0.0)),
                error_integral: Cell::new(0.0),
            },
            inspect: InspectData::new(inspect_root, "ThermalPolicy".to_string(), &self.config),
            config: self.config,
        });

        futures_out.push(node.clone().periodic_thermal_loop());
        Ok(node)
    }
}

/// Queries the provided `temperature_handler` node for their associated driver path. If any error
/// or unexpected response is encountered then an empty string is returned.
async fn query_driver_path(temperature_handler: &Rc<dyn Node>) -> String {
    match temperature_handler.handle_message(&Message::GetDriverPath).await {
        Ok(MessageReturn::GetDriverPath(path)) => path,
        _ => String::new(),
    }
}

pub struct ThermalPolicy {
    /// Topological path to the temperature driver that we're bound to.
    driver_path: String,

    config: ThermalConfig,
    state: ThermalState,

    /// A struct for managing Component Inspection data.
    inspect: InspectData,
}

/// A struct to store all configurable aspects of the ThermalPolicy node
pub struct ThermalConfig {
    /// The node to provide temperature readings for the thermal control loop. It is expected that
    /// this node responds to the ReadTemperature message.
    pub temperature_node: Rc<dyn Node>,

    /// The nodes used to impose limits on CPU power state. There will be one node for each CPU
    /// power domain (e.g., big.LITTLE). It is expected that these nodes respond to the
    /// SetMaxPowerConsumption message.
    pub cpu_control_nodes: Vec<Rc<dyn Node>>,

    /// The node to handle system power state changes (e.g., shutdown). It is expected that this
    /// node responds to the SystemShutdown message.
    pub sys_pwr_handler: Rc<dyn Node>,

    /// The nodes that are interested in receiving updated thermal load values. It is expected that
    /// these nodes respond to the UpdateThermalLoad message.
    pub thermal_load_notify_nodes: Vec<Rc<dyn Node>>,

    /// All parameter values relating to the thermal policy itself
    pub policy_params: ThermalPolicyParams,

    /// Node that we'll notify with relevant platform metrics.
    pub platform_metrics_node: Rc<dyn Node>,
}

/// A struct to store all configurable aspects of the thermal policy itself
pub struct ThermalPolicyParams {
    /// The thermal control loop parameters
    pub controller_params: ThermalControllerParams,

    /// If temperature reaches or exceeds this value, the policy will command a system shutdown
    pub thermal_shutdown_temperature: Celsius,
}

/// A struct to store the tunable thermal control loop parameters
#[derive(Clone, Debug)]
pub struct ThermalControllerParams {
    /// The interval at which to run the thermal control loop
    pub sample_interval: Seconds,

    /// Time constant for the low-pass filter used for smoothing the temperature input signal
    pub filter_time_constant: Seconds,

    /// Target temperature for the PI control calculation
    pub target_temperature: Celsius,

    /// Minimum integral error [degC * s] for the PI control calculation
    pub e_integral_min: f64,

    /// Maximum integral error [degC * s] for the PI control calculation
    pub e_integral_max: f64,

    /// The available power when there is no temperature error
    pub sustainable_power: Watts,

    /// The proportional gain [W / degC] for the PI control calculation
    pub proportional_gain: f64,

    /// The integral gain [W / (degC * s)] for the PI control calculation
    pub integral_gain: f64,
}

/// State information that is used for calculations across controller iterations
struct ThermalState {
    /// Provides filtered temperature values according to the configured filter constant.
    temperature_filter: TemperatureFilter,

    /// The time of the previous controller iteration
    prev_timestamp: Cell<Nanoseconds>,

    /// The largest observed time between controller iterations (may be used to detect hangs)
    max_time_delta: Cell<Seconds>,

    /// The integral error [degC * s] that is accumulated across controller iterations
    error_integral: Cell<f64>,
}

impl ThermalPolicy {
    /// Creates a Future driven by a timer firing at the interval specified by
    /// ThermalControllerParams.sample_interval. At each fire, `iterate_thermal_control` is called
    /// and any resulting errors are logged.
    fn periodic_thermal_loop<'a>(self: Rc<Self>) -> LocalBoxFuture<'a, ()> {
        let mut periodic_timer = fasync::Interval::new(
            self.config.policy_params.controller_params.sample_interval.into(),
        );

        async move {
            while let Some(()) = periodic_timer.next().await {
                fuchsia_trace::instant!(
                    "power_manager",
                    "ThermalPolicy::periodic_timer_fired",
                    fuchsia_trace::Scope::Thread
                );
                let result = self.iterate_thermal_control().await;
                log_if_err!(result, "Error while running thermal control iteration");
                fuchsia_trace::instant!(
                    "power_manager",
                    "ThermalPolicy::iterate_thermal_control_result",
                    fuchsia_trace::Scope::Thread,
                    "result" => format!("{:?}", result).as_str()
                );
            }
        }
        .boxed_local()
    }

    /// This is the main body of the closed loop thermal control logic. The function is called
    /// periodically by the timer started in `start_periodic_thermal_loop`. For each iteration, the
    /// following steps will be taken:
    ///     1. Read the current temperature from the temperature driver specified in ThermalConfig
    ///     2. Filter the raw temperature value using a low-pass filter
    ///     3. Use the new filtered temperature to calculate the proportional error and integral
    ///        error of temperature relative to the configured target temperature
    ///     4. Use the proportional error and integral error to derive thermal load and available
    ///        power values
    ///     5. Update the relevant nodes with the new thermal load and available power information
    async fn iterate_thermal_control(&self) -> Result<(), Error> {
        fuchsia_trace::duration!("power_manager", "ThermalPolicy::iterate_thermal_control");

        let timestamp = get_current_timestamp();
        let time_delta = self.get_time_delta(timestamp);

        let temperature = self.state.temperature_filter.get_temperature(timestamp).await?;
        let (error_proportional, error_integral) =
            self.get_temperature_error(temperature.filtered, time_delta);
        let thermal_load = Self::calculate_thermal_load(
            error_integral,
            self.config.policy_params.controller_params.e_integral_min,
            self.config.policy_params.controller_params.e_integral_max,
        );

        self.log_thermal_iteration_metrics(
            timestamp,
            time_delta,
            temperature.raw,
            temperature.filtered,
            error_integral,
            thermal_load,
        );

        // If the new temperature is above the critical threshold then shut down the system
        let result = self.check_critical_temperature(temperature.raw).await;
        log_if_err!(result, "Error checking critical temperature");
        fuchsia_trace::instant!(
            "power_manager",
            "ThermalPolicy::check_critical_temperature_result",
            fuchsia_trace::Scope::Thread,
            "result" => format!("{:?}", result).as_str()
        );

        // Determine the new thermal load and update `thermal_load_notify_nodes`
        let result = self.process_thermal_load(thermal_load).await;
        log_if_err!(result, "Error updating thermal load");
        fuchsia_trace::instant!(
            "power_manager",
            "ThermalPolicy::process_thermal_load_result",
            fuchsia_trace::Scope::Thread,
            "result" => format!("{:?}", result).as_str()
        );

        // Update the power allocation according to the new temperature error readings
        let result = self.update_power_allocation(error_proportional, error_integral).await;
        log_if_err!(result, "Error running thermal feedback controller");
        fuchsia_trace::instant!(
            "power_manager",
            "ThermalPolicy::update_power_allocation_result",
            fuchsia_trace::Scope::Thread,
            "result" => format!("{:?}", result).as_str()
        );

        Ok(())
    }

    /// Gets the time delta from the previous call to this function using the provided timestamp.
    /// Logs the largest delta into Inspect.
    fn get_time_delta(&self, timestamp: Nanoseconds) -> Seconds {
        let time_delta = (timestamp - self.state.prev_timestamp.get()).into();
        if time_delta > self.state.max_time_delta.get().into() {
            self.state.max_time_delta.set(time_delta);
            self.inspect.max_time_delta.set(time_delta.0);
        }
        self.state.prev_timestamp.set(timestamp);
        time_delta
    }

    /// Calculates proportional error and integral error of temperature using the provided input
    /// temperature and time delta. Stores the new integral error and logs it to Inspect.
    fn get_temperature_error(&self, temperature: Celsius, time_delta: Seconds) -> (f64, f64) {
        let controller_params = &self.config.policy_params.controller_params;
        let error_proportional = controller_params.target_temperature.0 - temperature.0;
        let error_integral = num_traits::clamp(
            self.state.error_integral.get() + error_proportional * time_delta.0,
            controller_params.e_integral_min,
            controller_params.e_integral_max,
        );
        self.state.error_integral.set(error_integral);
        self.inspect.error_integral.set(error_integral);
        (error_proportional, error_integral)
    }

    /// Logs various state data that is updated on each iteration of the thermal policy.
    fn log_thermal_iteration_metrics(
        &self,
        timestamp: Nanoseconds,
        time_delta: Seconds,
        raw_temperature: Celsius,
        filtered_temperature: Celsius,
        temperature_error_integral: f64,
        thermal_load: ThermalLoad,
    ) {
        self.inspect.timestamp.set(timestamp.0);
        self.inspect.time_delta.set(time_delta.0);
        self.inspect.temperature_raw.set(raw_temperature.0);
        self.inspect.temperature_filtered.set(filtered_temperature.0);
        self.inspect.thermal_load.set(thermal_load.0.into());
        fuchsia_trace::instant!(
            "power_manager",
            "ThermalPolicy::thermal_control_iteration_data",
            fuchsia_trace::Scope::Thread,
            "timestamp" => timestamp.0
        );
        fuchsia_trace::counter!(
            "power_manager",
            "ThermalPolicy raw_temperature",
            0,
            "raw_temperature" => raw_temperature.0
        );
        fuchsia_trace::counter!(
            "power_manager",
            "ThermalPolicy filtered_temperature",
            0,
            "filtered_temperature" => filtered_temperature.0
        );
        fuchsia_trace::counter!(
            "power_manager",
            "ThermalPolicy error_integral", 0,
            "error_integral" => temperature_error_integral
        );
        fuchsia_trace::counter!(
            "power_manager",
            "ThermalPolicy thermal_load",
            0,
            "thermal_load" => thermal_load.0
        );
    }

    /// Compares the supplied temperature with the thermal config thermal shutdown temperature. If
    /// we've reached or exceeded the shutdown temperature, message the system power handler node
    /// to initiate a system shutdown.
    async fn check_critical_temperature(&self, temperature: Celsius) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalPolicy::check_critical_temperature",
            "temperature" => temperature.0
        );

        // Temperature has exceeded the thermal shutdown temperature
        if temperature.0 >= self.config.policy_params.thermal_shutdown_temperature.0 {
            fuchsia_trace::instant!(
                "power_manager",
                "ThermalPolicy::thermal_shutdown_reached",
                fuchsia_trace::Scope::Thread,
                "temperature" => temperature.0,
                "shutdown_temperature" => self.config.policy_params.thermal_shutdown_temperature.0
            );

            self.log_platform_metric(PlatformMetric::ThrottlingResultShutdown).await;

            self.send_message(
                &self.config.sys_pwr_handler,
                &Message::SystemShutdown(ShutdownRequest::Reboot(RebootReason::HighTemperature)),
            )
            .await
            .map_err(|e| format_err!("Failed to shut down the system: {}", e))?;
        }

        Ok(())
    }

    /// Process a new thermal load value. If there is a change from the cached thermal_load, then
    /// the new value is sent out to `thermal_load_notify_nodes`.
    async fn process_thermal_load(&self, new_load: ThermalLoad) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalPolicy::process_thermal_load",
            "new_load" => new_load.0
        );

        self.send_message_to_many(
            &self.config.thermal_load_notify_nodes,
            &Message::UpdateThermalLoad(new_load, self.driver_path.to_string()),
        )
        .await
        .into_iter()
        .collect::<Result<Vec<_>, _>>()?;

        Ok(())
    }

    async fn log_platform_metric(&self, metric: PlatformMetric) {
        let msg = Message::LogPlatformMetric(metric);
        log_if_err!(
            self.send_message(&self.config.platform_metrics_node, &msg).await,
            format!("Failed to log platform metric {:?}", msg)
        );
    }

    /// Calculates the thermal load which is a value in the range [0 - MAX_THERMAL_LOAD] defined as
    /// ((error_integral - range_start) / (range_end - range_start) * MAX_THERMAL_LOAD), where the
    /// range is defined by the maximum and minimum integral error according to the controller
    /// parameters.
    fn calculate_thermal_load(
        error_integral: f64,
        error_integral_min: f64,
        error_integral_max: f64,
    ) -> ThermalLoad {
        debug_assert!(
            error_integral >= error_integral_min,
            "error_integral ({}) less than error_integral_min ({})",
            error_integral,
            error_integral_min
        );
        debug_assert!(
            error_integral <= error_integral_max,
            "error_integral ({}) greater than error_integral_max ({})",
            error_integral,
            error_integral_max
        );

        if error_integral < error_integral_min {
            error!(
                "error_integral {} less than error_integral_min {}",
                error_integral, error_integral_min
            );
            ThermalLoad(100)
        } else if error_integral > error_integral_max {
            error!(
                "error_integral {} greater than error_integral_max {}",
                error_integral, error_integral_max
            );
            ThermalLoad(0)
        } else {
            ThermalLoad(
                ((error_integral - error_integral_max) / (error_integral_min - error_integral_max)
                    * 100.0) as u32,
            )
        }
    }

    /// Updates the power allocation according to the provided proportional error and integral error
    /// of temperature.
    async fn update_power_allocation(
        &self,
        error_proportional: f64,
        error_integral: f64,
    ) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalPolicy::update_power_allocation",
            "error_proportional" => error_proportional,
            "error_integral" => error_integral
        );
        let available_power = self.calculate_available_power(error_proportional, error_integral);
        self.log_platform_metric(PlatformMetric::AvailablePower(available_power)).await;
        fuchsia_trace::counter!(
            "power_manager",
            "ThermalPolicy available_power",
            0,
            "available_power" => available_power.0
        );

        self.distribute_power(available_power).await
    }

    /// A PI control algorithm that uses temperature as the measured process variable, and
    /// available power as the control variable.
    fn calculate_available_power(&self, error_proportional: f64, error_integral: f64) -> Watts {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalPolicy::calculate_available_power",
            "error_proportional" => error_proportional,
            "error_integral" => error_integral
        );

        let controller_params = &self.config.policy_params.controller_params;
        let p_term = error_proportional * controller_params.proportional_gain;
        let i_term = error_integral * controller_params.integral_gain;
        let power_available =
            f64::max(0.0, controller_params.sustainable_power.0 + p_term + i_term);

        Watts(power_available)
    }

    /// This function is responsible for distributing the available power (as determined by the
    /// prior PI calculation) to the various power actors that are included in this closed loop
    /// system. Initially, CPU is the only power actor. In later versions of the thermal policy,
    /// there may be more power actors with associated "weights" for distributing power amongst
    /// them.
    async fn distribute_power(&self, mut total_available_power: Watts) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalPolicy::distribute_power",
            "total_available_power" => total_available_power.0
        );

        // The power distribution currently works by allocating the total available power to the
        // first CPU control node in `cpu_control_nodes`. The node replies to the
        // SetMaxPowerConsumption message with the amount of power it was able to utilize. This
        // utilized amount is subtracted from the total available power, then the remaining power is
        // allocated to the remaining CPU control nodes in the same way.

        // TODO(fxbug.dev/48205): We may want to revisit this distribution algorithm to avoid potentially
        // starving some CPU control nodes. We'll want to have some discussions and learn more about
        // intended big.LITTLE scheduling and operation to better inform our decisions here. We may
        // find that we'll need to first query the nodes to learn how much power they intend to use
        // before making allocation decisions.
        for node in self.config.cpu_control_nodes.iter() {
            if let MessageReturn::SetMaxPowerConsumption(power_used) = self
                .send_message(&node, &Message::SetMaxPowerConsumption(total_available_power))
                .await?
            {
                self.log_platform_metric(PlatformMetric::CpuPowerUsage(node.name(), power_used))
                    .await;
                total_available_power = total_available_power - power_used;
            }
        }

        Ok(())
    }
}

#[async_trait(?Send)]
impl Node for ThermalPolicy {
    fn name(&self) -> String {
        "ThermalPolicy".to_string()
    }
}

struct InspectData {
    // Nodes
    root_node: inspect::Node,

    // Properties
    timestamp: inspect::IntProperty,
    time_delta: inspect::DoubleProperty,
    temperature_raw: inspect::DoubleProperty,
    temperature_filtered: inspect::DoubleProperty,
    error_integral: inspect::DoubleProperty,
    thermal_load: inspect::UintProperty,
    max_time_delta: inspect::DoubleProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, name: String, config: &ThermalConfig) -> Self {
        // Create a local root node and properties
        let root_node = parent.create_child(name);
        let state_node = root_node.create_child("state");
        let stats_node = root_node.create_child("stats");
        let timestamp = state_node.create_int("timestamp (ns)", 0);
        let time_delta = state_node.create_double("time_delta (s)", 0.0);
        let temperature_raw = state_node.create_double("temperature_raw (C)", 0.0);
        let temperature_filtered = state_node.create_double("temperature_filtered (C)", 0.0);
        let error_integral = state_node.create_double("error_integral", 0.0);
        let thermal_load = state_node.create_uint("thermal_load", 0);
        let max_time_delta = stats_node.create_double("max_time_delta (s)", 0.0);

        // Pass ownership of the new nodes to the root node, otherwise they'll be dropped
        root_node.record(state_node);
        root_node.record(stats_node);

        let inspect_data = InspectData {
            root_node,
            timestamp,
            time_delta,
            max_time_delta,
            temperature_raw,
            temperature_filtered,
            error_integral,
            thermal_load,
        };
        inspect_data.set_thermal_config(config);
        inspect_data
    }

    fn set_thermal_config(&self, config: &ThermalConfig) {
        let policy_params_node = self.root_node.create_child("policy_params");
        let ctrl_params_node = policy_params_node.create_child("controller_params");
        let params = &config.policy_params.controller_params;
        ctrl_params_node.record_double("sample_interval (s)", params.sample_interval.0);
        ctrl_params_node.record_double("filter_time_constant (s)", params.filter_time_constant.0);
        ctrl_params_node.record_double("target_temperature (C)", params.target_temperature.0);
        ctrl_params_node.record_double("e_integral_min", params.e_integral_min);
        ctrl_params_node.record_double("e_integral_max", params.e_integral_max);
        ctrl_params_node.record_double("sustainable_power (W)", params.sustainable_power.0);
        ctrl_params_node.record_double("proportional_gain", params.proportional_gain);
        ctrl_params_node.record_double("integral_gain", params.integral_gain);
        policy_params_node.record(ctrl_params_node);

        self.root_node.record(policy_params_node);
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::test::mock_node::{create_dummy_node, MessageMatcher, MockNodeMaker};
    use crate::{msg_eq, msg_ok_return};
    use inspect::testing::assert_data_tree;

    pub fn get_sample_interval(thermal_policy: &ThermalPolicy) -> Seconds {
        thermal_policy.config.policy_params.controller_params.sample_interval
    }

    fn default_policy_params() -> ThermalPolicyParams {
        ThermalPolicyParams {
            controller_params: ThermalControllerParams {
                sample_interval: Seconds(1.0),
                filter_time_constant: Seconds(10.0),
                target_temperature: Celsius(85.0),
                e_integral_min: -20.0,
                e_integral_max: 0.0,
                sustainable_power: Watts(1.1),
                proportional_gain: 0.0,
                integral_gain: 0.2,
            },
            thermal_shutdown_temperature: Celsius(95.0),
        }
    }

    /// Tests the calculate_thermal_load function for correctness.
    #[test]
    fn test_calculate_thermal_load() {
        // These tests using invalid error integral values will panic on debug builds and the
        // `test_calculate_thermal_load_error_integral_*` tests will verify that. For now (non-debug
        // build), just test that the invalid values are clamped to a valid ThermalLoad value.
        if cfg!(not(debug_assertions)) {
            // An invalid error_integral greater than e_integral_max should clamp at ThermalLoad(0)
            assert_eq!(ThermalPolicy::calculate_thermal_load(5.0, -20.0, 0.0), ThermalLoad(0));

            // An invalid error_integral less than e_integral_min should clamp at ThermalLoad(100)
            assert_eq!(ThermalPolicy::calculate_thermal_load(-25.0, -20.0, 0.0), ThermalLoad(100));
        }

        // Test some error_integral values ranging from e_integral_max to e_integral_min
        assert_eq!(ThermalPolicy::calculate_thermal_load(0.0, -20.0, 0.0), ThermalLoad(0));
        assert_eq!(ThermalPolicy::calculate_thermal_load(-10.0, -20.0, 0.0), ThermalLoad(50));
        assert_eq!(ThermalPolicy::calculate_thermal_load(-20.0, -20.0, 0.0), ThermalLoad(100));
    }

    /// Tests that an invalid low error integral value will cause a panic on debug builds.
    #[test]
    #[should_panic = "error_integral (-25) less than error_integral_min (-20)"]
    #[cfg(debug_assertions)]
    fn test_calculate_thermal_load_error_integral_low_panic() {
        assert_eq!(ThermalPolicy::calculate_thermal_load(-25.0, -20.0, 0.0), ThermalLoad(100));
    }

    /// Tests that an invalid high error integral value will cause a panic on debug builds.
    #[test]
    #[should_panic = "error_integral (5) greater than error_integral_max (0)"]
    #[cfg(debug_assertions)]
    fn test_calculate_thermal_load_error_integral_high_panic() {
        assert_eq!(ThermalPolicy::calculate_thermal_load(5.0, -20.0, 0.0), ThermalLoad(0));
    }

    /// Tests that the `get_time_delta` function correctly calculates time delta while updating the
    /// `max_time_delta` state variable.
    #[fasync::run_singlethreaded(test)]
    async fn test_get_time_delta() {
        let thermal_config = ThermalConfig {
            temperature_node: create_dummy_node(),
            cpu_control_nodes: vec![create_dummy_node()],
            sys_pwr_handler: create_dummy_node(),
            thermal_load_notify_nodes: vec![create_dummy_node()],
            policy_params: default_policy_params(),
            platform_metrics_node: create_dummy_node(),
        };

        let node_futures = FuturesUnordered::new();
        let node = ThermalPolicyBuilder::new(thermal_config).build(&node_futures).await.unwrap();

        assert_eq!(node.get_time_delta(Seconds(1.5).into()), Seconds(1.5));
        assert_eq!(node.get_time_delta(Seconds(2.0).into()), Seconds(0.5));
        assert_eq!(node.state.max_time_delta.get(), Seconds(1.5));
    }

    /// Tests that the `get_temperature_error` function correctly calculates proportional error and
    /// integral error.
    #[fasync::run_singlethreaded(test)]
    async fn test_get_temperature_error() {
        let mut policy_params = default_policy_params();
        policy_params.controller_params.target_temperature = Celsius(80.0);
        policy_params.controller_params.e_integral_min = -20.0;
        policy_params.controller_params.e_integral_max = 0.0;

        let thermal_config = ThermalConfig {
            temperature_node: create_dummy_node(),
            cpu_control_nodes: vec![create_dummy_node()],
            sys_pwr_handler: create_dummy_node(),
            thermal_load_notify_nodes: vec![create_dummy_node()],
            policy_params,
            platform_metrics_node: create_dummy_node(),
        };
        let node_futures = FuturesUnordered::new();
        let node = ThermalPolicyBuilder::new(thermal_config).build(&node_futures).await.unwrap();

        assert_eq!(node.get_temperature_error(Celsius(40.0), Seconds(1.0)), (40.0, 0.0));
        assert_eq!(node.get_temperature_error(Celsius(90.0), Seconds(1.0)), (-10.0, -10.0));
        assert_eq!(node.get_temperature_error(Celsius(90.0), Seconds(1.0)), (-10.0, -20.0));
    }

    /// Tests that the ThermalPolicy will correctly divide total available power amongst multiple
    /// CPU control nodes.
    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_cpu_actors() {
        let mut mock_maker = MockNodeMaker::new();

        // Set up the two CpuControlHandler mock nodes. The message reply to SetMaxPowerConsumption
        // indicates how much power the mock node was able to utilize, and ultimately drives the
        // test logic.
        let cpu_node_1 = mock_maker.make(
            "CpuCtrlNode1",
            vec![
                // On the first iteration, this node will consume all available power (1W)
                (
                    msg_eq!(SetMaxPowerConsumption(Watts(1.0))),
                    msg_ok_return!(SetMaxPowerConsumption(Watts(1.0))),
                ),
                // On the second iteration, this node will consume half of the available power
                // (0.5W)
                (
                    msg_eq!(SetMaxPowerConsumption(Watts(1.0))),
                    msg_ok_return!(SetMaxPowerConsumption(Watts(0.5))),
                ),
                // On the third iteration, this node will consume none of the available power
                // (0.0W)
                (
                    msg_eq!(SetMaxPowerConsumption(Watts(1.0))),
                    msg_ok_return!(SetMaxPowerConsumption(Watts(0.0))),
                ),
            ],
        );
        let cpu_node_2 = mock_maker.make(
            "CpuCtrlNode2",
            vec![
                // On the first iteration, the first node consumes all available power (1W), so
                // expect to receive a power allocation of 0W
                (
                    msg_eq!(SetMaxPowerConsumption(Watts(0.0))),
                    msg_ok_return!(SetMaxPowerConsumption(Watts(0.0))),
                ),
                // On the second iteration, the first node consumes half of the available power
                // (1W), so expect to receive a power allocation of 0.5W
                (
                    msg_eq!(SetMaxPowerConsumption(Watts(0.5))),
                    msg_ok_return!(SetMaxPowerConsumption(Watts(0.5))),
                ),
                // On the third iteration, the first node consumes none of the available power
                // (1W), so expect to receive a power allocation of 1W
                (
                    msg_eq!(SetMaxPowerConsumption(Watts(1.0))),
                    msg_ok_return!(SetMaxPowerConsumption(Watts(1.0))),
                ),
            ],
        );

        let thermal_config = ThermalConfig {
            temperature_node: mock_maker.make(
                "TemperatureNode",
                vec![(msg_eq!(GetDriverPath), msg_ok_return!(GetDriverPath(String::new())))],
            ),
            cpu_control_nodes: vec![cpu_node_1, cpu_node_2],
            sys_pwr_handler: mock_maker.make("SysPwrNode", vec![]),
            thermal_load_notify_nodes: vec![mock_maker.make("ThermalLoadNotify", vec![])],
            policy_params: default_policy_params(),
            platform_metrics_node: create_dummy_node(),
        };
        let node_futures = FuturesUnordered::new();
        let node = ThermalPolicyBuilder::new(thermal_config).build(&node_futures).await.unwrap();

        // Distribute 1W of total power across the two CPU nodes. The real test logic happens inside
        // the mock node, where we verify that the expected power amounts are granted to both CPU
        // nodes via the SetMaxPowerConsumption message. Repeat for the number of messages that the
        // mock nodes expect to receive (three).
        node.distribute_power(Watts(1.0)).await.unwrap();
        node.distribute_power(Watts(1.0)).await.unwrap();
        node.distribute_power(Watts(1.0)).await.unwrap();
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let mut mock_maker = MockNodeMaker::new();

        let policy_params = default_policy_params();
        let thermal_config = ThermalConfig {
            temperature_node: mock_maker.make(
                "TemperatureNode",
                vec![(msg_eq!(GetDriverPath), msg_ok_return!(GetDriverPath(String::new())))],
            ),
            cpu_control_nodes: vec![mock_maker.make("CpuCtrlNode", vec![])],
            sys_pwr_handler: mock_maker.make("SysPwrNode", vec![]),
            thermal_load_notify_nodes: vec![mock_maker.make("ThermalLoadNotify", vec![])],
            policy_params: default_policy_params(),
            platform_metrics_node: create_dummy_node(),
        };
        let inspector = inspect::Inspector::new();
        let node_futures = FuturesUnordered::new();
        let _node = ThermalPolicyBuilder::new(thermal_config)
            .with_inspect_root(inspector.root())
            .build(&node_futures)
            .await
            .unwrap();

        assert_data_tree!(
            inspector,
            root: {
                ThermalPolicy: {
                    state: contains {},
                    stats: contains {},
                    policy_params: {
                        controller_params: {
                            "sample_interval (s)":
                                policy_params.controller_params.sample_interval.0,
                            "filter_time_constant (s)":
                                policy_params.controller_params.filter_time_constant.0,
                            "target_temperature (C)":
                                policy_params.controller_params.target_temperature.0,
                            "e_integral_min": policy_params.controller_params.e_integral_min,
                            "e_integral_max": policy_params.controller_params.e_integral_max,
                            "sustainable_power (W)":
                                policy_params.controller_params.sustainable_power.0,
                            "proportional_gain": policy_params.controller_params.proportional_gain,
                            "integral_gain": policy_params.controller_params.integral_gain,
                        }
                    }
                }
            }
        );
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "ThermalPolicy",
            "name": "thermal_policy",
            "config": {
                "thermal_shutdown_temperature": 95.0,
                "controller_params": {
                    "sample_interval": 1.0,
                    "filter_time_constant": 5.0,
                    "target_temperature": 80.0,
                    "e_integral_min": -20.0,
                    "e_integral_max": 0.0,
                    "sustainable_power": 0.876,
                    "proportional_gain": 0.0,
                    "integral_gain": 0.08
                }
            },
            "dependencies": {
                "cpu_control_nodes": [
                    "cpu_control"
                ],
                "system_power_handler_node": "sys_power",
                "temperature_handler_node": "temperature",
                "thermal_load_notify_nodes": ["thermal_load_notify"],
                "platform_metrics_node": "metrics"
              },
        });

        let mut nodes: HashMap<String, Rc<dyn Node>> = HashMap::new();
        nodes.insert("temperature".to_string(), create_dummy_node());
        nodes.insert("cpu_control".to_string(), create_dummy_node());
        nodes.insert("sys_power".to_string(), create_dummy_node());
        nodes.insert("thermal_load_notify".to_string(), create_dummy_node());
        nodes.insert("metrics".to_string(), create_dummy_node());
        let _ = ThermalPolicyBuilder::new_from_json(json_data, &nodes);
    }

    /// Tests that the ThermalPolicy correctly updates the PlatformMetrics node as its thermal state
    /// cycles between the various states.
    #[test]
    fn test_platform_metrics() {
        let mut mock_maker = MockNodeMaker::new();

        // Set custom thermal policy parameters to have easier control over throttling state changes
        let mut policy_params = default_policy_params();
        policy_params.controller_params.target_temperature = Celsius(50.0);
        policy_params.controller_params.e_integral_max = 0.0;
        policy_params.controller_params.e_integral_min = -20.0;
        policy_params.thermal_shutdown_temperature = Celsius(80.0);
        policy_params.controller_params.proportional_gain = 0.0;
        policy_params.controller_params.integral_gain = 0.1;
        policy_params.controller_params.sustainable_power = Watts(1.0);
        policy_params.controller_params.filter_time_constant = Seconds(0.0);

        // The executor's fake time must be set before node creation to ensure the periodic timer's
        // deadline is properly initialized.
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        executor.set_fake_time(Seconds(0.0).into());

        let mock_metrics = mock_maker.make("MockPlatformMetrics", vec![]);
        let mock_temperature = mock_maker.make(
            "MockTemperature",
            vec![(msg_eq!(GetDriverPath), msg_ok_return!(GetDriverPath("sensor1".to_string())))],
        );

        let node_futures = FuturesUnordered::new();
        let node_builder = ThermalPolicyBuilder::new(ThermalConfig {
            temperature_node: mock_temperature.clone(),
            cpu_control_nodes: vec![create_dummy_node()],
            sys_pwr_handler: create_dummy_node(),
            thermal_load_notify_nodes: vec![],
            platform_metrics_node: mock_metrics.clone(),
            policy_params,
        })
        .build(&node_futures);

        let _node = match executor.run_until_stalled(&mut node_builder.boxed_local()) {
            futures::task::Poll::Ready(Ok(node)) => node,
            _ => panic!("Failed to create node"),
        };

        // Wrap the executor, node, and futures in a simple struct that drives time steps. This
        // enables the control flow:
        // 1. Set metric expectations (add expected message to mock node(s)).
        // 2. Call TimeStepper::iterate_policy to wake the periodic timer and iterate the
        //    ThermalPolicy.
        // 3. Verify metric expectations.
        struct TimeStepper<'a> {
            executor: fasync::TestExecutor,
            node_futures: FuturesUnordered<LocalBoxFuture<'a, ()>>,
        }

        impl<'a> TimeStepper<'a> {
            fn iterate_policy(&mut self) {
                let wakeup_time = self.executor.wake_next_timer().unwrap();
                self.executor.set_fake_time(wakeup_time);

                assert_eq!(
                    futures::task::Poll::Pending,
                    self.executor.run_until_stalled(&mut self.node_futures.next())
                );
            }
        }

        let mut stepper = TimeStepper { executor, node_futures };

        // Not throttled yet
        mock_temperature.add_msg_response_pair((
            msg_eq!(ReadTemperature),
            msg_ok_return!(ReadTemperature(Celsius(50.0))),
        ));
        mock_metrics.add_msg_response_pair((
            msg_eq!(LogPlatformMetric(PlatformMetric::AvailablePower(Watts(1.0),))),
            msg_ok_return!(LogPlatformMetric),
        ));
        stepper.iterate_policy();

        // Throttling begins because 55 degC is greater than `target_temperature` (50 degC)
        mock_temperature.add_msg_response_pair((
            msg_eq!(ReadTemperature),
            msg_ok_return!(ReadTemperature(Celsius(52.0))),
        ));
        mock_metrics.add_msg_response_pair((
            msg_eq!(LogPlatformMetric(PlatformMetric::AvailablePower(Watts(0.8),))),
            msg_ok_return!(LogPlatformMetric),
        ));
        stepper.iterate_policy();

        // Throttling still active, available power is reduced
        mock_temperature.add_msg_response_pair((
            msg_eq!(ReadTemperature),
            msg_ok_return!(ReadTemperature(Celsius(52.0))),
        ));
        mock_metrics.add_msg_response_pair((
            msg_eq!(LogPlatformMetric(PlatformMetric::AvailablePower(Watts(0.6),))),
            msg_ok_return!(LogPlatformMetric),
        ));
        stepper.iterate_policy();

        // End thermal throttling
        mock_temperature.add_msg_response_pair((
            msg_eq!(ReadTemperature),
            msg_ok_return!(ReadTemperature(Celsius(35.0))),
        ));
        mock_metrics.add_msg_response_pair((
            msg_eq!(LogPlatformMetric(PlatformMetric::AvailablePower(Watts(1.0),))),
            msg_ok_return!(LogPlatformMetric),
        ));
        stepper.iterate_policy();

        // Cause the thermal policy to enter thermal shutdown
        mock_temperature.add_msg_response_pair((
            msg_eq!(ReadTemperature),
            msg_ok_return!(ReadTemperature(Celsius(90.0))),
        ));
        mock_metrics.add_msg_response_pair((
            msg_eq!(LogPlatformMetric(PlatformMetric::ThrottlingResultShutdown)),
            msg_ok_return!(LogPlatformMetric),
        ));

        // On a real system, the shutdown would occur before these messages are sent. But since the
        // test continues executing even after the shutdown request is sent, we need to add them to
        // the expected messages.
        mock_metrics.add_msg_response_pair((
            msg_eq!(LogPlatformMetric(PlatformMetric::AvailablePower(Watts(0.0),))),
            msg_ok_return!(LogPlatformMetric),
        ));
        stepper.iterate_policy();
    }

    #[allow(clippy::unit_cmp)] // TODO(fxbug.dev/95036)
    /// Tests that the ThermalPolicy node populates the correct temperature sensor driver path in
    /// its UpdateThermalLoad messages.
    #[fasync::run_singlethreaded(test)]
    async fn test_thermal_load_sensor_path() {
        let mut mock_maker = MockNodeMaker::new();

        // Set up the ThermalPolicy node
        let thermal_config = ThermalConfig {
            temperature_node: mock_maker.make(
                "TemperatureNode",
                vec![(
                    msg_eq!(GetDriverPath),
                    msg_ok_return!(GetDriverPath("Sensor1".to_string())),
                )],
            ),
            cpu_control_nodes: vec![create_dummy_node()],
            sys_pwr_handler: create_dummy_node(),
            thermal_load_notify_nodes: vec![mock_maker.make(
                "ThermalLoadNotify",
                vec![(
                    msg_eq!(UpdateThermalLoad(ThermalLoad(20), "Sensor1".to_string())),
                    msg_ok_return!(UpdateThermalLoad),
                )],
            )],
            policy_params: default_policy_params(),
            platform_metrics_node: create_dummy_node(),
        };

        let node = ThermalPolicyBuilder::new(thermal_config)
            .build(&FuturesUnordered::new())
            .await
            .unwrap();

        // When `process_thermal_load` runs, the new ThermalLoad value will be sent to the
        // ThermalLoadNotify node. The mock will verify the correct sensor path is found in the
        // UpdateThermalLoad message.
        assert_eq!(node.process_thermal_load(ThermalLoad(20)).await.unwrap(), ());
    }

    #[allow(clippy::unit_cmp)] // TODO(fxbug.dev/95036)
    /// Tests that each of the configured `thermal_load_notify_nodes` nodes receive an
    /// UpdateThermalLoad message as expected.
    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_thermal_load_notify_nodes() {
        let mut mock_maker = MockNodeMaker::new();

        let mock_notify1 = mock_maker.make("ThermalLoadNotify1", vec![]);
        let mock_notify2 = mock_maker.make("ThermalLoadNotify2", vec![]);

        // Set up the ThermalPolicy node
        let thermal_config = ThermalConfig {
            temperature_node: mock_maker.make(
                "TemperatureNode",
                vec![(
                    msg_eq!(GetDriverPath),
                    msg_ok_return!(GetDriverPath("Sensor1".to_string())),
                )],
            ),
            cpu_control_nodes: vec![create_dummy_node()],
            sys_pwr_handler: create_dummy_node(),
            thermal_load_notify_nodes: vec![mock_notify1.clone(), mock_notify2.clone()],
            policy_params: default_policy_params(),
            platform_metrics_node: create_dummy_node(),
        };

        let node = ThermalPolicyBuilder::new(thermal_config)
            .build(&FuturesUnordered::new())
            .await
            .unwrap();

        // When `process_thermal_load` runs, the mocks will verify they each receive the
        // UpdateThermalLoad message
        mock_notify1.add_msg_response_pair((
            msg_eq!(UpdateThermalLoad(ThermalLoad(20), "Sensor1".to_string())),
            msg_ok_return!(UpdateThermalLoad),
        ));
        mock_notify2.add_msg_response_pair((
            msg_eq!(UpdateThermalLoad(ThermalLoad(20), "Sensor1".to_string())),
            msg_ok_return!(UpdateThermalLoad),
        ));
        assert_eq!(node.process_thermal_load(ThermalLoad(20)).await.unwrap(), ());

        // Even if thermal load is unchanged, the nodes should still be updated
        mock_notify1.add_msg_response_pair((
            msg_eq!(UpdateThermalLoad(ThermalLoad(20), "Sensor1".to_string())),
            msg_ok_return!(UpdateThermalLoad),
        ));
        mock_notify2.add_msg_response_pair((
            msg_eq!(UpdateThermalLoad(ThermalLoad(20), "Sensor1".to_string())),
            msg_ok_return!(UpdateThermalLoad),
        ));
        assert_eq!(node.process_thermal_load(ThermalLoad(20)).await.unwrap(), ());
    }
}
