// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::{Celsius, Watts};

/// Defines the message types and arguments to be used for inter-node communication
#[derive(Debug)]
#[allow(dead_code)]
pub enum Message<'a> {
    /// Read the temperature from a given temperature driver
    /// Arg: a string specifying the complete driver path to read
    ReadTemperature(&'a str),

    /// Get the number of CPUs in the system
    GetNumCpus,

    /// Get the total CPU load which is the sum of the load of all CPUs in the system. Per-CPU load
    /// is reported as a value between 0.0 - 1.0 and is calculated by dividing the total time a
    /// CPU spent not idle during a duration by the total time elapsed during the same duration,
    /// where the duration is defined as the time since the previous GetTotalCpuLoad call. The
    /// first call returns a load of 0.0 because the time duration required to calculate load is
    /// undefined without a second call.
    GetTotalCpuLoad,

    /// Instruct the node to limit the power consumption of its corresponding component (e.g., CPU)
    /// Arg: the max number of watts that the component should be allowed to consume
    SetMaxPowerConsumption(Watts),
}

/// Defines the return values for each of the Message types from above
#[derive(Debug)]
#[allow(dead_code)]
pub enum MessageReturn {
    /// Arg: temperature in Celsius
    ReadTemperature(Celsius),

    /// Arg: the number of CPUs in the system
    GetNumCpus(u32),

    /// Arg: the sum of the load from all CPUs in the system. The value is defined as
    /// 0.0 - [number_cpus]. The first call will return a load of 0.0.
    GetTotalCpuLoad(f32),

    /// There is no arg in this MessageReturn type. It essentially only serves as an ACK.
    SetMaxPowerConsumption,
}
