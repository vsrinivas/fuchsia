// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::{Farads, Hertz, Volts, Watts};
use async_trait::async_trait;
use failure::{format_err, Error};
use std::cell::{Cell, RefCell};
use std::rc::Rc;

/// Node: CpuControlHandler
///
/// Summary: WIP
///
/// Handles Messages: WIP
///
/// Sends Messages: WIP
///
/// FIDL dependencies: WIP

/// Describes a processor performance state.
#[derive(Clone, Debug)]
pub struct PState {
    pub frequency: Hertz,
    pub voltage: Volts,
}

#[derive(Debug)]
pub struct CpuControlParams {
    /// Available P-states of the CPU. These must be in order of descending power usage, per
    /// section 8.4.6.2 of ACPI spec version 6.3.
    pub p_states: Vec<PState>,
    /// Model capacitance of each CPU core. Required to estimate power usage.
    pub capacitance: Farads,
}

impl CpuControlParams {
    /// Checks that the list of P-states is valid:
    ///  - Contains at least one element;
    ///  - Is in order of decreasing nominal power consumption.
    fn validate(&self) -> Result<(), Error> {
        if self.p_states.len() == 0 {
            return Err(format_err!("Must have at least one P-state"));
        } else if self.p_states.len() > 1 {
            let mut last_power = get_cpu_power(
                self.capacitance,
                self.p_states[0].voltage,
                self.p_states[0].frequency,
            );
            for i in 1..self.p_states.len() {
                let p_state = &self.p_states[i];
                let power = get_cpu_power(self.capacitance, p_state.voltage, p_state.frequency);
                if power >= last_power {
                    return Err(format_err!(
                        "P-states must be in order of decreasing power consumption \
                         (violated by state {})",
                        i
                    ));
                }
                last_power = power;
            }
        }
        Ok(())
    }
}

/// Returns the modeled power consumed by the CPU completing operations at the specified rate.
/// Note that we assume zero static power consumption in this model, and that `op_completion_rate`
/// is only the CPU's clock speed if the CPU is never idle.
pub fn get_cpu_power(capacitance: Farads, voltage: Volts, op_completion_rate: Hertz) -> Watts {
    Watts(capacitance.0 * voltage.0.powi(2) * op_completion_rate.0)
}

pub trait SetPStateIndex {
    fn set_p_state_index(&mut self, state: usize);
}

struct DefaultFakeCpuDevice {
    p_state_index: usize,
}

impl SetPStateIndex for DefaultFakeCpuDevice {
    fn set_p_state_index(&mut self, index: usize) {
        self.p_state_index = index;
    }
}

// TODO(fxb/41453): The current implementation is incomplete, meant to support an initial version
// of the thermal simulator. The following issues remain to be addressed:
// - The list of available P-states should be obtained from the CPU driver rather than input
//   parameters.
// - The `cpu_device` field should be replaced with a proxy for the fuchsia.device.Controller
//   protocol.
// - `current_p_state_index` should be initialized to the P-state reported by the CPU driver.
// - The CPU load provided by `get_load` should be low-pass filtered.
// - Consider whether the CPU driver should provide the capacitance used in the power model.
pub struct CpuControlHandler {
    params: CpuControlParams,
    current_p_state_index: usize,
    cpu_stats_handler: Rc<dyn Node>,
    cpu_device: RefCell<Box<dyn SetPStateIndex>>,
    num_cpus: Cell<Option<u32>>,
}

impl CpuControlHandler {
    pub fn new(
        params: CpuControlParams,
        cpu_stats_handler: Rc<dyn Node>,
    ) -> Result<Rc<Self>, Error> {
        let cpu_device = Box::new(DefaultFakeCpuDevice { p_state_index: 0 });
        Self::new_with_cpu_device(params, cpu_stats_handler, cpu_device)
    }

    fn new_with_cpu_device(
        params: CpuControlParams,
        cpu_stats_handler: Rc<dyn Node>,
        cpu_device: Box<dyn SetPStateIndex>,
    ) -> Result<Rc<Self>, Error> {
        params.validate()?;
        Ok(Rc::new(Self {
            params,
            current_p_state_index: 0,
            cpu_stats_handler,
            cpu_device: RefCell::new(cpu_device),
            num_cpus: Cell::new(None),
        }))
    }

