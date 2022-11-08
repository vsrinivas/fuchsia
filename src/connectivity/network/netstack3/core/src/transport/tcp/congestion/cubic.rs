// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The CUBIC congestion control algorithm as described in
//! [RFC 8312](https://tools.ietf.org/html/rfc8312).
//!
//! Note: This module uses floating point arithmetics, assuming the TCP stack is
//! in user space, as it is on Fuchsia. By not restricting ourselves, it is more
//! straightforward to implement and easier to understand. We don't need to care
//! about overflows and we get better precision. However, if this algorithm ever
//! needs to be run in kernel space, especially when fp arithmentics are not
//! allowed when the kernel deems saving fp registers too expensive, we should
//! use fixed point arithmetic. Casts from u32 to f32 are always fine as f32 can
//! represent a much bigger value range than u32; On the other hand, f32 to u32
//! casts are also fine because Rust guarantees rounding towards zero (+inf is
//! converted to u32::MAX), which aligns with our intention well.
//!
//! Reference: https://doc.rust-lang.org/reference/expressions/operator-expr.html#type-cast-expressions

use core::{num::NonZeroU32, time::Duration};

use crate::{
    transport::tcp::{seqnum::WindowSize, DEFAULT_MAXIMUM_SEGMENT_SIZE},
    Instant,
};

/// Per RFC 8312 (https://tools.ietf.org/html/rfc8312#section-4.5):
///  Parameter beta_cubic SHOULD be set to 0.7.
const CUBIC_BETA: f32 = 0.7;
/// Per RFC 8312 (https://tools.ietf.org/html/rfc8312#section-5):
///  Therefore, C SHOULD be set to 0.4.
const CUBIC_C: f32 = 0.4;

/// The CUBIC algorithm state variables.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(super) struct Cubic<I: Instant> {
    /// The start of the current congestion avoidance epoch.
    epoch_start: Option<I>,
    /// Slow start threshold.
    pub(super) ssthresh: u32,
    /// Congestion control window size, in bytes.
    pub(super) cwnd: u32,
    /// Sender MSS.
    pub(super) mss: u32,
    /// Coefficient for the cubic term of time into the current congestion
    /// avoidance epoch.
    k: f32,
    /// The window size when the last congestion event occurred, in bytes.
    w_max: u32,
    /// The running count of acked bytes during congestion avoidance.
    bytes_acked: u32,
}

impl<I: Instant> Default for Cubic<I> {
    fn default() -> Self {
        Self::with_mss(DEFAULT_MAXIMUM_SEGMENT_SIZE)
    }
}

