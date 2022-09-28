// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::node::Node;
use anyhow::{format_err, Context, Error};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use fuchsia_inspect::component;
use futures::{
    future::{join_all, LocalBoxFuture},
    stream::{FuturesUnordered, StreamExt},
};
use log::*;
use power_manager_config_lib;
use serde_json as json;
use std::collections::HashMap;
use std::rc::Rc;

// nodes
use crate::{
    activity_handler, cpu_control_handler, cpu_device_handler, cpu_manager, cpu_stats_handler,
    crash_report_handler, debug_service, dev_control_handler, driver_manager_handler,
    input_settings_handler, lid_shutdown, platform_metrics, shutdown_watcher, syscall_handler,
    system_power_mode_handler, system_profile_handler, system_shutdown_handler,
    temperature_handler, thermal_load_driver, thermal_policy, thermal_shutdown,
    thermal_state_handler,
};

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

        // Required call to serve the inspect tree
        let inspector = component::inspector();
        inspect_runtime::serve(inspector, &mut fs).context("Failed to serve inspect")?;

        let config = power_manager_config_lib::Config::take_from_startup_handle();
        config.record_inspect(fuchsia_inspect::component::inspector().root());
        log_config(&config);

        // Create the nodes according to the config file
        let node_futures = FuturesUnordered::new();
        self.create_nodes_from_config(&config.node_config_path, &mut fs, &node_futures)
            .await
            .context("Failed to create nodes from config")?;

        if config.enable_debug_service {
            debug_service::publish_debug_service(fs.dir("svc"), self.nodes.clone());
        }

        // Begin serving FIDL requests. It's important to do this after creating nodes but before
        // initializing them, since some nodes depend on incoming FIDL requests for their `init()`
        // process.
        fs.take_and_serve_directory_handle()?;

        // Technically these two tasks could be merged into a single `fuchsia_async::Task`. However,
        // without a clear benefit to do so, it reads nicer to keep the two buckets of futures
        // separated. Looking forward, we may try to get rid of the concept of `node_futures` anyway
        // (opting instead for nodes to own their own `fuchsia_async::Task` which polls any
        // long-running futures they may require).
        let node_futures_task = fasync::Task::local(node_futures.collect::<()>());
        let service_fs_task = fasync::Task::local(fs.collect::<()>());

        self.init_nodes().await?;

        info!("Setup complete");

        // Run the ServiceFs (handles incoming request streams) and node futures. This future never
        // completes.
        futures::join!(service_fs_task, node_futures_task);

        Err(format_err!("Tasks completed unexpectedly"))
    }

    /// Create the nodes by reading and parsing the node config JSON file.
    async fn create_nodes_from_config<'a, 'b, 'c>(
        &mut self,
        node_config_path: &str,
        service_fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
        node_futures: &FuturesUnordered<LocalBoxFuture<'c, ()>>,
    ) -> Result<(), Error> {
        let contents = std::fs::read_to_string(node_config_path)?;
        let json_data: json::Value = serde_json5::from_str(&contents)
            .context(format!("Failed to parse file {}", node_config_path))?;

        info!("Creating nodes from config file: {}", node_config_path);
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
            info!("Creating node {}", node_config["name"]);
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
        let node_name = json_data["name"].clone();
        let _log_warning_task = fasync::Task::local(async move {
            fasync::Timer::new(fasync::Duration::from_seconds(30)).await;
            warn!("Creating {} not complete after 30s", node_name);
        });

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
                    .build()?
            }
            "CpuDeviceHandler" => {
                cpu_device_handler::CpuDeviceHandlerBuilder::new_from_json(json_data, &self.nodes)
                    .build()
                    .await?
            }
            "CpuManager" => {
                cpu_manager::CpuManagerBuilder::new_from_json(json_data, &self.nodes)
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
                .build()?
            }
            "InputSettingsHandler" => {
                input_settings_handler::InputSettingsHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                )
                .build(node_futures)?
            }
            "LidShutdown" => {
                lid_shutdown::LidShutdownBuilder::new_from_json(json_data, &self.nodes).build()?
            }
            "PlatformMetrics" => {
                platform_metrics::PlatformMetricsBuilder::new_from_json(json_data, &self.nodes)
                    .build(node_futures)?
            }
            "SyscallHandler" => syscall_handler::SyscallHandlerBuilder::new().build().await?,
            "SystemPowerModeHandler" => {
                system_power_mode_handler::SystemPowerModeHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                    service_fs,
                )
                .build()?
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
            "TemperatureHandler" => temperature_handler::TemperatureHandlerBuilder::new_from_json(
                json_data,
                &self.nodes,
            )
            .build()?,
            "ThermalLoadDriver" => {
                thermal_load_driver::ThermalLoadDriverBuilder::new_from_json(json_data, &self.nodes)
                    .build()?
            }
            "ThermalPolicy" => {
                thermal_policy::ThermalPolicyBuilder::new_from_json(json_data, &self.nodes)
                    .build(node_futures)
                    .await?
            }
            "ThermalShutdown" => {
                thermal_shutdown::ThermalShutdownBuilder::new_from_json(json_data, &self.nodes)
                    .build(node_futures)?
            }
            "ThermalStateHandler" => {
                thermal_state_handler::ThermalStateHandlerBuilder::new_from_json(
                    json_data,
                    &self.nodes,
                    service_fs,
                )
                .build()?
            }
            unknown => panic!("Unknown node type: {}", unknown),
        })
    }

    async fn init_nodes(&self) -> Result<(), Error> {
        info!("Initializing nodes");

        join_all(self.nodes.iter().map(|node| async move {
            let node_name = node.0.clone();
            let _log_warning_task = fasync::Task::local(async move {
                fasync::Timer::new(fasync::Duration::from_seconds(30)).await;
                warn!("Init {} not complete after 30s", node_name);
            });
            node.1.init().await.context(format!("Failed to init node: {}", node.0))
        }))
        .await
        .into_iter()
        .collect::<Result<Vec<_>, _>>()
        .map(|_| ())
    }
}

fn log_config(config: &power_manager_config_lib::Config) {
    let power_manager_config_lib::Config { enable_debug_service, node_config_path } = config;
    info!(
        "Configuration: enable_debug_service={} node_config_path={}",
        enable_debug_service, node_config_path
    );
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

    /// Tests that if any node's init() fails, then the `init_nodes()` function returns an error.
    #[fasync::run_singlethreaded(test)]
    async fn test_init_failure() {
        use async_trait::async_trait;

        struct InitSuccessNode;
        #[async_trait(?Send)]
        impl Node for InitSuccessNode {
            fn name(&self) -> String {
                "InitSuccessNode".to_string()
            }
        }

        struct InitFailureNode;
        #[async_trait(?Send)]
        impl Node for InitFailureNode {
            fn name(&self) -> String {
                "InitFailureNode".to_string()
            }

            async fn init(&self) -> Result<(), Error> {
                Err(format_err!("Init failure"))
            }
        }

        let success_node = Rc::new(InitSuccessNode {});
        let failure_node = Rc::new(InitFailureNode {});

        let power_manager = PowerManager {
            nodes: HashMap::from([
                ("init_success_node".into(), success_node as Rc<dyn Node>),
                ("init_failure_node".into(), failure_node as Rc<dyn Node>),
            ]),
        };

        assert!(power_manager.init_nodes().await.is_err());
    }
}
