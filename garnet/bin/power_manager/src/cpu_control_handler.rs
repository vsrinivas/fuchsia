// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::dev_control_handler;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::{Farads, Hertz, Volts, Watts};
use crate::utils::connect_proxy;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_hardware_cpu_ctrl as fcpuctrl;
use fuchsia_inspect::{self as inspect, Property};
use std::cell::{Cell, RefCell, RefMut};
use std::rc::Rc;

/// Node: CpuControlHandler
///
/// Summary: Provides a mechanism for controlling the performance state of a CPU domain. The node
///          mostly relies on functionality provided by the DeviceControlHandler node for
///          performance state control, but this node enhances that basic functionality by
///          integrating performance state metadata (CPU P-state information) from the CpuCtrl
///          interface.
///
/// Handles Messages:
///     - SetMaxPowerConsumption
///
/// Sends Messages:
///     - GetTotalCpuLoad
///     - GetPerformanceState
///     - SetPerformanceState
///
/// FIDL dependencies:
///     - fuchsia.hardware.cpu.ctrl.Device: the node uses this protocol to communicate with the
///       CpuCtrl interface of the CPU device specified in the CpuControlHandler constructor

/// Describes a processor performance state.
#[derive(Clone, Debug, Copy)]
pub struct PState {
    pub frequency: Hertz,
    pub voltage: Volts,
}

/// Describes the parameters of the CPU domain.
pub struct CpuControlParams {
    /// Available P-states of the CPU. These must be in order of descending power usage, per
    /// section 8.4.6.2 of ACPI spec version 6.3.
    pub p_states: Vec<PState>,
    /// Model capacitance of each CPU core. Required to estimate power usage.
    pub capacitance: Farads,
    /// Number of cores contained within this CPU domain.
    pub num_cores: u32,
}

impl CpuControlParams {
    /// Checks that the list of P-states is valid:
    ///  - Contains at least one element;
    ///  - Is in order of decreasing nominal power consumption.
    fn validate(&self) -> Result<(), Error> {
        if self.num_cores == 0 {
            return Err(format_err!("Must have > 0 cores"));
        }
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

/// A builder for constructing the CpuControlHandler node. The fields of this struct are documented
/// as part of the CpuControlHandler struct.
pub struct CpuControlHandlerBuilder<'a> {
    cpu_driver_path: String,
    capacitance: Farads,
    cpu_stats_handler: Rc<dyn Node>,
    cpu_dev_handler_node: Rc<dyn Node>,
    cpu_ctrl_proxy: Option<fcpuctrl::DeviceProxy>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> CpuControlHandlerBuilder<'a> {
    pub fn new_with_driver_path(
        cpu_driver_path: String,
        // TODO(pshickel): Eventually we may want to query capacitance from the CPU driver (same as
        // we do for CPU P-states)
        capacitance: Farads,
        cpu_stats_handler: Rc<dyn Node>,
        cpu_dev_handler_node: Rc<dyn Node>,
    ) -> Self {
        Self {
            cpu_driver_path,
            capacitance,
            cpu_stats_handler,
            cpu_dev_handler_node,
            cpu_ctrl_proxy: None,
            inspect_root: None,
        }
    }

    #[cfg(test)]
    pub fn new_with_proxy(
        cpu_driver_path: String,
        proxy: fcpuctrl::DeviceProxy,
        capacitance: Farads,
        cpu_stats_handler: Rc<dyn Node>,
        cpu_dev_handler_node: Rc<dyn Node>,
    ) -> Self {
        Self {
            cpu_driver_path,
            cpu_ctrl_proxy: Some(proxy),
            capacitance,
            cpu_stats_handler,
            cpu_dev_handler_node,
            inspect_root: None,
        }
    }

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    pub fn build(self) -> Result<Rc<CpuControlHandler>, Error> {
        // Optionally use the default proxy
        let proxy = if self.cpu_ctrl_proxy.is_none() {
            connect_proxy::<fcpuctrl::DeviceMarker>(&self.cpu_driver_path)?
        } else {
            self.cpu_ctrl_proxy.unwrap()
        };

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        Ok(Rc::new(CpuControlHandler {
            cpu_driver_path: self.cpu_driver_path.clone(),
            cpu_control_params: RefCell::new(CpuControlParams {
                p_states: Vec::new(),
                capacitance: self.capacitance,
                num_cores: 0,
            }),
            current_p_state_index: Cell::new(None),
            cpu_stats_handler: self.cpu_stats_handler,
            cpu_dev_handler_node: self.cpu_dev_handler_node,
            cpu_ctrl_proxy: proxy,
            inspect: InspectData::new(
                inspect_root,
                format!("CpuControlHandler ({})", self.cpu_driver_path),
            ),
        }))
    }
}

pub struct CpuControlHandler {
    /// The path to the driver that this node controls.
    cpu_driver_path: String,

