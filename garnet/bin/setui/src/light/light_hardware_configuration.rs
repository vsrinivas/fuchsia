// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::light_types::LightType;
use serde::{Deserialize, Serialize};

#[derive(PartialEq, Debug, Clone, Deserialize)]
pub struct LightHardwareConfiguration {
    /// List of light groups to surface to clients of the API.
    pub light_groups: Vec<LightGroupConfiguration>,
}

#[derive(PartialEq, Debug, Clone, Deserialize)]
pub struct LightGroupConfiguration {
    /// Name of the light group.
    ///
    /// Must be unique as this is the primary identifier for light groups.
    pub name: String,

    /// Each light in the underlying fuchsia.hardware.light API has a unique, fixed index. We need
    /// to remember the index of the lights in this light group in order to write values back.
    pub hardware_index: Vec<u32>,

    /// Type of values the light group supports, must match the underlying type of all the lights in
    /// the group.
    pub light_type: LightType,

    /// True if the values of this light group should be persisted across reboots and restored when
    /// the settings service starts.
    pub persist: bool,

    /// A list of conditions under which the "enabled" field of the light group should be false,
    /// which signals to clients the light's state is being overridden by external conditions, such
    /// as an LED dedicated to showing that a device's mic is muted that is off when the mic is not
    /// muted.
    ///
    /// Lights that are disabled can still have their value set, but the changes may not be
    /// noticeable to the user until the condition disabling/overriding ends.
    pub disable_conditions: Vec<DisableConditions>,
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub enum DisableConditions {
    /// Signals that the light group should be marked as disabled when the device's mic switch is
    /// set to "on".
    MicSwitch,
}
