// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::thermal_limiter;
use crate::types::{Celsius, Nanoseconds, Seconds, ThermalLoad, Watts};
use async_trait::async_trait;
use failure::{format_err, Error};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_vlog};
use fuchsia_zircon as zx;
use futures::prelude::*;
use std::cell::Cell;
use std::rc::Rc;

/// Node: ThermalPolicy
///
/// Summary: Implements the closed loop thermal control policy for the system
///
/// Message Inputs: N/A
///
/// Message Outputs:
///     - ReadTemperature
///     - SetMaxPowerConsumption
///     - SystemShutdown
///
/// FIDL dependencies: N/A

pub struct ThermalPolicy {
    config: ThermalConfig,
    state: ThermalState,
}

/// A struct to store all configurable aspects of the ThermalPolicy node
pub struct ThermalConfig {
    /// The node to provide temperature readings for the thermal control loop. It is expected that
    /// this node responds to the ReadTemperature message.
    pub temperature_node: Rc<dyn Node>,

    /// The node to impose limits on the CPU power state. It is expected that this node responds to
    /// the SetMaxPowerConsumption message.
    pub cpu_control_node: Rc<dyn Node>,

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
    pub thermal_limit_begin_temperature: Celsius,

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

    /// The temperature reading from the previous controller iteration
    prev_temperature: Cell<Celsius>,

    /// The integral error [degC * s] that is accumulated across controller iterations
    error_integral: Cell<f64>,

    /// A flag to know if the rest of ThermalState has not been initialized yet
    state_initialized: Cell<bool>,

    /// A cached value in the range [0 - MAX_THERMAL_LOAD] which is defined as
    /// ((temperature - range_start) / (range_end - range_start) * MAX_THERMAL_LOAD), where
    /// range_start and range_end are the thermal_limit_begin_temperature and
    /// thermal_shutdown_temperature, respectively.
    thermal_load: Cell<ThermalLoad>,
}

impl ThermalPolicy {
    pub fn new(config: ThermalConfig) -> Result<Rc<Self>, Error> {
        let node = Rc::new(Self {
            config,
            state: ThermalState {
                prev_timestamp: Cell::new(Nanoseconds(0)),
                prev_temperature: Cell::new(Celsius(0.0)),
                error_integral: Cell::new(0.0),
                state_initialized: Cell::new(false),
                thermal_load: Cell::new(ThermalLoad(0)),
            },
        });
        node.clone().start_periodic_thermal_loop()?;
        Ok(node)
    }

    /// Starts a periodic timer that fires at the interval specified by
    /// ThermalControllerParams.sample_interval. At each timer, `iterate_thermal_control` is called
    /// and any resulting errors are logged.
    fn start_periodic_thermal_loop(self: Rc<Self>) -> Result<(), Error> {
        let mut periodic_timer = fasync::Interval::new(zx::Duration::from_nanos(
            self.config.policy_params.controller_params.sample_interval.into_nanos(),
        ));

        fasync::spawn_local(async move {
            while let Some(()) = periodic_timer.next().await {
                log_if_error(
                    self.iterate_thermal_control().await,
                    "Error while running thermal control iteration",
                );
            }
        });

        Ok(())
    }

