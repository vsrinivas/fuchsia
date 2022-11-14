// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements loss-based congestion control algorithms.
//!
//! The currently implemented algorithms are CUBIC from [RFC 8312] and RENO
//! style fast retransmit and fast recovery from [RFC 5681].
//!
//! [RFC 8312]: https://www.rfc-editor.org/rfc/rfc8312
//! [RFC 5681]: https://www.rfc-editor.org/rfc/rfc5681

mod cubic;

use core::{
    cmp::Ordering,
    num::{NonZeroU32, NonZeroU8},
    time::Duration,
};

use crate::{
    transport::tcp::{
        seqnum::{SeqNum, WindowSize},
        DEFAULT_MAXIMUM_SEGMENT_SIZE,
    },
    Instant,
};

// Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#section-3.2):
///   The fast retransmit algorithm uses the arrival of 3 duplicate ACKs (...)
///   as an indication that a segment has been lost.
const DUP_ACK_THRESHOLD: u8 = 3;

/// Holds the parameters of congestion control that are common to algorithms.
#[derive(Debug)]
struct CongestionControlParams {
    /// Slow start threshold.
    ssthresh: u32,
    /// Congestion control window size, in bytes.
    cwnd: u32,
    /// Sender MSS.
    mss: u32,
}

impl CongestionControlParams {
    fn with_mss(mss: u32) -> Self {
        // Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#page-5):
        //   IW, the initial value of cwnd, MUST be set using the following
        //   guidelines as an upper bound.
        //   If SMSS > 2190 bytes:
        //       IW = 2 * SMSS bytes and MUST NOT be more than 2 segments
        //   If (SMSS > 1095 bytes) and (SMSS <= 2190 bytes):
        //       IW = 3 * SMSS bytes and MUST NOT be more than 3 segments
        //   if SMSS <= 1095 bytes:
        //       IW = 4 * SMSS bytes and MUST NOT be more than 4 segments
        let cwnd = if mss > 2190 {
            mss * 2
        } else if mss > 1095 {
            mss * 3
        } else {
            mss * 4
        };
        Self { cwnd, ssthresh: u32::MAX, mss }
    }

    fn rounded_cwnd(&self) -> WindowSize {
        WindowSize::from_u32(self.cwnd / self.mss * self.mss).unwrap_or(WindowSize::MAX)
    }
}

impl Default for CongestionControlParams {
    fn default() -> Self {
        Self::with_mss(DEFAULT_MAXIMUM_SEGMENT_SIZE)
    }
}

/// Congestion control with four intertwined algorithms.
///
/// - Slow start
/// - Congestion avoidance from a loss-based algorithm
/// - Fast retransmit
/// - Fast recovery: https://datatracker.ietf.org/doc/html/rfc5681#section-3
#[derive(Debug)]
pub(super) struct CongestionControl<I: Instant> {
    params: CongestionControlParams,
    algorithm: LossBasedAlgorithm<I>,
    /// The connection is in fast recovery when this field is a [`Some`].
    fast_recovery: Option<FastRecovery>,
}

/// Available congestion control algorithms.
#[derive(Debug)]
enum LossBasedAlgorithm<I: Instant> {
    Cubic(cubic::Cubic<I>),
}

impl<I: Instant> LossBasedAlgorithm<I> {
    /// Called when there is a loss detected.
    ///
    /// Specifically, packet loss means
    /// - either when the retransmission timer fired;
    /// - or when we have received a certain amount of duplicate acks.
    fn on_loss_detected(&mut self, params: &mut CongestionControlParams) {
        match self {
            LossBasedAlgorithm::Cubic(cubic) => cubic.on_loss_detected(params),
        }
    }

    /// Called when we recovered from packet loss when receiving an ACK that
    /// acknowledges new data.
    fn on_loss_recovered(&mut self, params: &mut CongestionControlParams) {
        // Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#section-3.2):
        //   When the next ACK arrives that acknowledges previously
        //   unacknowledged data, a TCP MUST set cwnd to ssthresh (the value
        //   set in step 2).  This is termed "deflating" the window.
        params.cwnd = params.ssthresh;
    }

    fn on_ack(
        &mut self,
        params: &mut CongestionControlParams,
        bytes_acked: NonZeroU32,
        now: I,
        rtt: Duration,
    ) {
        match self {
            LossBasedAlgorithm::Cubic(cubic) => cubic.on_ack(params, bytes_acked, now, rtt),
        }
    }

    fn on_retransmission_timeout(&mut self, params: &mut CongestionControlParams) {
        match self {
            LossBasedAlgorithm::Cubic(cubic) => cubic.on_retransmission_timeout(params),
        }
    }
}

impl<I: Instant> CongestionControl<I> {
    /// Called when there are previously unacknowledged bytes being acked.
    pub(super) fn on_ack(&mut self, bytes_acked: NonZeroU32, now: I, rtt: Duration) {
        let Self { params, algorithm, fast_recovery } = self;
        // Exit fast recovery since there is an ACK that acknowledges new data.
        if let Some(_fast_recovery) = fast_recovery.take() {
            algorithm.on_loss_recovered(params);
        };
        algorithm.on_ack(params, bytes_acked, now, rtt);
    }

