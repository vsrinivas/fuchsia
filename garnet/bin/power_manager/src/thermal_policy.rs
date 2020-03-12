// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::thermal_limiter;
use crate::types::{Celsius, Nanoseconds, Seconds, ThermalLoad, Watts};
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, ArrayProperty, Property};
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use futures::prelude::*;
use std::cell::Cell;
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

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    pub fn build(self) -> Result<Rc<ThermalPolicy>, Error> {
        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        let node = Rc::new(ThermalPolicy {
            config: self.config,
            state: ThermalState {
                prev_timestamp: Cell::new(Nanoseconds(0)),
                max_time_delta: Cell::new(Seconds(0.0)),
                prev_temperature: Cell::new(Celsius(0.0)),
                error_integral: Cell::new(0.0),
                state_initialized: Cell::new(false),
                thermal_load: Cell::new(ThermalLoad(0)),
            },
            inspect: InspectData::new(inspect_root, "ThermalPolicy".to_string()),
        });

        node.inspect.set_thermal_config(&node.config);
        node.clone().start_periodic_thermal_loop();
        Ok(node)
    }
}

pub struct ThermalPolicy {
    config: ThermalConfig,
    state: ThermalState,

    /// A struct for managing Component Inspection data
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

    /// The node which will impose thermal limits on external clients according to the thermal
    /// load of the system. It is expected that this node responds to the UpdateThermalLoad
    /// message.
    pub thermal_limiter_node: Rc<dyn Node>,

    /// All parameter values relating to the thermal policy itself
    pub policy_params: ThermalPolicyParams,
}

/// A struct to store all configurable aspects of the thermal policy itself
pub struct ThermalPolicyParams {
    /// The thermal control loop parameters
    pub controller_params: ThermalControllerParams,

    /// The temperature at which to begin limiting external subsystems which are not managed by the
    /// thermal feedback controller
    pub thermal_limiting_range: [Celsius; 2],

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

    /// Target temperature for the PID control calculation
    pub target_temperature: Celsius,

    /// Minimum integral error [degC * s] for the PID control calculation
    pub e_integral_min: f64,

    /// Maximum integral error [degC * s] for the PID control calculation
    pub e_integral_max: f64,

    /// The available power when there is no temperature error
    pub sustainable_power: Watts,

    /// The proportional gain [W / degC] for the PID control calculation
    pub proportional_gain: f64,

    /// The integral gain [W / (degC * s)] for the PID control calculation
    pub integral_gain: f64,
}

/// State information that is used for calculations across controller iterations
struct ThermalState {
    /// The time of the previous controller iteration
    prev_timestamp: Cell<Nanoseconds>,

    /// The largest observed time between controller iterations (may be used to detect hangs)
    max_time_delta: Cell<Seconds>,

    /// The temperature reading from the previous controller iteration
    prev_temperature: Cell<Celsius>,

    /// The integral error [degC * s] that is accumulated across controller iterations
    error_integral: Cell<f64>,

    /// A flag to know if the rest of ThermalState has not been initialized yet
    state_initialized: Cell<bool>,

    /// A cached value in the range [0 - MAX_THERMAL_LOAD] which is defined as
    /// ((temperature - range_start) / (range_end - range_start) * MAX_THERMAL_LOAD).
    thermal_load: Cell<ThermalLoad>,
}

impl ThermalPolicy {
    /// Starts a periodic timer that fires at the interval specified by
    /// ThermalControllerParams.sample_interval. At each timer, `iterate_thermal_control` is called
    /// and any resulting errors are logged.
    fn start_periodic_thermal_loop(self: Rc<Self>) {
        let mut periodic_timer = fasync::Interval::new(zx::Duration::from_nanos(
            self.config.policy_params.controller_params.sample_interval.into_nanos(),
        ));

        fasync::spawn_local(async move {
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
        });
    }

