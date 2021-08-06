// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log_if_err;
use crate::message::Message;
use crate::node::Node;
use crate::shutdown_request::{RebootReason, ShutdownRequest};
use crate::temperature_handler::TemperatureFilter;
use crate::thermal_limiter;
use crate::types::{Celsius, Seconds, ThermalLoad};
use crate::utils::get_current_timestamp;
use anyhow::{format_err, Error, Result};
use async_trait::async_trait;
use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, Property};
use futures::{
    future::{join_all, FutureExt, LocalBoxFuture},
    stream::FuturesUnordered,
    StreamExt, TryFutureExt as _,
};
use log::*;
use serde_derive::Deserialize;
use serde_json as json;
use std::cell::Cell;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: ThermalLoadDriver
///
/// Summary: The purpose of this node is to determine and communicate the thermal load value in the
///   system.
///
///   The behavior is unique from the ThermalPolicy node (which is also capable of determining and
///   communicating thermal load values) because it is purpose-built with the ability to source
///   multiple temperature sensors to determine the thermal load value. It also differs from
///   ThermalPolicy because it calculates ThermalLoad based on observed filtered temperature with
///   respect to configured onset/reboot temperatures, whereas ThermalPolicy uses integral error
///   (where "error" is the filtered temperature delta with respect to a configured target
///   temperature).
///
///   To do this, the node polls each of the provided temperature handler nodes at the specified
///   polling interval. The temperature is used to calculate a per-sensor thermal load value (each
///   sensor may configure its own unique onset and reboot temperatures which define that sensor's
///   thermal load range). The end-result thermal load value is determined to be the max from each
///   sensor's calculated thermal load. This new thermal load value is then communicated to each
///   node specified by `thermal_load_notify_nodes`.
///
/// Handles Messages: N/A
///
/// Sends Messages:
///     - SystemShutdown
///     - UpdateThermalLoad
///
/// FIDL dependencies: N/A

pub struct ThermalLoadDriverBuilder<'a> {
    poll_interval: Seconds,
    temperature_inputs: Vec<TemperatureInput>,
    system_shutdown_node: Rc<dyn Node>,
    thermal_load_notify_nodes: Vec<Rc<dyn Node>>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> ThermalLoadDriverBuilder<'a> {
    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct TemperatureConfig {
            temperature_handler_node_name: String,
            onset_temperature_c: f64,
            reboot_temperature_c: f64,
        }

        #[derive(Deserialize)]
        struct Config {
            poll_interval_s: f64,
            filter_time_constant_s: f64,
            temperature_configs: Vec<TemperatureConfig>,
        }

        #[derive(Deserialize)]
        struct Dependencies {
            system_shutdown_node: String,
            thermal_load_notify_nodes: Vec<String>,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
            dependencies: Dependencies,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        Self {
            poll_interval: Seconds(data.config.poll_interval_s),
            system_shutdown_node: nodes[&data.dependencies.system_shutdown_node].clone(),
            temperature_inputs: data
                .config
                .temperature_configs
                .iter()
                .map(|config| TemperatureInput {
                    name: config.temperature_handler_node_name.clone(),
                    temperature_getter: Box::new(GetFilteredTemperature {
                        filter: TemperatureFilter::new(
                            nodes[&config.temperature_handler_node_name].clone(),
                            Seconds(data.config.filter_time_constant_s),
                        ),
                    }),
                    onset_temperature: Celsius(config.onset_temperature_c),
                    reboot_temperature: Celsius(config.reboot_temperature_c),
                })
                .collect(),
            thermal_load_notify_nodes: data
                .dependencies
                .thermal_load_notify_nodes
                .iter()
                .map(|node_name| nodes[node_name].clone())
                .collect(),
            inspect_root: None,
        }
    }

    pub fn build<'b>(
        self,
        futures_out: &FuturesUnordered<LocalBoxFuture<'b, ()>>,
    ) -> Result<Rc<ThermalLoadDriver>, Error> {
        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        let node = Rc::new(ThermalLoadDriver {
            poll_interval: self.poll_interval,
            inspect: InspectData::new(
                inspect_root,
                "ThermalLoadDriver".to_string(),
                &self.temperature_inputs,
            ),
            temperature_inputs: self.temperature_inputs,
            thermal_load: Cell::new(ThermalLoad(0)),
            system_shutdown_node: self.system_shutdown_node,
            thermal_load_notify_nodes: self.thermal_load_notify_nodes,
        });

        futures_out.push(node.clone().polling_loop());
        Ok(node)
    }
}

