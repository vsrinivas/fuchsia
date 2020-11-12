// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::test::mock_node::create_dummy_node;
use crate::thermal_policy::tests::get_sample_interval;
use crate::thermal_policy::*;
use crate::types::{Celsius, Farads, Hertz, Nanoseconds, Seconds, Volts, Watts};
use crate::{
    cpu_control_handler, cpu_stats_handler, dev_control_handler, system_shutdown_handler,
    temperature_handler, thermal_limiter,
};
use cpu_control_handler::PState;
use fuchsia_async as fasync;
use futures::{
    future::LocalBoxFuture,
    stream::{FuturesUnordered, StreamExt},
};
use rkf45;
use std::cell::RefCell;
use std::rc::Rc;
use test_util::assert_near;

#[derive(Clone, Debug)]
struct SimulatedCpuParams {
    num_cpus: u32,
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

/// Method for rolling over incomplete operations within OperationScheduler.
enum OperationRolloverMethod {
    /// Enqueue imcomplete operations for the next time interval.
    _Enqueue,
    /// Drop incomplete operations.
    Drop,
}

/// Schedules operations to send to the simulated CPU.
struct OperationScheduler {
    /// Rate of operations sent to the CPU, scheduled as a function of time.
    rate_schedule: Box<dyn Fn(Seconds) -> Hertz>,
    /// Method for rolling over incomplete operations.
    rollover_method: OperationRolloverMethod,
    /// Number of incomplete operations. Recorded as a float rather than an integer for ease
    /// of use in associated calculations.
    num_operations: f64,
}

impl OperationScheduler {
    fn new(
        rate_schedule: Box<dyn Fn(Seconds) -> Hertz>,
        rollover_method: OperationRolloverMethod,
    ) -> OperationScheduler {
        Self { rate_schedule, rollover_method, num_operations: 0.0 }
    }

    /// Steps from time `t` to `t+dt`, accumulating new operations accordingly.
    fn step(&mut self, t: Seconds, dt: Seconds) {
        if let OperationRolloverMethod::Drop = self.rollover_method {
            self.num_operations = 0.0;
        }
        self.num_operations += (self.rate_schedule)(t) * dt;
    }

    // Marks `num` operations complete.
    fn complete_operations(&mut self, num: f64) {
        assert!(
            num <= self.num_operations,
            "More operations marked complete than were available ({} vs. {})",
            num,
            self.num_operations,
        );
        self.num_operations -= num;
    }
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
    /// Schedules simulated CPU operations.
    op_scheduler: OperationScheduler,
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
    /// Schedules simulated CPU operations.
    op_scheduler: OperationScheduler,
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
            op_scheduler: p.op_scheduler,
            idle_times: vec![Nanoseconds(0); p.cpu_params.num_cpus as usize],
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

    fn make_p_state_getter(sim: &Rc<RefCell<Self>>) -> impl Fn() -> u32 {
        let s = sim.clone();
        move || s.borrow().p_state_index as u32
    }

    /// Returns a closure to set the simulator's P-state, for a device controller handler test
    /// node.
    fn make_p_state_setter(sim: &Rc<RefCell<Self>>) -> impl FnMut(u32) {
        let s = sim.clone();
        move |state| s.borrow_mut().p_state_index = state as usize
    }

    fn make_shutdown_function(sim: &Rc<RefCell<Self>>) -> impl Fn() {
        let s = sim.clone();
        move || {
            s.borrow_mut().shutdown_applied = true;
        }
    }