    /// This is the main body of the closed loop thermal control logic. The function is called
    /// periodically by the timer started in `start_periodic_thermal_loop`. For each iteration, the
    /// following steps will be taken:
    ///     1. Read the current temperature from the temperature driver specified in ThermalConfig
    ///     2. Filter the raw temperature value using a low-pass filter
    ///     3. Use the new filtered temperature value as input to the PID control algorithm
    ///     4. The PID algorithm outputs the available power limit to impose in the system
    ///     5. Distribute the available power to the power actors (initially this is only the CPU)
    pub async fn iterate_thermal_control(&self) -> Result<(), Error> {
        fuchsia_trace::duration!("power_manager", "ThermalPolicy::iterate_thermal_control");

        let raw_temperature = self.get_temperature().await?;

        // Record the timestamp for this iteration now that we have all the data we need to proceed
        let timestamp = Nanoseconds(fasync::Time::now().into_nanos());

        // We should have run the iteration at least once before proceeding
        if !self.state.state_initialized.get() {
            self.state.prev_temperature.set(raw_temperature);
            self.state.prev_timestamp.set(timestamp);
            self.state.state_initialized.set(true);
            self.inspect.state_initialized.set(1);
            return Ok(());
        }

        let time_delta = Seconds::from_nanos(timestamp.0 - self.state.prev_timestamp.get().0);
        if time_delta.0 > self.state.max_time_delta.get().0 {
            self.state.max_time_delta.set(time_delta);
            self.inspect.max_time_delta.set(time_delta.0);
        }
        self.state.prev_timestamp.set(timestamp);

        let filtered_temperature = Celsius(low_pass_filter(
            raw_temperature.0,
            self.state.prev_temperature.get().0,
            time_delta.0,
            self.config.policy_params.controller_params.filter_time_constant.0,
        ));
        self.state.prev_temperature.set(filtered_temperature);

        self.inspect.timestamp.set(timestamp.0);
        self.inspect.time_delta.set(time_delta.0);
        self.inspect.temperature_raw.set(raw_temperature.0);
        self.inspect.temperature_filtered.set(filtered_temperature.0);
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

        // If the new temperature is above the critical threshold then shutdown the system
        let result = self.check_critical_temperature(filtered_temperature).await;
        log_if_err!(result, "Error checking critical temperature");
        fuchsia_trace::instant!(
            "power_manager",
            "ThermalPolicy::check_critical_temperature_result",
            fuchsia_trace::Scope::Thread,
            "result" => format!("{:?}", result).as_str()
        );

        // Update the ThermalLimiter node with the latest thermal load
        let result = self.update_thermal_load(timestamp, filtered_temperature).await;
        log_if_err!(result, "Error updating thermal load");
        fuchsia_trace::instant!(
            "power_manager",
            "ThermalPolicy::update_thermal_load_result",
            fuchsia_trace::Scope::Thread,
            "result" => format!("{:?}", result).as_str()
        );

        // Run the thermal feedback controller
        let result = self.iterate_controller(filtered_temperature, time_delta).await;
        log_if_err!(result, "Error running thermal feedback controller");
        fuchsia_trace::instant!(
            "power_manager",
            "ThermalPolicy::iterate_controller_result",
            fuchsia_trace::Scope::Thread,
            "result" => format!("{:?}", result).as_str()
        );

        Ok(())
    }

    /// Query the current temperature from the temperature handler node
    async fn get_temperature(&self) -> Result<Celsius, Error> {
        fuchsia_trace::duration!("power_manager", "ThermalPolicy::get_temperature");
        match self.send_message(&self.config.temperature_node, &Message::ReadTemperature).await {
            Ok(MessageReturn::ReadTemperature(t)) => Ok(t),
            Ok(r) => Err(format_err!("ReadTemperature had unexpected return value: {:?}", r)),
            Err(e) => Err(format_err!("ReadTemperature failed: {:?}", e)),
        }
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
            // TODO(pshickel): We shouldn't ever get an error here. But we should probably have
            // some type of fallback or secondary mechanism of halting the system if it somehow
            // does happen. This could have physical safety implications.
            self.send_message(
                &self.config.sys_pwr_handler,
                &Message::SystemShutdown(
                    format!(
                        "Exceeded thermal limit ({}C > {}C)",
                        temperature.0, self.config.policy_params.thermal_shutdown_temperature.0
                    )
                    .to_string(),
                ),
            )
            .await
            .map_err(|e| format_err!("Failed to shutdown the system: {}", e))?;
        }

