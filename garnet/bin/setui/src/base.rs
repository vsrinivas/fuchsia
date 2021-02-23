// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Service-wide definitions.
//!
//! # Summary
//!
//! The base mod houses the core definitions for communicating information
//! across the service. Note that there are currently references to types in
//! other nested base mods. It is the long-term intention that the common
//! general (non-domain specific or overarching) definitions are migrated here,
//! while particular types, such as setting-specific definitions, are moved to
//! a common base mod underneath the parent setting mod.

use crate::accessibility::types::AccessibilityInfo;
use crate::audio::types::AudioInfo;
use crate::device::types::DeviceInfo;
use crate::display::types::{DisplayInfo, LightData};
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::factory_reset::types::FactoryResetInfo;
use crate::input::types::InputInfo;
use crate::intl::types::IntlInfo;
use crate::light::types::LightInfo;
use crate::night_mode::types::NightModeInfo;
use crate::privacy::types::PrivacyInfo;
use crate::setup::types::SetupInfo;
use serde::{Deserialize, Serialize};

/// The setting types supported by the service.
#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy, Serialize, Deserialize)]
pub enum SettingType {
    /// This value is reserved for testing purposes.
    #[cfg(test)]
    Unknown,
    Accessibility,
    Account,
    Audio,
    Device,
    Display,
    DoNotDisturb,
    FactoryReset,
    Input,
    Intl,
    Light,
    LightSensor,
    NightMode,
    Power,
    Privacy,
    Setup,
}

/// Enumeration over the possible info types available in the service.
#[derive(PartialEq, Debug, Clone)]
pub enum SettingInfo {
    /// This value is reserved for testing purposes.
    #[cfg(test)]
    Unknown(UnknownInfo),
    Accessibility(AccessibilityInfo),
    Audio(AudioInfo),
    Brightness(DisplayInfo),
    Device(DeviceInfo),
    FactoryReset(FactoryResetInfo),
    Light(LightInfo),
    LightSensor(LightData),
    DoNotDisturb(DoNotDisturbInfo),
    Input(InputInfo),
    Intl(IntlInfo),
    NightMode(NightModeInfo),
    Privacy(PrivacyInfo),
    Setup(SetupInfo),
}

impl SettingInfo {
    /// Returns the name of the enum and its value, debug-formatted, for writing to inspect.
    // TODO(fxbug.dev/56718): simplify this with a macro.
    // TODO(fxbug.dev/66690): move this into InspectBroker
    pub fn for_inspect(&self) -> (&'static str, String) {
        match self {
            #[cfg(test)]
            SettingInfo::Unknown(info) => ("Unknown", format!("{:?}", info)),
            SettingInfo::Accessibility(info) => ("Accessibility", format!("{:?}", info)),
            SettingInfo::Audio(info) => ("Audio", format!("{:?}", info)),
            SettingInfo::Brightness(info) => ("Brightness", format!("{:?}", info)),
            SettingInfo::Device(info) => ("Device", format!("{:?}", info)),
            SettingInfo::FactoryReset(info) => ("FactoryReset", format!("{:?}", info)),
            SettingInfo::Light(info) => ("Light", format!("{:?}", info)),
            SettingInfo::LightSensor(info) => ("LightSensor", format!("{:?}", info)),
            SettingInfo::DoNotDisturb(info) => ("DoNotDisturb", format!("{:?}", info)),
            SettingInfo::Input(info) => ("Input", format!("{:?}", info)),
            SettingInfo::Intl(info) => ("Intl", format!("{:?}", info)),
            SettingInfo::NightMode(info) => ("NightMode", format!("{:?}", info)),
            SettingInfo::Privacy(info) => ("Privacy", format!("{:?}", info)),
            SettingInfo::Setup(info) => ("Setup", format!("{:?}", info)),
        }
    }
}

/// This struct is reserved for testing purposes. Some tests need to verify data changes, bool value
/// can be used for this purpose.
#[derive(PartialEq, Debug, Clone)]
#[cfg(test)]
pub struct UnknownInfo(pub bool);