pub struct ThermalLoadDriver {
    /// Time interval in seconds that the node will read a new temperature value from all
    /// temperature inputs.
    poll_interval: Seconds,

    inspect: InspectData,

    /// A vector of all temperature input sources. On each interval, every input in the vector is
    /// polled for their new thermal load values.
    temperature_inputs: Vec<TemperatureInput>,

    /// Current thermal load cached value. Used to detect when the thermal load value has changed.
    thermal_load: Cell<ThermalLoad>,

    /// Node that we use to initiate a reboot when any temperature input source reaches their
    /// configured reboot temperature.
    system_shutdown_node: Rc<dyn Node>,

    /// Nodes that we notify when the thermal load value has changed.
    thermal_load_notify_nodes: Vec<Rc<dyn Node>>,
}

impl ThermalLoadDriver {
    /// Returns a future that calls `update_thermal_load` at `poll_interval`.
    fn polling_loop<'a>(self: Rc<Self>) -> LocalBoxFuture<'a, ()> {
        let mut periodic_timer = fasync::Interval::new(self.poll_interval.into());

        async move {
            while let Some(()) = periodic_timer.next().await {
                log_if_err!(self.update_thermal_load().await, "Error while polling temperature");
            }
        }
        .boxed_local()
    }

    /// Polls all temperature inputs to get their new individual thermal load values. The new
    /// thermal load value is determined to be the max of these individual thermal loads. When the
    /// thermal load value changes, each node in `thermal_load_notify_nodes` are sent a message to
    /// update them.
    async fn update_thermal_load(&self) -> Result<(), Error> {
        // Poll each temperature input source in a separate future and join them, filtering out any
        // error results
        let thermal_loads = join_all(
            self.temperature_inputs
                .iter()
                .map(|d| d.get_thermal_load().map_ok(move |r| (&d.name, r))),
        )
        .await
        .into_iter()
        .filter_map(|result| result.ok());

        // Get the max of all the individual thermal loads
        let new_thermal_load = thermal_loads
            .map(|(name, load)| {
                self.inspect.log_thermal_load(name, load);
                load
            })
            .max()
            .ok_or_else(|| format_err!("All thermal load updates failed"))?;

        debug_assert!(new_thermal_load <= thermal_limiter::MAX_THERMAL_LOAD);

        // If the thermal load value changed, then update all nodes in `thermal_load_notify_nodes`,
        // or initiate a system reboot if any temperature input has reached their configured reboot
        // temperature.
        if new_thermal_load != self.thermal_load.get() {
            self.thermal_load.set(new_thermal_load);

            if new_thermal_load >= thermal_limiter::MAX_THERMAL_LOAD {
                log_if_err!(
                    self.send_message(
                        &self.system_shutdown_node,
                        &Message::SystemShutdown(ShutdownRequest::Reboot(
                            RebootReason::HighTemperature,
                        )),
                    )
                    .await,
                    "Failed to send shutdown request"
                );
            } else {
                let results = self
                    .send_message_to_many(
                        &self.thermal_load_notify_nodes,
                        &Message::UpdateThermalLoad(new_thermal_load),
                    )
                    .await;

                match results.iter().find(|r| r.is_err()) {
                    None => {}
                    Some(err_result) => {
                        log_if_err!(err_result, "Failed to send thermal load update")
                    }
                }
            }
        }

        Ok(())
    }
}