        Ok(())
    }

    /// Determines the current thermal load. If there is a change from the cached thermal_load,
    /// then the new value is sent out to the ThermalLimiter node.
    async fn update_thermal_load(
        &self,
        timestamp: Nanoseconds,
        temperature: Celsius,
    ) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalPolicy::update_thermal_load",
            "temperature" => temperature.0
        );
        let thermal_load = Self::calculate_thermal_load(
            temperature,
            &self.config.policy_params.thermal_limiting_range,
        );
        fuchsia_trace::counter!(
            "power_manager",
            "ThermalPolicy thermal_load",
            0,
            "thermal_load" => thermal_load.0
        );
        if thermal_load != self.state.thermal_load.get() {
            fuchsia_trace::instant!(
                "power_manager",
                "ThermalPolicy::thermal_load_changed",
                fuchsia_trace::Scope::Thread,
                "old_load" => self.state.thermal_load.get().0,
                "new_load" => thermal_load.0
            );
            self.state.thermal_load.set(thermal_load);
            self.inspect.thermal_load.set(thermal_load.0.into());
            if thermal_load.0 == 0 {
                self.inspect.last_throttle_end_time.set(timestamp.0);
            }
            self.send_message(
                &self.config.thermal_limiter_node,
                &Message::UpdateThermalLoad(thermal_load),
            )
            .await?;
        }

        Ok(())
    }

    /// Calculates the thermal load which is a value in the range [0 - MAX_THERMAL_LOAD] defined as
    /// ((temperature - range_start) / (range_end - range_start) * MAX_THERMAL_LOAD)
    fn calculate_thermal_load(temperature: Celsius, range: &[Celsius; 2]) -> ThermalLoad {
        let range_start = range[0];
        let range_end = range[1];
        if temperature.0 < range_start.0 {
            ThermalLoad(0)
        } else if temperature.0 > range_end.0 {
            thermal_limiter::MAX_THERMAL_LOAD
        } else {
            ThermalLoad(
                ((temperature.0 - range_start.0) / (range_end.0 - range_start.0)
                    * thermal_limiter::MAX_THERMAL_LOAD.0 as f64) as u32,
            )
        }
    }

    /// Execute the thermal feedback control loop
    async fn iterate_controller(
        &self,
        filtered_temperature: Celsius,
        time_delta: Seconds,
    ) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalPolicy::iterate_controller",
            "filtered_temperature" => filtered_temperature.0,
            "time_delta" => time_delta.0
        );
        let available_power = self.calculate_available_power(filtered_temperature, time_delta);
        self.inspect.available_power.set(available_power.0);
        fuchsia_trace::counter!(
            "power_manager",
            "ThermalPolicy available_power",
            0,
            "available_power" => available_power.0
        );

        self.distribute_power(available_power).await
    }

    /// A PID control algorithm that uses temperature as the measured process variable, and
    /// available power as the control variable. Each call to the function will also
    /// update the state variable `error_integral` to be used on subsequent iterations.
    fn calculate_available_power(&self, temperature: Celsius, time_delta: Seconds) -> Watts {
        fuchsia_trace::duration!(
            "power_manager",
            "ThermalPolicy::calculate_available_power",
            "temperature" => temperature.0,
            "time_delta" => time_delta.0
        );
        let controller_params = &self.config.policy_params.controller_params;
        let temperature_error = controller_params.target_temperature.0 - temperature.0;
        let error_integral = clamp(
            self.state.error_integral.get() + temperature_error * time_delta.0,
            controller_params.e_integral_min,
            controller_params.e_integral_max,
        );
        self.state.error_integral.set(error_integral);
        self.inspect.error_integral.set(error_integral);
        fuchsia_trace::counter!(
            "power_manager",
            "ThermalPolicy error_integral", 0,
            "error_integral" => error_integral
        );

        let p_term = temperature_error * controller_params.proportional_gain;
        let i_term = error_integral * controller_params.integral_gain;
        let power_available =
            f64::max(0.0, controller_params.sustainable_power.0 + p_term + i_term);

        Watts(power_available)
    }

    /// This function is responsible for distributing the available power (as determined by the
    /// prior PID calculation) to the various power actors that are included in this closed loop
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

        // TODO(fxb/48205): We may want to revisit this distribution algorithm to avoid potentially
        // starving some CPU control nodes. We'll want to have some discussions and learn more about
        // intended big.LITTLE scheduling and operation to better inform our decisions here. We may
        // find that we'll need to first query the nodes to learn how much power they intend to use
        // before making allocation decisions.
        for node in &self.config.cpu_control_nodes {
            if let MessageReturn::SetMaxPowerConsumption(power_used) = self
                .send_message(&node, &Message::SetMaxPowerConsumption(total_available_power))
                .await?
            {
                total_available_power = total_available_power - power_used;
            }
        }

        Ok(())
    }
}

