// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::node::Node;
use anyhow::{Context, Error};
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use fuchsia_inspect::component;
use futures::{
    future::LocalBoxFuture,
    stream::{FuturesUnordered, StreamExt},
};
use serde_json as json;
use std::collections::HashMap;
use std::fs::File;
use std::io::BufReader;
use std::rc::Rc;

// nodes
use crate::{
    activity_handler, cpu_control_handler, cpu_stats_handler, crash_report_handler,
    dev_control_handler, driver_manager_handler, input_settings_handler, lid_shutdown,
    shutdown_watcher, system_profile_handler, system_shutdown_handler, temperature_handler,
    thermal_limiter, thermal_load_driver, thermal_policy, thermal_shutdown,
};

/// Path to the node config JSON file.
const NODE_CONFIG_PATH: &'static str = "/pkg/config/power_manager/node_config.json";

pub struct PowerManager {
    nodes: HashMap<String, Rc<dyn Node>>,
}

impl PowerManager {
    pub fn new() -> Self {
        Self { nodes: HashMap::new() }
    }

    /// Perform the node initialization and begin running the PowerManager.
    pub async fn run(&mut self) -> Result<(), Error> {
        // Create a new ServiceFs to handle incoming service requests for the various services that
        // the PowerManager hosts.
        let mut fs = ServiceFs::new_local();

        // Allow our services to be discovered.
        fs.take_and_serve_directory_handle()?;

        // Required call to serve the inspect tree
        let inspector = component::inspector();
        inspect_runtime::serve(inspector, &mut fs)?;

        // Create the nodes according to the config file
        let node_futures = FuturesUnordered::new();
        self.create_nodes_from_config(&mut fs, &node_futures).await?;

        // Run the ServiceFs (handles incoming request streams) and node futures. This future never
        // completes.
        futures::stream::select(fs, node_futures).collect::<()>().await;

        Ok(())
    }

