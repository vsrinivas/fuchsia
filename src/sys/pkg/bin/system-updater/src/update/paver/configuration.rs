// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_paver::Configuration;

/// The [`fidl_fuchsia_paver::Configuration`]s to which an image should be written.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TargetConfiguration {
    /// The image has a well defined configuration to which it should be written. For example,
    /// Recovery, or, for devices that support ABR, the "inactive" configuration.
    Single(Configuration),

    /// The image would target the A or B configuration, but ABR is not supported on this platform,
    /// so write to A and try to write to B (if a B partition exists).
    AB,
}

/// The configuration which will be used as the default boot choice on a normal cold boot, which
/// may or may not be the currently running configuration, or NotSupported if the device doesn't
/// support ABR.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ActiveConfiguration {
    A,
    B,
    NotSupported,
}

/// The configuration which will *not* be used as the default boot choice on a normal cold boot,
/// which may or may not be the currently running configuration (for example, if the device has an
/// update staged but hasn't rebooted yet), or NotSupported if the device doesn't support ABR.
///
/// The inverse of [`ActiveConfiguration`] iff ABR is supported.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InactiveConfiguration {
    A,
    B,
    NotSupported,
}

impl ActiveConfiguration {
    /// Determines the [`InactiveConfiguration`] for this [`ActiveConfiguration`].
    pub fn to_inactive_configuration(self) -> InactiveConfiguration {
        match self {
            ActiveConfiguration::A => InactiveConfiguration::B,
            ActiveConfiguration::B => InactiveConfiguration::A,
            ActiveConfiguration::NotSupported => InactiveConfiguration::NotSupported,
        }
    }

    /// Converts this [`ActiveConfiguration`] into a specific configuration, or none if ABR is
    /// not supported.
    pub fn to_configuration(self) -> Option<Configuration> {
        match self {
            ActiveConfiguration::A => Some(Configuration::A),
            ActiveConfiguration::B => Some(Configuration::B),
            ActiveConfiguration::NotSupported => None,
        }
    }
}

impl InactiveConfiguration {
    /// For an image that should target the inactive configuration, determines the appropriate
    /// configurations to target.
    pub fn to_target_configuration(self) -> TargetConfiguration {
        match self {
            InactiveConfiguration::A => TargetConfiguration::Single(Configuration::A),
            InactiveConfiguration::B => TargetConfiguration::Single(Configuration::B),
            InactiveConfiguration::NotSupported => TargetConfiguration::AB,
        }
    }

    /// Converts this [`InactiveConfiguration`] into a specific configuration, or none if ABR is
    /// not supported.
    pub fn to_configuration(self) -> Option<Configuration> {
        match self {
            InactiveConfiguration::A => Some(Configuration::A),
            InactiveConfiguration::B => Some(Configuration::B),
            InactiveConfiguration::NotSupported => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abr_not_supported_targets_ab() {
        assert_eq!(
            ActiveConfiguration::NotSupported.to_inactive_configuration().to_configuration(),
            None,
        );
        assert_eq!(
            ActiveConfiguration::NotSupported.to_inactive_configuration().to_target_configuration(),
            TargetConfiguration::AB,
        );
    }

    #[test]
    fn active_a_targets_b() {
        assert_eq!(
            ActiveConfiguration::A.to_inactive_configuration().to_configuration(),
            Some(Configuration::B),
        );
        assert_eq!(
            ActiveConfiguration::A.to_inactive_configuration().to_target_configuration(),
            TargetConfiguration::Single(Configuration::B),
        );
    }

    #[test]
    fn active_b_targets_a() {
        assert_eq!(
            ActiveConfiguration::B.to_inactive_configuration().to_configuration(),
            Some(Configuration::A),
        );
        assert_eq!(
            ActiveConfiguration::B.to_inactive_configuration().to_target_configuration(),
            TargetConfiguration::Single(Configuration::A),
        );
    }
}
