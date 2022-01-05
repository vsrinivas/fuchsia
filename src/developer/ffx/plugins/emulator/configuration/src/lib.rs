// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module holds the common data types for emulator engines. These are implementation-agnostic
//! data types, not the engine-specific command types that each engine will define for itself. These
//! types will be directly deserializable from the PBM, and converted into engine-specific types at
//! runtime.

mod conversions;
mod enumerations;
mod interface_types;

pub use conversions::*;
pub use enumerations::*;
pub use interface_types::*;

pub use sdk_metadata::{AudioDevice, AudioModel, DataAmount, DataUnits, PointingDevice};
