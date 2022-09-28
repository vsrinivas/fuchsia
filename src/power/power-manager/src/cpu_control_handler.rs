// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::dev_control_handler;
use crate::error::PowerManagerError;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::ok_or_default_err;
use crate::types::{Farads, Hertz, PState, Volts, Watts};
use crate::utils::connect_to_driver;
use anyhow::{format_err, Context, Error};
use async_trait::async_trait;
use async_utils::event::Event as AsyncEvent;
use fidl_fuchsia_hardware_cpu_ctrl as fcpuctrl;
use fuchsia_inspect::{self as inspect, Property};
use serde_derive::Deserialize;
use serde_json as json;
use std::cell::{Ref, RefCell};
use std::collections::hash_map::DefaultHasher;
use std::collections::HashMap;
use std::hash::{Hash, Hasher};
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
///     - GetCpuLoads
///     - GetPerformanceState
///     - SetPerformanceState
///
/// FIDL dependencies:
///     - fuchsia.hardware.cpu.ctrl.Device: the node uses this protocol to communicate with the
///       CpuCtrl interface of the CPU device specified in the CpuControlHandler constructor

/// Describes the parameters of the CPU domain.
pub struct CpuControlParams {
    /// Available P-states of the CPU. These must be in order of descending power usage, per
    /// section 8.4.6.2 of ACPI spec version 6.3.
    pub p_states: Vec<PState>,
    /// Model capacitance of each CPU core. Required to estimate power usage.
    pub capacitance: Farads,
    /// Logical CPU numbers contained within this CPU domain.
    pub logical_cpu_numbers: Vec<u32>,
}