    /// Create the nodes by reading and parsing the node config JSON file.
    async fn create_nodes_from_config<'a, 'b, 'c>(
        &mut self,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
        node_futures: &FuturesUnordered<LocalBoxFuture<'c, ()>>,
    ) -> Result<(), Error> {
        let json_data: json::Value =
            json::from_reader(BufReader::new(File::open(NODE_CONFIG_PATH)?))?;
        self.create_nodes(json_data, service_fs, node_futures).await
    }

    /// Creates the nodes using the specified JSON object, adding them to the `nodes` HashMap.
    async fn create_nodes<'a, 'b, 'c>(
        &mut self,
        json_data: json::Value,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
        node_futures: &FuturesUnordered<LocalBoxFuture<'c, ()>>,
    ) -> Result<(), Error> {
        // Iterate through each object in the top-level array, which represents configuration for a
        // single node
        for node_config in json_data.as_array().unwrap().iter() {
            let node = self
                .create_node(node_config.clone(), service_fs, node_futures)
                .await
                .with_context(|| format!("Failed creating node {}", node_config["name"]))?;
            self.nodes.insert(node_config["name"].as_str().unwrap().to_string(), node);
        }
        Ok(())
    }

    /// Uses the supplied `json_data` to construct a single node, where `json_data` is the JSON
    /// object corresponding to a single node configuration.
    async fn create_node<'a, 'b, 'c>(
        &mut self,
        json_data: json::Value,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
        node_futures: &FuturesUnordered<LocalBoxFuture<'c, ()>>,
    ) -> Result<Rc<dyn Node>, Error> {
        Ok(match json_data["type"].as_str().unwrap() {
            "ActivityHandler" => {
                activity_handler::ActivityHandlerBuilder::new_from_json(json_data, &self.nodes)
                    .build(node_futures)?
            }
            "CrashReportHandler" => {
                crash_report_handler::CrashReportHandlerBuilder::new().build()?
            }
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
                .build()
                .await?
            }
            "DriverManagerHandler" => {
                driver_manager_handler::DriverManagerHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                    service_fs,
                )
                .build()
                .await?
            }
            "InputSettingsHandler" => {
                input_settings_handler::InputSettingsHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                )
                .build(node_futures)?
            }
            "LidShutdown" => {
                lid_shutdown::LidShutdownBuilder::new_from_json(json_data, &self.nodes)
                    .build(node_futures)
                    .await?
            }
            "SystemProfileHandler" => {
                system_profile_handler::SystemProfileHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                    service_fs,
                )
                .build()?
            }
            "SystemShutdownHandler" => {
                system_shutdown_handler::SystemShutdownHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                    service_fs,
                )
                .build()?
            }
            "ShutdownWatcher" => shutdown_watcher::ShutdownWatcherBuilder::new_from_json(
                json_data,
                &self.nodes,
                service_fs,
            )
            .build()?,
            "TemperatureHandler" => {
                temperature_handler::TemperatureHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                )
                .build()
                .await?
            }
            "ThermalLimiter" => thermal_limiter::ThermalLimiterBuilder::new_from_json(
                json_data,
                &self.nodes,
                service_fs,
            )
            .build()?,
            "ThermalLoadDriver" => {
                thermal_load_driver::ThermalLoadDriverBuilder::new_from_json(json_data, &self.nodes)
                    .build(node_futures)?
            }
            "ThermalPolicy" => {
                thermal_policy::ThermalPolicyBuilder::new_from_json(json_data, &self.nodes)
                    .build(node_futures)?
            }
            "ThermalShutdown" => {
                thermal_shutdown::ThermalShutdownBuilder::new_from_json(json_data, &self.nodes)
                    .build(node_futures)?
            }
            unknown => panic!("Unknown node type: {}", unknown),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;
    use fuchsia_async as fasync;
    use std::collections::HashSet;

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
        let node_futures = FuturesUnordered::new();
        power_manager
            .create_nodes(json_data, &mut ServiceFs::new_local(), &node_futures)
            .await
            .unwrap();
    }

    /// Tests that all nodes in a given config file have a unique name.
    #[test]
    fn test_config_file_unique_names() -> Result<(), anyhow::Error> {
        crate::utils::test_each_node_config_file(|config_file| {
            let mut set = HashSet::new();
            for node in config_file {
                let node_name = node["name"].as_str().unwrap().to_string();
                if set.contains(&node_name) {
                    return Err(format_err!("Node with name {} already specified", node_name));
                }

                set.insert(node_name);
            }

            Ok(())
        })
    }

    /// Tests each node config file for correct node dependency ordering. The test expects a node's
    /// dependencies to be listed under a "dependencies" object as an array or nested object.
    ///
    /// For each node (`dependent_node`) in the config file, the test ensures that any node
    /// specified within that node config's "dependencies" object (`required_node_name`) occurs in
    /// the node config file at a position before the dependent node.
    #[test]
    fn test_each_node_config_file_dependency_ordering() -> Result<(), anyhow::Error> {
        fn to_string(v: &serde_json::Value) -> String {
            v.as_str().unwrap().to_string()
        }

        // Flattens the provided JSON value to extract all child strings. This is used to extract
        // node dependency names from a node config's "dependencies" object even for nodes with a
        // more complex format.
        fn flatten_node_names(obj: &serde_json::Value) -> Vec<String> {
            use serde_json::Value;
            match obj {
                Value::String(s) => vec![s.to_string()],
                Value::Array(arr) => arr.iter().map(|v| flatten_node_names(v)).flatten().collect(),
                Value::Object(obj) => {
                    obj.values().map(|v| flatten_node_names(v)).flatten().collect()
                }
                e => panic!("Invalid JSON type in dependency object: {:?}", e),
            }
        }

        crate::utils::test_each_node_config_file(|config_file| {
            for (dependent_idx, dependent_node) in config_file.iter().enumerate() {
                if let Some(dependencies_obj) = dependent_node.get("dependencies") {
                    for required_node_name in flatten_node_names(dependencies_obj) {
                        let dependent_node_name = to_string(&dependent_node["name"]);
                        let required_node_index = config_file
                            .iter()
                            .position(|n| to_string(&n["name"]) == required_node_name);
                        match required_node_index {
                            Some(found_at_index) if found_at_index > dependent_idx => {
                                return Err(anyhow::format_err!(
                                    "Dependency {} must be specified before node {}",
                                    required_node_name,
                                    dependent_node_name
                                ));
                            }
                            Some(found_at_index) if found_at_index == dependent_idx => {
                                return Err(anyhow::format_err!(
                                    "Invalid to specify self as dependency for node {}",
                                    dependent_node_name
                                ));
                            }
                            None => {
                                return Err(anyhow::format_err!(
                                    "Missing dependency {} for node {}",
                                    required_node_name,
                                    dependent_node_name
                                ));
                            }
                            _ => {}
                        }
                    }
                }
            }

            Ok(())
        })
    }
}
