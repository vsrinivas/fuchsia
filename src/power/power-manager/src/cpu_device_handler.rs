// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::dev_control_handler::{self, DeviceControlHandler, DeviceControlHandlerBuilder};
use crate::error::PowerManagerError;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::{Hertz, PState, Volts};
use crate::utils::connect_to_driver;
use anyhow::{Context as _, Error};
use async_trait::async_trait;
use fidl_fuchsia_hardware_cpu_ctrl as fcpu_ctrl;
use fuchsia_inspect as inspect;
use serde_derive::Deserialize;
use serde_json as json;
use std::collections::HashMap;
use std::rc::Rc;

/// Node: CpuDeviceHandler
///
/// Summary: Provides an interface to interact with a CPU driver, both as a generic driver via
///          fuchsia.device.Controller and specifically as a CPU driver via
///          fuchsia.hardware.cpu_ctrl.Device. Light wrapper around DeviceControlHandler.
///          Similar to CpuControlHandler in its logical management of a single CPU device, but is
///          more narrowly-scoped, as it does not administer thermal policy.
///
/// Handles Messages:
///     - GetPerformanceState
///     - SetPerformanceState
///     - GetCpuPerformanceStates
///
/// Sends Messages (via proxy to owned DeviceControlHandler):
///     - GetPerformanceState
///     - SetPerformanceState
///
/// FIDL dependencies:
///     - fuchsia.device.Controller: used via the owned DeviceControlHandler to control the
///       performance states of a CPU device
///     - fuchsia.hardware.cpu_ctrl.Device: used to query descriptions of CPU performance states
//
// TODO(fxbug.dev/84191): Update summary when CpuControlHandler is removed.

/// Builder struct for CpuDeviceHandler.
pub struct CpuDeviceHandlerBuilder<'a, 'b> {
    /// Path to the CPU driver
    driver_path: String,

    /// Builder for the DeviceControlHandler that CpuDeviceHandler will own
    dev_handler_builder: DeviceControlHandlerBuilder<'b>,

    cpu_ctrl_proxy: Option<fcpu_ctrl::DeviceProxy>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a, 'b> CpuDeviceHandlerBuilder<'a, 'b> {
    pub fn new_from_json(json_data: json::Value, _nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Config {
            driver_path: String,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        Self::new_with_driver_path(data.config.driver_path)
    }

    /// Constructs a CpuDeviceHandlerBuilder from the provided CPU driver path
    pub fn new_with_driver_path(driver_path: String) -> Self {
        Self {
            driver_path: driver_path.clone(),
            dev_handler_builder: DeviceControlHandlerBuilder::new().driver_path(&driver_path),
            cpu_ctrl_proxy: None,
            inspect_root: None,
        }
    }

    /// Test-only interface to construct a builder with fake proxies
    #[cfg(test)]
    fn new_with_proxies(
        driver_path: String,
        controller_proxy: fidl_fuchsia_device::ControllerProxy,
        cpu_ctrl_proxy: fcpu_ctrl::DeviceProxy,
    ) -> Self {
        let dev_handler_builder = DeviceControlHandlerBuilder::new()
            .driver_path(&driver_path)
            .driver_proxy(controller_proxy);
        Self {
            driver_path,
            dev_handler_builder,
            cpu_ctrl_proxy: Some(cpu_ctrl_proxy),
            inspect_root: None,
        }
    }

    /// Test-only interface to override the Inspect root
    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    /// Retrieves all P-states from the provided cpu_ctrl proxy
    async fn get_pstates(
        cpu_driver_path: &str,
        cpu_ctrl_proxy: &fcpu_ctrl::DeviceProxy,
    ) -> Result<Vec<PState>, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuDeviceHandler::get_pstates",
            "driver" => cpu_driver_path
        );

        // Query P-state metadata from the cpu_ctrl interface. Each supported performance state
        // has accompanying P-state metadata.
        let mut pstates = Vec::new();

        for i in 0..dev_control_handler::MAX_PERF_STATES {
            if let Ok(info) = cpu_ctrl_proxy.get_performance_state_info(i).await? {
                pstates.push(PState {
                    frequency: Hertz(info.frequency_hz as f64),
                    voltage: Volts(info.voltage_uv as f64 / 1e6),
                })
            } else {
                break;
            }
        }