    /// This is the main body of the closed loop thermal control logic. The function is called
    /// periodically by the timer started in `start_periodic_thermal_loop`. For each iteration, the
    /// following steps will be taken:
    ///     1. Read the current temperature from the temperature driver specified in ThermalConfig
    ///     2. Filter the raw temperature value using a low-pass filter
    ///     3. Use the new filtered temperature value as input to the PID control algorithm
    ///     4. The PID algorithm outputs the available power limit to impose in the system
    ///     5. Distribute the available power to the power actors (initially this is only the CPU)
    async fn iterate_thermal_control(&self) -> Result<(), Error> {
        let raw_temperature = self.get_temperature().await?;

        // Record the timestamp for this iteration now that we have all the data we need to proceed
        let timestamp = Nanoseconds(fasync::Time::now().into_nanos());

        // We should have run the iteration at least once before proceeding
        if !self.state.state_initialized.get() {
            self.state.prev_temperature.set(raw_temperature);
            self.state.prev_timestamp.set(timestamp);
            self.state.state_initialized.set(true);
            return Ok(());
        }

        let time_delta = Seconds::from_nanos(timestamp.0 - self.state.prev_timestamp.get().0);
        self.state.prev_timestamp.set(timestamp);

        let filtered_temperature = Celsius(low_pass_filter(
            raw_temperature.0,
            self.state.prev_temperature.get().0,
            time_delta.0,
            self.config.policy_params.controller_params.filter_time_constant.0,
        ));
        self.state.prev_temperature.set(filtered_temperature);

        fx_vlog!(
            1,
            "iteration_period={}s; filtered_temperature={}C; raw_temperature={}C",
            time_delta.0,
            filtered_temperature.0,
            raw_temperature.0
        );

        // If the new temperature is above the critical threshold then shutdown the system
        log_if_error(
            self.check_critical_temperature(filtered_temperature).await,
            "Error checking critical temperature",
        );

        // Update the ThermalLimiter node with the latest thermal load
        log_if_error(
            self.update_thermal_load(filtered_temperature).await,
            "Error updating thermal load",
        );

        // Run the thermal feedback controller
        log_if_error(
            self.iterate_controller(filtered_temperature, time_delta).await,
            "Error running thermal feedback controller",
        );

        Ok(())
    }