    /// Called when a duplicate ack is arrived.
    pub(super) fn on_dup_ack(&mut self, seg_ack: SeqNum) {
        let Self { params, algorithm, fast_recovery } = self;
        match fast_recovery {
            None => *fast_recovery = Some(FastRecovery::new()),
            Some(fast_recovery) => fast_recovery.on_dup_ack(params, algorithm, seg_ack),
        }
    }

    /// Called upon a retransmission timeout.
    pub(super) fn on_retransmission_timeout(&mut self) {
        let Self { params, algorithm, fast_recovery } = self;
        *fast_recovery = None;
        algorithm.on_retransmission_timeout(params);
    }

    /// Gets the current congestion window size in bytes.
    ///
    /// This normally just returns whatever value the loss-based algorithm tells
    /// us, with the exception that in limited transmit case, the cwnd is
    /// inflated by dup_ack_cnt * mss, to allow unsent data packets to enter the
    /// network and trigger more duplicate ACKs to enter fast retransmit. Note
    /// that this still conforms to the RFC because we don't change the cwnd of
    /// our algorithm, the algorithm is not aware of this "inflation".
    pub(super) fn cwnd(&self) -> WindowSize {
        let Self { params, algorithm: _, fast_recovery } = self;
        let cwnd = params.rounded_cwnd();
        if let Some(fast_recovery) = fast_recovery {
            // Per RFC 3042 (https://www.rfc-editor.org/rfc/rfc3042#section-2):
            //   ... the Limited Transmit algorithm, which calls for a TCP
            //   sender to transmit new data upon the arrival of the first two
            //   consecutive duplicate ACKs ...
            //   The amount of outstanding data would remain less than or equal
            //   to the congestion window plus 2 segments.  In other words, the
            //   sender can only send two segments beyond the congestion window
            //   (cwnd).
            // Note: We don't directly change cwnd in the loss-based algorithm
            // because the RFC says one MUST NOT do that. We follow the
            // requirement here by not changing the cwnd of the algorithm - if
            // a new ACK is received after the two dup acks, the loss-based
            // algorithm will continue to operate the same way as if the 2 SMSS
            // is never added to cwnd.
            if fast_recovery.dup_acks.get() < DUP_ACK_THRESHOLD {
                return cwnd.saturating_add(u32::from(fast_recovery.dup_acks.get()) * params.mss);
            }
        }
        cwnd
    }

    /// Returns the starting sequence number of the segment that needs to be
    /// retransmitted, if any.
    pub(super) fn fast_retransmit(&mut self) -> Option<SeqNum> {
        self.fast_recovery.as_mut().and_then(|r| r.fast_retransmit.take())
    }

    pub(super) fn cubic() -> Self {
        Self {
            params: CongestionControlParams::default(),
            algorithm: LossBasedAlgorithm::Cubic(Default::default()),
            fast_recovery: None,
        }
    }

    pub(super) fn take(&mut self) -> Self {
        core::mem::replace(self, Self::cubic())
    }
}

/// Reno style Fast Recovery algorithm as described in
/// [RFC 5681](https://tools.ietf.org/html/rfc5681).
#[derive(Debug)]
pub struct FastRecovery {
    /// Holds the sequence number of the segment to fast retransmit, if any.
    fast_retransmit: Option<SeqNum>,
    /// The running count of consecutive duplicate ACKs we have received so far.
    ///
    /// Here we limit the maximum number of duplicate ACKS we track to 255, as
    /// per a note in the RFC:
    ///
    /// Note: [SCWA99] discusses a receiver-based attack whereby many
    /// bogus duplicate ACKs are sent to the data sender in order to
    /// artificially inflate cwnd and cause a higher than appropriate
    /// sending rate to be used.  A TCP MAY therefore limit the number of
    /// times cwnd is artificially inflated during loss recovery to the
    /// number of outstanding segments (or, an approximation thereof).
    ///
    /// [SCWA99]: https://homes.cs.washington.edu/~tom/pubs/CCR99.pdf
    dup_acks: NonZeroU8,
}

impl FastRecovery {
    fn new() -> Self {
        Self { dup_acks: NonZeroU8::new(1).unwrap(), fast_retransmit: None }
    }

    fn on_dup_ack<I: Instant>(
        &mut self,
        params: &mut CongestionControlParams,
        loss_based: &mut LossBasedAlgorithm<I>,
        seg_ack: SeqNum,
    ) {
        self.dup_acks = self.dup_acks.saturating_add(1);

        match self.dup_acks.get().cmp(&DUP_ACK_THRESHOLD) {
            Ordering::Less => {}
            Ordering::Equal => {
                loss_based.on_loss_detected(params);
                // Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#section-3.2):
                //   The lost segment starting at SND.UNA MUST be retransmitted
                //   and cwnd set to ssthresh plus 3*SMSS.  This artificially
                //   "inflates" the congestion window by the number of segments
                //   (three) that have left the network and which the receiver
                //   has buffered.
                self.fast_retransmit = Some(seg_ack);
                params.cwnd = params.ssthresh + u32::from(DUP_ACK_THRESHOLD) * params.mss;
            }
            Ordering::Greater => {
                // Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#section-3.2):
                //   For each additional duplicate ACK received (after the third),
                //   cwnd MUST be incremented by SMSS. This artificially inflates
                //   the congestion window in order to reflect the additional
                //   segment that has left the network.
                params.cwnd += params.mss;
            }
        }
    }
}
