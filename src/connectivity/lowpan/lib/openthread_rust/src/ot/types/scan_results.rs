// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// This structure holds information about a network beacon discovered during an active scan.
///
/// Functional equivalent of [`otsys::otActiveScanResult`](crate::otsys::otActiveScanResult).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct ActiveScanResult(pub otActiveScanResult);

impl_ot_castable!(ActiveScanResult, otActiveScanResult);

impl ActiveScanResult {
    /// Channel index.
    pub fn channel(&self) -> ChannelIndex {
        self.0.mChannel
    }

    /// RSSI of this beacon.
    pub fn rssi(&self) -> Decibels {
        self.0.mRssi
    }

    /// IEEE 802.15.4 Extended Address for this beacon.
    pub fn ext_address(&self) -> ExtAddress {
        self.0.mExtAddress.into()
    }

    /// Extended PAN-ID for this beacon.
    pub fn extended_pan_id(&self) -> ExtendedPanId {
        self.0.mExtendedPanId.into()
    }

    /// Returns true if this beacon is joinable.
    pub fn is_joinable(&self) -> bool {
        self.0.mIsJoinable()
    }

    /// Returns true if this beacon is "native".
    pub fn is_native(&self) -> bool {
        self.0.mIsNative()
    }

    /// UDP Joiner port
    pub fn joiner_udp_port(&self) -> u16 {
        self.0.mJoinerUdpPort
    }

    /// LQI of this beacon.
    pub fn lqi(&self) -> u8 {
        self.0.mLqi
    }

    /// Network Name from this beacon.
    pub fn network_name(&self) -> &NetworkName {
        NetworkName::ref_from_ot_ref(&self.0.mNetworkName)
    }

    /// PAN-ID from this beacon.
    pub fn pan_id(&self) -> PanId {
        self.0.mPanId
    }

    /// Version field from this beacon.
    pub fn version(&self) -> u32 {
        self.0.mVersion().try_into().unwrap()
    }

    /// Steering Data.
    pub fn steering_data(&self) -> &[u8] {
        &self.0.mSteeringData.m8[0..self.0.mSteeringData.mLength as usize]
    }
}

/// This structure holds information about energy levels detected on a channel.
///
/// Functional equivalent of [`otsys::otEnergyScanResult`](crate::otsys::otEnergyScanResult).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct EnergyScanResult(pub otEnergyScanResult);

impl_ot_castable!(EnergyScanResult, otEnergyScanResult);

impl EnergyScanResult {
    /// Channel index.
    pub fn channel(&self) -> ChannelIndex {
        self.0.mChannel
    }

    /// Max RSSI
    pub fn max_rssi(&self) -> Decibels {
        self.0.mMaxRssi
    }
}