impl CpuControlParams {
    /// Checks that the list of P-states is valid:
    ///  - Contains at least one element;
    ///  - Is in order of decreasing nominal power consumption.
    fn validate(&self) -> Result<(), Error> {
        if self.logical_cpu_numbers.len() == 0 {
            return Err(format_err!("Must have > 0 CPUs"));
        }
        if !self.logical_cpu_numbers.windows(2).all(|w| w[0] < w[1]) {
            return Err(format_err!("CPUs must be sorted and non-repeating"));
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
    cpu_driver_path: Option<String>,
    cpu_stats_handler: Option<Rc<dyn Node>>,
    cpu_dev_handler: Option<Rc<dyn Node>>,
    cpu_ctrl_proxy: Option<fcpuctrl::DeviceProxy>,
    inspect_root: Option<&'a inspect::Node>,
    min_cpu_clock_speed: Option<Hertz>,
    capacitance: Option<Farads>,
    logical_cpu_numbers: Option<Vec<u32>>,
}

impl<'a> CpuControlHandlerBuilder<'a> {
    #[cfg(test)]
    pub fn new() -> Self {
        use crate::test::mock_node::create_dummy_node;

        Self {
            cpu_driver_path: Some("TestCpuControlHandler".to_string()),
            cpu_stats_handler: Some(create_dummy_node()),
            cpu_dev_handler: Some(create_dummy_node()),
            cpu_ctrl_proxy: None,
            inspect_root: None,
            min_cpu_clock_speed: Some(Hertz(0.0)),
            capacitance: Some(Farads(0.0)),
            logical_cpu_numbers: Some(vec![0]),
        }
    }

    #[cfg(test)]
    pub fn cpu_stats_handler(mut self, handler: Rc<dyn Node>) -> Self {
        self.cpu_stats_handler = Some(handler);
        self
    }

    #[cfg(test)]
    pub fn cpu_dev_handler(mut self, handler: Rc<dyn Node>) -> Self {
        self.cpu_dev_handler = Some(handler);
        self
    }

    #[cfg(test)]
    pub fn cpu_ctrl_proxy(mut self, proxy: fcpuctrl::DeviceProxy) -> Self {
        self.cpu_ctrl_proxy = Some(proxy);
        self
    }

    #[cfg(test)]
    pub fn capacitance(mut self, capacitance: Farads) -> Self {
        self.capacitance = Some(capacitance);
        self
    }

    #[cfg(test)]
    fn inspect_root(mut self, inspect_root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(inspect_root);
        self
    }

    #[cfg(test)]
    pub fn logical_cpu_numbers(mut self, cpu_numbers: Vec<u32>) -> Self {
        self.logical_cpu_numbers = Some(cpu_numbers);
        self
    }

    #[cfg(test)]
    fn min_cpu_clock_speed(mut self, clock_speed: Hertz) -> Self {
        self.min_cpu_clock_speed = Some(clock_speed);
        self
    }

    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Config {
            driver_path: String,
            capacitance: f64,
            min_cpu_clock_speed: f64,
            logical_cpu_numbers: Vec<u32>,
        }

        #[derive(Deserialize)]
        struct Dependencies {
            cpu_stats_handler_node: String,
            cpu_dev_handler_node: String,
        }

        #[derive(Deserialize)]
        struct JsonData {
            config: Config,
            dependencies: Dependencies,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        Self {
            cpu_driver_path: Some(data.config.driver_path),
            min_cpu_clock_speed: Some(Hertz(data.config.min_cpu_clock_speed)),
            cpu_stats_handler: Some(nodes[&data.dependencies.cpu_stats_handler_node].clone()),
            cpu_dev_handler: Some(nodes[&data.dependencies.cpu_dev_handler_node].clone()),
            capacitance: Some(Farads(data.config.capacitance)),
            logical_cpu_numbers: Some(data.config.logical_cpu_numbers),
            cpu_ctrl_proxy: None,
            inspect_root: None,
        }
    }

    pub fn build(self) -> Result<Rc<CpuControlHandler>, Error> {
        let cpu_driver_path = ok_or_default_err!(self.cpu_driver_path)?;
        let cpu_stats_handler = ok_or_default_err!(self.cpu_stats_handler)?;
        let cpu_dev_handler = ok_or_default_err!(self.cpu_dev_handler)?;
        let min_cpu_clock_speed = ok_or_default_err!(self.min_cpu_clock_speed)?;
        let capacitance = ok_or_default_err!(self.capacitance)?;
        let logical_cpu_numbers = ok_or_default_err!(self.logical_cpu_numbers)?;

        // Optionally use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());
        let inspect =
            InspectData::new(inspect_root, format!("CpuControlHandler ({})", cpu_driver_path));

        let mutable_inner = MutableInner {
            current_p_state_index: 0,
            cpu_control_params: CpuControlParams {
                p_states: Vec::new(),
                capacitance,
                logical_cpu_numbers,
            },
            cpu_ctrl_proxy: self.cpu_ctrl_proxy,
        };

        let mut hasher = DefaultHasher::new();
        cpu_driver_path.hash(&mut hasher);
        let trace_counter_id = hasher.finish();

        Ok(Rc::new(CpuControlHandler {
            init_done: AsyncEvent::new(),
            cpu_driver_path,
            cpu_stats_handler,
            cpu_dev_handler,
            inspect,
            trace_counter_id,
            min_cpu_clock_speed,
            mutable_inner: RefCell::new(mutable_inner),
        }))
    }

    #[cfg(test)]
    pub async fn build_and_init(self) -> Rc<CpuControlHandler> {
        let node = self.build().unwrap();
        node.init().await.unwrap();
        node
    }
}

pub struct CpuControlHandler {
    /// Signalled after `init()` has completed. Used to ensure node doesn't process messages until
    /// its `init()` has completed.
    init_done: AsyncEvent,

    /// The path to the driver that this node controls.
    cpu_driver_path: String,

    /// The node which will provide CPU load information. It is expected that this node responds to
    /// the GetCpuLoads message.
    cpu_stats_handler: Rc<dyn Node>,

    /// The node to be used for CPU performance state control. It is expected that this node
    /// responds to the Get/SetPerformanceState messages.
    cpu_dev_handler: Rc<dyn Node>,

    /// A struct for managing Component Inspection data
    inspect: InspectData,

    /// Identifies trace counters between CpuControlHandler instances for different drivers.
    trace_counter_id: u64,

    /// Minimum CPU clock speed to set. Selectable CPU P-states with a frequency below this value
    /// are filtered out.
    min_cpu_clock_speed: Hertz,

    /// Mutable inner state.
    mutable_inner: RefCell<MutableInner>,
}

impl CpuControlHandler {
    /// Convenience accessor for borrowing `cpu_control_params`.
    fn cpu_control_params(&self) -> Ref<'_, CpuControlParams> {
        Ref::map(self.mutable_inner.borrow(), |inner| &inner.cpu_control_params)
    }

    /// Returns the total CPU load (averaged since the previous call)
    async fn get_load(&self) -> Result<f32, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuControlHandler::get_load",
            "driver" => self.cpu_driver_path.as_str()
        );