        Ok(pstates)
    }

    /// Checks that the given list of P-states satisfies the following conditions:
    ///  - Contains at least one element;
    ///  - Is primarily sorted by frequency;
    ///  - Is strictly secondarily sorted by voltage.
    fn validate_pstates(pstates: &Vec<PState>) -> Result<(), Error> {
        if pstates.len() == 0 {
            anyhow::bail!("Must have at least one P-state");
        } else if pstates.len() > 1 {
            for pair in pstates.as_slice().windows(2) {
                if pair[1].frequency > pair[0].frequency
                    || (pair[1].frequency == pair[0].frequency
                        && pair[1].voltage >= pair[0].voltage)
                {
                    anyhow::bail!(
                        "P-states must be primarily sorted by decreasing frequency and secondarily \
                        sorted by decreasing voltage; violated by {:?} and {:?}.",
                        pair[0],
                        pair[1]
                    );
                }
            }
        }
        Ok(())
    }

    pub async fn build(self) -> Result<Rc<CpuDeviceHandler>, Error> {
        // Optionally use the default proxy
        let cpu_ctrl_proxy = match self.cpu_ctrl_proxy {
            Some(proxy) => proxy,
            None => connect_to_driver::<fcpu_ctrl::DeviceMarker>(&self.driver_path)
                .await
                .context("Failed connecting to CPU driver")?,
        };

        let pstates = Self::get_pstates(&self.driver_path, &cpu_ctrl_proxy).await?;
        Self::validate_pstates(&pstates)?;

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        // Create the root Inspect node for CpuDeviceHandler
        let local_root =
            inspect_root.create_child(format!("CpuDeviceHandler ({})", &self.driver_path));
        Self::add_pstates_to_inspect(&local_root, &pstates);

        // Build the DeviceControlHandler
        let dev_handler_builder = self.dev_handler_builder.inspect_root(&local_root);
        let dev_control_handler = dev_handler_builder.build()?;
        dev_control_handler.init().await?;

        // Since CpuDeviceHandler has no dynamic Inspect data, its node will be owned by the root
        inspect_root.record(local_root);

        Ok(Rc::new(CpuDeviceHandler {
            driver_path: self.driver_path,
            dev_control_handler,
            pstates,
        }))
    }

    fn add_pstates_to_inspect(parent: &inspect::Node, pstates: &Vec<PState>) {
        parent.record_child("P-states", |pstates_node| {
            // Iterate P-states in reverse order so that the Inspect nodes appear in the same order
            // as the vector (`record_child` inserts nodes at the head).
            for (i, pstate) in pstates.iter().enumerate().rev() {
                pstates_node.record_child(format!("pstate_{:02}", i), |node| {
                    node.record_double("voltage (V)", pstate.voltage.0);
                    node.record_double("frequency (Hz)", pstate.frequency.0);
                });
            }
        });
    }
}

pub struct CpuDeviceHandler {
    /// Path to the underlying CPU driver
    driver_path: String,

    /// Child node to handle GetPerformanceState and SetPerformanceState
    dev_control_handler: Rc<DeviceControlHandler>,

    /// All P-states provided by the underlying CPU driver
    pstates: Vec<PState>,
}