    /// The parameters of the CPU domain which are lazily queried from the CPU driver.
    cpu_control_params: RefCell<CpuControlParams>,

    /// The current CPU P-state index which is initially None and lazily queried from the CPU
    /// DeviceControlHandler node.
    current_p_state_index: Cell<Option<usize>>,

    /// The node which will provide CPU load information. It is expected that this node responds to
    /// the GetTotalCpuLoad message.
    cpu_stats_handler: Rc<dyn Node>,

    /// The node to be used for CPU performance state control. It is expected that this node
    /// responds to the Get/SetPerformanceState messages.
    cpu_dev_handler_node: Rc<dyn Node>,

    /// A proxy handle to communicate with the CPU driver CpuCtrl interface.
    cpu_ctrl_proxy: fcpuctrl::DeviceProxy,

    /// A struct for managing Component Inspection data
    inspect: InspectData,
}

impl CpuControlHandler {
    /// Construct CpuControlParams by querying the required information from the CpuCtrl interface.
    async fn get_cpu_params(&self) -> Result<(), Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuControlHandler::get_cpu_params",
            "driver" => self.cpu_driver_path.as_str()
        );
        // Query P-state metadata from the CpuCtrl interface. Each supported performance state
        // has accompanying P-state metadata.
        let mut p_states = Vec::new();
        for i in 0..dev_control_handler::MAX_PERF_STATES {
            if let Ok(info) = self.cpu_ctrl_proxy.get_performance_state_info(i).await? {
                p_states.push(PState {
                    frequency: Hertz(info.frequency_hz as f64),
                    voltage: Volts(info.voltage_uv as f64 / 1e6),
                });
            } else {
                break;
            }
        }

        let mut params = self.cpu_control_params.borrow_mut();
        params.p_states = p_states;
        params.num_cores = self.cpu_ctrl_proxy.get_num_logical_cores().await? as u32;
        params.validate()?;
        self.inspect.set_cpu_control_params(&params);
        fuchsia_trace::instant!(
            "power_manager",
            "CpuControlHandler::received_cpu_params",
            fuchsia_trace::Scope::Thread,
            "valid" => 1,
            "p_states" => format!("{:?}", params.p_states).as_str(),
            "capacitance" => params.capacitance.0,
            "num_cores" => params.num_cores
        );