        // Get load for all CPUs in the system
        let cpu_loads =
            match self.send_message(&self.cpu_stats_handler, &Message::GetCpuLoads).await {
                Ok(MessageReturn::GetCpuLoads(loads)) => Ok(loads),
                Ok(r) => Err(format_err!("GetCpuLoads had unexpected return value: {:?}", r)),
                Err(e) => Err(format_err!("GetCpuLoads failed: {:?}", e)),
            }?;

        // Filter down to only the ones we're concerned with
        Ok(self
            .cpu_control_params()
            .logical_cpu_numbers
            .iter()
            .map(|i| cpu_loads[*i as usize])
            .sum())
    }

    /// Returns the current CPU P-state index
    async fn get_current_p_state_index(&self) -> Result<usize, Error> {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuControlHandler::get_current_p_state_index",
            "driver" => self.cpu_driver_path.as_str()
        );
        match self.send_message(&self.cpu_dev_handler, &Message::GetPerformanceState).await {
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
    ) -> Result<MessageReturn, PowerManagerError> {
        fuchsia_trace::duration!(
            "power_manager",
            "CpuControlHandler::handle_set_max_power_consumption",
            "driver" => self.cpu_driver_path.as_str(),
            "max_power" => max_power.0
        );

        self.init_done.wait().await;

        let current_p_state_index = self.mutable_inner.borrow().current_p_state_index;

        // This is reused several times.
        let num_cores = self.cpu_control_params().logical_cpu_numbers.len() as f64;

        // The operation completion rate over the last sample interval is
        //     num_operations / sample_interval,
        // where
        //     num_operations = last_load * last_frequency * sample_interval.
        // Hence,
        //     last_op_rate = last_load * last_frequency.
        let (last_op_rate, last_max_op_rate) = {
            let last_load = self.get_load().await? as f64;
            self.inspect.last_load.set(last_load);
            fuchsia_trace::counter!(
                "power_manager",
                "CpuControlHandler last_load",
                self.trace_counter_id,
                self.cpu_driver_path.as_str() => last_load
            );

            let last_frequency =
                self.cpu_control_params().p_states[current_p_state_index].frequency;
            (last_frequency.mul_scalar(last_load), last_frequency.mul_scalar(num_cores))
        };

        self.inspect.last_op_rate.set(last_op_rate.0);
        fuchsia_trace::instant!(
            "power_manager",
            "CpuControlHandler::set_max_power_consumption_data",
            fuchsia_trace::Scope::Thread,
            "driver" => self.cpu_driver_path.as_str(),
            "current_p_state_index" => current_p_state_index as u32,
            "last_op_rate" => last_op_rate.0
        );

        let mut p_state_index = 0;
        let mut estimated_power = Watts(0.0);

        // Iterate through the list of available P-states (guaranteed to be sorted in order of
        // decreasing power consumption) and choose the first that will operate within the
        // `max_power` constraint.
        for (i, state) in self.cpu_control_params().p_states.iter().enumerate() {
            // We assume that the last operation rate carries over to the next interval unless:
            //  - It exceeds the max operation rate at the new frequency, in which case it is
            //    truncated to the new max.
            //  - It is within a small delta of the max rate at the last frequency, in which case we
            //    assume that it would rise to the new maximum if the clock speed were to increase.
            const ESSENTIALLY_MAX_LOAD_FRACTION: f64 = 0.99;
            let new_max_op_rate = state.frequency.mul_scalar(num_cores);
            let estimated_op_rate = if last_op_rate > new_max_op_rate
                || last_op_rate > last_max_op_rate.mul_scalar(ESSENTIALLY_MAX_LOAD_FRACTION)
            {
                new_max_op_rate
            } else {
                last_op_rate
            };

            p_state_index = i;
            estimated_power = get_cpu_power(
                self.cpu_control_params().capacitance,
                state.voltage,
                estimated_op_rate,
            );

            if estimated_power <= *max_power {
                break;
            }
        }

        if p_state_index != current_p_state_index {
            fuchsia_trace::instant!(
                "power_manager",
                "CpuControlHandler::updated_p_state_index",
                fuchsia_trace::Scope::Thread,
                "driver" => self.cpu_driver_path.as_str(),
                "old_index" => current_p_state_index as u32,
                "new_index" => p_state_index as u32
            );

            // Tell the CPU DeviceControlHandler to update the performance state
            self.send_message(
                &self.cpu_dev_handler,
                &Message::SetPerformanceState(p_state_index as u32),
            )
            .await?;

            // Cache the new P-state index for calculations on the next iteration
            self.mutable_inner.borrow_mut().current_p_state_index = p_state_index;
            self.inspect.p_state_index.set(p_state_index as u64);
        }

        fuchsia_trace::counter!(
            "power_manager",
            "CpuControlHandler p_state",
            self.trace_counter_id,
            self.cpu_driver_path.as_str() => p_state_index as u32
        );

        Ok(MessageReturn::SetMaxPowerConsumption(estimated_power))
    }
}