    /// Steps the simulator ahead in time by `dt`.
    fn step(&mut self, dt: Seconds) {
        self.op_scheduler.step(self.time, dt);

        // `step_cpu` needs to run before `step_thermal_model`, so we know how many operations
        // can actually be completed at the current P-state.
        let num_operations_completed = self.step_cpu(dt, self.op_scheduler.num_operations);
        self.op_scheduler.complete_operations(num_operations_completed);

        self.step_thermal_model(dt, num_operations_completed);
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

    /// Steps the simulated CPU ahead by `dt`, updating `self.idle_times` and returning the
    /// number of operations completed.
    fn step_cpu(&mut self, dt: Seconds, num_operations_requested: f64) -> f64 {
        let frequency = self.get_p_state().frequency;
        let num_operations_completed =
            f64::min(num_operations_requested, frequency * dt * self.cpu_params.num_cpus as f64);

        let total_cpu_time = num_operations_completed / frequency;
        let active_time_per_core = total_cpu_time.div_scalar(self.cpu_params.num_cpus as f64);

        // Calculation of `num_operations_completed` should guarantee this condition.
        assert!(active_time_per_core <= dt);

        let idle_time_per_core = dt - active_time_per_core;
        self.idle_times.iter_mut().for_each(|x| *x += idle_time_per_core.into());

        num_operations_completed
    }
}

/// Coordinates execution of tests of ThermalPolicy.
struct ThermalPolicyTest<'a> {
    executor: fasync::Executor,
    time: Seconds,
    thermal_policy: Rc<ThermalPolicy>,
    policy_futures: FuturesUnordered<LocalBoxFuture<'a, ()>>,
    sim: Rc<RefCell<Simulator>>,
}

impl<'a> ThermalPolicyTest<'a> {
    /// Iniitalizes a new ThermalPolicyTest.
    fn new(sim_params: SimulatorParams, policy_params: ThermalPolicyParams) -> Self {
        {
            let p = &sim_params.cpu_params;
            let max_op_rate = p.p_states[0].frequency.mul_scalar(p.num_cpus as f64);
            let max_power = cpu_control_handler::get_cpu_power(
                p.capacitance,
                p.p_states[0].voltage,
                max_op_rate,
            );
            assert!(
                max_power < policy_params.controller_params.sustainable_power,
                "Sustainable power does not support running CPU at maximum power."
            );
        }
        let time = Seconds(0.0);
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();
        executor.set_fake_time(time.into());

        let cpu_params = sim_params.cpu_params.clone();
        let sim = Simulator::new(sim_params);
        let policy_futures = FuturesUnordered::new();
        let thermal_policy = match executor.run_until_stalled(&mut Box::pin(
            Self::init_thermal_policy(&sim, &cpu_params, policy_params, &policy_futures),
        )) {
            futures::task::Poll::Ready(policy) => policy,
            _ => panic!("Failed to create ThermalPolicy"),
        };

        Self { executor, time, sim, thermal_policy, policy_futures }
    }

    /// Initializes the ThermalPolicy. Helper function for new().
    async fn init_thermal_policy(
        sim: &Rc<RefCell<Simulator>>,
        cpu_params: &SimulatedCpuParams,
        policy_params: ThermalPolicyParams,
        futures: &FuturesUnordered<LocalBoxFuture<'a, ()>>,
    ) -> Rc<ThermalPolicy> {
        let temperature_node = temperature_handler::tests::setup_test_node(
            Simulator::make_temperature_fetcher(&sim),
            fuchsia_zircon::Duration::from_millis(50),
        );
        let cpu_stats_node =
            cpu_stats_handler::tests::setup_test_node(Simulator::make_idle_times_fetcher(&sim))
                .await;
        let sys_pwr_handler = system_shutdown_handler::tests::setup_test_node(
            Simulator::make_shutdown_function(&sim),
        );

        let cpu_dev_handler = dev_control_handler::tests::setup_test_node(
            Simulator::make_p_state_getter(&sim),
            Simulator::make_p_state_setter(&sim),
        );

        // Note that the model capacitance used by the control node could differ from the one
        // used by the simulator. This could be leveraged to simulate discrepancies between
        // the on-device power model (in the thermal policy) and reality (as represented by the
        // simulator).
        let cpu_control_params = cpu_control_handler::CpuControlParams {
            p_states: cpu_params.p_states.clone(),
            capacitance: cpu_params.capacitance,
            num_cores: cpu_params.num_cpus,
        };
        let cpu_control_node = cpu_control_handler::tests::setup_test_node(
            cpu_control_params,
            cpu_stats_node.clone(),
            cpu_dev_handler,
        )
        .await;

        let thermal_limiter_node = thermal_limiter::tests::setup_test_node();

        let thermal_config = ThermalConfig {
            temperature_node,
            cpu_control_nodes: vec![cpu_control_node],
            sys_pwr_handler,
            thermal_limiter_node,
            crash_report_handler: create_dummy_node(),
            policy_params,
        };
        ThermalPolicyBuilder::new(thermal_config).build(futures).unwrap()
    }

