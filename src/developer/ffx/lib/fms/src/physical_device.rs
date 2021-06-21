// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Representation of the physical_device metadata.

use serde::Deserialize;

/// Specifics for a CPU.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct Cpu {
    /// Likely one of "x64" or "arm64".
    arch: String,
}

/// Specifics for a given hardware platform.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct Hardware {
    /// Details of the Central Processing Unit (CPU).
    cpu: Cpu,
}

/// Description of a physical (rather than virtual) hardware device.
///
/// This does not include the data "envelope", i.e. it begins within /data in
/// the source json file.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct PhysicalDeviceSpec {
    /// A unique name identifying this FMS entry.
    pub name: String,

    /// Always "physical_device" for a PhysicalDeviceSpec. This is valuable for
    /// debugging or when writing this record to a json string.
    pub kind: String,

    /// Details about the properties of the device.
    pub hardware: Hardware,
}
