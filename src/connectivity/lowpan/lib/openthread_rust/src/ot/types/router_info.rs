// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Functional equivalent of [`otsys::otRouterInfo`](crate::otsys::otRouterInfo).
#[derive(Default, Clone)]
#[repr(transparent)]
pub struct RouterInfo(pub otRouterInfo);

impl_ot_castable!(RouterInfo, otRouterInfo);

impl RouterInfo {
    /// Returns an empty router info.
    pub fn empty() -> RouterInfo {
        Self::default()
    }
}

impl std::fmt::Debug for RouterInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut ds = f.debug_struct("RouterInfo");

        ds.field("ext_address", &self.get_ext_address());
        ds.field("rloc16", &self.get_rloc16());
        ds.field("router_id", &self.get_router_id());
        ds.field("next_hop", &self.get_next_hop());
        ds.field("path_cost", &self.get_path_cost());
        ds.field("link_quality_in", &self.get_link_quality_in());
        ds.field("link_quality_out", &self.get_link_quality_out());
        ds.field("age", &self.get_age());
        ds.field("allocated", &self.get_allocated());
        ds.field("link_established", &self.get_link_established());

        ds.finish()
    }
}

impl RouterInfo {
    /// Returns the IEEE 802.15.4 extended address.
    pub fn get_ext_address(&self) -> ExtAddress {
        self.0.mExtAddress.into()
    }

    /// Returns the RLOC16.
    pub fn get_rloc16(&self) -> u16 {
        self.0.mRloc16
    }

    /// Returns the router id.
    pub fn get_router_id(&self) -> u8 {
        self.0.mRouterId
    }

    /// Returns the next hop to router.
    pub fn get_next_hop(&self) -> u8 {
        self.0.mNextHop
    }

    /// Returns the path cost to router.
    pub fn get_path_cost(&self) -> u8 {
        self.0.mPathCost
    }

    /// Returns the link quality in.
    pub fn get_link_quality_in(&self) -> u8 {
        self.0.mLinkQualityIn
    }

    /// Returns the link quality out.
    pub fn get_link_quality_out(&self) -> u8 {
        self.0.mLinkQualityOut
    }

    /// Returns the age since the time last heard.
    pub fn get_age(&self) -> u8 {
        self.0.mAge
    }

    /// Returns whether the router is allocated.
    pub fn get_allocated(&self) -> bool {
        self.0.mAllocated()
    }

    /// Returns whether the link is established.
    pub fn get_link_established(&self) -> bool {
        self.0.mLinkEstablished()
    }
}
