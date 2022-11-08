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
    transport::tcp::seqnum::{SeqNum, WindowSize},
    Instant,
};

// Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#section-3.2):
///   The fast retransmit algorithm uses the arrival of 3 duplicate ACKs (...)
///   as an indication that a segment has been lost.
const DUP_ACK_THRESHOLD: u8 = 3;

/// Congestion control with four intertwined algorithms.
///
/// - Slow start
/// - Congestion avoidance from a loss-based algorithm
/// - Fast retransmit
/// - Fast recovery: https://datatracker.ietf.org/doc/html/rfc5681#section-3
#[derive(Debug)]
pub(super) struct CongestionControl<I: Instant> {
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
    fn on_loss_detected(&mut self) {
        match self {
            LossBasedAlgorithm::Cubic(cubic) => cubic.on_loss_detected(),
        }
    }

    /// Called when we recovered from packet loss when receiving an ACK that
    /// acknowledges new data.
    fn on_loss_recovered(&mut self) {
        // Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#section-3.2):
        //   When the next ACK arrives that acknowledges previously
        //   unacknowledged data, a TCP MUST set cwnd to ssthresh (the value
        //   set in step 2).  This is termed "deflating" the window.
        match self {
            LossBasedAlgorithm::Cubic(cubic) => cubic.on_loss_recovered(),
        }
    }

    fn on_ack(&mut self, bytes_acked: NonZeroU32, now: I, rtt: Duration) {
        match self {
            LossBasedAlgorithm::Cubic(cubic) => cubic.on_ack(bytes_acked, now, rtt),
        }
    }

    fn on_retransmission_timeout(&mut self) {
        match self {
            LossBasedAlgorithm::Cubic(cubic) => cubic.on_retransmission_timeout(),
        }
    }
}

impl<I: Instant> CongestionControl<I> {
    /// Called when there are previously unacknowledged bytes being acked.
    pub(super) fn on_ack(&mut self, bytes_acked: NonZeroU32, now: I, rtt: Duration) {
        let Self { algorithm, fast_recovery } = self;
        // Exit fast recovery since there is an ACK that acknowledges new data.
        if let Some(_fast_recovery) = fast_recovery.take() {
            algorithm.on_loss_recovered();
        };
        algorithm.on_ack(bytes_acked, now, rtt);
    }

    /// Called when a duplicate ack is arrived.
    pub(super) fn on_dup_ack(&mut self, seg_ack: SeqNum) {
        match &mut self.fast_recovery {
            None => self.fast_recovery = Some(FastRecovery::new()),
            Some(fast_recovery) => fast_recovery.on_dup_ack(&mut self.algorithm, seg_ack),
        }
    }

    /// Called upon a retransmission timeout.
    pub(super) fn on_retransmission_timeout(&mut self) {
        let Self { algorithm, fast_recovery } = self;
        *fast_recovery = None;
        algorithm.on_retransmission_timeout();
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
        let Self { algorithm, fast_recovery } = self;
        let (cwnd, mss) = match algorithm {
            LossBasedAlgorithm::Cubic(cubic) => (cubic.cwnd(), cubic.mss),
        };
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
                return cwnd.saturating_add(u32::from(fast_recovery.dup_acks.get()) * mss);
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
        Self { algorithm: LossBasedAlgorithm::Cubic(Default::default()), fast_recovery: None }
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

    fn on_dup_ack<I: Instant>(&mut self, loss_based: &mut LossBasedAlgorithm<I>, seg_ack: SeqNum) {
        self.dup_acks = self.dup_acks.saturating_add(1);

        match self.dup_acks.get().cmp(&DUP_ACK_THRESHOLD) {
            Ordering::Less => {}
            Ordering::Equal => {
                loss_based.on_loss_detected();
                // Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#section-3.2):
                //   The lost segment starting at SND.UNA MUST be retransmitted
                //   and cwnd set to ssthresh plus 3*SMSS.  This artificially
                //   "inflates" the congestion window by the number of segments
                //   (three) that have left the network and which the receiver
                //   has buffered.
                self.fast_retransmit = Some(seg_ack);
                let (cwnd, ssthresh, mss) = match loss_based {
                    LossBasedAlgorithm::Cubic(cubic) => {
                        (&mut cubic.cwnd, cubic.ssthresh, cubic.mss)
                    }
                };
                *cwnd = ssthresh + u32::from(DUP_ACK_THRESHOLD) * mss;
            }
            Ordering::Greater => {
                // Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#section-3.2):
                //   For each additional duplicate ACK received (after the third),
                //   cwnd MUST be incremented by SMSS. This artificially inflates
                //   the congestion window in order to reflect the additional
                //   segment that has left the network.
                let (cwnd, mss) = match loss_based {
                    LossBasedAlgorithm::Cubic(cubic) => (&mut cubic.cwnd, cubic.mss),
                };
                *cwnd += mss;
            }
        }
    }
}