impl<I: Instant> Cubic<I> {
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
        Self { k: 0.0, w_max: 0, epoch_start: None, cwnd, ssthresh: u32::MAX, mss, bytes_acked: 0 }
    }

    /// Returns the window size governed by the cubic growth function, in bytes.
    ///
    /// This function is responsible for the concave/convex regions described
    /// in the RFC.
    fn cubic_window(&self, t: Duration) -> u32 {
        // Per RFC 8312 (https://www.rfc-editor.org/rfc/rfc8312#section-4.1):
        //   W_cubic(t) = C*(t-K)^3 + W_max (Eq. 1)
        let x = t.as_secs_f32() - self.k;
        let w_cubic = (self.cubic_c() * f32::powi(x, 3)) + self.w_max as f32;
        w_cubic as u32
    }

    /// Returns the window size for standard TCP, in bytes.
    fn standard_tcp_window(&self, t: Duration, rtt: Duration) -> u32 {
        // Per RFC 8312 (https://www.rfc-editor.org/rfc/rfc8312#section-4.2):
        //   W_est(t) = W_max*beta_cubic +
        //         [3*(1-beta_cubic)/(1+beta_cubic)] * (t/RTT) (Eq. 4)
        let round_trips = t.as_secs_f32() / rtt.as_secs_f32();
        let w_tcp = self.w_max as f32 * CUBIC_BETA
            + (3.0 * (1.0 - CUBIC_BETA) / (1.0 + CUBIC_BETA)) * round_trips * self.mss as f32;
        w_tcp as u32
    }

    pub(super) fn on_ack(&mut self, mut bytes_acked: NonZeroU32, now: I, rtt: Duration) {
        if self.cwnd < self.ssthresh {
            // Slow start, Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#page-6):
            // we RECOMMEND that TCP implementations increase cwnd, per:
            //   cwnd += min (N, SMSS)                      (2)
            self.cwnd += u32::min(bytes_acked.get(), self.mss);
            // Now that we are moving out of slow start, we need to treat the
            // extra bytes differently, set the cwnd back to ssthresh and then
            // backtrack the portion of bytes that should be processed in
            // congestion avoidance.
            match self.cwnd.checked_sub(self.ssthresh).and_then(NonZeroU32::new) {
                None => return,
                Some(diff) => bytes_acked = diff,
            }
            self.cwnd = self.ssthresh;
        }

        // Congestion avoidance.
        let epoch_start = match self.epoch_start {
            Some(epoch_start) => epoch_start,
            None => {
                // Setup the parameters for the current congestion avoidance epoch.
                if let Some(w_max_diff_cwnd) = self.w_max.checked_sub(self.cwnd) {
                    // K is the time period that the above function takes to
                    // increase the current window size to W_max if there are no
                    // further congestion events and is calculated using the
                    // following equation:
                    //   K = cubic_root(W_max*(1-beta_cubic)/C) (Eq. 2)
                    self.k = (w_max_diff_cwnd as f32 / self.cubic_c()).cbrt();
                } else {
                    // Per RFC 8312 (https://www.rfc-editor.org/rfc/rfc8312#section-4.8):
                    //   In the case when CUBIC runs the hybrid slow start [HR08],
                    //   it may exit the first slow start without incurring any
                    //   packet loss and thus W_max is undefined. In this special
                    //   case, CUBIC switches to congestion avoidance and increases
                    //   its congestion window size using Eq. 1, where t is the
                    //   elapsed time since the beginning of the current congestion
                    //   avoidance, K is set to 0, and W_max is set to the
                    //   congestion window size at the beginning of the current
                    //   congestion avoidance.
                    self.k = 0.0;
                    self.w_max = self.cwnd;
                }
                self.epoch_start = Some(now);
                now
            }
        };

        // Per RFC 8312 (https://www.rfc-editor.org/rfc/rfc8312#section-4.7):
        //   Upon receiving an ACK during congestion avoidance, CUBIC computes
        //   the window increase rate during the next RTT period using Eq. 1.
        //   It sets W_cubic(t+RTT) as the candidate target value of the
        //   congestion window, where RTT is the weighted average RTT calculated
        //   by Standard TCP.
        let t = now.duration_since(epoch_start);
        let target = self.cubic_window(t + rtt);

        // In a *very* rare case, we might overflow the counter if the acks
        // keep coming in and we can't increase our congestion window. Use
        // wrapping add here as a defense so that we don't lost ack counts
        // by accident.
        self.bytes_acked = self.bytes_acked.saturating_add(bytes_acked.get());

        // Per RFC 8312 (https://www.rfc-editor.org/rfc/rfc8312#section-4.3):
        //   cwnd MUST be incremented by (W_cubic(t+RTT) - cwnd)/cwnd for each
        //   received ACK.
        // Note: Here we use a similar approach as in appropriate byte counting
        // (RFC 3465) - We count how many bytes are now acked, then we use Eq. 1
        // to calculate how many acked bytes are needed before we can increase
        // our cwnd by 1 MSS. The increase rate is (target - cwnd)/cwnd segments
        // per ACK which is the same as 1 segment per cwnd/(target - cwnd) ACKs.
        // Because our cubic function is a monotonically increasing function,
        // this method is slightly more aggressive - if we need N acks to
        // increase our window by 1 MSS, then it would take the RFC method at
        // least N acks to increase the same amount. This method is used in the
        // original CUBIC paper[1], and it eliminates the need to use f32 for
        // cwnd, which is a bit awkward especially because our unit is in bytes
        // and it doesn't make much sense to have byte number not to be a whole
        // number.
        // [1]: (https://www.cs.princeton.edu/courses/archive/fall16/cos561/papers/Cubic08.pdf)
        if target >= self.cwnd + self.mss // An increase to cwnd is needed
            && self.bytes_acked >= self.cwnd / (target - self.cwnd) * self.mss
        // And the # of acked bytes is at least the required amount of bytes for
        // increasing 1 MSS.
        {
            self.bytes_acked -= self.cwnd / (target - self.cwnd) * self.mss;
            self.cwnd += self.mss;
        }

        // Per RFC 8312 (https://www.rfc-editor.org/rfc/rfc8312#section-4.2):
        //   CUBIC checks whether W_cubic(t) is less than W_est(t).  If so,
        //   CUBIC is in the TCP-friendly region and cwnd SHOULD be set to
        //   W_est(t) at each reception of an ACK.
        let w_tcp = self.standard_tcp_window(t, rtt);
        if self.cwnd < w_tcp {
            self.cwnd = w_tcp;
        }
    }

    pub(super) fn on_loss_detected(&mut self) {
        // End the current congestion avoidance epoch.
        self.epoch_start = None;
        // Per RFC 8312 (https://www.rfc-editor.org/rfc/rfc8312#section-4.7):
        //   In case of timeout, CUBIC follows Standard TCP to reduce cwnd
        //   [RFC5681], but sets ssthresh using beta_cubic (same as in
        //   Section 4.5) that is different from Standard TCP [RFC5681].
        self.w_max = self.cwnd;
        self.ssthresh = u32::max((self.cwnd as f32 * CUBIC_BETA) as u32, 2 * self.mss);
        self.cwnd = self.ssthresh;
        // Reset our running count of the acked bytes.
        self.bytes_acked = 0;
    }

    pub(super) fn on_retransmission_timeout(&mut self) {
        self.on_loss_detected();
        // Per RFC 5681 (https://www.rfc-editor.org/rfc/rfc5681#page-8):
        //   Furthermore, upon a timeout (as specified in [RFC2988]) cwnd MUST be
        //   set to no more than the loss window, LW, which equals 1 full-sized
        //   segment (regardless of the value of IW).
        self.cwnd = self.mss;
    }

    pub(super) fn cwnd(&self) -> WindowSize {
        WindowSize::from_u32(self.cwnd / self.mss * self.mss).unwrap_or(WindowSize::MAX)
    }

    fn cubic_c(&self) -> f32 {
        // Note: cwnd and w_max are in unit of bytes as opposed to segments in
        // RFC, so C should be CUBIC_C * mss for our implementation.
        CUBIC_C * self.mss as f32
    }

    pub(crate) fn on_loss_recovered(&mut self) {
        self.cwnd = self.ssthresh;
    }
}