fn low_pass_filter(y: f64, y_prev: f64, time_delta: f64, time_constant: f64) -> f64 {
    y_prev + (time_delta / time_constant) * (y - y_prev)
}

fn clamp<T: std::cmp::PartialOrd>(val: T, min: T, max: T) -> T {
    if val < min {
        min
    } else if val > max {
        max
    } else {
        val
    }
}

#[async_trait(?Send)]
impl Node for ThermalPolicy {
    fn name(&self) -> &'static str {
        "ThermalPolicy"
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            _ => Err(PowerManagerError::Unsupported),
        }
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
    state_initialized: inspect::UintProperty,
    thermal_load: inspect::UintProperty,
    available_power: inspect::DoubleProperty,
    max_time_delta: inspect::DoubleProperty,
    last_throttle_end_time: inspect::IntProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, name: String) -> Self {
        // Create a local root node and properties
        let root_node = parent.create_child(name);
        let state_node = root_node.create_child("state");
        let stats_node = root_node.create_child("stats");
        let timestamp = state_node.create_int("timestamp (ns)", 0);
        let time_delta = state_node.create_double("time_delta (s)", 0.0);
        let temperature_raw = state_node.create_double("temperature_raw (C)", 0.0);
        let temperature_filtered = state_node.create_double("temperature_filtered (C)", 0.0);
        let error_integral = state_node.create_double("error_integral", 0.0);
        let state_initialized = state_node.create_uint("state_initialized", 0);
        let thermal_load = state_node.create_uint("thermal_load", 0);
        let available_power = state_node.create_double("available_power (W)", 0.0);
        let last_throttle_end_time = stats_node.create_int("last_throttle_end_time (ns)", 0);
        let max_time_delta = stats_node.create_double("max_time_delta (s)", 0.0);

        // Pass ownership of the new nodes to the root node, otherwise they'll be dropped
        root_node.record(state_node);
        root_node.record(stats_node);

        InspectData {
            root_node,
            timestamp,
            time_delta,
            max_time_delta,
            temperature_raw,
            temperature_filtered,
            error_integral,
            state_initialized,
            thermal_load,
            available_power,
            last_throttle_end_time,
        }
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

        let thermal_range = policy_params_node.create_double_array("thermal_limiting_range (C)", 2);
        thermal_range.set(0, config.policy_params.thermal_limiting_range[0].0);
        thermal_range.set(1, config.policy_params.thermal_limiting_range[1].0);
        policy_params_node.record(thermal_range);

        self.root_node.record(policy_params_node);
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::test::mock_node::{create_mock_node, MessageMatcher};
    use crate::{msg_eq, msg_ok_return};
    use inspect::assert_inspect_tree;

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
            thermal_limiting_range: [Celsius(75.0), Celsius(85.0)],
            thermal_shutdown_temperature: Celsius(95.0),
        }
    }

    #[test]
    fn test_low_pass_filter() {
        let y_0 = 0.0;
        let y_1 = 10.0;
        let time_delta = 1.0;
        let time_constant = 10.0;
        assert_eq!(low_pass_filter(y_1, y_0, time_delta, time_constant), 1.0);
    }

    #[test]
    fn test_calculate_thermal_load() {
        let thermal_limiting_range = [Celsius(85.0), Celsius(95.0)];

        struct TestCase {
            temperature: Celsius,      // observed temperature
            thermal_load: ThermalLoad, // expected thermal load
        };

        let test_cases = vec![
            // before thermal limit range
            TestCase { temperature: Celsius(50.0), thermal_load: ThermalLoad(0) },
            // start of thermal limit range
            TestCase { temperature: Celsius(85.0), thermal_load: ThermalLoad(0) },
            // arbitrary point within thermal limit range
            TestCase { temperature: Celsius(88.0), thermal_load: ThermalLoad(30) },
            // arbitrary point within thermal limit range
            TestCase { temperature: Celsius(93.0), thermal_load: ThermalLoad(80) },
            // end of thermal limit range
            TestCase { temperature: Celsius(95.0), thermal_load: ThermalLoad(100) },
            // beyond thermal limit range
            TestCase { temperature: Celsius(100.0), thermal_load: ThermalLoad(100) },
        ];

        for test_case in test_cases {
            assert_eq!(
                ThermalPolicy::calculate_thermal_load(
                    test_case.temperature,
                    &thermal_limiting_range,
                ),
                test_case.thermal_load
            );
        }
    }

    /// Tests that the ThermalPolicy will correctly divide total available power amongst multiple
    /// CPU control nodes.
    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_cpu_actors() {
        // Setup the two CpuControlHandler mock nodes. The message reply to SetMaxPowerConsumption
        // indicates how much power the mock node was able to utilize, and ultimately drives the
        // test logic.
        let cpu_node_1 = create_mock_node(
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
        let cpu_node_2 = create_mock_node(
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
            temperature_node: create_mock_node("TemperatureNode", vec![]),
            cpu_control_nodes: vec![cpu_node_1, cpu_node_2],
            sys_pwr_handler: create_mock_node("SysPwrNode", vec![]),
            thermal_limiter_node: create_mock_node("ThermalLimiterNode", vec![]),
            policy_params: default_policy_params(),
        };
        let node = ThermalPolicyBuilder::new(thermal_config).build().unwrap();

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
        let policy_params = default_policy_params();
        let thermal_config = ThermalConfig {
            temperature_node: create_mock_node("TemperatureNode", vec![]),
            cpu_control_nodes: vec![create_mock_node("CpuCtrlNode", vec![])],
            sys_pwr_handler: create_mock_node("SysPwrNode", vec![]),
            thermal_limiter_node: create_mock_node("ThermalLimiterNode", vec![]),
            policy_params: default_policy_params(),
        };
        let inspector = inspect::Inspector::new();
        let _node = ThermalPolicyBuilder::new(thermal_config)
            .with_inspect_root(inspector.root())
            .build()
            .unwrap();

        assert_inspect_tree!(
            inspector,
            root: {
                ThermalPolicy: {
                    state: contains {},
                    stats: contains {},
                    policy_params: {
                        "thermal_limiting_range (C)": vec![
                                policy_params.thermal_limiting_range[0].0,
                                policy_params.thermal_limiting_range[1].0
                            ],
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
}
