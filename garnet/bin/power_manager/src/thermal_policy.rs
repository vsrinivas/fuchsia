// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::{Celsius, Nanoseconds, Seconds, Watts};
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

    /// The thermal control loop parameters
    pub thermal_params: ThermalParams,
}

/// A struct to store the tunable thermal control loop parameters
pub struct ThermalParams {
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

    /// The integral error [degC * s] that is accumlated across controller iterations
    error_integral: Cell<f64>,

    /// A flag to know if the rest of ThermalState has not been initialized yet
    state_initialized: Cell<bool>,
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
            },
        });
        node.clone().start_periodic_thermal_loop()?;
        Ok(node)
    }

    /// Starts a periodic timer that fires at the interval specified by
    /// ThermalParams.sample_interval. At each timer, `iterate_thermal_control` is called and any
    /// resulting errors are logged.
    fn start_periodic_thermal_loop(self: Rc<Self>) -> Result<(), Error> {
        let mut periodic_timer = fasync::Interval::new(zx::Duration::from_nanos(
            self.config.thermal_params.sample_interval.into_nanos(),
        ));

        let s = self.clone();

        fasync::spawn_local(async move {
            while let Some(()) = periodic_timer.next().await {
                if let Err(e) = s.iterate_thermal_control().await {
                    fx_log_err!("{}", e);
                }
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
            self.config.thermal_params.filter_time_constant.0,
        ));
        self.state.prev_temperature.set(filtered_temperature);

        let available_power = self.calculate_available_power(filtered_temperature, time_delta);
        fx_vlog!(
            1,
            "iteration_period={}s; raw_temperature={}C; available_power={}W",
            time_delta.0,
            raw_temperature.0,
            available_power.0
        );
        self.distribute_power(available_power).await
    }

    async fn get_temperature(&self) -> Result<Celsius, Error> {
        match self.send_message(&self.config.temperature_node, &Message::ReadTemperature).await {
            Ok(MessageReturn::ReadTemperature(t)) => Ok(t),
            Ok(r) => Err(format_err!("ReadTemperature had unexpected return value: {:?}", r)),
            Err(e) => Err(format_err!("ReadTemperature failed: {:?}", e)),
        }
    }

    /// A PID control algorithm that uses temperature as the measured process variable, and
    /// available power as the control variable. Each call to the function will also
    /// update the state variable `error_integral` to be used on subsequent iterations.
    fn calculate_available_power(&self, temperature: Celsius, time_delta: Seconds) -> Watts {
        let temperature_error = self.config.thermal_params.target_temperature.0 - temperature.0;
        let error_integral = clamp(
            self.state.error_integral.get() + temperature_error * time_delta.0,
            self.config.thermal_params.e_integral_min,
            self.config.thermal_params.e_integral_max,
        );
        self.state.error_integral.set(error_integral);

        let p_term = temperature_error * self.config.thermal_params.proportional_gain;
        let i_term = error_integral * self.config.thermal_params.integral_gain;
        let power_available = self.config.thermal_params.sustainable_power.0 + p_term + i_term;

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
    use crate::{cpu_control_handler, temperature_handler};

    // TODO (pshickel): The most useful test for this node will likely come from claridge@'s
    // thermal simulator. Once the simulator is ready, we can use it here to test the control
    // algorithm and connections to required nodes.
    #[allow(dead_code)]
    fn setup_test_node(temperature_readings: Vec<f32>) -> Rc<ThermalPolicy> {
        let temperature_node = temperature_handler::tests::setup_test_node(temperature_readings);
        let cpu_control_node = cpu_control_handler::tests::setup_test_node();
        let thermal_config = ThermalConfig {
            temperature_node,
            cpu_control_node,
            thermal_params: ThermalParams {
                sample_interval: Seconds(0.0),
                filter_time_constant: Seconds(0.0),
                target_temperature: Celsius(0.0),
                e_integral_min: 0.0,
                e_integral_max: 0.0,
                sustainable_power: Watts(0.0),
                proportional_gain: 0.0,
                integral_gain: 0.0,
            },
        };
        ThermalPolicy::new(thermal_config).unwrap()
    }

    #[test]
    fn test_low_pass_filter() {
        let y_0 = 0.0;
        let y_1 = 10.0;
        let time_delta = 1.0;
        let time_constant = 10.0;
        assert_eq!(low_pass_filter(y_1, y_0, time_delta, time_constant), 1.0);
    }
}
