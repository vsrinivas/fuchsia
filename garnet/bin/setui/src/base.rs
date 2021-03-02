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

/// This macro takes an enum, which has variants associated with exactly one data, and
/// generates the same enum and implements a for_inspect method.
/// The for_inspect method returns variants' names and formated data contents.
#[macro_export]
macro_rules! generate_inspect_with_info {
    ($(#[$metas:meta])* pub enum $name:ident {
        $(
            $(#[doc = $str:expr])*
            $(#[cfg($test:meta)])?
            $variant:ident ( $data:ty )
        ),* $(,)?
    }
    ) => {
        $(#[$metas])*
        pub enum $name {
            $(
                $(#[doc = $str])*
                $(#[cfg($test)])?
                $variant($data),
            )*
        }

        impl $name {
            /// Returns the name of the enum and its value, debug-formatted, for writing to inspect.
            pub fn for_inspect(&self) -> (&'static str, String) {
                match self {
                    $(
                        $(#[cfg($test)])?
                        $name::$variant(info) => (stringify!($variant), format!("{:?}", info)),
                    )*
                }
            }
        }
    };
}

generate_inspect_with_info! {
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
}

/// This struct is reserved for testing purposes. Some tests need to verify data changes, bool value
/// can be used for this purpose.
#[derive(PartialEq, Debug, Clone)]
#[cfg(test)]
pub struct UnknownInfo(pub bool);
