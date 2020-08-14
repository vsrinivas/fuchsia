// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::shutdown_request::{RebootReason, ShutdownRequest};
use crate::temperature_handler::TemperatureFilter;
use crate::types::{Celsius, Seconds};
use crate::utils::get_current_timestamp;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fuchsia_async as fasync;
use futures::StreamExt;
use log::*;
use serde_derive::Deserialize;
use serde_json as json;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: ThermalShutdown
///
/// Summary: Monitors a single temperature sensor using a provided TemperatureHandler node. When the
/// observed temperature crosses the configured temperature threshold, the node will command a
/// system reboot.
///
/// Handles Messages: N/A
///
/// Sends Messages:
///     - ReadTemperature
///     - SystemShutdown
///
/// FIDL dependencies: N/A

pub struct ThermalShutdownBuilder {
    thermal_shutdown_temperature: Celsius,
    poll_interval: Seconds,
    filter_time_constant: Seconds,
    temperature_node: Rc<dyn Node>,
    system_shutdown_node: Rc<dyn Node>,
}

impl ThermalShutdownBuilder {
    #[cfg(test)]
    fn new(temperature_node: Rc<dyn Node>, system_shutdown_node: Rc<dyn Node>) -> Self {
        ThermalShutdownBuilder {
            thermal_shutdown_temperature: Celsius(0.0),
            poll_interval: Seconds(0.0),
            filter_time_constant: Seconds(0.0),
            temperature_node,
            system_shutdown_node,
        }
    }

    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Config {
            thermal_shutdown_temperature_c: f64,
            poll_interval_s: f64,
            filter_time_constant_s: f64,
        };

        #[derive(Deserialize)]
        struct Dependencies {
            temperature_handler_node: String,
            system_shutdown_node: String,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
            dependencies: Dependencies,
        };

        let data: JsonData = json::from_value(json_data).unwrap();
        Self {
            thermal_shutdown_temperature: Celsius(data.config.thermal_shutdown_temperature_c),
            poll_interval: Seconds(data.config.poll_interval_s),
            filter_time_constant: Seconds(data.config.filter_time_constant_s),
            temperature_node: nodes[&data.dependencies.temperature_handler_node].clone(),
            system_shutdown_node: nodes[&data.dependencies.system_shutdown_node].clone(),
        }
    }

    #[cfg(test)]
    pub fn with_thermal_shutdown_temperature(mut self, temperature: Celsius) -> Self {
        self.thermal_shutdown_temperature = temperature;
        self
    }

    pub fn build(self) -> Result<Rc<ThermalShutdown>, Error> {
        let node = Rc::new(ThermalShutdown {
            temperature_filter: TemperatureFilter::new(
                self.temperature_node.clone(),
                self.filter_time_constant,
            ),
            thermal_shutdown_temperature: self.thermal_shutdown_temperature,
            poll_interval: self.poll_interval,
            temperature_node: self.temperature_node,
            system_shutdown_node: self.system_shutdown_node,
        });

        node.clone().start_polling_loop();
        Ok(node)
    }
}

pub struct ThermalShutdown {
    /// Provides filtered temperature values according to the configured filter constant.
    temperature_filter: TemperatureFilter,

    /// Temperature threshold in Celsius at which the node will command a system reboot.
    thermal_shutdown_temperature: Celsius,

    /// Time interval in seconds that the node will read a new temperature value.
    poll_interval: Seconds,

    /// Node to provide the temperature readings via the ReadTemperature message.
    temperature_node: Rc<dyn Node>,

    /// Node to provide the system reboot functionality via the SystemShutdown message.
    system_shutdown_node: Rc<dyn Node>,
}

impl ThermalShutdown {
    /// Spawns a task that periodically polls the temperature and checks it against the configured
    /// threshold.
    fn start_polling_loop(self: Rc<Self>) {
        let mut periodic_timer = fasync::Interval::new(self.poll_interval.into());

        fasync::Task::local(async move {
            while let Some(()) = periodic_timer.next().await {
                let result = self.poll_temperature().await;
                log_if_err!(result, "Error while polling temperature");
            }
        })
        .detach();
    }

    /// Polls the temperature sensor once and checks the reading against the configured threshold.
    async fn poll_temperature(&self) -> Result<(), Error> {
        fuchsia_trace::duration!("power_manager", "ThermalShutdown::poll_temperature");

        if self.temperature_filter.get_temperature(get_current_timestamp()).await?.filtered
            >= self.thermal_shutdown_temperature
        {
            info!("{:?} crossed high temperature mark. Rebooting...", self.temperature_node.name());
            self.send_message(
                &self.system_shutdown_node,
                &Message::SystemShutdown(ShutdownRequest::Reboot(RebootReason::HighTemperature)),
            )
            .await
            .map_err(|e| format_err!("Failed to shut down the system: {}", e))?;
        }

        Ok(())
    }
}

#[async_trait(?Send)]
impl Node for ThermalShutdown {
    fn name(&self) -> String {
        "ThermalShutdown".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::mock_node::{create_dummy_node, create_mock_node, MessageMatcher};
    use crate::{msg_eq, msg_ok_return};

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "ThermalShutdown",
            "name": "thermal_shutdown",
            "config": {
                "thermal_shutdown_temperature_c": 1.2,
                "poll_interval_s": 3.4,
                "filter_time_constant_s": 5.6
            },
            "dependencies": {
                "system_shutdown_node": "shutdown",
                "temperature_handler_node": "temperature",
              },
        });

        let mut nodes: HashMap<String, Rc<dyn Node>> = HashMap::new();
        nodes.insert("temperature".to_string(), create_dummy_node());
        nodes.insert("shutdown".to_string(), create_dummy_node());
        let _ = ThermalShutdownBuilder::new_from_json(json_data, &nodes);
    }

    /// Tests that when the node reads a temperature that exceeds the configured threshold value, it
    /// triggers a system shutdown.
    #[fasync::run_singlethreaded(test)]
    async fn test_triggered_shutdown() {
        let temperature_node = create_mock_node(
            "Temperature",
            vec![(msg_eq!(ReadTemperature), msg_ok_return!(ReadTemperature(Celsius(100.0))))],
        );
        let shutdown_node = create_mock_node(
            "Shutdown",
            vec![(
                msg_eq!(SystemShutdown(ShutdownRequest::Reboot(RebootReason::HighTemperature))),
                msg_ok_return!(SystemShutdown),
            )],
        );

        let node = ThermalShutdownBuilder::new(temperature_node, shutdown_node)
            .with_thermal_shutdown_temperature(Celsius(99.0))
            .build()
            .unwrap();
        assert!(node.poll_temperature().await.is_ok());
    }
}
