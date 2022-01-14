// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// This structure holds diagnostic information for a neighboring Thread node.
///
/// Functional equivalent of [`otsys::otNeighborInfo`](crate::otsys::otNeighborInfo).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct NeighborInfo(pub otNeighborInfo);

impl_ot_castable!(NeighborInfo, otNeighborInfo);

impl NeighborInfo {
    /// Last time heard from this neighbor, in seconds.
    pub fn age(&self) -> u32 {
        self.0.mAge
    }

    /// Average RSSI of this neighbor.
    pub fn average_rssi(&self) -> Decibels {
        self.0.mAverageRssi
    }

    /// Last RSSI of this neighbor.
    pub fn last_rssi(&self) -> Decibels {
        self.0.mLastRssi
    }

    /// IEEE 802.15.4 Extended Address for this neighbor.
    pub fn ext_address(&self) -> ExtAddress {
        self.0.mExtAddress.into()
    }

    /// Returns true if this neighbor needs full network data.
    pub fn needs_full_network_data(&self) -> bool {
        self.0.mFullNetworkData()
    }

    /// Returns true if this neighbor is a full thread device.
    pub fn is_full_thread_device(&self) -> bool {
        self.0.mFullThreadDevice()
    }

    /// Returns true if this neighbor is a child of this device.
    pub fn is_child(&self) -> bool {
        self.0.mIsChild()
    }

    /// Link Frame Counter for this neighbor.
    pub fn link_frame_counter(&self) -> u32 {
        self.0.mLinkFrameCounter
    }

    /// MLE Frame Counter for this neighbor.
    pub fn mle_frame_counter(&self) -> u32 {
        self.0.mMleFrameCounter
    }

    /// Inbound Link-Quality Indicator value for this neighbor.
    pub fn lqi_in(&self) -> u8 {
        self.0.mLinkQualityIn
    }

    /// Returns true when this neighbor has RX on when idle.
    pub fn has_rx_on_when_idle(&self) -> bool {
        self.0.mRxOnWhenIdle()
    }

    /// RLOC16 for this neighbor.
    pub fn rloc16(&self) -> ShortAddress {
        self.0.mRloc16
    }
}
