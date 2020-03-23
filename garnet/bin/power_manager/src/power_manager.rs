// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::node::Node;
use anyhow::Error;
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use fuchsia_inspect::component;
use futures::stream::StreamExt;
use serde_json as json;
use std::collections::HashMap;
use std::fs::File;
use std::io::BufReader;
use std::rc::Rc;

// nodes
use crate::{
    cpu_control_handler, cpu_stats_handler, dev_control_handler, system_power_handler,
    temperature_handler, thermal_limiter, thermal_policy,
};

/// Path to the node config JSON file.
const NODE_CONFIG_PATH: &'static str = "/config/data/node_config.json";

pub struct PowerManager {
    nodes: HashMap<String, Rc<dyn Node>>,
}

impl PowerManager {
    pub fn new() -> Self {
        Self { nodes: HashMap::new() }
    }

    /// Perform the node initialization and begin running the PowerManager.
    pub async fn run(&mut self) -> Result<(), Error> {
        // Create a new ServiceFs to incoming handle service requests for the various services that
        // the PowerManager hosts.
        let mut fs = ServiceFs::new_local();

        // Required call to serve the inspect tree
        let inspector = component::inspector();
        inspector.serve(&mut fs)?;

        // Create the nodes according to the config file
        self.create_nodes_from_config(&mut fs).await?;

        // Allow our services to be discovered.
        fs.take_and_serve_directory_handle()?;

        // Run the ServiceFs (handles incoming request streams). This future never completes.
        fs.collect::<()>().await;

        Ok(())
    }

    /// Create the nodes by reading and parsing the node config JSON file.
    async fn create_nodes_from_config<'a, 'b>(
        &mut self,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Result<(), Error> {
        let json_data: json::Value =
            json::from_reader(BufReader::new(File::open(NODE_CONFIG_PATH)?))?;
        self.create_nodes(json_data, service_fs).await
    }

    /// Creates the nodes using the specified JSON object, adding them to the `nodes` HashMap.
    async fn create_nodes<'a, 'b>(
        &mut self,
        json_data: json::Value,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Result<(), Error> {
        // Iterate through each object in the top-level array, which represents configuration for a
        // single node
        for node_config in json_data.as_array().unwrap().iter() {
            let node = self.create_node(node_config.clone(), service_fs).await?;
            self.nodes.insert(node_config["name"].as_str().unwrap().to_string(), node);
        }
        Ok(())
    }

    /// Uses the supplied `json_data` to construct a single node, where `json_data` is the JSON
    /// object corresponding to a single node configuration.
    async fn create_node<'a, 'b>(
        &mut self,
        json_data: json::Value,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Result<Rc<dyn Node>, Error> {
        Ok(match json_data["type"].as_str().unwrap() {
            "CpuControlHandler" => {
                cpu_control_handler::CpuControlHandlerBuilder::new_from_json(json_data, &self.nodes)
                    .build()
                    .await?
            }
            "CpuStatsHandler" => {
                cpu_stats_handler::CpuStatsHandlerBuilder::new_from_json(json_data, &self.nodes)
                    .build()
                    .await?
            }
            "DeviceControlHandler" => {
                dev_control_handler::DeviceControlHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                )
                .build()?
            }
            "SystemPowerHandler" => {
                system_power_handler::SystemPowerStateHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                )
                .build()?
            }
            "TemperatureHandler" => temperature_handler::TemperatureHandlerBuilder::new_from_json(
                json_data,
                &self.nodes,
            )
            .build()?,
            "ThermalLimiter" => thermal_limiter::ThermalLimiterBuilder::new_from_json(
                json_data,
                &self.nodes,
                service_fs,
            )
            .build()?,
            "ThermalPolicy" => {
                thermal_policy::ThermalPolicyBuilder::new_from_json(json_data, &self.nodes)
                    .build()?
            }
            unknown => panic!("Unknown node type: {}", unknown),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    /// Tests that well-formed configuration JSON does not cause an unexpected panic in the
    /// `create_nodes` function. With this test JSON, we expect a panic with the message stated
    /// below indicating a node with the given type doesn't exist. By this point the JSON parsing
    /// will have already been validated.
    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "Unknown node type: test_type")]
    async fn test_create_nodes() {
        let json_data = json::json!([
            {
                "type": "test_type",
                "name": "test_name"
            },
        ]);
        let mut power_manager = PowerManager::new();
        power_manager.create_nodes(json_data, &mut ServiceFs::new_local()).await.unwrap();
    }
}
