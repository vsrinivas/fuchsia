// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::dev_control_handler;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::types::{Farads, Hertz, Volts, Watts};
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_hardware_cpu_ctrl as fcpuctrl;
use std::cell::{Cell, RefCell};
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
#[derive(Clone, Debug)]
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

/// The CpuControlHandler node.
pub struct CpuControlHandler {
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
}

impl CpuControlHandler {
    pub fn new(
        cpu_driver_path: String,
        // TODO(pshickel): Eventually we may want to query capacitance from the CPU driver (same as
        // we do for CPU P-states)
        capacitance: Farads,
        cpu_stats_handler: Rc<dyn Node>,
        cpu_dev_handler_node: Rc<dyn Node>,
    ) -> Result<Rc<Self>, Error> {
        Ok(Self::new_with_cpu_ctrl_proxy(
            Self::connect_cpu_proxy(cpu_driver_path)?,
            capacitance,
            cpu_stats_handler,
            cpu_dev_handler_node,
        ))
    }

    /// Create the node with an existing CpuCtrl proxy (test configuration can use this
    /// to pass a proxy which connects to a fake driver)
    fn new_with_cpu_ctrl_proxy(
        cpu_ctrl_proxy: fcpuctrl::DeviceProxy,
        capacitance: Farads,
        cpu_stats_handler: Rc<dyn Node>,
        cpu_dev_handler_node: Rc<dyn Node>,
    ) -> Rc<Self> {
        Rc::new(Self {
            cpu_control_params: RefCell::new(CpuControlParams {
                p_states: Vec::new(),
                capacitance,
                num_cores: 0,
            }),
            current_p_state_index: Cell::new(None),
            cpu_stats_handler,
            cpu_dev_handler_node,
            cpu_ctrl_proxy,
        })
    }

    fn connect_cpu_proxy(cpu_driver_path: String) -> Result<fcpuctrl::DeviceProxy, Error> {
        let (proxy, server) = fidl::endpoints::create_proxy::<fcpuctrl::DeviceMarker>()
            .map_err(|e| format_err!("Failed to create proxy: {}", e))?;

        fdio::service_connect(&cpu_driver_path, server.into_channel()).map_err(|s| {
            format_err!("Failed to connect to service at {}: {}", cpu_driver_path, s)
        })?;
        Ok(proxy)
    }

    /// Construct CpuControlParams by querying the required information from the CpuCtrl interface.
    async fn get_cpu_params(&self) -> Result<(), Error> {
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
        Ok(())
    }

    /// Returns the total CPU load (averaged since the previous call)
    async fn get_load(&self) -> Result<f32, Error> {
        match self.send_message(&self.cpu_stats_handler, &Message::GetTotalCpuLoad).await {
            Ok(MessageReturn::GetTotalCpuLoad(load)) => Ok(load),
            Ok(r) => Err(format_err!("GetTotalCpuLoad had unexpected return value: {:?}", r)),
            Err(e) => Err(format_err!("GetTotalCpuLoad failed: {:?}", e)),
        }
    }

    /// Returns the current CPU P-state index
    async fn get_current_p_state_index(&self) -> Result<usize, Error> {
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
        // The operation completion rate over the last sample interval is
        //     num_operations / sample_interval,
        // where
        //     num_operations = last_load * last_frequency * sample_interval.
        // Hence,
        //     last_operation_rate = last_load * last_frequency.

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

        let last_operation_rate = {
            // TODO(pshickel): Eventually we'll need a way to query the load only from the cores we
            // care about. As far as I can tell, there isn't currently a way to correlate the CPU
            // info coming from CpuStats with that from CpuCtrl.
            let last_load = self.get_load().await? as f64;
            let last_frequency = cpu_params.p_states[current_p_state_index].frequency;
            last_frequency.mul_scalar(last_load)
        };

        // If no P-states meet the selection criterion, use the lowest-power state.
        let mut p_state_index = cpu_params.p_states.len() - 1;

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
            // Tell the CPU DeviceControlHandler to update the performance state
            self.send_message(
                &self.cpu_dev_handler_node,
                &Message::SetPerformanceState(p_state_index as u32),
            )
            .await?;

            // Cache the new P-state index for calculations on the next iteration
            self.current_p_state_index.set(Some(p_state_index));
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

#[cfg(test)]
pub mod tests {
    use super::*;
    use crate::cpu_stats_handler;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::TryStreamExt;

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
        CpuControlHandler::new_with_cpu_ctrl_proxy(
            setup_fake_service(params),
            capacitance,
            cpu_stats_handler,
            cpu_dev_handler_node,
        )
    }

    #[test]
    fn test_get_cpu_power() {
        assert_eq!(get_cpu_power(Farads(100.0e-12), Volts(1.0), Hertz(1.0e9)), Watts(0.1));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_max_power_consumption() {
        let recvd_perf_state = Rc::new(Cell::new(0));
        let recvd_perf_state_clone = recvd_perf_state.clone();
        let set_performance_state = move |state| {
            recvd_perf_state_clone.set(state);
        };
        let devhost_node = dev_control_handler::tests::setup_test_node(set_performance_state);

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
        let cpu_ctrl_node = setup_test_node(cpu_params, cpu_stats_node, devhost_node);

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
        assert_eq!(recvd_perf_state.get(), 0);

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
        assert_eq!(recvd_perf_state.get(), 1);

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
        assert_eq!(recvd_perf_state.get(), 1);
    }
}
