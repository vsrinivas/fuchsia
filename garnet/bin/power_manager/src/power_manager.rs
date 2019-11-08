// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::node::Node;
use failure::{format_err, Error};
use fuchsia_syslog::fx_log_info;
use std::rc::Rc;

// nodes
use crate::{cpu_control_handler, cpu_stats_handler, temperature_handler, thermal_policy};

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
        let temperature = temperature_handler::TemperatureHandler::new();
        self.nodes.push(temperature.clone());

        let stats = cpu_stats_handler::CpuStatsHandler::new()?;
        self.nodes.push(stats.clone());

        let control = cpu_control_handler::CpuControlHandler::new();
        self.nodes.push(control.clone());

        let thermal_config = thermal_policy::ThermalConfig {
            temperature_driver: "/dev/class/thermal/000".to_string(),
            poll_interval_ms: 1000,
        };
        let thermal =
            thermal_policy::ThermalPolicy::new(thermal_config, temperature, stats, control)?;
        self.nodes.push(thermal);

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
    use fuchsia_async as fasync;

    fn test_expected_nodes(board: &'static str, expected: Vec<&'static str>) {
        let mut pm = PowerManager::new(board.to_string()).unwrap();
        pm.init().unwrap();
        assert!(expected.iter().eq(pm.list_nodes().iter()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_expected_nodes_astro() {
        let expected_nodes =
            vec!["TemperatureHandler", "CpuStatsHandler", "CpuControlHandler", "ThermalPolicy"];
        test_expected_nodes("astro", expected_nodes);
    }
}
