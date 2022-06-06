// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// This structure represents the MAC layer counters.
///
/// Functional equivalent of [`otsys::otMacCounters`](crate::otsys::otMacCounters).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct MacCounters(pub otMacCounters);

impl_ot_castable!(MacCounters, otMacCounters);

impl MacCounters {
    /// The total number of unique MAC frame transmission requests.
    pub fn tx_total(&self) -> u32 {
        self.0.mTxTotal
    }

    /// The total number of unique unicast MAC frame transmission requests.
    pub fn tx_unicast(&self) -> u32 {
        self.0.mTxUnicast
    }

    /// The total number of unique broadcast MAC frame transmission requests.
    pub fn tx_broadcast(&self) -> u32 {
        self.0.mTxBroadcast
    }

    /// The total number of unique MAC frame transmission requests with requested acknowledgment.
    pub fn tx_ack_requested(&self) -> u32 {
        self.0.mTxAckRequested
    }

    /// The total number of unique MAC frame transmission requests that were acked.
    pub fn tx_acked(&self) -> u32 {
        self.0.mTxAcked
    }

    /// The total number of unique MAC frame transmission requests without requested acknowledgment.
    pub fn tx_no_ack_requested(&self) -> u32 {
        self.0.mTxNoAckRequested
    }

    /// The total number of unique MAC Data frame transmission requests.
    pub fn tx_data(&self) -> u32 {
        self.0.mTxData
    }

    /// The total number of unique MAC Data Poll frame transmission requests.
    pub fn tx_data_poll(&self) -> u32 {
        self.0.mTxDataPoll
    }

    /// The total number of unique MAC Beacon frame transmission requests.
    pub fn tx_beacon(&self) -> u32 {
        self.0.mTxBeacon
    }

    /// The total number of unique MAC Beacon Request frame transmission requests.
    pub fn tx_beacon_request(&self) -> u32 {
        self.0.mTxBeaconRequest
    }

    /// The total number of unique other MAC frame transmission requests.
    pub fn tx_other(&self) -> u32 {
        self.0.mTxOther
    }

    /// The total number of MAC retransmission attempts.
    pub fn tx_retry(&self) -> u32 {
        self.0.mTxRetry
    }

    /// The total number of unique MAC transmission packets that meet maximal retry limit for direct packets.
    pub fn tx_direct_max_retry_expiry(&self) -> u32 {
        self.0.mTxDirectMaxRetryExpiry
    }

    /// The total number of unique MAC transmission packets that meet maximal retry limit for indirect packets.
    pub fn tx_indirect_max_retry_expiry(&self) -> u32 {
        self.0.mTxIndirectMaxRetryExpiry
    }

    /// The total number of CCA failures.
    pub fn tx_err_cca(&self) -> u32 {
        self.0.mTxErrCca
    }

    /// The total number of unique MAC transmission request failures cause by an abort error.
    pub fn tx_err_abort(&self) -> u32 {
        self.0.mTxErrAbort
    }

    /// The total number of unique MAC transmission requests failures caused by a busy channel (a CSMA/CA fail).
    pub fn tx_err_busy_channel(&self) -> u32 {
        self.0.mTxErrBusyChannel
    }

    /// The total number of received frames.
    pub fn rx_total(&self) -> u32 {
        self.0.mRxTotal
    }

    /// The total number of unicast frames received.
    pub fn rx_unicast(&self) -> u32 {
        self.0.mRxUnicast
    }

    /// The total number of broadcast frames received.
    pub fn rx_broadcast(&self) -> u32 {
        self.0.mRxBroadcast
    }

    /// The total number of MAC Data frames received.
    pub fn rx_data(&self) -> u32 {
        self.0.mRxData
    }

    /// The total number of MAC Data Poll frames received.
    pub fn rx_data_poll(&self) -> u32 {
        self.0.mRxDataPoll
    }

    /// The total number of MAC Beacon frames received.
    pub fn rx_beacon(&self) -> u32 {
        self.0.mRxBeacon
    }

    /// The total number of MAC Beacon Request frames received.
    pub fn rx_beacon_request(&self) -> u32 {
        self.0.mRxBeaconRequest
    }

    /// The total number of other types of frames received.
    pub fn rx_other(&self) -> u32 {
        self.0.mRxOther
    }

    /// The total number of frames dropped by MAC Filter module.
    pub fn rx_address_filtered(&self) -> u32 {
        self.0.mRxAddressFiltered
    }

    /// The total number of frames dropped by destination address check.
    pub fn rx_dest_addr_filtered(&self) -> u32 {
        self.0.mRxDestAddrFiltered
    }

    /// The total number of frames dropped due to duplication.
    pub fn rx_duplicated(&self) -> u32 {
        self.0.mRxDuplicated
    }

    /// The total number of frames dropped because of missing or malformed content.
    pub fn rx_err_no_frame(&self) -> u32 {
        self.0.mRxErrNoFrame
    }

    /// The total number of frames dropped due to unknown neighbor.
    pub fn rx_err_unknown_neighbor(&self) -> u32 {
        self.0.mRxErrUnknownNeighbor
    }

    /// The total number of frames dropped due to invalid source address.
    pub fn rx_err_invalid_src_addr(&self) -> u32 {
        self.0.mRxErrInvalidSrcAddr
    }

    /// The total number of frames dropped due to security error.
    pub fn rx_err_sec(&self) -> u32 {
        self.0.mRxErrSec
    }

    /// The total number of frames dropped due to invalid FCS.
    pub fn rx_err_fcs(&self) -> u32 {
        self.0.mRxErrFcs
    }

    /// The total number of frames dropped due to other error.
    pub fn rx_err_other(&self) -> u32 {
        self.0.mRxErrOther
    }
}