struct MutableInner {
    /// The parameters of the CPU domain which are queried from the CPU driver.
    cpu_control_params: CpuControlParams,

    /// The current CPU P-state index which is queried from the CPU DeviceControlHandler node.
    current_p_state_index: usize,

    /// A proxy handle to communicate with the CPU driver CpuCtrl interface.
    cpu_ctrl_proxy: Option<fcpuctrl::DeviceProxy>,
}

#[async_trait(?Send)]
impl Node for CpuControlHandler {
    fn name(&self) -> String {
        format!("CpuControlHandler ({})", self.cpu_driver_path)
    }

    /// Initializes internal state.
    ///
    /// Connects to the cpu-ctrl driver unless a proxy was already provided (in a test).
    async fn init(&self) -> Result<(), Error> {
        fuchsia_trace::duration!("power_manager", "CpuControlHandler::init");

        // Connect to the cpu-ctrl driver. Typically this is None, but it may be set by tests.
        let cpu_ctrl_proxy = match &self.mutable_inner.borrow().cpu_ctrl_proxy {
            Some(p) => p.clone(),
            None => connect_to_driver::<fcpuctrl::DeviceMarker>(&self.cpu_driver_path).await?,
        };

        // Query the CPU P-states
        let p_states =
            get_p_states(&self.cpu_driver_path, &cpu_ctrl_proxy, self.min_cpu_clock_speed)
                .await
                .context("Failed to get CPU P-states")?;

        let current_p_state = self.get_current_p_state_index().await?;

        {
            let mut mutable_inner = self.mutable_inner.borrow_mut();
            let mut cpu_control_params = &mut mutable_inner.cpu_control_params;
            cpu_control_params.p_states = p_states;
            cpu_control_params.validate().context("Invalid CPU control params")?;
            self.inspect.set_cpu_control_params(&cpu_control_params);

            mutable_inner.cpu_ctrl_proxy = Some(cpu_ctrl_proxy);
            mutable_inner.current_p_state_index = current_p_state;
        }

        self.init_done.signal();

        Ok(())
    }