/// Configuration and polling mechanism for a single temperature handler.
struct TemperatureInput {
    /// Name assigned to this temperature handler. Used mainly for logging and Inspect.
    name: String,

    /// Provides a mechanism to poll for the current temperature. Trait-ified for testability.
    temperature_getter: Box<dyn GetTemperature>,

    /// Temperature at which thermal load will begin to increase. A temperature value of
    /// `onset_temperature` corresponds to a thermal load of 0. Beyond `onset_temperature`, thermal
    /// load will increase linearly with temperature until reaching `reboot_temperature.
    onset_temperature: Celsius,

    /// Temperature at which this node will initiate a system reboot due to critical temperature. A
    /// temperature value of `reboot_temperature` corresponds to a thermal load of 100.
    reboot_temperature: Celsius,
}

impl TemperatureInput {
    /// Gets the current thermal load value for this temperature handler.
    ///
    /// The function will first poll the temperature handler to retrieve the latest filtered
    /// temperature value. The temperature is then converted to a thermal load value by also
    /// considering `onset_temperature` and `reboot_temperature`.
    async fn get_thermal_load(&self) -> Result<ThermalLoad> {
        match self.temperature_getter.get_temperature().await {
            Ok(temperature) => Ok(temperature_to_thermal_load(
                temperature,
                self.onset_temperature,
                self.reboot_temperature,
            )),
            Err(e) => {
                log::error!("Failed to get temperature from {} ({})", self.name, e);
                Err(e)
            }
        }
    }
}

/// Trait to provide a `get_temperature` function. In real code, `GetFilteredTemperature` is the
/// only implementer. Test code will use this trait to inject custom temperature providers.
#[async_trait(?Send)]
trait GetTemperature {
    async fn get_temperature(&self) -> Result<Celsius>;
}

struct GetFilteredTemperature {
    filter: TemperatureFilter,
}

#[async_trait(?Send)]
impl GetTemperature for GetFilteredTemperature {
    /// Gets the current filtered temperature.
    async fn get_temperature(&self) -> Result<Celsius> {
        self.filter.get_temperature(get_current_timestamp()).await.map(|readings| readings.filtered)
    }
}

/// Converts temperature to thermal load as a function of temperature, onset temperature, and reboot
/// temperature.
fn temperature_to_thermal_load(
    temperature: Celsius,
    onset_temperature: Celsius,
    reboot_temperature: Celsius,
) -> ThermalLoad {
    if temperature < onset_temperature {
        ThermalLoad(0)
    } else if temperature > reboot_temperature {
        thermal_limiter::MAX_THERMAL_LOAD
    } else {
        ThermalLoad(
            ((temperature - onset_temperature).0 / (reboot_temperature - onset_temperature).0
                * thermal_limiter::MAX_THERMAL_LOAD.0 as f64) as u32,
        )
    }
}

// Implementing these traits lets us call `max()` on an iterator of ThermalLoads
impl Eq for ThermalLoad {}
impl Ord for ThermalLoad {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        if self.0 < other.0 {
            std::cmp::Ordering::Less
        } else if self.0 > other.0 {
            std::cmp::Ordering::Greater
        } else {
            std::cmp::Ordering::Equal
        }
    }
}

#[async_trait(?Send)]
impl Node for ThermalLoadDriver {
    fn name(&self) -> String {
        "ThermalLoadDriver".to_string()
    }
}

struct InspectData {
    thermal_load_properties: HashMap<String, inspect::UintProperty>,
}

