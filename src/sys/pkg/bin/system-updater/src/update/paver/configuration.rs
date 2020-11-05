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

/// The configuration which is *not* the running configuration, and is either A or B.
/// Used to compute target configurations for the update path.
/// Does not contain a representation of R to avoid ever attempting to set R as the target
/// configuration in the non-force-recovery path.
///
/// The inverse of [`CurrentConfiguration`] iff ABR is supported, except if the current
/// configuration is Recovery. In that case, NonCurrent should be A.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NonCurrentConfiguration {
    A,
    B,
    NotSupported,
}

impl NonCurrentConfiguration {
    /// Converts this [`NonCurrentConfiguration`] into a specific configuration, or none if ABR is
    /// not supported.
    pub fn to_configuration(self) -> Option<Configuration> {
        match self {
            NonCurrentConfiguration::A => Some(Configuration::A),
            NonCurrentConfiguration::B => Some(Configuration::B),
            NonCurrentConfiguration::NotSupported => None,
        }
    }

    /// For an image that should target the non-current configuration, determines the appropriate
    /// configurations to target.
    pub fn to_target_configuration(self) -> TargetConfiguration {
        match self {
            NonCurrentConfiguration::A => TargetConfiguration::Single(Configuration::A),
            NonCurrentConfiguration::B => TargetConfiguration::Single(Configuration::B),
            NonCurrentConfiguration::NotSupported => TargetConfiguration::AB,
        }
    }
}

/// The currently running configuration, or NotSupported if the device doesn't
/// support ABR.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CurrentConfiguration {
    A,
    B,
    Recovery,
    NotSupported,
}

impl CurrentConfiguration {
    /// Converts this [`CurrentConfiguration`] into a specific configuration, or none if ABR is
    /// not supported.
    pub fn to_configuration(self) -> Option<Configuration> {
        match self {
            CurrentConfiguration::A => Some(Configuration::A),
            CurrentConfiguration::B => Some(Configuration::B),
            CurrentConfiguration::Recovery => Some(Configuration::Recovery),
            CurrentConfiguration::NotSupported => None,
        }
    }

    /// Determines the [`NonCurrentConfiguration`] for this [`CurrentConfiguration`].
    /// Used to determine which partition to write to during the system update.
    pub fn to_non_current_configuration(self) -> NonCurrentConfiguration {
        match self {
            CurrentConfiguration::A => NonCurrentConfiguration::B,
            CurrentConfiguration::B => NonCurrentConfiguration::A,
            CurrentConfiguration::Recovery => NonCurrentConfiguration::A,
            CurrentConfiguration::NotSupported => NonCurrentConfiguration::NotSupported,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abr_not_supported_targets_ab() {
        assert_eq!(
            CurrentConfiguration::NotSupported
                .to_non_current_configuration()
                .to_target_configuration(),
            TargetConfiguration::AB,
        );
        assert_eq!(
            CurrentConfiguration::NotSupported.to_non_current_configuration().to_configuration(),
            None
        );
    }

    #[test]
    fn current_a_targets_b() {
        assert_eq!(
            CurrentConfiguration::A.to_non_current_configuration().to_target_configuration(),
            TargetConfiguration::Single(Configuration::B),
        );
        assert_eq!(
            CurrentConfiguration::A.to_non_current_configuration().to_configuration(),
            Some(Configuration::B),
        );
    }

    #[test]
    fn current_b_targets_a() {
        assert_eq!(
            CurrentConfiguration::B.to_non_current_configuration().to_target_configuration(),
            TargetConfiguration::Single(Configuration::A),
        );
        assert_eq!(
            CurrentConfiguration::B.to_non_current_configuration().to_configuration(),
            Some(Configuration::A),
        );
    }

    #[test]
    fn current_r_targets_a() {
        assert_eq!(
            CurrentConfiguration::Recovery.to_non_current_configuration().to_target_configuration(),
            TargetConfiguration::Single(Configuration::A),
        );
        assert_eq!(
            CurrentConfiguration::Recovery.to_non_current_configuration().to_configuration(),
            Some(Configuration::A),
        );
    }
}