    /// Iterates the policy n times.
    fn iterate_n_times(&mut self, n: u32) {
        let dt = get_sample_interval(&self.thermal_policy);

        for _ in 0..n {
            self.time += dt;
            self.executor.set_fake_time(self.time.into());
            self.sim.borrow_mut().step(dt);

            let wakeup_time = self.executor.wake_next_timer().unwrap();
            assert_eq!(fasync::Time::from(self.time), wakeup_time);
            assert_eq!(
                futures::task::Poll::Pending,
                self.executor.run_until_stalled(&mut self.policy_futures.next())
            );
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
            sustainable_power: Watts(1.3),
            proportional_gain: 0.0,
            integral_gain: 0.2,
        },
        thermal_shutdown_temperature: Celsius(95.0),
        throttle_end_delay: Seconds(0.0),
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
            op_scheduler: OperationScheduler::new(
                Box::new(move |_| operation_rate),
                OperationRolloverMethod::Drop,
            ),
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
    assert_near!(target_temp, sim.cpu_temperature.0, 1e-3);
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
            op_scheduler: OperationScheduler::new(
                Box::new(move |_| Hertz(3e9)),
                OperationRolloverMethod::Drop,
            ),
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
            op_scheduler: OperationScheduler::new(
                Box::new(move |_| Hertz(3e9)),
                OperationRolloverMethod::Drop,
            ),
            initial_cpu_temperature: shutdown_temperature - Celsius(10.0),
            initial_heat_sink_temperature: shutdown_temperature - Celsius(10.0),
            environment_temperature: shutdown_temperature,
        },
        policy_params,
    );

    let mut shutdown_verified = false;
    for _ in 0..3600 {
        test.iterate_n_times(1);

        // Since thermal shutdown uses raw temperature rather than filtered, shutdown will occur as
        // soon as the simulated temperature exceeds the shutdown threshold.
        if test.sim.borrow().cpu_temperature >= shutdown_temperature {
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
            op_scheduler: OperationScheduler::new(
                Box::new(move |_| operation_rate),
                OperationRolloverMethod::Drop,
            ),
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
    assert_near!(average_temperature, target_temperature.0, 0.1);
}

// Tests for a bug that led to jitter in P-state selection at max load.
//
// CpuControlHandler was originally implemented to estimate the operation rate in the upcoming
// cycle as the operation rate over the previous cycle, even if the previous rate was maximal.
// This underpredicted the new operation rate when the CPU was saturated.
//
// For example, suppose a 4-core CPU operated at 1.5 GHz over the previous cycle. If it was
// saturated, its operation rate was 6.0 GHz. If we raise the clock speed to 2GHz and the CPU
// remains saturated, we will have underpredicted its operation rate by 25%.
//
// This underestimation manifested as unwanted jitter between P-states. After transitioning from
// P0 to P1, for example, the available power required to select P0 would drop by the ratio of
// frequencies, f1/f0. This made an immediate transition back to P0 very likely.
//
// Note that since the CPU temperature immediately drops when its clock speed is lowered, this
// behavior of dropping clock speed for a single cycle may occur for good reason. To isolate the
// undesired behavior in this test, we use an extremely large time constant. Doing so mostly
// eliminates the change in filtered temperature in the cycles immediately following a P-state
// transition.
#[test]
fn test_no_jitter_at_max_load() {
    // Choose an operation rate that induces max load at highest frequency.
    let cpu_params = default_cpu_params();
    let operation_rate = cpu_params.p_states[0].frequency.mul_scalar(cpu_params.num_cpus as f64);

    // Use a very large filter time constant: 1 deg raw --> 0.001 deg filtered in the first
    // cycle after a change.
    let mut policy_params = default_policy_params();
    policy_params.controller_params.filter_time_constant = Seconds(1000.0);

    let mut test = ThermalPolicyTest::new(
        SimulatorParams {
            thermal_model_params: default_thermal_model_params(),
            cpu_params: cpu_params,
            op_scheduler: OperationScheduler::new(
                Box::new(move |_| operation_rate),
                OperationRolloverMethod::Drop,
            ),
            initial_cpu_temperature: Celsius(80.0),
            initial_heat_sink_temperature: Celsius(80.0),
            environment_temperature: Celsius(75.0),
        },
        policy_params,
    );

    // Run the simulation (up to 1 hour simulated time) until the CPU transitions to a lower
    // clock speed.
    let max_iterations = 3600;
    let mut throttling_started = false;
    for _ in 0..max_iterations {
        test.iterate_n_times(1);
        let s = test.sim.borrow();
        if s.p_state_index > 0 {
            assert_eq!(s.p_state_index, 1, "Should have transitioned to P-state 1.");
            throttling_started = true;
            break;
        }
    }
    assert!(
        throttling_started,
        format!("CPU throttling did not begin within {} iterations", max_iterations)
    );

    // Iterated one more time, and make sure the clock speed is still reduced.
    test.iterate_n_times(1);
    assert_ne!(test.sim.borrow().p_state_index, 0);
}