impl InspectData {
    fn new(
        parent: &inspect::Node,
        name: String,
        temperature_inputs: &Vec<TemperatureInput>,
    ) -> Self {
        let root = parent.create_child(name);
        let temperature_inputs_node = root.create_child("temperature_inputs");

        let mut thermal_load_properties = HashMap::new();
        for temperature_input in temperature_inputs {
            let child = temperature_inputs_node.create_child(&temperature_input.name);
            child.record_double("onset_temperature_c", temperature_input.onset_temperature.0);
            child.record_double("reboot_temperature_c", temperature_input.reboot_temperature.0);
            thermal_load_properties
                .insert(temperature_input.name.clone(), child.create_uint("thermal_load", 0));
            temperature_inputs_node.record(child);
        }

        root.record(temperature_inputs_node);
        parent.record(root);

        InspectData { thermal_load_properties }
    }

    fn log_thermal_load(&self, name: &str, load: ThermalLoad) {
        match self.thermal_load_properties.get(name) {
            Some(property) => {
                property.set(load.0.into());
            }
            None => {
                error!("Unknown temperature input name: {}", name);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::mock_node::{create_dummy_node, MessageMatcher, MockNodeMaker};
    use crate::{msg_eq, msg_ok_return};
    use fuchsia_inspect::assert_data_tree;

    /// Tests that each node config file has proper configuration for ThermalLoadDriver entries. The
    /// test ensures that any nodes that are specified inside the temperature_configs object are
    /// also specified under "dependencies". This is important not only for tracking the true
    /// dependencies of a node, but also to be able to take advantage of the node dependency tests
    /// in power_manager.rs (e.g. test_each_node_config_file_dependency_ordering).
    #[test]
    pub fn test_config_files() -> Result<(), anyhow::Error> {
        crate::utils::test_each_node_config_file(|config_file| {
            let thermal_load_driver_nodes =
                config_file.iter().filter(|n| n["type"] == "ThermalLoadDriver");

            for node in thermal_load_driver_nodes {
                let temperature_handler_node_deps = node["dependencies"].as_object().unwrap()
                    ["temperature_handler_node_names"]
                    .as_array()
                    .unwrap()
                    .iter()
                    .map(|node_name| node_name.as_str().unwrap())
                    .collect::<Vec<_>>();
                let temperature_config_node_refs = node["config"]["temperature_configs"]
                    .as_array()
                    .unwrap()
                    .iter()
                    .map(|temperature_config| {
                        temperature_config["temperature_handler_node_name"].as_str().unwrap()
                    })
                    .collect::<Vec<_>>();

                if temperature_handler_node_deps != temperature_config_node_refs {
                    return Err(format_err!(
                        "TemperatureHandler nodes listed under \"dependencies\" must match
                        the TemperatureHandler nodes referenced in \"temperature_configs\""
                    ));
                }
            }

            Ok(())
        })
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "ThermalLoadDriver",
            "name": "thermal_load_driver",
            "config": {
                "poll_interval_s": 3.4,
                "filter_time_constant_s": 5.6,
                "temperature_configs": [
                    {
                        "temperature_handler_node_name": "temp_sensor_1",
                        "onset_temperature_c": 50.0,
                        "reboot_temperature_c": 100.0,
                    },
                    {
                        "temperature_handler_node_name": "temp_sensor_2",
                        "onset_temperature_c": 50.0,
                        "reboot_temperature_c": 100.0,
                    }
                ]
            },
            "dependencies": {
                "system_shutdown_node": "shutdown",
                "temperature_handler_node_names": [
                    "temp_sensor_1",
                    "temp_sensor_2"
                ],
                "thermal_load_notify_nodes": [
                    "thermal_limiter"
                ]
              },
        });

        let mut nodes: HashMap<String, Rc<dyn Node>> = HashMap::new();
        nodes.insert("temp_sensor_1".to_string(), create_dummy_node());
        nodes.insert("temp_sensor_2".to_string(), create_dummy_node());
        nodes.insert("shutdown".to_string(), create_dummy_node());
        nodes.insert("thermal_limiter".to_string(), create_dummy_node());
        let _ = ThermalLoadDriverBuilder::new_from_json(json_data, &nodes);
    }

    /// A test-only struct for getting/setting fake temperature values. Mostly, this is useful to
    /// bypass the temperature filtering that the real code uses and therefore makes tests a bit
    /// more ergonomic.
    struct FakeTemperatureGetter {
        temperature_cache: Rc<Cell<Option<Celsius>>>,
    }
    impl FakeTemperatureGetter {
        fn new(temperature_cache: Rc<Cell<Option<Celsius>>>) -> Self {
            Self { temperature_cache }
        }
    }
    #[async_trait(?Send)]
    impl GetTemperature for FakeTemperatureGetter {
        async fn get_temperature(&self) -> Result<Celsius> {
            Ok(self.temperature_cache.take().unwrap())
        }
    }

    struct FakeTemperatureSetter {
        temperature_cache: Rc<Cell<Option<Celsius>>>,
    }
    impl FakeTemperatureSetter {
        fn new(temperature_cache: Rc<Cell<Option<Celsius>>>) -> Self {
            Self { temperature_cache }
        }
        fn set_fake_temperature(&self, temperature: Celsius) {
            self.temperature_cache.set(Some(temperature))
        }
    }

    /// Creates a new FakeTemperatureGetter and FakeTemperatureSetter that are bound together by a
    /// shared "temperature cache".
    fn fake_temperature_pair() -> (FakeTemperatureGetter, FakeTemperatureSetter) {
        let temperature_cache = Rc::new(Cell::new(None));
        (
            FakeTemperatureGetter::new(temperature_cache.clone()),
            FakeTemperatureSetter::new(temperature_cache),
        )
    }

    /// Tests that with multiple temperature handler nodes providing input to the ThermalLoadDriver,
    /// both are correctly polled for temperature and the correct thermal load value is ultimately
    /// determined and messaged out to other nodes.
    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_temperature_inputs() {
        let mut mock_maker = MockNodeMaker::new();
        let mock_thermal_load_receiver = mock_maker.make("mock_thermal_load_receiver", vec![]);
        let (fake_getter_1, fake_setter_1) = fake_temperature_pair();
        let (fake_getter_2, fake_setter_2) = fake_temperature_pair();

        let thermal_load_driver = ThermalLoadDriverBuilder {
            poll_interval: Seconds(1.0),
            temperature_inputs: vec![
                TemperatureInput {
                    name: "FakeTemperature1".to_string(),
                    temperature_getter: Box::new(fake_getter_1),
                    onset_temperature: Celsius(0.0),
                    reboot_temperature: Celsius(100.0),
                },
                TemperatureInput {
                    name: "FakeTemperature2".to_string(),
                    temperature_getter: Box::new(fake_getter_2),
                    onset_temperature: Celsius(0.0),
                    reboot_temperature: Celsius(100.0),
                },
            ],
            system_shutdown_node: create_dummy_node(),
            thermal_load_notify_nodes: vec![mock_thermal_load_receiver.clone()],
            inspect_root: None,
        }
        .build(&FuturesUnordered::new())
        .unwrap();

        // Set some arbitrary fake temperature inputs and expect to appropriate thermal load value
        // to be messaged out to the mock thermal load receiver node
        fake_setter_1.set_fake_temperature(Celsius(20.0));
        fake_setter_2.set_fake_temperature(Celsius(40.0));
        mock_thermal_load_receiver.add_msg_response_pair((
            msg_eq!(UpdateThermalLoad(ThermalLoad(40))),
            msg_ok_return!(UpdateThermalLoad),
        ));
        assert!(thermal_load_driver.update_thermal_load().await.is_ok());

        // Set some arbitrary fake temperature inputs and expect to appropriate thermal load value
        // to be messaged out to the mock thermal load receiver node
        fake_setter_1.set_fake_temperature(Celsius(60.0));
        fake_setter_2.set_fake_temperature(Celsius(50.0));
        mock_thermal_load_receiver.add_msg_response_pair((
            msg_eq!(UpdateThermalLoad(ThermalLoad(60))),
            msg_ok_return!(UpdateThermalLoad),
        ));
        assert!(thermal_load_driver.update_thermal_load().await.is_ok());

        // Set some arbitrary fake temperature inputs and expect to appropriate thermal load value
        // to be messaged out to the mock thermal load receiver node
        fake_setter_1.set_fake_temperature(Celsius(80.0));
        fake_setter_2.set_fake_temperature(Celsius(80.0));
        mock_thermal_load_receiver.add_msg_response_pair((
            msg_eq!(UpdateThermalLoad(ThermalLoad(80))),
            msg_ok_return!(UpdateThermalLoad),
        ));
        assert!(thermal_load_driver.update_thermal_load().await.is_ok());
    }

    /// Tests that when any of the temperature handler input nodes exceed `reboot_temperature`, then
    /// the ThermalLoadDriver node initiates a system reboot due to high temperature.
    #[fasync::run_singlethreaded(test)]
    async fn test_trigger_shutdown() {
        let mut mock_maker = MockNodeMaker::new();
        let system_shutdown_node = mock_maker.make(
            "mock_system_shutdown_node",
            vec![(
                msg_eq!(SystemShutdown(ShutdownRequest::Reboot(RebootReason::HighTemperature))),
                msg_ok_return!(SystemShutdown),
            )],
        );
        let (fake_getter, fake_setter) = fake_temperature_pair();
        let thermal_load_driver = ThermalLoadDriverBuilder {
            poll_interval: Seconds(1.0),
            temperature_inputs: vec![TemperatureInput {
                name: "FakeTemperature1".to_string(),
                temperature_getter: Box::new(fake_getter),
                onset_temperature: Celsius(0.0),
                reboot_temperature: Celsius(100.0),
            }],
            system_shutdown_node,
            thermal_load_notify_nodes: vec![],
            inspect_root: None,
        }
        .build(&FuturesUnordered::new())
        .unwrap();

        fake_setter.set_fake_temperature(Celsius(100.0));
        assert!(thermal_load_driver.update_thermal_load().await.is_ok());
    }

    /// Tests that the expected Inspect properties are present.
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let inspector = inspect::Inspector::new();

        // Create two temperature inputs and initialize with distinct arbitrary values
        let (fake_getter1, fake_setter1) = fake_temperature_pair();
        let (fake_getter2, fake_setter2) = fake_temperature_pair();
        fake_setter1.set_fake_temperature(Celsius(30.0));
        fake_setter2.set_fake_temperature(Celsius(60.0));

        let thermal_load_driver = ThermalLoadDriverBuilder {
            poll_interval: Seconds(1.0),
            temperature_inputs: vec![
                TemperatureInput {
                    name: "FakeTemperature1".to_string(),
                    temperature_getter: Box::new(fake_getter1),
                    onset_temperature: Celsius(0.0),
                    reboot_temperature: Celsius(100.0),
                },
                TemperatureInput {
                    name: "FakeTemperature2".to_string(),
                    temperature_getter: Box::new(fake_getter2),
                    onset_temperature: Celsius(10.0),
                    reboot_temperature: Celsius(110.0),
                },
            ],
            system_shutdown_node: create_dummy_node(),
            thermal_load_notify_nodes: vec![],
            inspect_root: Some(inspector.root()),
        }
        .build(&FuturesUnordered::new())
        .unwrap();

        // Run the `update_thermal_load` loop once to Inspect will be populated
        assert!(thermal_load_driver.update_thermal_load().await.is_ok());

        // Verify the expected thermal load values are present for both temperature inputs
        assert_data_tree!(
            inspector,
            root: {
                ThermalLoadDriver: {
                    temperature_inputs: {
                        FakeTemperature1: {
                            onset_temperature_c: 0.0,
                            reboot_temperature_c: 100.0,
                            thermal_load: 30u64
                        },
                        FakeTemperature2: {
                            onset_temperature_c: 10.0,
                            reboot_temperature_c: 110.0,
                            thermal_load: 50u64
                        }
                    }
                },
            }
        );
    }
}
