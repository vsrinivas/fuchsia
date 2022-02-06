// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Functional equivalent of [`otsys::otSecurityPolicy`](crate::otsys::otSecurityPolicy).
#[derive(Default, Clone)]
#[repr(transparent)]
pub struct SecurityPolicy(pub otSecurityPolicy);

impl_ot_castable!(SecurityPolicy, otSecurityPolicy);

impl std::fmt::Debug for SecurityPolicy {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SecurityPolicy")
            .field("rotation_time_hours", &self.get_rotation_time_in_hours())
            .field("version_threshold_for_routing", &self.get_version_threshold_for_routing())
            .field("is_obtain_network_key_enabled", &self.is_obtain_network_key_enabled())
            .field("is_native_commissioning_enabled", &self.is_native_commissioning_enabled())
            .field("is_routers_enabled", &self.is_routers_enabled())
            .field("is_external_commissioning_enabled", &self.is_external_commissioning_enabled())
            .field("is_beacons_enabled", &self.is_beacons_enabled())
            .field(
                "is_commercial_commissioning_enabled",
                &self.is_commercial_commissioning_enabled(),
            )
            .field("is_autonomous_enrollment_enabled", &self.is_autonomous_enrollment_enabled())
            .field(
                "is_network_key_provisioning_enabled",
                &self.is_network_key_provisioning_enabled(),
            )
            .field("is_toble_link_enabled", &self.is_toble_link_enabled())
            .field("is_non_ccm_routers_enabled", &self.is_non_ccm_routers_enabled())
            .finish()
    }
}

#[allow(missing_docs)]
impl SecurityPolicy {
    pub fn get_rotation_time_in_hours(&self) -> u16 {
        self.0.mRotationTime
    }

    pub fn is_obtain_network_key_enabled(&self) -> bool {
        self.0.mObtainNetworkKeyEnabled()
    }

    pub fn is_native_commissioning_enabled(&self) -> bool {
        self.0.mNativeCommissioningEnabled()
    }

    pub fn is_routers_enabled(&self) -> bool {
        self.0.mRoutersEnabled()
    }

    pub fn is_external_commissioning_enabled(&self) -> bool {
        self.0.mExternalCommissioningEnabled()
    }

    pub fn is_beacons_enabled(&self) -> bool {
        self.0.mBeaconsEnabled()
    }

    pub fn is_commercial_commissioning_enabled(&self) -> bool {
        self.0.mCommercialCommissioningEnabled()
    }

    pub fn is_autonomous_enrollment_enabled(&self) -> bool {
        self.0.mAutonomousEnrollmentEnabled()
    }

    pub fn is_network_key_provisioning_enabled(&self) -> bool {
        self.0.mNetworkKeyProvisioningEnabled()
    }

    pub fn is_toble_link_enabled(&self) -> bool {
        self.0.mTobleLinkEnabled()
    }

    pub fn is_non_ccm_routers_enabled(&self) -> bool {
        self.0.mNonCcmRoutersEnabled()
    }

    pub fn get_version_threshold_for_routing(&self) -> u8 {
        self.0.mVersionThresholdForRouting()
    }
}

#[allow(missing_docs)]
impl SecurityPolicy {
    pub fn set_rotation_time_in_hours(&mut self, hours: u16) {
        self.0.mRotationTime = hours
    }

    pub fn set_obtain_network_key_enabled(&mut self, x: bool) {
        self.0.set_mObtainNetworkKeyEnabled(x)
    }

    pub fn set_native_commissioning_enabled(&mut self, x: bool) {
        self.0.set_mNativeCommissioningEnabled(x)
    }

    pub fn set_routers_enabled(&mut self, x: bool) {
        self.0.set_mRoutersEnabled(x)
    }

    pub fn set_external_commissioning_enabled(&mut self, x: bool) {
        self.0.set_mExternalCommissioningEnabled(x)
    }

    pub fn set_beacons_enabled(&mut self, x: bool) {
        self.0.set_mBeaconsEnabled(x)
    }

    pub fn set_commercial_commissioning_enabled(&mut self, x: bool) {
        self.0.set_mCommercialCommissioningEnabled(x)
    }

    pub fn set_autonomous_enrollment_enabled(&mut self, x: bool) {
        self.0.set_mAutonomousEnrollmentEnabled(x)
    }

    pub fn set_network_key_provisioning_enabled(&mut self, x: bool) {
        self.0.set_mNetworkKeyProvisioningEnabled(x)
    }

    pub fn set_toble_link_enabled(&mut self, x: bool) {
        self.0.set_mTobleLinkEnabled(x)
    }

    pub fn set_non_ccm_routers_enabled(&mut self, x: bool) {
        self.0.set_mNonCcmRoutersEnabled(x)
    }

    pub fn set_version_threshold_for_routing(&mut self, x: u8) {
        self.0.set_mVersionThresholdForRouting(x)
    }
}
