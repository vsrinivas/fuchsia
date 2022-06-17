// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// This structure represents the IP layer counters.
///
/// Functional equivalent of [`otsys::otIpCounters`](crate::otsys::otIpCounters).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct IpCounters(pub otIpCounters);

impl_ot_castable!(IpCounters, otIpCounters);

impl IpCounters {
    /// The number of IPv6 packets successfully transmitted
    pub fn tx_success(&self) -> u32 {
        self.0.mTxSuccess
    }

    /// The number of IPv6 packets successfully received.
    pub fn rx_success(&self) -> u32 {
        self.0.mRxSuccess
    }

    /// The number of IPv6 packets failed to transmit.
    pub fn tx_failure(&self) -> u32 {
        self.0.mTxFailure
    }

    /// The number of IPv6 packets failed to receive.
    pub fn rx_failure(&self) -> u32 {
        self.0.mRxFailure
    }
}