#[cfg(test)]
mod tests {
    use test_case::test_case;

    use super::*;
    use crate::context::{testutil::FakeInstantCtx, InstantContext as _};

    impl<I: Instant> Cubic<I> {
        // Helper function in test that takes a u32 instead of a NonZeroU32
        // as we know we never pass 0 in the test and it's a bit clumsy to
        // convert a u32 into a NonZeroU32 every time.
        fn on_ack_u32(&mut self, bytes_acked: u32, now: I, rtt: Duration) {
            self.on_ack(NonZeroU32::new(bytes_acked).unwrap(), now, rtt)
        }
    }

    // The following expectations are extracted from table. 1 and table. 2 in
    // RFC 8312 (https://www.rfc-editor.org/rfc/rfc8312#section-5.1). Note that
    // some numbers do not match as-is, but the error rate is acceptable (~2%),
    // this can be attributed to a few things, e.g., the way we simulate is
    // slightly different from the the ideal process, as we start the first
    // congestion avoidance with the convex region which grows pretty fast, also
    // the theoretical estimation is an approximation already. The theoretical
    // value is included in the name for each case.
    #[test_case(Duration::from_millis(100), 100 => 11; "rtt=0.1 p=0.01 Wavg=12")]
    #[test_case(Duration::from_millis(100), 1_000 => 38; "rtt=0.1 p=0.001 Wavg=38")]
    #[test_case(Duration::from_millis(100), 10_000 => 186; "rtt=0.1 p=0.0001 Wavg=187")]
    #[test_case(Duration::from_millis(100), 100_000 => 1078; "rtt=0.1 p=0.00001 Wavg=1054")]
    #[test_case(Duration::from_millis(10), 100 => 11; "rtt=0.01 p=0.01 Wavg=12")]
    #[test_case(Duration::from_millis(10), 1_000 => 37; "rtt=0.01 p=0.001 Wavg=38")]
    #[test_case(Duration::from_millis(10), 10_000 => 121; "rtt=0.01 p=0.0001 Wavg=120")]
    #[test_case(Duration::from_millis(10), 100_000 => 384; "rtt=0.01 p=0.00001 Wavg=379")]
    #[test_case(Duration::from_millis(10), 1_000_000 => 1276; "rtt=0.01 p=0.000001 Wavg=1200")]
    fn average_window_size(rtt: Duration, loss_rate_reciprocal: u32) -> u32 {
        const ROUND_TRIPS: u32 = 100_000;

        let mut cubic = Cubic::default();
        // The theoretical value is a prediction for the congestion avoidance
        // only, set ssthresh to 1 so that we skip slow start. Slow start can
        // grow the window size very quickly.
        cubic.ssthresh = 1;

        let mut clock = FakeInstantCtx::default();

        let mut avg_pkts = 0.0f32;
        let mut ack_cnt = 0;

        // We simulate a deterministic loss model, i.e., for loss_rate p, we
        // drop one packet for every 1/p packets.
        for _ in 0..ROUND_TRIPS {
            let cwnd = u32::from(cubic.cwnd());
            if ack_cnt >= loss_rate_reciprocal {
                ack_cnt -= loss_rate_reciprocal;
                cubic.on_loss_detected();
            } else {
                ack_cnt += cwnd / cubic.mss;
                // We will get at least one ack for every two segments we send.
                for _ in 0..u32::max(cwnd / cubic.mss / 2, 1) {
                    cubic.on_ack_u32(2 * cubic.mss, clock.now(), rtt);
                }
            }
            clock.sleep(rtt);
            avg_pkts += (cubic.cwnd / cubic.mss) as f32 / ROUND_TRIPS as f32;
        }
        avg_pkts as u32
    }