    /// Returns the total CPU load.
    async fn get_load(&self) -> Result<f32, Error> {
        match self.send_message(&self.cpu_stats_handler, &Message::GetTotalCpuLoad).await {
            Ok(MessageReturn::GetTotalCpuLoad(load)) => Ok(load),
            Ok(r) => Err(format_err!("GetTotalCpuLoad had unexpected return value: {:?}", r)),
            Err(e) => Err(format_err!("GetTotalCpuLoad failed: {:?}", e)),
        }
    }

    /// Returns the number of CPUs.
    async fn get_num_cpus(&self) -> Result<u32, Error> {
        match self.send_message(&self.cpu_stats_handler, &Message::GetNumCpus).await {
            Ok(MessageReturn::GetNumCpus(num_cpus)) => Ok(num_cpus),
            Ok(r) => Err(format_err!("GetNumCpus had unexpected return value: {:?}", r)),
            Err(e) => Err(format_err!("GetNumCpus failed: {:?}", e)),
        }
    }

    /// Sets the P-state to the highest-power state with consumption below `max_power`.
    ///
    /// The estimated power consumption depends on the operation completion rate by the CPU.
    /// We assume that the rate of operations requested over the next sample interval will be
    /// the same as it was over the previous sample interval, up to CPU's max completion rate
    /// for a P-state under consideration.
    async fn handle_set_max_power_consumption(
        &self,
        max_power: &Watts,
    ) -> Result<MessageReturn, Error> {
        // The operation completion rate over the last sample interval is
        //     num_operations / sample_interval,
        // where
        //     num_operations = last_load * last_frequency * sample_interval.
        // Hence,
        //     last_operation_rate = last_load * last_frequency.
        let last_operation_rate = {
            let last_load = self.get_load().await? as f64;
            let last_frequency = self.params.p_states[self.current_p_state_index].frequency;
            last_frequency.mul_scalar(last_load)
        };

        // Lazily get the number of CPUs from the stats handler.
        let num_cpus = {
            if self.num_cpus.get().is_none() {
                self.num_cpus.set(Some(self.get_num_cpus().await?));
            }
            self.num_cpus.get().unwrap()
        };

        // If no P-states meet the selection criterion, use the lowest-power state.
        let mut p_state_index = self.params.p_states.len() - 1;

        for (i, state) in self.params.p_states.iter().enumerate() {
            // We estimate that the operation rate over the next interval will be the min of
            // the last operation rate and the frequency of the P-state under consideration.
            //
            // Note that we don't currently account for a rise in frequency allowing for a possible
            // increase in the operation rate.
            let max_operation_rate = state.frequency.mul_scalar(num_cpus as f64);
            let estimated_operation_rate = if max_operation_rate < last_operation_rate {
                max_operation_rate
            } else {
                last_operation_rate
            };

            let estimated_power =
                get_cpu_power(self.params.capacitance, state.voltage, estimated_operation_rate);
            if estimated_power <= *max_power {
                p_state_index = i;
                break;
            }
        }

        self.cpu_device.borrow_mut().set_p_state_index(p_state_index);
        Ok(MessageReturn::SetMaxPowerConsumption)
    }
}

#[async_trait(?Send)]
impl Node for CpuControlHandler {
    fn name(&self) -> &'static str {
        "CpuControlHandler"
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, Error> {
        match msg {
            Message::SetMaxPowerConsumption(p) => self.handle_set_max_power_consumption(p).await,
            _ => Err(format_err!("Unsupported message: {:?}", msg)),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    pub fn setup_test_node(
        params: CpuControlParams,
        cpu_stats_handler: Rc<dyn Node>,
        cpu_device: Box<dyn SetPStateIndex>,
    ) -> Rc<CpuControlHandler> {
        CpuControlHandler::new_with_cpu_device(params, cpu_stats_handler, cpu_device).unwrap()
    }
}
