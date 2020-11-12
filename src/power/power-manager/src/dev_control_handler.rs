// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::log_if_err;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::utils::connect_proxy;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_device as fdev;
use fuchsia_inspect::{self as inspect, NumericProperty, Property};
use fuchsia_zircon as zx;
use serde_derive::Deserialize;
use serde_json as json;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: DeviceControlHandler
///
/// Summary: Provides an interface to control the power, performance, and sleep states of a devhost
///          device
///
/// Handles Messages:
///     - GetPerformanceState
///     - SetPerformanceState
///
/// Sends Messages: N/A
///
/// FIDL dependencies:
///     - fuchsia.device.Controller: the node uses this protocol to control the power, performance,
///       and sleep states of a devhost device

pub const MAX_PERF_STATES: u32 = fdev::MAX_DEVICE_PERFORMANCE_STATES;

/// A builder for constructing the DeviceControlhandler node
pub struct DeviceControlHandlerBuilder<'a> {
    driver_path: String,
    driver_proxy: Option<fdev::ControllerProxy>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> DeviceControlHandlerBuilder<'a> {
    pub fn new_with_driver_path(driver_path: String) -> Self {
        Self { driver_path, driver_proxy: None, inspect_root: None }
    }

    #[cfg(test)]
    pub fn new_with_proxy(driver_path: String, proxy: fdev::ControllerProxy) -> Self {
        Self { driver_path, driver_proxy: Some(proxy), inspect_root: None }
    }

    pub fn new_from_json(json_data: json::Value, _nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Config {
            driver_path: String,
        };

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
        };

        let data: JsonData = json::from_value(json_data).unwrap();
        Self::new_with_driver_path(data.config.driver_path)
    }

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    pub fn build(self) -> Result<Rc<DeviceControlHandler>, Error> {
        // Optionally use the default proxy
        let proxy = if self.driver_proxy.is_none() {
            connect_proxy::<fdev::ControllerMarker>(&self.driver_path)?
        } else {
            self.driver_proxy.unwrap()
        };

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        Ok(Rc::new(DeviceControlHandler {
            driver_path: self.driver_path.clone(),
            driver_proxy: proxy,
            inspect: InspectData::new(
                inspect_root,
                format!("DeviceControlHandler ({})", self.driver_path),
            ),
        }))
    }
}

pub struct DeviceControlHandler {
    driver_path: String,
    driver_proxy: fdev::ControllerProxy,

    /// A struct for managing Component Inspection data
    inspect: InspectData,
}

impl DeviceControlHandler {
    async fn handle_get_performance_state(&self) -> Result<MessageReturn, PowerManagerError> {
        fuchsia_trace::duration!(
            "power_manager",
            "DeviceControlHandler::handle_get_performance_state",
            "driver" => self.driver_path.as_str()
        );

        let result = self.get_performance_state().await;
        log_if_err!(result, "Failed to get performance state");
        fuchsia_trace::instant!(
            "power_manager",
            "DeviceControlHandler::get_performance_state_result",
            fuchsia_trace::Scope::Thread,
            "driver" => self.driver_path.as_str(),
            "result" => format!("{:?}", result).as_str()
        );

        match result {
            Ok(state) => Ok(MessageReturn::GetPerformanceState(state)),
            Err(e) => {
                self.inspect.get_performance_state_errors.add(1);
                Err(PowerManagerError::GenericError(e))
            }
        }
    }

    async fn get_performance_state(&self) -> Result<u32, Error> {
        let state =
            self.driver_proxy.get_current_performance_state().await.map_err(|e| {
                format_err!("{}: get_performance_state IPC failed: {}", self.name(), e)
            })?;

        Ok(state)
    }

    async fn handle_set_performance_state(
        &self,
        in_state: u32,
    ) -> Result<MessageReturn, PowerManagerError> {
        fuchsia_trace::duration!(
            "power_manager",
            "DeviceControlHandler::handle_set_performance_state",
            "driver" => self.driver_path.as_str(),
            "state" => in_state
        );

        let result = self.set_performance_state(in_state).await;
        log_if_err!(result, "Failed to set performance state");
        fuchsia_trace::instant!(
            "power_manager",
            "DeviceControlHandler::set_performance_state_result",
            fuchsia_trace::Scope::Thread,
            "driver" => self.driver_path.as_str(),
            "result" => format!("{:?}", result).as_str()
        );

        match result {
            Ok(_) => {
                self.inspect.perf_state.set(in_state.into());
                Ok(MessageReturn::SetPerformanceState)
            }
            Err(e) => {
                self.inspect.set_performance_state_errors.add(1);
                self.inspect.last_set_performance_state_error.set(format!("{}", e).as_str());
                Err(PowerManagerError::GenericError(e))
            }
        }
    }

    async fn set_performance_state(&self, in_state: u32) -> Result<(), Error> {
        // Make the FIDL call
        let (status, _out_state) =
            self.driver_proxy.set_performance_state(in_state).await.map_err(|e| {
                format_err!("{}: set_performance_state IPC failed: {}", self.name(), e)
            })?;

        // Check the status code
        zx::Status::ok(status).map_err(|e| {
            format_err!("{}: set_performance_state driver returned error: {}", self.name(), e)
        })?;

        Ok(())
    }
}

