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

#[derive(Serialize, Deserialize, Debug, Hash, PartialEq, Eq, Clone, PartialOrd, Ord)]
#[serde(rename_all = "snake_case")]
pub enum ElementType {
    BanjoLibrary,
    CcPrebuiltLibrary,
    CcSourceLibrary,
    DartLibrary,
    DeviceProfile,
    Documentation,
    FidlLibrary,
    HostTool,
    LoadableModule,
    Sysroot,
}