        Ok(())
    }

    /// Returns the total CPU load (averaged since the previous call)
    async fn get_load(&self) -> Result<f32, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuControlHandler::get_load",
            "driver" => self.cpu_driver_path.as_str()
        );
        match self.send_message(&self.cpu_stats_handler, &Message::GetTotalCpuLoad).await {
            Ok(MessageReturn::GetTotalCpuLoad(load)) => Ok(load),
            Ok(r) => Err(format_err!("GetTotalCpuLoad had unexpected return value: {:?}", r)),
            Err(e) => Err(format_err!("GetTotalCpuLoad failed: {:?}", e)),
        }
    }

    /// Returns the current CPU P-state index
    async fn get_current_p_state_index(&self) -> Result<usize, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuControlHandler::get_current_p_state_index",
            "driver" => self.cpu_driver_path.as_str()
        );
        match self.send_message(&self.cpu_dev_handler_node, &Message::GetPerformanceState).await {
            Ok(MessageReturn::GetPerformanceState(state)) => Ok(state as usize),
            Ok(r) => Err(format_err!("GetPerformanceState had unexpected return value: {:?}", r)),
            Err(e) => Err(format_err!("GetPerformanceState failed: {:?}", e)),
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
        fuchsia_trace::duration!(
            "power_manager",
            "CpuControlHandler::handle_set_max_power_consumption",
            "driver" => self.cpu_driver_path.as_str(),
            "max_power" => max_power.0
        );

        // Lazily query the current P-state index on the first iteration
        let current_p_state_index = {
            if self.current_p_state_index.get().is_none() {
                self.current_p_state_index.set(Some(self.get_current_p_state_index().await?));
            }
            self.current_p_state_index.get().unwrap()
        };

        // Lazily query the CpuControlParams on the first iteration
        let cpu_params = {
            if self.cpu_control_params.borrow().p_states.len() == 0 {
                self.get_cpu_params().await?;
            }
            self.cpu_control_params.borrow()
        };

        // The operation completion rate over the last sample interval is
        //     num_operations / sample_interval,
        // where
        //     num_operations = last_load * last_frequency * sample_interval.
        // Hence,
        //     last_operation_rate = last_load * last_frequency.
        let last_load = self.get_load().await? as f64;
        let last_operation_rate = {
            // TODO(pshickel): Eventually we'll need a way to query the load only from the cores we
            // care about. As far as I can tell, there isn't currently a way to correlate the CPU
            // info coming from CpuStats with that from CpuCtrl.
            let last_frequency = cpu_params.p_states[current_p_state_index].frequency;
            last_frequency.mul_scalar(last_load)
        };

        // If no P-states meet the selection criterion, use the lowest-power state.
        let mut p_state_index = cpu_params.p_states.len() - 1;

        self.inspect.last_op_rate.set(last_operation_rate.0);
        self.inspect.last_load.set(last_load);
        fuchsia_trace::instant!(
            "power_manager",
            "CpuControlHandler::set_max_power_consumption_data",
            fuchsia_trace::Scope::Thread,
            "driver" => self.cpu_driver_path.as_str(),
            "current_p_state_index" => current_p_state_index as u32,
            "last_op_rate" => last_operation_rate.0,
            "last_load" => last_load
        );

        for (i, state) in cpu_params.p_states.iter().enumerate() {
            // We estimate that the operation rate over the next interval will be the min of
            // the last operation rate and the frequency of the P-state under consideration.
            //
            // Note that we don't currently account for a rise in frequency allowing for a possible
            // increase in the operation rate.
            let max_operation_rate = state.frequency.mul_scalar(cpu_params.num_cores as f64);
            let estimated_operation_rate = if max_operation_rate < last_operation_rate {
                max_operation_rate
            } else {
                last_operation_rate
            };

            let estimated_power =
                get_cpu_power(cpu_params.capacitance, state.voltage, estimated_operation_rate);
            if estimated_power <= *max_power {
                p_state_index = i;
                break;
            }
        }

        if p_state_index != self.current_p_state_index.get().unwrap() {
            fuchsia_trace::instant!(
                "power_manager",
                "CpuControlHandler::updated_p_state_index",
                fuchsia_trace::Scope::Thread,
                "driver" => self.cpu_driver_path.as_str(),
                "old_index" => self.current_p_state_index.get().unwrap() as u32,
                "new_index" => p_state_index as u32
            );
            // Tell the CPU DeviceControlHandler to update the performance state
            self.send_message(
                &self.cpu_dev_handler_node,
                &Message::SetPerformanceState(p_state_index as u32),
            )
            .await?;

            // Cache the new P-state index for calculations on the next iteration
            self.current_p_state_index.set(Some(p_state_index));
            self.inspect.p_state_index.set(p_state_index as u64);
        }

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

struct InspectData {
    // Nodes
    root_node: inspect::Node,

    // Properties
    p_state_index: inspect::UintProperty,
    last_op_rate: inspect::DoubleProperty,
    last_load: inspect::DoubleProperty,
}

impl InspectData {
    fn new(parent: &inspect::Node, node_name: String) -> Self {
        // Create a local root node and properties
        let root_node = parent.create_child(node_name);
        let p_state_index = root_node.create_uint("p_state_index", 0);
        let last_op_rate = root_node.create_double("last_op_rate", 0.0);
        let last_load = root_node.create_double("last_load", 0.0);

        InspectData { root_node, p_state_index, last_op_rate, last_load }
    }

    fn set_cpu_control_params(&self, params: &RefMut<CpuControlParams>) {
        let cpu_params_node = self.root_node.create_child("cpu_control_params");

        // Iterate `params.p_states` in reverse order so that the Inspect nodes appear in the same
        // order as the vector (`create_child` inserts nodes at the head).
        for (i, p_state) in params.p_states.iter().enumerate().rev() {
            let p_state_node = cpu_params_node.create_child(format!("p_state_{}", i));
            p_state_node.record_double("voltage (V)", p_state.voltage.0);
            p_state_node.record_double("frequency (Hz)", p_state.frequency.0);

            // Pass ownership of the new P-state node to the parent `cpu_params_node`
            cpu_params_node.record(p_state_node);
        }

        cpu_params_node.record_double("capacitance (F)", params.capacitance.0);
        cpu_params_node.record_uint("num_cores", params.num_cores.into());

        // Pass ownership of the new `cpu_params_node` to the root node
        self.root_node.record(cpu_params_node);
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::cpu_stats_handler;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::TryStreamExt;
    use inspect::assert_inspect_tree;

    fn setup_fake_service(params: CpuControlParams) -> fcpuctrl::DeviceProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fcpuctrl::DeviceMarker>().unwrap();

        fasync::spawn_local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fcpuctrl::DeviceRequest::GetNumLogicalCores { responder }) => {
                        let _ = responder.send(params.num_cores as u64);
                    }
                    Some(fcpuctrl::DeviceRequest::GetPerformanceStateInfo { state, responder }) => {
                        let index = state as usize;
                        let mut result = if index < params.p_states.len() {
                            Ok(fcpuctrl::CpuPerformanceStateInfo {
                                frequency_hz: params.p_states[index].frequency.0 as i64,
                                voltage_uv: (params.p_states[index].voltage.0 * 1e6) as i64,
                            })
                        } else {
                            Err(zx::Status::NOT_SUPPORTED.into_raw())
                        };
                        let _ = responder.send(&mut result);
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    pub fn setup_test_node(
        params: CpuControlParams,
        cpu_stats_handler: Rc<dyn Node>,
        cpu_dev_handler_node: Rc<dyn Node>,
    ) -> Rc<CpuControlHandler> {
        let capacitance = params.capacitance;
        CpuControlHandlerBuilder::new_with_proxy(
            "Fake".to_string(),
            setup_fake_service(params),
            capacitance,
            cpu_stats_handler,
            cpu_dev_handler_node,
        )
        .build()
        .unwrap()
    }

    #[test]
    fn test_get_cpu_power() {
        assert_eq!(get_cpu_power(Farads(100.0e-12), Volts(1.0), Hertz(1.0e9)), Watts(0.1));
    }

    async fn get_perf_state(devhost_node: Rc<dyn Node>) -> u32 {
        match devhost_node.handle_message(&Message::GetPerformanceState).await.unwrap() {
            MessageReturn::GetPerformanceState(state) => state,
            _ => panic!(),
        }
    }

    /// Tests that the SetMaxPowerConsumption message causes the node to correctly consider CPU load
    /// and parameters to choose the appropriate P-states.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_max_power_consumption() {
        // With these PStates and capacitance, the modeled power consumption (W) is:
        //  - 4.05
        //  - 1.8
        let cpu_params = CpuControlParams {
            num_cores: 4,
            p_states: vec![
                PState { frequency: Hertz(2.0e9), voltage: Volts(4.5) },
                PState { frequency: Hertz(2.0e9), voltage: Volts(3.0) },
            ],
            capacitance: Farads(100.0e-12),
        };
        let cpu_stats_node = cpu_stats_handler::tests::setup_simple_test_node();
        let devhost_node = dev_control_handler::tests::setup_simple_test_node();
        let cpu_ctrl_node = setup_test_node(cpu_params, cpu_stats_node, devhost_node.clone());

        // Allow power consumption greater than all PStates, expect to be in state 0
        let max_power = Watts(5.0);
        match cpu_ctrl_node
            .handle_message(&Message::SetMaxPowerConsumption(max_power))
            .await
            .unwrap()
        {
            MessageReturn::SetMaxPowerConsumption => {}
            _ => panic!(),
        }
        assert_eq!(get_perf_state(devhost_node.clone()).await, 0);

        // Lower power consumption limit such that we expect to switch to state 1
        let max_power = Watts(4.0);
        match cpu_ctrl_node
            .handle_message(&Message::SetMaxPowerConsumption(max_power))
            .await
            .unwrap()
        {
            MessageReturn::SetMaxPowerConsumption => {}
            _ => panic!(),
        }
        assert_eq!(get_perf_state(devhost_node.clone()).await, 1);

        // If we cannot accomodate the power limit, we should still be in the least power consuming
        // state
        let max_power = Watts(0.0);
        match cpu_ctrl_node
            .handle_message(&Message::SetMaxPowerConsumption(max_power))
            .await
            .unwrap()
        {
            MessageReturn::SetMaxPowerConsumption => {}
            _ => panic!(),
        }
        assert_eq!(get_perf_state(devhost_node.clone()).await, 1);
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        // Some dummy CpuControlParams to verify the params get published in Inspect
        let num_cores = 4;
        let p_state = PState { frequency: Hertz(2.0e9), voltage: Volts(4.5) };
        let capacitance = Farads(100.0e-12);
        let params = CpuControlParams { num_cores, p_states: vec![p_state], capacitance };

        let inspector = inspect::Inspector::new();
        let node = CpuControlHandlerBuilder::new_with_proxy(
            "Fake".to_string(),
            setup_fake_service(params),
            capacitance,
            cpu_stats_handler::tests::setup_simple_test_node(),
            dev_control_handler::tests::setup_simple_test_node(),
        )
        .with_inspect_root(inspector.root())
        .build()
        .unwrap();

        // Sending this message causes the node to lazily query the CpuParams from the fake driver.
        // After this point, the CpuParams should be populated into the Inspect tree.
        match node.handle_message(&Message::SetMaxPowerConsumption(Watts(1.0))).await.unwrap() {
            MessageReturn::SetMaxPowerConsumption => {}
            _ => panic!(),
        }

        assert_inspect_tree!(
            inspector,
            root: {
                "CpuControlHandler (Fake)": contains {
                    cpu_control_params: {
                        "capacitance (F)": capacitance.0,
                        num_cores: num_cores as u64,
                        p_state_0: {
                            "voltage (V)": p_state.voltage.0,
                            "frequency (Hz)": p_state.frequency.0
                        }
                    },
                }
            }
        );
    }
}
