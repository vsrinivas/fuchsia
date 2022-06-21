// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// This structure represents radio coexistence metrics.
///
/// Functional equivalent of [`otsys::otRadioCoexMetrics`](crate::otsys::otRadioCoexMetrics).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct RadioCoexMetrics(pub otRadioCoexMetrics);

impl_ot_castable!(RadioCoexMetrics, otRadioCoexMetrics);

impl RadioCoexMetrics {
    /// Number of grant glitches.
    pub fn num_grant_glitch(&self) -> u32 {
        self.0.mNumGrantGlitch
    }

    ///  Number of tx requests.
    pub fn num_tx_request(&self) -> u32 {
        self.0.mNumTxRequest
    }

    /// Number of tx requests while grant was active.
    pub fn num_tx_grant_immediate(&self) -> u32 {
        self.0.mNumTxGrantImmediate
    }

    /// Number of tx requests while grant was inactive.
    pub fn num_tx_grant_wait(&self) -> u32 {
        self.0.mNumTxGrantWait
    }

    /// Number of tx requests while grant was inactive that were ultimately granted.
    pub fn num_tx_grant_wait_activated(&self) -> u32 {
        self.0.mNumTxGrantWaitActivated
    }

    /// Number of tx requests while grant was inactive that timed out.
    pub fn num_tx_grant_wait_timeout(&self) -> u32 {
        self.0.mNumTxGrantWaitTimeout
    }

    /// Number of tx that were in progress when grant was deactivated.
    pub fn num_tx_grant_deactivated_during_request(&self) -> u32 {
        self.0.mNumTxGrantDeactivatedDuringRequest
    }

    /// Number of tx requests that were not granted within 50us.
    pub fn num_tx_delayed_grant(&self) -> u32 {
        self.0.mNumTxDelayedGrant
    }

    /// Average time in usec from tx request to grant.
    pub fn avg_tx_request_to_grant_time(&self) -> u32 {
        self.0.mAvgTxRequestToGrantTime
    }

    /// Number of rx requests.
    pub fn num_rx_request(&self) -> u32 {
        self.0.mNumRxRequest
    }

    /// Number of rx requests while grant was active.
    pub fn num_rx_grant_immediate(&self) -> u32 {
        self.0.mNumRxGrantImmediate
    }

    /// Number of rx requests while grant was inactive.
    pub fn num_rx_grant_wait(&self) -> u32 {
        self.0.mNumRxGrantWait
    }

    /// Number of rx requests while grant was inactive that were ultimately granted.
    pub fn num_rx_grant_wait_activated(&self) -> u32 {
        self.0.mNumRxGrantWaitActivated
    }

    /// Number of rx requests while grant was inactive that timed out.
    pub fn num_rx_grant_wait_timeout(&self) -> u32 {
        self.0.mNumRxGrantWaitTimeout
    }

    /// Number of rx that were in progress when grant was deactivated.
    pub fn num_rx_grant_deactivated_during_request(&self) -> u32 {
        self.0.mNumRxGrantDeactivatedDuringRequest
    }

    /// Number of rx requests that were not granted within 50us.
    pub fn num_rx_delayed_grant(&self) -> u32 {
        self.0.mNumRxDelayedGrant
    }

    /// Average time in usec from rx request to grant.
    pub fn avg_rx_request_to_grant_time(&self) -> u32 {
        self.0.mAvgRxRequestToGrantTime
    }

    /// Number of rx requests that completed without receiving grant.
    pub fn num_rx_grant_none(&self) -> u32 {
        self.0.mNumRxGrantNone
    }

    /// Stats collection stopped due to saturation.
    pub fn stopped(&self) -> bool {
        self.0.mStopped
    }
}