#[async_trait(?Send)]
impl Node for CpuDeviceHandler {
    fn name(&self) -> String {
        format!("CpuDeviceHandler ({})", self.driver_path)
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            &Message::GetPerformanceState | Message::SetPerformanceState(_) => {
                self.dev_control_handler.handle_message(msg).await
            }
            Message::GetCpuPerformanceStates => {
                Ok(MessageReturn::GetCpuPerformanceStates(self.pstates.clone()))
            }
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::TryStreamExt;
    use inspect::assert_data_tree;
    use std::cell::Cell;

    // Creates a fake fuchsia.device.Controller proxy
    fn setup_fake_controller_proxy() -> fidl_fuchsia_device::ControllerProxy {
        let perf_state = Rc::new(Cell::new(0));
        let perf_state_clone_1 = perf_state.clone();
        let perf_state_clone_2 = perf_state.clone();
        let get_performance_state = move || perf_state_clone_1.get();
        let set_performance_state = move |state| {
            perf_state_clone_2.set(state);
        };
        dev_control_handler::tests::fake_dev_ctrl_driver(
            get_performance_state,
            set_performance_state,
        )
    }

    /// Creates a fake fuchsia.hardware.cpu_ctrl.Device proxy
    fn setup_fake_cpu_ctrl_proxy(pstates: Vec<PState>) -> fcpu_ctrl::DeviceProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fcpu_ctrl::DeviceMarker>().unwrap();

        fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fcpu_ctrl::DeviceRequest::GetPerformanceStateInfo {
                        state,
                        responder,
                    }) => {
                        let index = state as usize;
                        let mut result = if index < pstates.len() {
                            Ok(fcpu_ctrl::CpuPerformanceStateInfo {
                                frequency_hz: pstates[index].frequency.0 as i64,
                                voltage_uv: (pstates[index].voltage.0 * 1e6) as i64,
                            })
                        } else {
                            Err(zx::Status::NOT_SUPPORTED.into_raw())
                        };
                        let _ = responder.send(&mut result);
                    }
                    Some(other) => panic!("Unexpected request: {:?}", other),
                    None => break, // Stream terminates when client is dropped
                }
            }
        })
        .detach();

        proxy
    }

    async fn setup_simple_test_node(pstates: Vec<PState>) -> Rc<CpuDeviceHandler> {
        let builder = CpuDeviceHandlerBuilder::new_with_proxies(
            "fake_path".to_string(),
            setup_fake_controller_proxy(),
            setup_fake_cpu_ctrl_proxy(pstates),
        );
        builder.build().await.unwrap()
    }

    /// Tests that an unsupported message is handled gracefully and an Unsupported error is returned
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let pstates = vec![PState { frequency: Hertz(1e9), voltage: Volts(1.0) }];
        let node = setup_simple_test_node(pstates).await;
        match node.handle_message(&Message::ReadTemperature).await {
            Err(PowerManagerError::Unsupported) => {}
            e => panic!("Unexpected return value: {:?}", e),
        }
    }

    /// Tests that the Get/SetPerformanceState messages cause the node to call the appropriate
    /// device controller FIDL APIs via DeviceControllerHandler.
    #[fasync::run_singlethreaded(test)]
    async fn test_performance_state() {
        let pstates = vec![PState { frequency: Hertz(1e9), voltage: Volts(1.0) }];
        let node = setup_simple_test_node(pstates).await;

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

    /// Tests that a GetCpuPerformanceStates message is handled properly.
    #[fasync::run_singlethreaded(test)]
    async fn test_get_cpu_performance_states() {
        let pstates = vec![
            PState { frequency: Hertz(1.4e9), voltage: Volts(0.9) },
            PState { frequency: Hertz(1.3e9), voltage: Volts(0.8) },
            PState { frequency: Hertz(1.2e9), voltage: Volts(0.7) },
        ];
        let node = setup_simple_test_node(pstates.clone()).await;

        let received_pstates =
            match node.handle_message(&Message::GetCpuPerformanceStates).await.unwrap() {
                MessageReturn::GetCpuPerformanceStates(v) => v,
                e => panic!("Unexpected return value: {:?}", e),
            };

        assert_eq!(pstates, received_pstates);
    }

    /// Tests that P-state validation works as expected.
    #[fasync::run_singlethreaded(test)]
    async fn test_pstate_validation() {
        // Primary sort by frequency is violated.
        let pstates = vec![
            PState { frequency: Hertz(1.5e9), voltage: Volts(1.0) },
            PState { frequency: Hertz(1.6e9), voltage: Volts(1.0) },
        ];
        let builder = CpuDeviceHandlerBuilder::new_with_proxies(
            "fake_path".to_string(),
            setup_fake_controller_proxy(),
            setup_fake_cpu_ctrl_proxy(pstates),
        );
        assert!(builder.build().await.is_err());

        // Secondary sort by voltage is violated.
        let pstates = vec![
            PState { frequency: Hertz(1.5e9), voltage: Volts(1.0) },
            PState { frequency: Hertz(1.5e9), voltage: Volts(1.1) },
        ];
        let builder = CpuDeviceHandlerBuilder::new_with_proxies(
            "fake_path".to_string(),
            setup_fake_controller_proxy(),
            setup_fake_cpu_ctrl_proxy(pstates),
        );
        assert!(builder.build().await.is_err());

        // Duplicated P-state (detected as violation of secondary sort by voltage).
        let pstates = vec![
            PState { frequency: Hertz(1.5e9), voltage: Volts(1.0) },
            PState { frequency: Hertz(1.5e9), voltage: Volts(1.0) },
        ];
        let builder = CpuDeviceHandlerBuilder::new_with_proxies(
            "fake_path".to_string(),
            setup_fake_controller_proxy(),
            setup_fake_cpu_ctrl_proxy(pstates),
        );
        assert!(builder.build().await.is_err());
    }

    /// Tests that Inspect data is populated as expected
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let pstates = vec![
            PState { frequency: Hertz(1.3e9), voltage: Volts(0.8) },
            PState { frequency: Hertz(1.2e9), voltage: Volts(0.7) },
        ];

        let inspector = inspect::Inspector::new();
        let builder = CpuDeviceHandlerBuilder::new_with_proxies(
            "fake_path".to_string(),
            setup_fake_controller_proxy(),
            setup_fake_cpu_ctrl_proxy(pstates.clone()),
        )
        .with_inspect_root(inspector.root());

        let _node = builder.build().await.unwrap();

        assert_data_tree!(
            inspector,
            root: {
                "CpuDeviceHandler (fake_path)": {
                    "P-states": {
                        pstate_00: {
                            "frequency (Hz)": pstates[0].frequency.0,
                            "voltage (V)": pstates[0].voltage.0,
                        },
                        pstate_01: {
                            "frequency (Hz)": pstates[1].frequency.0,
                            "voltage (V)": pstates[1].voltage.0,
                        },
                    },
                    "DeviceControlHandler (fake_path)": contains {
                        performance_state: 0u64,
                    }
                }
            }
        );
    }
}
