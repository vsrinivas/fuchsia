// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::shutdown_request::ShutdownRequest;
use crate::types::{Celsius, Nanoseconds, PState, ThermalLoad, Watts};
use fuchsia_zircon_sys as sys;

/// Defines the message types and arguments to be used for inter-node communication
#[derive(Debug, PartialEq)]
#[allow(dead_code)]
pub enum Message {
    /// Read the temperature
    ReadTemperature,

    /// Get the number of CPUs in the system
    GetNumCpus,

    /// Get the current load from each CPU in the system as a vector of values in the range [0.0 -
    /// 1.0]. Load is calculated by dividing the total time a CPU spent not idle during a duration
    /// by the total time elapsed during the same duration, where the duration is defined as the
    /// time since the previous GetCpuLoads call. The returned vector will have NUM_CPUS elements,
    /// where NUM_CPUS is the value returned by the GetNumCpus message.
    GetCpuLoads,

    /// Get all performance states for the handler's CPU domain
    GetCpuPerformanceStates,

    // Issues the zx_system_set_performance_info syscall.
    SetCpuPerformanceInfo(Vec<sys::zx_cpu_performance_info_t>),

    /// Instruct the node to limit the power consumption of its corresponding component (e.g., CPU)
    /// Arg: the max number of watts that the component should be allowed to consume
    SetMaxPowerConsumption(Watts),

    /// Command a system shutdown
    /// Arg: a ShutdownRequest indicating the requested shutdown state and reason
    SystemShutdown(ShutdownRequest),

    /// Instruct a node to update its thermal load value
    /// Arg: a ThermalLoad value which represents the severity of thermal load in the system
    UpdateThermalLoad(ThermalLoad),

    /// Get the current performance state
    GetPerformanceState,

    /// Set the new performance state
    /// Arg: a value in the range [0 - x] where x is an upper bound defined in the
    /// dev_control_handler crate. An increasing value indicates a lower performance state.
    SetPerformanceState(u32),

    /// File a crash report
    /// Arg: the crash report signature
    FileCrashReport(String),

    /// Specify the termination system state, intended to be used in the DriverManagerHandler node.
    /// Arg: the SystemPowerState value indicating the termination state
    SetTerminationSystemState(fidl_fuchsia_hardware_power_statecontrol::SystemPowerState),

    /// Notify that the mic enabled state has changed
    /// Arg: the new enabled state
    NotifyMicEnabledChanged(bool),

    /// Notify that the user active state has changed
    /// Arg: the new active state
    NotifyUserActiveChanged(bool),

    /// Log the start of throttling with the Platform Metrics node
    /// Arg: timestamp of the event
    LogThrottleStart(Nanoseconds),

    /// Log the end of throttling (due to successful mitigation) with the Platform Metrics node
    /// Arg: timestamp of the event
    LogThrottleEndMitigated(Nanoseconds),

    /// Log the end of throttling (due to critical shutdown) with the Platform Metrics node
    /// Arg: timestamp of the event
    LogThrottleEndShutdown(Nanoseconds),
}

/// Defines the return values for each of the Message types from above
#[derive(Debug)]
#[allow(dead_code)]
pub enum MessageReturn {
    /// Arg: temperature in Celsius
    ReadTemperature(Celsius),

    /// Arg: the number of CPUs in the system
    GetNumCpus(u32),

    /// Arg: the current load from each CPU in the system as a vector of values in the range [0.0 -
    /// 1.0]. The returned vector will have NUM_CPUS elements, where NUM_CPUS is the value returned
    /// by the GetNumCpus message.
    GetCpuLoads(Vec<f32>),

    /// Arg: all performance states for the CPU domain seviced by the message handler.
    GetCpuPerformanceStates(Vec<PState>),

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    SetCpuPerformanceInfo,

    /// Arg: the max number of watts that the component will use. This number should typically be at
    /// or below the number that was specified in the Message, but there may be cases where it
    /// actually exceeds that number (e.g., a CPU that cannot operate below the requested power
    /// level).
    SetMaxPowerConsumption(Watts),

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    SystemShutdown,

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    UpdateThermalLoad,

    /// Arg: the performance state returned from the node
    GetPerformanceState(u32),

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    SetPerformanceState,

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    FileCrashReport,

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    SetTerminationSystemState,

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    NotifyMicEnabledChanged,

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    NotifyUserActiveChanged,

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    LogThrottleStart,

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    LogThrottleEndMitigated,

    /// There is no arg in this MessageReturn type. It only serves as an ACK.
    LogThrottleEndShutdown,
}

pub type MessageResult = Result<MessageReturn, crate::error::PowerManagerError>;
