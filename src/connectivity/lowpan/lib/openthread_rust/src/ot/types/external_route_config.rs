// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use core::fmt::{Debug, Formatter};

/// Data type representing an external route configuration.
/// Functional equivalent of [`otsys::otExternalRouteConfig`](crate::otsys::otExternalRouteConfig).
#[derive(Default, Clone, Copy)]
#[repr(transparent)]
pub struct ExternalRouteConfig(pub otExternalRouteConfig);

impl_ot_castable!(ExternalRouteConfig, otExternalRouteConfig);

impl Debug for ExternalRouteConfig {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        self.prefix().fmt(f)?;
        if self.is_stable() {
            write!(f, " STABLE")?;
        }
        if self.is_next_hop_this_device() {
            write!(f, " NEXT_HOP_IS_THIS_DEVICE")?;
        }
        Ok(())
    }
}

impl std::fmt::Display for ExternalRouteConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

impl ExternalRouteConfig {
    /// Creates a default, stable `ExternalRouteConfig` from the given `Ip6Prefix`.
    pub fn from_prefix<T: Into<otIp6Prefix>>(prefix: T) -> ExternalRouteConfig {
        let mut ret = ExternalRouteConfig(otExternalRouteConfig {
            mPrefix: prefix.into(),
            mRloc16: 0,
            ..otExternalRouteConfig::default()
        });
        ret.set_stable(true);
        ret
    }

    /// Returns the `Ip6Prefix` for this external route configuration.
    pub fn prefix(&self) -> &Ip6Prefix {
        (&self.0.mPrefix).into()
    }

    /// Returns the RLOC16 for the router that owns this external route configuration.
    pub fn rloc16(&self) -> u16 {
        self.0.mRloc16
    }

    /// Returns the route preference.
    pub fn route_preference(&self) -> RoutePreference {
        RoutePreference::from_i32(self.0.mPreference()).expect("Invalid route preference")
    }

    /// Sets the route preference.
    pub fn set_route_preference(&mut self, pref: RoutePreference) {
        self.0.set_mPreference(pref as i32);
    }

    /// Returns the value of the `stable` flag.
    pub fn is_stable(&self) -> bool {
        self.0.mStable()
    }

    /// Sets the value of the `stable` flag.
    pub fn set_stable(&mut self, x: bool) {
        self.0.set_mStable(x)
    }

    /// Returns true if the next hop for this route is this device.
    pub fn is_next_hop_this_device(&self) -> bool {
        self.0.mNextHopIsThisDevice()
    }
}
