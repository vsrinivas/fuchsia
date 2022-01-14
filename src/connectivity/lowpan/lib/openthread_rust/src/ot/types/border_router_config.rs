// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use core::fmt::{Debug, Formatter};

/// Functional equivalent of [`otsys::otBorderRouterConfig`](crate::otsys::otBorderRouterConfig).
#[derive(Default, Clone, Copy)]
#[repr(transparent)]
pub struct BorderRouterConfig(pub otBorderRouterConfig);

impl_ot_castable!(BorderRouterConfig, otBorderRouterConfig);

impl Debug for BorderRouterConfig {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        self.prefix().fmt(f)?;
        match self.default_route_preference() {
            Some(RoutePreference::Low) => write!(f, " DEFAULT_LOW")?,
            Some(RoutePreference::Medium) => write!(f, " DEFAULT")?,
            Some(RoutePreference::High) => write!(f, " DEFAULT_HIGH")?,
            None => (),
        }

        if self.is_on_mesh() {
            write!(f, " ON_MESH")?;
        }

        if self.is_slaac() {
            write!(f, " SLAAC")?;
        }

        if self.is_preferred() {
            write!(f, " PREFERRED")?;
        }

        if self.is_dhcp() {
            write!(f, " DHCP")?;
        }

        if self.is_nd_dns() {
            write!(f, " ND_DNS")?;
        }

        if self.is_stable() {
            write!(f, " STABLE")?;
        }

        if self.is_domain_prefix() {
            write!(f, " DOMAIN_PREFIX")?;
        }

        // TODO(rquattle): Needs OpenThread update
        // if self.is_valid() {
        //     write!(f, " VALID")?;
        // }

        Ok(())
    }
}

impl std::fmt::Display for BorderRouterConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        std::fmt::Debug::fmt(self, f)
    }
}

impl BorderRouterConfig {
    /// Creates a new default `BorderRouterConfig` (with no flags set)
    /// from the given [`Ip6Prefix`].
    pub fn from_prefix(prefix: Ip6Prefix) -> BorderRouterConfig {
        BorderRouterConfig(otBorderRouterConfig {
            mPrefix: prefix.into(),
            mRloc16: 0,
            ..otBorderRouterConfig::default()
        })
    }

    /// Creates a new SLAAC `BorderRouterConfig` from the given [`Ip6Prefix`].
    pub fn slaac_from_prefix(prefix: Ip6Prefix) -> BorderRouterConfig {
        let mut ret = Self::from_prefix(prefix);

        // These are reasonable default flags for a SLAAC prefix.
        ret.set_stable(true);
        ret.set_on_mesh(true);
        ret.set_slaac(true);
        ret.set_preferred(true);
        // TODO(rquattle): Needs OpenThread update
        //ret.set_valid(true);
        ret
    }

    /// Returns a reference to the [`Ip6Prefix`] for this `BorderRouterConfig`.
    pub fn prefix(&self) -> &Ip6Prefix {
        (&self.0.mPrefix).into()
    }

    /// Returns the RLOC16 for the parent router that owns this `BorderRouterConfig`.
    pub fn rloc16(&self) -> u16 {
        self.0.mRloc16
    }

    /// Returns the value of the `stable` flag.
    pub fn is_stable(&self) -> bool {
        self.0.mStable()
    }

    /// Sets the value of the `stable` flag.
    pub fn set_stable(&mut self, x: bool) {
        self.0.set_mStable(x)
    }

    /// Returns the value of the `dhcp` flag.
    /// If this flag is set, addresses are managed and assigned by a DHCP server.
    pub fn is_dhcp(&self) -> bool {
        self.0.mDhcp()
    }

    /// Sets the value of the `dhcp` flag.
    pub fn set_dhcp(&mut self, x: bool) {
        self.0.set_mDhcp(x)
    }

    /// Returns the value of the `nd_dns` flag.
    pub fn is_nd_dns(&self) -> bool {
        self.0.mNdDns()
    }

    /// Sets the value of the `nd_dns` flag.
    pub fn set_nd_dns(&mut self, x: bool) {
        self.0.set_mNdDns(x)
    }

    /// Returns the routing preference for this `BorderRouterConfig`.
    /// If this config is not a default route, returns `None`.
    pub fn default_route_preference(&self) -> Option<RoutePreference> {
        if self.0.mDefaultRoute() {
            Some(RoutePreference::from_i32(self.0.mPreference()).expect("Invalid route preference"))
        } else {
            None
        }
    }

    /// If a value is provided, sets the default route flag and default route preference.
    /// Otherwise the default route flag is cleared.
    pub fn set_default_route_preference(&mut self, pref: Option<RoutePreference>) {
        if let Some(pref) = pref {
            self.0.set_mDefaultRoute(true);
            self.0.set_mPreference(pref as i32);
        } else {
            self.0.set_mDefaultRoute(false);
        }
    }

    /// Returns the value of the `slaac` flag.
    /// If this flag is set, addresses self-assigned using SLAAC.
    pub fn is_slaac(&self) -> bool {
        self.0.mSlaac()
    }

    /// Sets the value of the `slaac` flag.
    pub fn set_slaac(&mut self, x: bool) {
        self.0.set_mSlaac(x)
    }

    /// Returns the value of the `preferred` flag.
    pub fn is_preferred(&self) -> bool {
        self.0.mPreferred()
    }

    /// Sets the value of the `preferred` flag.
    pub fn set_preferred(&mut self, x: bool) {
        self.0.set_mPreferred(x)
    }

    /// Returns the value of the `on_mesh` flag.
    pub fn is_on_mesh(&self) -> bool {
        self.0.mOnMesh()
    }

    /// Sets the value of the `on_mesh` flag.
    pub fn set_on_mesh(&mut self, x: bool) {
        self.0.set_mOnMesh(x)
    }

    /// Returns the value of the `dp` (domain prefix) flag.
    pub fn is_domain_prefix(&self) -> bool {
        self.0.mDp()
    }

    /// Sets the value of the `dp` (domain prefix) flag.
    pub fn set_domain_prefix(&mut self, x: bool) {
        self.0.set_mDp(x)
    }

    // TODO(rquattle): Needs OpenThread update
    // pub fn is_valid(&self) -> bool {
    //     self.0.mValid()
    // }
    // pub fn set_valid(&mut self, x: bool) {
    //     self.0.set_mValid(x)
    // }
}
