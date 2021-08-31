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
use std::array;
use std::collections::HashSet;
use std::convert::TryFrom;

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
    Privacy,
    Setup,
}

/// [Entity] defines the types of components that exist within the setting service. Entities can be
/// any part of the system that can be interacted with. Others can reference [Entities](Entity) to
/// declare associations, such as dependencies.
#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum Entity {
    /// A component that handles requests for the specified [SettingType].
    Handler(SettingType),
}

/// A [Dependency] declares a reliance of a particular configuration/feature/component/etc. within
/// the setting service. [Dependencies](Dependency) are used to generate the necessary component map
/// to support a particular service configuration. It can used to determine if the platform/product
/// configuration can support the requested service configuration.
#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub(crate) enum Dependency {
    /// An [Entity] is a component within the setting service.
    Entity(Entity),
}

impl Dependency {
    /// Returns whether the [Dependency] can be handled by the provided environment. Currently, this
    /// only involves [SettingType] handlers.
    pub(crate) fn is_fulfilled(&self, entities: &HashSet<Entity>) -> bool {
        match self {
            Dependency::Entity(entity) => entities.contains(entity),
        }
    }
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
            pub(crate) fn for_inspect(&self) -> (&'static str, String) {
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

pub(crate) trait HasSettingType {
    const SETTING_TYPE: SettingType;
}

macro_rules! conversion_impls {
    ($($(#[cfg($test:meta)])? $variant:ident($info_ty:ty) => $ty_variant:ident ),+ $(,)?) => {
        $(
            $(#[cfg($test)])?
            impl HasSettingType for $info_ty {
                const SETTING_TYPE: SettingType = SettingType::$ty_variant;
            }

            $(#[cfg($test)])?
            impl TryFrom<SettingInfo> for $info_ty {
                type Error = ();

                fn try_from(setting_info: SettingInfo) -> Result<Self, ()> {
                    match setting_info {
                        SettingInfo::$variant(info) => Ok(info),
                        _ => Err(()),
                    }
                }
            }
        )+
    }
}

conversion_impls! {
    #[cfg(test)] Unknown(UnknownInfo) => Unknown,
    Accessibility(AccessibilityInfo) => Accessibility,
    Audio(AudioInfo) => Audio,
    Brightness(DisplayInfo) => Display,
    Device(DeviceInfo) => Device,
    FactoryReset(FactoryResetInfo) => FactoryReset,
    Light(LightInfo) => Light,
    LightSensor(LightData) => LightSensor,
    DoNotDisturb(DoNotDisturbInfo) => DoNotDisturb,
    Input(InputInfo) => Input,
    Intl(IntlInfo) => Intl,
    NightMode(NightModeInfo) => NightMode,
    Privacy(PrivacyInfo) => Privacy,
    Setup(SetupInfo) => Setup,
}

impl From<&SettingInfo> for SettingType {
    fn from(info: &SettingInfo) -> SettingType {
        match info {
            #[cfg(test)]
            SettingInfo::Unknown(_) => SettingType::Unknown,
            SettingInfo::Accessibility(_) => SettingType::Accessibility,
            SettingInfo::Audio(_) => SettingType::Audio,
            SettingInfo::Brightness(_) => SettingType::Display,
            SettingInfo::Device(_) => SettingType::Device,
            SettingInfo::DoNotDisturb(_) => SettingType::DoNotDisturb,
            SettingInfo::FactoryReset(_) => SettingType::FactoryReset,
            SettingInfo::Input(_) => SettingType::Input,
            SettingInfo::Intl(_) => SettingType::Intl,
            SettingInfo::Light(_) => SettingType::Light,
            SettingInfo::LightSensor(_) => SettingType::LightSensor,
            SettingInfo::NightMode(_) => SettingType::NightMode,
            SettingInfo::Privacy(_) => SettingType::Privacy,
            SettingInfo::Setup(_) => SettingType::Setup,
        }
    }
}

/// This struct is reserved for testing purposes. Some tests need to verify data changes, bool value
/// can be used for this purpose.
#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
#[cfg(test)]
pub struct UnknownInfo(pub bool);

/// The `Merge` trait allows merging two structs.
pub(crate) trait Merge<Other = Self> {
    /// Returns a copy of the original struct where the values of all fields set in `other`
    /// replace the matching fields in the copy of `self`.
    fn merge(&self, other: Other) -> Self;
}

/// Returns default setting types. These types should be product-agnostic,
/// capable of operating with platform level support.
pub fn get_default_setting_types() -> HashSet<SettingType> {
    array::IntoIter::new([
        SettingType::Accessibility,
        SettingType::Device,
        SettingType::Intl,
        SettingType::Privacy,
        SettingType::Setup,
    ])
    .collect()
}

/// Returns all known setting types. New additions to SettingType should also
/// be inserted here.
#[cfg(test)]
pub(crate) fn get_all_setting_types() -> HashSet<SettingType> {
    array::IntoIter::new([
        SettingType::Accessibility,
        SettingType::Audio,
        SettingType::Device,
        SettingType::Display,
        SettingType::DoNotDisturb,
        SettingType::FactoryReset,
        SettingType::Input,
        SettingType::Intl,
        SettingType::Light,
        SettingType::LightSensor,
        SettingType::NightMode,
        SettingType::Privacy,
        SettingType::Setup,
    ])
    .collect()
}

#[cfg(test)]
mod testing {
    use super::{SettingInfo, UnknownInfo};
    use crate::handler::device_storage::DeviceStorageCompatible;

    impl DeviceStorageCompatible for UnknownInfo {
        const KEY: &'static str = "unknown_info";

        fn default_value() -> Self {
            Self(false)
        }
    }

    impl From<UnknownInfo> for SettingInfo {
        fn from(info: UnknownInfo) -> SettingInfo {
            SettingInfo::Unknown(info)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[allow(clippy::bool_assert_comparison)]
    #[test]
    fn test_dependency_fulfillment() {
        let target_entity = Entity::Handler(SettingType::Unknown);
        let dependency = Dependency::Entity(target_entity);
        let mut available_entities = HashSet::new();

        // Verify that an empty entity set does not fulfill dependency.
        assert_eq!(dependency.is_fulfilled(&available_entities), false);

        // Verify an entity set without the target entity does not fulfill dependency.
        available_entities.insert(Entity::Handler(SettingType::FactoryReset));
        assert_eq!(dependency.is_fulfilled(&available_entities), false);

        // Verify an entity set with target entity does fulfill dependency.
        available_entities.insert(target_entity);
        assert!(dependency.is_fulfilled(&available_entities));
    }
}
