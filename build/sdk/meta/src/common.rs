// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

pub type File = String;

pub type FidlLibraryName = String;

pub type CcLibraryName = String;

pub type BanjoLibraryName = String;

#[derive(Serialize, Deserialize, Debug, Hash, Clone, PartialOrd, Ord, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum TargetArchitecture {
    Arm64,
    X64,
}

impl Default for TargetArchitecture {
    fn default() -> Self {
        TargetArchitecture::X64
    }
}

#[derive(Serialize, Deserialize, Debug, Hash, Clone, PartialOrd, Ord, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum PointingDevice {
    Mouse,
    None,
    Touch,
}

impl Default for PointingDevice {
    fn default() -> Self {
        PointingDevice::None
    }
}

#[derive(Serialize, Deserialize, Debug, Hash, Clone, PartialOrd, Ord, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum ScreenUnits {
    Pixels,
}

impl Default for ScreenUnits {
    fn default() -> Self {
        ScreenUnits::Pixels
    }
}

#[derive(Serialize, Deserialize, Debug, Hash, Clone, PartialOrd, Ord, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum DataUnits {
    Bytes,
    Kilobytes,
    Megabytes,
    Gigabytes,
    Terabytes,
}

impl Default for DataUnits {
    fn default() -> Self {
        DataUnits::Bytes
    }
}

#[derive(Serialize, Deserialize, Debug, Hash, Clone, PartialOrd, Ord, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum AudioModel {
    AC97,
    AdLib,
    Cs4231A,
    ES1370,
    Gus,
    Hda,
    None,
    PcSpk,
    SB16,
}

impl Default for AudioModel {
    fn default() -> Self {
        AudioModel::None
    }
}

#[derive(Serialize, Deserialize, Debug, Hash, PartialEq, Eq, Clone, PartialOrd, Ord)]
#[serde(rename_all = "snake_case")]
pub enum ElementType {
    BanjoLibrary,
    CcPrebuiltLibrary,
    CcSourceLibrary,
    Config,
    DartLibrary,
    Documentation,
    FidlLibrary,
    HostTool,
    License,
    LoadableModule,
    PhysicalDevice,
    ProductBundle,
    Sysroot,
    VirtualDevice,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct Envelope<D> {
    /// The value of the $id field of the schema constraining the envelope.
    pub schema_id: String,
    pub data: D,
}
