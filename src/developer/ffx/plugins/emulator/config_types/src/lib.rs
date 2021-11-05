// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module holds the common data types for emulator engines. These are implementation-agnostic
//! data types, not the engine-specific command types that each engine will define for itself. These
//! types will be directly deserializable from the PBM, and converted into engine-specific types at
//! runtime.

use ffx_emulator_engines_vdl_args::VdlConfig;
use serde::Deserialize;

/// This holds the image files and other information specific to the guest OS.
#[derive(Debug, Deserialize)]
pub struct GuestConfig {}

/// This holds the engine-specific configuration data.
#[derive(Debug, Deserialize)]
pub enum HostConfig {
    Vdl(VdlConfig),
}
