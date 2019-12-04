// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::node::Node;
use crate::types::{Celsius, Seconds, Watts};
use failure::{format_err, Error};
use fuchsia_syslog::fx_log_info;
use std::rc::Rc;

// nodes
use crate::{
    cpu_control_handler, cpu_stats_handler, system_power_handler, temperature_handler,
    thermal_policy,
};

pub struct PowerManager {
    board: String,
    nodes: Vec<Rc<dyn Node>>,
}

impl PowerManager {
    pub fn new(board: String) -> Result<Self, Error> {
        let pm = PowerManager { board, nodes: Vec::new() };
        Ok(pm)
    }

    pub fn init(&mut self) -> Result<(), Error> {
        fx_log_info!("Begin init: {}", self.board);

        match self.board.as_ref() {
            "astro" => self.init_astro(),
            _ => Err(format_err!("Invalid target: {}", self.board)),
        }?;

        self.nodes.iter().for_each(|n| fx_log_info!("Added node: {}", n.name()));

        fx_log_info!("init complete");

        Ok(())
    }

    fn init_astro(&mut self) -> Result<(), Error> {
        let cpu_temperature =
            temperature_handler::TemperatureHandler::new("/dev/class/thermal/000".to_string())?;
        self.nodes.push(cpu_temperature.clone());

        let cpu_stats_node = cpu_stats_handler::CpuStatsHandler::new()?;
        self.nodes.push(cpu_stats_node);

        let cpu_control_node = cpu_control_handler::CpuControlHandler::new();
        self.nodes.push(cpu_control_node.clone());

        let sys_pwr_handler = system_power_handler::SystemPowerStateHandler::new()?;
        self.nodes.push(sys_pwr_handler.clone());

        let thermal_config = thermal_policy::ThermalConfig {
            temperature_node: cpu_temperature,
            cpu_control_node,
            sys_pwr_handler,

            // TODO(fxb/41452): these are just placeholder ThermalPolicyParams. The real params
            // should be populated here once they have been properly determined.
            thermal_params: thermal_policy::ThermalPolicyParams {
                controller_params: thermal_policy::ThermalControllerParams {
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
            },
        };
        let thermal_policy = thermal_policy::ThermalPolicy::new(thermal_config)?;
        self.nodes.push(thermal_policy);

        Ok(())
    }

    #[cfg(test)]
    fn list_nodes(&self) -> Vec<&'static str> {
        self.nodes.iter().map(|node| node.name()).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_create_power_manager() {
        let board_name = "astro";
        let power_manager = PowerManager::new(board_name.to_string()).unwrap();
        assert_eq!(power_manager.board, board_name);
        assert_eq!(power_manager.list_nodes().len(), 0);
    }
}