    async fn handle_message(&self, msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        match msg {
            Message::SetMaxPowerConsumption(p) => self.handle_set_max_power_consumption(p).await,
            _ => Err(PowerManagerError::Unsupported),
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

    fn set_cpu_control_params(&self, params: &CpuControlParams) {
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
        cpu_params_node.record_string(
            "logical_cpu_numbers",
            format!("{:?}", params.logical_cpu_numbers).as_str(),
        );

        // Pass ownership of the new `cpu_params_node` to the root node
        self.root_node.record(cpu_params_node);
    }
}

/// Query the CPU P-states from the CpuCtrl driver.
async fn get_p_states(
    cpu_driver_path: &str,
    cpu_ctrl_proxy: &fcpuctrl::DeviceProxy,
    min_cpu_clock_speed: Hertz,
) -> Result<Vec<PState>, Error> {
    fuchsia_trace::duration!(
        "power_manager",
        "cpu_control_handler::get_p_states",
        "driver" => cpu_driver_path
    );

    // Query P-state metadata from the CpuCtrl interface. Each supported performance state has
    // accompanying P-state metadata.
    let mut p_states = Vec::new();
    let mut skipped_p_states = Vec::new();
    for i in 0..dev_control_handler::MAX_PERF_STATES {
        if let Ok(info) = cpu_ctrl_proxy.get_performance_state_info(i).await? {
            let frequency = Hertz(info.frequency_hz as f64);
            let voltage = Volts(info.voltage_uv as f64 / 1e6);
            let p_state = PState { frequency, voltage };

            // Filter out P-states where CPU frequency is unacceptably low
            if frequency >= min_cpu_clock_speed {
                p_states.push(p_state);
            } else {
                skipped_p_states.push(p_state);
            }
        } else {
            break;
        }
    }

    fuchsia_trace::instant!(
        "power_manager",
        "cpu_control_handler::received_cpu_p_states",
        fuchsia_trace::Scope::Thread,
        "driver" => cpu_driver_path,
        "valid" => 1,
        "p_states" => format!("{:?}", p_states).as_str(),
        "skipped_p_states" => format!("{:?}", skipped_p_states).as_str()
    );

    Ok(p_states)
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::test::mock_node::{MessageMatcher, MockNodeMaker};
    use crate::{msg_eq, msg_ok_return};
    use assert_matches::assert_matches;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::TryStreamExt;
    use inspect::assert_data_tree;
    use std::collections::HashSet;

    // Returns a proxy to a fake CpuCtrl driver pre-baked to return a single (fake) CPU P-state.
    fn fake_cpu_ctrl_driver() -> fcpuctrl::DeviceProxy {
        fake_cpu_ctrl_driver_with_p_states(vec![PState {
            frequency: Hertz(0.0),
            voltage: Volts(0.0),
        }])
    }

    // Returns a proxy to a fake CpuCtrl driver pre-baked to return the given set of CPU P-states.
    pub fn fake_cpu_ctrl_driver_with_p_states(p_states: Vec<PState>) -> fcpuctrl::DeviceProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fcpuctrl::DeviceMarker>().unwrap();

        fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(fcpuctrl::DeviceRequest::GetPerformanceStateInfo { state, responder }) => {
                        let index = state as usize;
                        let mut result = if index < p_states.len() {
                            Ok(fcpuctrl::CpuPerformanceStateInfo {
                                frequency_hz: p_states[index].frequency.0 as i64,
                                voltage_uv: (p_states[index].voltage.0 * 1e6) as i64,
                            })
                        } else {
                            Err(zx::Status::NOT_SUPPORTED.into_raw())
                        };
                        let _ = responder.send(&mut result);
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        proxy
    }

    #[test]
    fn test_get_cpu_power() {
        assert_eq!(get_cpu_power(Farads(100.0e-12), Volts(1.0), Hertz(1.0e9)), Watts(0.1));
    }

    /// Tests that an unsupported message is handled gracefully and an Unsupported error is returned
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let mut mock_maker = MockNodeMaker::new();
        let devhost_node = mock_maker.make(
            "DevHostNode",
            vec![
                // CpuControlHandler queries performance state during its initialization
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(0))),
            ],
        );

        let cpu_ctrl_node = CpuControlHandlerBuilder::new()
            .cpu_ctrl_proxy(fake_cpu_ctrl_driver())
            .cpu_dev_handler(devhost_node)
            .build_and_init()
            .await;

