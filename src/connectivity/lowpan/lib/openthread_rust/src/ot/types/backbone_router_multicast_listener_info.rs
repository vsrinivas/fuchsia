// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Functional equivalent of [`otsys::otBackboneRouterMUlticastListenerInfo`](crate::otsys::otBackboneRouterMUlticastListenerInfo).
#[derive(Default, Clone)]
#[repr(transparent)]
pub struct BackboneRouterMulticastListenerInfo(pub otBackboneRouterMulticastListenerInfo);

impl_ot_castable!(BackboneRouterMulticastListenerInfo, otBackboneRouterMulticastListenerInfo);

impl BackboneRouterMulticastListenerInfo {
    /// Returns an empty router info.
    pub fn empty() -> BackboneRouterMulticastListenerInfo {
        Self::default()
    }
}

impl std::fmt::Debug for BackboneRouterMulticastListenerInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut ds = f.debug_struct("BackboneRouterMulticastListenerInfo");

        ds.field("address", &self.get_address());
        ds.field("timeout", &self.get_timeout_sec());

        ds.finish()
    }
}

impl BackboneRouterMulticastListenerInfo {
    /// Returns the IPv6 address
    pub fn get_address(&self) -> ot::Ip6Address {
        ot::Ip6Address::from_ot(self.0.mAddress)
    }

    /// Sets the IPv6 multicast address:
    pub fn set_address(&mut self, addr: ot::Ip6Address) {
        self.0.mAddress = addr.into_ot();
    }

    /// Returns the Timeout.
    pub fn get_timeout_sec(&self) -> u32 {
        self.0.mTimeout
    }

    /// Sets the Timeout.
    pub fn set_timeout_sec(&mut self, timeout: u32) {
        self.0.mTimeout = timeout;
    }
}