#[async_trait(?Send)]
impl Node for DeviceControlHandler {
    fn name(&self) -> String {
        format!("DeviceControlHandler ({})", self.driver_path)
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::GetPerformanceState => self.handle_get_performance_state().await,
            Message::SetPerformanceState(state) => self.handle_set_performance_state(*state).await,
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

struct InspectData {
    perf_state: inspect::UintProperty,
    get_performance_state_errors: inspect::UintProperty,
    set_performance_state_errors: inspect::UintProperty,
    last_set_performance_state_error: inspect::StringProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, name: String) -> Self {
        // Create a local root node and properties
        let root = parent.create_child(name);
        let perf_state = root.create_uint("performance_state", 0);
        let get_performance_state_errors = root.create_uint("get_performance_state_errors", 0);
        let set_performance_state_errors = root.create_uint("set_performance_state_errors", 0);
        let last_set_performance_state_error =
            root.create_string("last_set_performance_state_error", "");

        // Pass ownership of the new node to the parent node, otherwise it'll be dropped
        parent.record(root);

        InspectData {
            perf_state,
            get_performance_state_errors,
            set_performance_state_errors,
            last_set_performance_state_error,
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use futures::TryStreamExt;
    use inspect::assert_inspect_tree;
    use std::cell::Cell;

    fn setup_fake_driver(
        get_performance_state: impl Fn() -> u32 + 'static,
        mut set_performance_state: impl FnMut(u32) + 'static,
    ) -> fdev::ControllerProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fdev::ControllerMarker>().unwrap();
        fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fdev::ControllerRequest::GetCurrentPerformanceState { responder }) => {
                        let _ = responder.send(get_performance_state());
                    }
                    Some(fdev::ControllerRequest::SetPerformanceState {
                        requested_state,
                        responder,
                    }) => {
                        set_performance_state(requested_state as u32);
                        let _ = responder.send(zx::Status::OK.into_raw(), requested_state);
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        proxy
    }

    pub fn setup_test_node(
        get_performance_state: impl Fn() -> u32 + 'static,
        set_performance_state: impl FnMut(u32) + 'static,
    ) -> Rc<DeviceControlHandler> {
        DeviceControlHandlerBuilder::new_with_proxy(
            "Fake".to_string(),
            setup_fake_driver(get_performance_state, set_performance_state),
        )
        .build()
        .unwrap()
    }

    pub fn setup_simple_test_node() -> Rc<DeviceControlHandler> {
        let perf_state = Rc::new(Cell::new(0));
        let perf_state_clone_1 = perf_state.clone();
        let perf_state_clone_2 = perf_state.clone();
        let get_performance_state = move || perf_state_clone_1.get();
        let set_performance_state = move |state| {
            perf_state_clone_2.set(state);
        };
        setup_test_node(get_performance_state, set_performance_state)
    }

    /// Tests that an unsupported message is handled gracefully and an Unsupported error is returned
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let node = setup_simple_test_node();
        match node.handle_message(&Message::ReadTemperature).await {
            Err(PowerManagerError::Unsupported) => {}
            e => panic!("Unexpected return value: {:?}", e),
        }
    }

    /// Tests that the Get/SetPerformanceState messages cause the node to call the appropriate
    /// device controller FIDL APIs.
    #[fasync::run_singlethreaded(test)]
    async fn test_performance_state() {
        let node = setup_simple_test_node();

        // Send SetPerformanceState message to set a state of 1
        let commanded_perf_state = 1;
        match node
            .handle_message(&Message::SetPerformanceState(commanded_perf_state))
            .await
            .unwrap()
        {
            MessageReturn::SetPerformanceState => {}
            e => panic!("Unexpected return value: {:?}", e),
        }

        // Verify GetPerformanceState reads back the same state
        let received_perf_state =
            match node.handle_message(&Message::GetPerformanceState).await.unwrap() {
                MessageReturn::GetPerformanceState(state) => state,
                e => panic!("Unexpected return value: {:?}", e),
            };
        assert_eq!(commanded_perf_state, received_perf_state);

        // Send SetPerformanceState message to set a state of 2
        let commanded_perf_state = 2;
        match node
            .handle_message(&Message::SetPerformanceState(commanded_perf_state))
            .await
            .unwrap()
        {
            MessageReturn::SetPerformanceState => {}
            e => panic!("Unexpected return value: {:?}", e),
        }

        // Verify GetPerformanceState reads back the same state
        let received_perf_state =
            match node.handle_message(&Message::GetPerformanceState).await.unwrap() {
                MessageReturn::GetPerformanceState(state) => state,
                e => panic!("Unexpected return value: {:?}", e),
            };
        assert_eq!(commanded_perf_state, received_perf_state);
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let inspector = inspect::Inspector::new();
        let _node = DeviceControlHandlerBuilder::new_with_proxy(
            "Fake".to_string(),
            fidl::endpoints::create_proxy::<fdev::ControllerMarker>().unwrap().0,
        )
        .with_inspect_root(inspector.root())
        .build()
        .unwrap();

        assert_inspect_tree!(
            inspector,
            root: {
                "DeviceControlHandler (Fake)": contains {}
            }
        );
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "DeviceControlHandler",
            "name": "dev_control",
            "config": {
                "driver_path": "/dev/class/cpu-ctrl/000"
            }
        });
        let _ = DeviceControlHandlerBuilder::new_from_json(json_data, &HashMap::new());
    }
}