        assert_matches!(
            cpu_ctrl_node.handle_message(&Message::ReadTemperature).await,
            Err(PowerManagerError::Unsupported)
        );
    }

    /// Tests that CpuControlParams' `validate` correctly returns an error under invalid inputs.
    #[test]
    fn test_invalid_cpu_params() {
        // Empty CPUs
        assert!(CpuControlParams {
            logical_cpu_numbers: vec![],
            p_states: vec![PState { frequency: Hertz(0.0), voltage: Volts(0.0) }],
            capacitance: Farads(100e-12),
        }
        .validate()
        .is_err());

        // Repeating CPUs
        assert!(CpuControlParams {
            logical_cpu_numbers: vec![0, 0],
            p_states: vec![PState { frequency: Hertz(0.0), voltage: Volts(0.0) }],
            capacitance: Farads(100e-12)
        }
        .validate()
        .is_err());

        // Non-ascending CPUs
        assert!(CpuControlParams {
            logical_cpu_numbers: vec![1, 0],
            p_states: vec![PState { frequency: Hertz(0.0), voltage: Volts(0.0) }],
            capacitance: Farads(100e-12)
        }
        .validate()
        .is_err());

        // Empty p_states
        assert!(CpuControlParams {
            logical_cpu_numbers: vec![0],
            p_states: vec![],
            capacitance: Farads(100e-12)
        }
        .validate()
        .is_err());

        // p_states in order of increasing power usage
        assert!(CpuControlParams {
            logical_cpu_numbers: vec![0],
            p_states: vec![
                PState { frequency: Hertz(1.0), voltage: Volts(1.0) },
                PState { frequency: Hertz(2.0), voltage: Volts(1.0) }
            ],
            capacitance: Farads(100e-12)
        }
        .validate()
        .is_err());

        // p_states with identical power usage
        assert!(CpuControlParams {
            logical_cpu_numbers: vec![0],
            p_states: vec![
                PState { frequency: Hertz(1.0), voltage: Volts(1.0) },
                PState { frequency: Hertz(1.0), voltage: Volts(1.0) }
            ],
            capacitance: Farads(100e-12)
        }
        .validate()
        .is_err());
    }

    async fn get_perf_state(devhost_node: Rc<dyn Node>) -> u32 {
        match devhost_node.handle_message(&Message::GetPerformanceState).await.unwrap() {
            MessageReturn::GetPerformanceState(state) => state,
            e => panic!("Unexpected return value: {:?}", e),
        }
    }

    /// Tests that the SetMaxPowerConsumption message causes the node to correctly consider CPU load
    /// and parameters to choose the appropriate P-states.
    #[fasync::run_singlethreaded(test)]
    async fn test_set_max_power_consumption() {
        let mut mock_maker = MockNodeMaker::new();

        // Arbitrary CpuControlParams chosen to allow the node to demonstrate P-state selection
        let cpu_params = CpuControlParams {
            logical_cpu_numbers: vec![0, 1, 2, 3],
            p_states: vec![
                PState { frequency: Hertz(2.0e9), voltage: Volts(5.0) },
                PState { frequency: Hertz(2.0e9), voltage: Volts(4.0) },
                PState { frequency: Hertz(2.0e9), voltage: Volts(3.0) },
            ],
            capacitance: Farads(100.0e-12),
        };

        // The modeled power consumption at each P-state
        let power_consumption: Vec<Watts> = cpu_params
            .p_states
            .iter()
            .map(|p_state| {
                get_cpu_power(
                    cpu_params.capacitance,
                    p_state.voltage,
                    p_state.frequency.mul_scalar(cpu_params.logical_cpu_numbers.len() as f64),
                )
            })
            .collect();

        let stats_node = mock_maker.make(
            "StatsNode",
            // The CpuControlHandler node queries the current CPU load each time it receives a
            // SetMaxPowerConsumption message
            vec![
                // Make StatsNode give load for more CPUs than we care about to test the filtering
                // logic
                (msg_eq!(GetCpuLoads), msg_ok_return!(GetCpuLoads(vec![1.0; 8]))),
                (msg_eq!(GetCpuLoads), msg_ok_return!(GetCpuLoads(vec![1.0; 8]))),
                (msg_eq!(GetCpuLoads), msg_ok_return!(GetCpuLoads(vec![1.0; 8]))),
            ],
        );
        let devhost_node = mock_maker.make(
            "DevHostNode",
            vec![
                // CpuControlHandler queries performance state during its initialization
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(0))),
                // The test queries for current performance state
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(0))),
                // CpuControlHandler changes performance state to 1
                (msg_eq!(SetPerformanceState(1)), msg_ok_return!(SetPerformanceState)),
                // The test queries for current performance state
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(1))),
                // CpuControlHandler changes performance state to 2
                (msg_eq!(SetPerformanceState(2)), msg_ok_return!(SetPerformanceState)),
                // The test queries for current performance state
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(1))),
            ],
        );
        let cpu_ctrl_node = CpuControlHandlerBuilder::new()
            .cpu_stats_handler(stats_node)
            .cpu_dev_handler(devhost_node.clone())
            .cpu_ctrl_proxy(fake_cpu_ctrl_driver_with_p_states(cpu_params.p_states))
            .capacitance(cpu_params.capacitance)
            .logical_cpu_numbers(cpu_params.logical_cpu_numbers)
            .build_and_init()
            .await;

        // Test case 1: Allow power consumption of the highest P-state; expect to be in P-state 0
        let commanded_power = power_consumption[0].mul_scalar(1.01);
        let expected_power = power_consumption[0];
        assert_matches!(
            cpu_ctrl_node.handle_message(&Message::SetMaxPowerConsumption(commanded_power)).await,
            Ok(MessageReturn::SetMaxPowerConsumption(power)) if power == expected_power
        );
        assert_eq!(get_perf_state(devhost_node.clone()).await, 0);

        // Test case 2: Lower power consumption to that of P-state 1; expect to be in P-state 1
        let commanded_power = power_consumption[1].mul_scalar(1.01);
        let expected_power = power_consumption[1];
        assert_matches!(
            cpu_ctrl_node.handle_message(&Message::SetMaxPowerConsumption(commanded_power)).await,
            Ok(MessageReturn::SetMaxPowerConsumption(power)) if power == expected_power
        );
        assert_eq!(get_perf_state(devhost_node.clone()).await, 1);

        // Test case 3: Reduce the power consumption limit below the lowest P-state; expect to drop
        // to the lowest P-state
        let commanded_power = Watts(0.0);
        let expected_power = power_consumption[2];
        assert_matches!(
            cpu_ctrl_node.handle_message(&Message::SetMaxPowerConsumption(commanded_power)).await,
            Ok(MessageReturn::SetMaxPowerConsumption(power)) if power == expected_power
        );
        assert_eq!(get_perf_state(devhost_node.clone()).await, 1);
    }

    /// Tests that when a minimum CPU clock speed is specified, a P-state with a lower CPU frequency
    /// is never selected.
    #[fasync::run_singlethreaded(test)]
    async fn test_min_cpu_clock_speed() {
        let mut mock_maker = MockNodeMaker::new();

        // Arbitrary CpuControlParams chosen to allow the node to demonstrate P-state selection
        let capacitance = Farads(100.0e-12);
        let logical_cpu_numbers = vec![0, 1, 2, 3];
        let p_states = vec![
            PState { frequency: Hertz(2.0e9), voltage: Volts(3.0) },
            PState { frequency: Hertz(1.0e9), voltage: Volts(3.0) },
            PState { frequency: Hertz(0.5e9), voltage: Volts(3.0) },
        ];

        // The modeled power consumption at each P-state
        let power_consumption: Vec<Watts> = p_states
            .iter()
            .map(|p_state| {
                get_cpu_power(
                    capacitance,
                    p_state.voltage,
                    Hertz(p_state.frequency.0 * logical_cpu_numbers.len() as f64),
                )
            })
            .collect();

        let stats_node = mock_maker.make(
            "StatsNode",
            // The CpuControlHandler node queries the current CPU load each time it receives a
            // SetMaxPowerConsumption message
            vec![
                (msg_eq!(GetCpuLoads), msg_ok_return!(GetCpuLoads(vec![1.0; 4]))),
                (msg_eq!(GetCpuLoads), msg_ok_return!(GetCpuLoads(vec![1.0; 4]))),
            ],
        );
        let devhost_node = mock_maker.make(
            "DevHostNode",
            vec![
                // CpuControlHandler lazy queries performance state during its initialization
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(0))),
                // The test queries for current performance state
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(0))),
                // CpuControlHandler changes performance state to 1
                (msg_eq!(SetPerformanceState(1)), msg_ok_return!(SetPerformanceState)),
                // The test queries for current performance state
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(1))),
            ],
        );

        let cpu_ctrl_node = CpuControlHandlerBuilder::new()
            .cpu_ctrl_proxy(fake_cpu_ctrl_driver_with_p_states(p_states))
            .capacitance(capacitance)
            .logical_cpu_numbers(logical_cpu_numbers)
            .cpu_stats_handler(stats_node)
            .cpu_dev_handler(devhost_node.clone())
            .min_cpu_clock_speed(Hertz(1.0e9))
            .build_and_init()
            .await;

        // Test case 1: Allow power consumption of the highest P-state; expect to be in P-state 0
        let commanded_power = power_consumption[0].mul_scalar(1.01);
        let expected_power = power_consumption[0];
        assert_matches!(
            cpu_ctrl_node.handle_message(&Message::SetMaxPowerConsumption(commanded_power)).await,
            Ok(MessageReturn::SetMaxPowerConsumption(power)) if power == expected_power
        );
        assert_eq!(get_perf_state(devhost_node.clone()).await, 0);

        // Test case 2: Reduce power consumption to below the lowest P-state; expect to be in
        // P-state 1 (state 2 should be disallowed).
        let commanded_power = Watts(0.0);
        let expected_power = power_consumption[1];
        assert_matches!(
            cpu_ctrl_node.handle_message(&Message::SetMaxPowerConsumption(commanded_power)).await,
            Ok(MessageReturn::SetMaxPowerConsumption(power)) if power == expected_power
        );
        assert_eq!(get_perf_state(devhost_node.clone()).await, 1);
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let mut mock_maker = MockNodeMaker::new();
        let devhost_node = mock_maker.make(
            "DevHostNode",
            vec![
                // CpuControlHandler queries performance state during its initialization
                (msg_eq!(GetPerformanceState), msg_ok_return!(GetPerformanceState(0))),
            ],
        );

        // Some dummy CpuControlParams to verify the params get published in Inspect
        let p_state = PState { frequency: Hertz(2.0e9), voltage: Volts(4.5) };
        let capacitance = Farads(100.0e-12);
        let logical_cpu_numbers = vec![0, 1, 2, 3];

        let inspector = inspect::Inspector::new();

        let _node = CpuControlHandlerBuilder::new()
            .cpu_ctrl_proxy(fake_cpu_ctrl_driver_with_p_states(vec![p_state]))
            .cpu_dev_handler(devhost_node)
            .capacitance(capacitance)
            .logical_cpu_numbers(logical_cpu_numbers)
            .inspect_root(inspector.root())
            .build_and_init()
            .await;

        assert_data_tree!(
            inspector,
            root: {
                "CpuControlHandler (TestCpuControlHandler)": contains {
                    cpu_control_params: {
                        "capacitance (F)": 100.0e-12,
                        logical_cpu_numbers: "[0, 1, 2, 3]",
                        p_state_0: {
                            "voltage (V)": 4.5,
                            "frequency (Hz)": 2.0e9
                        }
                    },
                }
            }
        );
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "CpuControlHandler",
            "name": "cpu_control",
            "config": {
                "driver_path": "/dev/class/cpu-ctrl/000",
                "capacitance": 1.2E-10,
                "min_cpu_clock_speed": 1.0e9,
                "logical_cpu_numbers": [0, 1]
            },
            "dependencies": {
                "cpu_stats_handler_node": "cpu_stats",
                "cpu_dev_handler_node": "cpu_dev"
            }
        });

        let mut mock_maker = MockNodeMaker::new();
        let mut nodes: HashMap<String, Rc<dyn Node>> = HashMap::new();
        nodes.insert("cpu_stats".to_string(), mock_maker.make("MockNode", vec![]));
        nodes.insert("cpu_dev".to_string(), mock_maker.make("MockNode", vec![]));
        let _ = CpuControlHandlerBuilder::new_from_json(json_data, &nodes);
    }

    /// Tests that node config files do not contain instances of CpuControlHandler nodes with
    /// overlapping CPU numbers.
    #[test]
    pub fn test_config_files() -> Result<(), anyhow::Error> {
        crate::utils::test_each_node_config_file(|config_file| {
            let cpu_control_handlers =
                config_file.iter().filter(|n| n["type"] == "CpuControlHandler");

            let mut set = HashSet::new();
            for node in cpu_control_handlers {
                for cpu in node["config"]["logical_cpu_numbers"].as_array().unwrap() {
                    let cpu_idx = cpu.as_i64().unwrap();
                    if set.contains(&cpu_idx) {
                        return Err(format_err!("CPU {} already specified", cpu_idx));
                    }

                    set.insert(cpu_idx);
                }
            }

            Ok(())
        })
    }
}
