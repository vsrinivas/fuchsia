// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Deserialize;

#[derive(Default, Debug, Deserialize, PartialEq)]
/// This is a placeholder for VDL-engine-specific parameters. Historically
/// these were held in the StartCommand type, but in the new design we will
/// have engine-specific types that are the inner type of a HostConfig
/// variant. See emulator/config_types for the HostConfig parent. Note that
/// this is distinct from the DeviceSpec and GuestConfig, which contain
/// parameters for the virtual device configuration and the guest operating
/// system configuration respectively.
pub struct VdlConfig {}
