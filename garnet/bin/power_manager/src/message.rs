// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::Celsius;

/// Defines the message types and arguments to be used for inter-node communication
#[derive(Debug)]
pub enum Message<'a> {
    /// Purpose: read the temperature from a given temperature driver
    /// Arg: string specifying the complete driver path to read
    ReadTemperature(&'a str),

    /// Purpose: get the CPU % idle
    /// TODO(pshickel): may want to add an argument to specify a time duration
    GetCpuIdlePct,
}

/// Defines the return values for each of the Message types from above
#[derive(Debug)]
#[allow(dead_code)]
pub enum MessageReturn {
    /// Arg: temperature in Celsius
    ReadTemperature(Celsius),

    /// Arg: percent of time CPU was idle
    GetCpuIdlePct(u32),
}
