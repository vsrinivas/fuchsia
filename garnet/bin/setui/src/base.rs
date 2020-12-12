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

use crate::switchboard::accessibility_types::AccessibilityInfo;
use crate::switchboard::base::{
    AudioInfo, DeviceInfo, DisplayInfo, DoNotDisturbInfo, FactoryResetInfo, InputInfo, LightData,
    NightModeInfo, PrivacyInfo, SetupInfo,
};
use crate::switchboard::intl_types::IntlInfo;
use crate::switchboard::light_types::LightInfo;

/// Enumeration over the possible info types available in the service.
#[derive(PartialEq, Debug, Clone)]
pub enum SettingInfo {
    /// This value is reserved for testing purposes.
    Unknown,
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