    #[test]
    fn cubic_example() {
        let mut clock = FakeInstantCtx::default();
        let mut cubic = Cubic::default();
        const RTT: Duration = Duration::from_millis(100);

        // Assert we have the correct initial window.
        assert_eq!(cubic.cwnd, 4 * DEFAULT_MAXIMUM_SEGMENT_SIZE);

        // Slow start.
        clock.sleep(RTT);
        for _seg in 0..cubic.cwnd / DEFAULT_MAXIMUM_SEGMENT_SIZE {
            cubic.on_ack_u32(DEFAULT_MAXIMUM_SEGMENT_SIZE, clock.now(), RTT);
        }
        assert_eq!(cubic.cwnd, 8 * DEFAULT_MAXIMUM_SEGMENT_SIZE);

        clock.sleep(RTT);
        cubic.on_retransmission_timeout();
        assert_eq!(cubic.cwnd, DEFAULT_MAXIMUM_SEGMENT_SIZE);

        // We are now back in slow start.
        clock.sleep(RTT);
        cubic.on_ack_u32(DEFAULT_MAXIMUM_SEGMENT_SIZE, clock.now(), RTT);
        assert_eq!(cubic.cwnd, 2 * DEFAULT_MAXIMUM_SEGMENT_SIZE);

        clock.sleep(RTT);
        for _ in 0..2 {
            cubic.on_ack_u32(DEFAULT_MAXIMUM_SEGMENT_SIZE, clock.now(), RTT);
        }
        assert_eq!(cubic.cwnd, 4 * DEFAULT_MAXIMUM_SEGMENT_SIZE);

        // In this roundtrip, we enter a new congestion epoch from slow start,
        // in this round trip, both cubic and tcp equations will have t=0, so
        // the cwnd in this round trip will be ssthresh, which is 3001 bytes,
        // or 5 full sized segments.
        clock.sleep(RTT);
        for _seg in 0..cubic.cwnd / DEFAULT_MAXIMUM_SEGMENT_SIZE {
            cubic.on_ack_u32(DEFAULT_MAXIMUM_SEGMENT_SIZE, clock.now(), RTT);
        }
        assert_eq!(u32::from(cubic.cwnd()), 5 * DEFAULT_MAXIMUM_SEGMENT_SIZE);

        // Now we are at `epoch_start+RTT`, the window size should grow by at
        // lease 1 MSS per RTT (standard TCP).
        clock.sleep(RTT);
        for _seg in 0..cubic.cwnd / DEFAULT_MAXIMUM_SEGMENT_SIZE {
            cubic.on_ack_u32(DEFAULT_MAXIMUM_SEGMENT_SIZE, clock.now(), RTT);
        }
        assert_eq!(u32::from(cubic.cwnd()), 6 * DEFAULT_MAXIMUM_SEGMENT_SIZE);
    }
}