    /// Query the current temperature from the temperature handler node
    async fn get_temperature(&self) -> Result<Celsius, Error> {
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
        // Temperature has exceeded the thermal shutdown temperature
        if temperature.0 >= self.config.policy_params.thermal_shutdown_temperature.0 {
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
    async fn update_thermal_load(&self, temperature: Celsius) -> Result<(), Error> {
        let thermal_load = Self::calculate_thermal_load(
            temperature,
            self.config.policy_params.thermal_limit_begin_temperature,
            self.config.policy_params.thermal_shutdown_temperature,
        );
        if thermal_load != self.state.thermal_load.get() {
            self.state.thermal_load.set(thermal_load);
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
    fn calculate_thermal_load(
        temperature: Celsius,
        range_start: Celsius,
        range_end: Celsius,
    ) -> ThermalLoad {
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
        let available_power = self.calculate_available_power(filtered_temperature, time_delta);
        fx_vlog!(1, "available_power={}W", available_power.0);
        self.distribute_power(available_power).await
    }

    /// A PID control algorithm that uses temperature as the measured process variable, and
    /// available power as the control variable. Each call to the function will also
    /// update the state variable `error_integral` to be used on subsequent iterations.
    fn calculate_available_power(&self, temperature: Celsius, time_delta: Seconds) -> Watts {
        let controller_params = &self.config.policy_params.controller_params;
        let temperature_error = controller_params.target_temperature.0 - temperature.0;
        let error_integral = clamp(
            self.state.error_integral.get() + temperature_error * time_delta.0,
            controller_params.e_integral_min,
            controller_params.e_integral_max,
        );
        self.state.error_integral.set(error_integral);

        let p_term = temperature_error * controller_params.proportional_gain;
        let i_term = error_integral * controller_params.integral_gain;
        let power_available = controller_params.sustainable_power.0 + p_term + i_term;

        Watts(power_available)
    }

    /// This function is responsible for distributing the available power (as determined by the
    /// prior PID calculation) to the various power actors that are included in this closed loop
    /// system. Initially, CPU is the only power actor. In later versions of the thermal policy,
    /// there may be more power actors with associated "weights" for distributing power amongst
    /// them.
    async fn distribute_power(&self, available_power: Watts) -> Result<(), Error> {
        let message = Message::SetMaxPowerConsumption(available_power);
        self.send_message(&self.config.cpu_control_node, &message).await?;
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

fn log_if_error<T>(result: Result<T, Error>, prefix: &'static str) {
    if let Err(e) = result {
        fx_log_err!("{}: {}", prefix, e);
    }
}

#[async_trait(?Send)]
impl Node for ThermalPolicy {
    fn name(&self) -> &'static str {
        "ThermalPolicy"
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, Error> {
        match msg {
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::{Farads, Hertz, Volts};
    use crate::{
        cpu_control_handler, cpu_stats_handler, system_power_handler, temperature_handler,
    };
    use cpu_control_handler::PState;
    use rkf45;
    use std::cell::RefCell;

    #[derive(Clone, Debug)]
    struct SimulatedCpuParams {
        num_cpus: usize,
        p_states: Vec<PState>,
        capacitance: Farads,
    }

    /// Parameters for a linear thermal model including a CPU, heat sink, and environment.
    /// For simplicity, we assume heat flow directly between the CPU and environment is negligible.
    #[derive(Clone, Debug)]
    struct ThermalModelParams {
        /// Thermal energy transfer rate [W/deg C] between CPU and heat sink.
        cpu_to_heat_sink_thermal_rate: f64,
        /// Thermal energy transfer rate [W/deg C] between heat sink and environment.
        heat_sink_to_env_thermal_rate: f64,
        /// Thermal capacity [J/deg C] of the CPU.
        cpu_thermal_capacity: f64,
        /// Thermal capacity [J/deg C] of the heat sink.
        heat_sink_thermal_capacity: f64,
    }

    struct Simulator {
        /// CPU temperature.
        cpu_temperature: Celsius,
        /// Heat sink temperature.
        heat_sink_temperature: Celsius,
        /// Environment temperature.
        environment_temperature: Celsius,
        /// Simulated time.
        time: Seconds,
        /// Rate of operations sent to the CPU, scheduled as a function of time.
        operation_rate_schedule: Box<dyn Fn(Seconds) -> Hertz>,
        /// Accumulated idle time on each simulated CPU.
        idle_times: Vec<Nanoseconds>,
        /// Parameters for the simulated CPUs.
        cpu_params: SimulatedCpuParams,
        /// Index of the simulated CPUs' current P-state.
        p_state_index: usize,
        /// Parameters for the thermal dynamics model.
        thermal_model_params: ThermalModelParams,
        /// Whether the shutdown signal has been applied.
        shutdown_applied: bool,
    }

    /// Initialization parameters for a new Simulator.
    struct SimulatorParams {
        /// Parameters for the underlying thermal model.
        thermal_model_params: ThermalModelParams,
        /// Parameters for the simulated CPU.
        cpu_params: SimulatedCpuParams,
        /// Rate of operations sent to the CPU, scheduled as a function of time.
        operation_rate_schedule: Box<dyn Fn(Seconds) -> Hertz>,
        /// Initial temperature of the CPU.
        initial_cpu_temperature: Celsius,
        /// Initial temperature of the heat sink.
        initial_heat_sink_temperature: Celsius,
        /// Temperature of the environment (constant).
        environment_temperature: Celsius,
    }

    impl Simulator {
        /// Creates a new Simulator.
        fn new(p: SimulatorParams) -> Rc<RefCell<Self>> {
            Rc::new(RefCell::new(Self {
                cpu_temperature: p.initial_cpu_temperature,
                heat_sink_temperature: p.initial_heat_sink_temperature,
                environment_temperature: p.environment_temperature,
                time: Seconds(0.0),
                operation_rate_schedule: p.operation_rate_schedule,
                idle_times: vec![Nanoseconds(0); p.cpu_params.num_cpus],
                p_state_index: 0,
                thermal_model_params: p.thermal_model_params,
                cpu_params: p.cpu_params,
                shutdown_applied: false,
            }))
        }

        /// Returns the power consumed by the simulated CPU at the indicated P-state and operation
        /// rate.
        fn get_cpu_power(&self, p_state_index: usize, operation_rate: Hertz) -> Watts {
            cpu_control_handler::get_cpu_power(
                self.cpu_params.capacitance,
                self.cpu_params.p_states[p_state_index].voltage,
                operation_rate,
            )
        }

        /// Returns the steady-state temperature of the CPU for the provided power consumption.
        /// This assumes all energy consumed is converted into thermal energy.
        fn get_steady_state_cpu_temperature(&self, power: Watts) -> Celsius {
            self.environment_temperature
                + Celsius(
                    (1.0 / self.thermal_model_params.cpu_to_heat_sink_thermal_rate
                        + 1.0 / self.thermal_model_params.heat_sink_to_env_thermal_rate)
                        * power.0,
                )
        }

        /// Returns a closure to fetch CPU temperature, for a temperature handler test node.
        fn make_temperature_fetcher(sim: &Rc<RefCell<Self>>) -> impl FnMut() -> Celsius {
            let s = sim.clone();
            move || s.borrow().cpu_temperature
        }

        /// Returns a closure to fetch idle times, for a CPU stats handler test node.
        fn make_idle_times_fetcher(sim: &Rc<RefCell<Self>>) -> impl FnMut() -> Vec<Nanoseconds> {
            let s = sim.clone();
            move || s.borrow().idle_times.clone()
        }

        /// Returns a closure to set the simulator's P-state, for a CPU control handler test node.
        fn make_p_state_setter(
            sim: &Rc<RefCell<Self>>,
        ) -> Box<dyn cpu_control_handler::SetPStateIndex> {
            struct PStateSetter {
                sim: Rc<RefCell<Simulator>>,
            }
            impl cpu_control_handler::SetPStateIndex for PStateSetter {
                fn set_p_state_index(&mut self, index: usize) {
                    self.sim.borrow_mut().p_state_index = index;
                }
            }
            Box::new(PStateSetter { sim: sim.clone() })
        }

        fn make_shutdown_function(sim: &Rc<RefCell<Self>>) -> impl FnMut() {
            let s = sim.clone();
            move || {
                s.borrow_mut().shutdown_applied = true;
            }
        }

        /// Steps the simulator ahead in time by `dt`.
        fn step(&mut self, dt: Seconds) {
            let num_operations = (self.operation_rate_schedule)(self.time).0 * dt.0;
            self.step_thermal_model(dt, num_operations);
            self.step_idle_times(dt, num_operations);
            self.time += dt;
        }

        /// Returns the current P-state of the simulated CPU.
        fn get_p_state(&self) -> &PState {
            &self.cpu_params.p_states[self.p_state_index]
        }

        /// Steps the thermal model ahead in time by `dt`.
        fn step_thermal_model(&mut self, dt: Seconds, num_operations: f64) {
            // Define the derivative closure for `rkf45_adaptive`.
            let p = &self.thermal_model_params;
            let dydt = |_t: f64, y: &[f64]| -> Vec<f64> {
                // Aliases for convenience. `0` refers to the CPU and `1` refers to the heat sink,
                // corresponding to their indices in the `temperatures` array passed to
                // rkf45_adaptive.
                let a01 = p.cpu_to_heat_sink_thermal_rate;
                let a1env = p.heat_sink_to_env_thermal_rate;
                let c0 = p.cpu_thermal_capacity;
                let c1 = p.heat_sink_thermal_capacity;

                let power = self.get_cpu_power(self.p_state_index, num_operations / dt);
                vec![
                    (a01 * (y[1] - y[0]) + power.0) / c0,
                    (a01 * (y[0] - y[1]) + a1env * (self.environment_temperature.0 - y[1])) / c1,
                ]
            };

            // Configure `rkf45_adaptive`.
            //
            // The choice for `dt_initial` is currently naive. Given the need, we could try to
            // choose it more intelligently to avoide some discarded time steps in `rkf45_adaptive.`
            //
            // `error_control` is chosen to keep errors near f32 machine epsilon.
            let solver_options = rkf45::AdaptiveOdeSolverOptions {
                t_initial: self.time.0,
                t_final: (self.time + dt).0,
                dt_initial: dt.0,
                error_control: rkf45::ErrorControlOptions::simple(1e-8),
            };

            // Run `rkf45_adaptive`, and update the simulated temperatures.
            let mut temperatures = [self.cpu_temperature.0, self.heat_sink_temperature.0];
            rkf45::rkf45_adaptive(&mut temperatures, &dydt, &solver_options).unwrap();
            self.cpu_temperature = Celsius(temperatures[0]);
            self.heat_sink_temperature = Celsius(temperatures[1]);
        }

        /// Steps accumulated idle times of the simulated CPUs ahead by `dt`.
        fn step_idle_times(&mut self, dt: Seconds, num_operations: f64) {
            let frequency = self.get_p_state().frequency;
            let total_cpu_time = num_operations / frequency;
            let active_time_per_core = total_cpu_time.div_scalar(self.cpu_params.num_cpus as f64);

            // For now, we require that the cores do not saturate.
            assert!(active_time_per_core <= dt);

            let idle_time_per_core = dt - active_time_per_core;
            self.idle_times
                .iter_mut()
                .for_each(|x| *x += Nanoseconds(idle_time_per_core.into_nanos()));
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
        let thermal_limit_range_start = Celsius(85.0);
        let thermal_limit_range_stop = Celsius(95.0);

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
                    thermal_limit_range_start,
                    thermal_limit_range_stop
                ),
                test_case.thermal_load
            );
        }
    }

    /// Coordinates execution of tests of ThermalPolicy.
    struct ThermalPolicyTest {
        executor: fasync::Executor,
        time: Seconds,
        thermal_policy: Rc<ThermalPolicy>,
        sim: Rc<RefCell<Simulator>>,
    }

    impl ThermalPolicyTest {
        /// Iniitalizes a new ThermalPolicyTest.
        fn new(sim_params: SimulatorParams, policy_params: ThermalPolicyParams) -> Self {
            let time = Seconds(0.0);
            let mut executor = fasync::Executor::new_with_fake_time().unwrap();
            executor.set_fake_time(fasync::Time::from_nanos(time.into_nanos()));

            let cpu_params = sim_params.cpu_params.clone();
            let sim = Simulator::new(sim_params);
            let thermal_policy = Self::init_thermal_policy(&sim, &cpu_params, policy_params);

            // Run the thermal policy once to initialize. The future must be dropped to eliminate
            // its borrow of `thermal_policy`.
            let mut future = Box::pin(async {
                thermal_policy.iterate_thermal_control().await.unwrap();
            });
            assert!(executor.run_until_stalled(&mut future).is_ready());
            drop(future);

            Self { executor, time, sim, thermal_policy }
        }

        /// Initializes the ThermalPolicy. Helper function for new().
        fn init_thermal_policy(
            sim: &Rc<RefCell<Simulator>>,
            cpu_params: &SimulatedCpuParams,
            policy_params: ThermalPolicyParams,
        ) -> Rc<ThermalPolicy> {
            let temperature_node = temperature_handler::tests::setup_test_node(
                Simulator::make_temperature_fetcher(&sim),
            );
            let cpu_stats_node =
                cpu_stats_handler::tests::setup_test_node(Simulator::make_idle_times_fetcher(&sim));
            let sys_pwr_handler = system_power_handler::tests::setup_test_node(
                Simulator::make_shutdown_function(&sim),
            );

            // Note that the model capacitance used by the control node could differ from the one
            // used by the simulator. This could be leveraged to simulate discrepancies between
            // the on-device power model (in the thermal policy) and reality (as represented by the
            // simulator).
            let cpu_control_params = cpu_control_handler::CpuControlParams {
                p_states: cpu_params.p_states.clone(),
                capacitance: cpu_params.capacitance,
            };
            let cpu_control_node = cpu_control_handler::tests::setup_test_node(
                cpu_control_params,
                cpu_stats_node.clone(),
                Simulator::make_p_state_setter(&sim),
            );

            let thermal_limiter_node = thermal_limiter::tests::setup_test_node();

            let thermal_config = ThermalConfig {
                temperature_node,
                cpu_control_node,
                sys_pwr_handler,
                thermal_limiter_node,
                policy_params,
            };
            ThermalPolicy::new(thermal_config).unwrap()
        }

        /// Iterates the policy n times.
        fn iterate_n_times(&mut self, n: u32) {
            let dt = self.thermal_policy.config.policy_params.controller_params.sample_interval;

            for _ in 0..n {
                self.time += dt;
                self.executor.set_fake_time(fasync::Time::from_nanos(self.time.into_nanos()));
                self.sim.borrow_mut().step(dt);
                // In the future below, the compiler would see `self.thermal_policy` as triggering
                // an immutable borrow of `self.executor`, which cannot occur within
                // `self.executor.run_until_stalled`. Pulling out the reference here avoids that
                // problem.
                let thermal_policy = &mut self.thermal_policy;
                assert!(self
                    .executor
                    .run_until_stalled(&mut Box::pin(async {
                        thermal_policy.iterate_thermal_control().await.unwrap();
                    }))
                    .is_ready());
            }
        }
    }

    fn default_cpu_params() -> SimulatedCpuParams {
        SimulatedCpuParams {
            num_cpus: 4,
            p_states: vec![
                PState { frequency: Hertz(2.0e9), voltage: Volts(1.0) },
                PState { frequency: Hertz(1.5e9), voltage: Volts(0.8) },
                PState { frequency: Hertz(1.2e9), voltage: Volts(0.7) },
            ],
            capacitance: Farads(150.0e-12),
        }
    }

    fn default_thermal_model_params() -> ThermalModelParams {
        ThermalModelParams {
            cpu_to_heat_sink_thermal_rate: 0.14,
            heat_sink_to_env_thermal_rate: 0.035,
            cpu_thermal_capacity: 0.003,
            heat_sink_thermal_capacity: 28.0,
        }
    }

    fn default_policy_params() -> ThermalPolicyParams {
        ThermalPolicyParams {
            controller_params: ThermalControllerParams {
                // NOTE: Many tests invoke `iterate_n_times` under the assumption that this interval
                // is 1 second, at least in their comments.
                sample_interval: Seconds(1.0),
                filter_time_constant: Seconds(10.0),
                target_temperature: Celsius(85.0),
                e_integral_min: -20.0,
                e_integral_max: 0.0,
                sustainable_power: Watts(1.1),
                proportional_gain: 0.0,
                integral_gain: 0.2,
            },
            thermal_limit_begin_temperature: Celsius(85.0),
            thermal_shutdown_temperature: Celsius(95.0),
        }
    }

    // Verifies that the simulated CPU follows expected fast-scale thermal dynamics.
    #[test]
    fn test_fast_scale_thermal_dynamics() {
        // Use a fixed operation rate for this test.
        let operation_rate = Hertz(3e9);

        let mut test = ThermalPolicyTest::new(
            SimulatorParams {
                thermal_model_params: default_thermal_model_params(),
                cpu_params: default_cpu_params(),
                operation_rate_schedule: Box::new(move |_| operation_rate),
                initial_cpu_temperature: Celsius(30.0),
                initial_heat_sink_temperature: Celsius(30.0),
                environment_temperature: Celsius(22.0),
            },
            default_policy_params(),
        );

        // After ten seconds with no intervention by the thermal policy, the CPU temperature should
        // be very close to the value dictated by the fast-scale thermal dynamics.
        test.iterate_n_times(10);
        let sim = test.sim.borrow();
        let power = sim.get_cpu_power(0, operation_rate);
        let target_temp = sim.heat_sink_temperature.0
            + power.0 / sim.thermal_model_params.cpu_to_heat_sink_thermal_rate;
        assert!((target_temp - sim.cpu_temperature.0).abs() < 1e-3);
    }

    // Verifies that when the system runs consistently over the target temeprature, the CPU will
    // be driven to its lowest-power P-state.
    #[test]
    fn test_use_lowest_p_state_when_hot() {
        let policy_params = default_policy_params();
        let target_temperature = policy_params.controller_params.target_temperature;

        let mut test = ThermalPolicyTest::new(
            SimulatorParams {
                thermal_model_params: default_thermal_model_params(),
                cpu_params: default_cpu_params(),
                operation_rate_schedule: Box::new(move |_| Hertz(3e9)),
                initial_cpu_temperature: target_temperature,
                initial_heat_sink_temperature: target_temperature,
                environment_temperature: target_temperature,
            },
            default_policy_params(),
        );

        // Within a relatively short time, the integral error should accumulate enough to drive
        // the CPU to its lowest-power P-state.
        test.iterate_n_times(10);
        let s = test.sim.borrow();
        assert_eq!(s.p_state_index, s.cpu_params.p_states.len() - 1);
    }

    // Verifies that system shutdown is issued at a sufficiently high temperature. We set the
    // environment temperature to the shutdown temperature to ensure that the CPU temperature
    // will be driven high enough.
    #[test]
    fn test_shutdown() {
        let policy_params = default_policy_params();
        let shutdown_temperature = policy_params.thermal_shutdown_temperature;

        let mut test = ThermalPolicyTest::new(
            SimulatorParams {
                thermal_model_params: default_thermal_model_params(),
                cpu_params: default_cpu_params(),
                operation_rate_schedule: Box::new(move |_| Hertz(3e9)),
                initial_cpu_temperature: shutdown_temperature - Celsius(10.0),
                initial_heat_sink_temperature: shutdown_temperature - Celsius(10.0),
                environment_temperature: shutdown_temperature,
            },
            policy_params,
        );

        let mut shutdown_verified = false;
        for _ in 0..3600 {
            test.iterate_n_times(1);
            if test.sim.borrow().cpu_temperature.0 >= shutdown_temperature.0 {
                // Now that the simulated CPU temperature is above the shutdown threshold, run for
                // 20 more seconds to allow the policy's temperature filter to reach the threshold
                // as well.
                test.iterate_n_times(20);
                assert!(test.sim.borrow().shutdown_applied);
                shutdown_verified = true;
                break;
            }
        }
        assert!(shutdown_verified);
    }

    // Tests that under a constant operation rate, the thermal policy drives the average CPU
    // temperature to the target temperature.
    #[test]
    fn test_average_temperature() {
        let policy_params = default_policy_params();
        let target_temperature = policy_params.controller_params.target_temperature;

        // Use a fixed operation rate for this test.
        let operation_rate = Hertz(3e9);

        let mut test = ThermalPolicyTest::new(
            SimulatorParams {
                thermal_model_params: default_thermal_model_params(),
                cpu_params: default_cpu_params(),
                operation_rate_schedule: Box::new(move |_| operation_rate),
                initial_cpu_temperature: Celsius(80.0),
                initial_heat_sink_temperature: Celsius(80.0),
                environment_temperature: Celsius(75.0),
            },
            policy_params,
        );

        // Make sure that for the operation rate we're using, the steady-state temperature for the
        // highest-power P-state is above the target temperature, while the one for the
        // lowest-power P-state is below it.
        {
            // This borrow must be dropped before calling test.iterate_n_times, which mutably
            // borrows `sim`.
            let s = test.sim.borrow();
            assert!(
                s.get_steady_state_cpu_temperature(s.get_cpu_power(0, operation_rate))
                    > target_temperature
            );
            assert!(
                s.get_steady_state_cpu_temperature(
                    s.get_cpu_power(s.cpu_params.p_states.len() - 1, operation_rate)
                ) < target_temperature
            );
        }

        // Warm up for 30 minutes of simulated time.
        test.iterate_n_times(1800);

        // Calculate the average CPU temperature over the next 100 iterations, and ensure that it's
        // close to the target temperature.
        let average_temperature = {
            let mut cumulative_sum = 0.0;
            for _ in 0..100 {
                test.iterate_n_times(1);
                cumulative_sum += test.sim.borrow().cpu_temperature.0;
            }
            cumulative_sum / 100.0
        };
        assert!((average_temperature - target_temperature.0).abs() < 0.1);
    }
}
