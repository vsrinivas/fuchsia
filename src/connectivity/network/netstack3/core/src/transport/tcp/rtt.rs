// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! TCP RTT estimation per [RFC 6298](https://tools.ietf.org/html/rfc6298).
use core::{default::Default, time::Duration};

#[cfg_attr(test, derive(Debug, PartialEq))]
pub(super) enum Estimator {
    NoSample,
    Measured {
        /// The smoothed round-trip time.
        srtt: Duration,
        /// The round-trip time variation.
        rtt_var: Duration,
    },
}

impl Default for Estimator {
    fn default() -> Self {
        Self::NoSample
    }
}

impl Estimator {
    /// The following constants are defined in [RFC 6298 Section 2]:
    ///
    /// [RFC 6298]: https://tools.ietf.org/html/rfc6298#section-2
    const K: u32 = 4;
    const G: Duration = Duration::from_millis(100);
    const RTO_INIT: Duration = Duration::from_secs(1);

    /// Updates the estimates with a newly sampled RTT.
    pub(super) fn sample(&mut self, rtt: Duration) {
        match self {
            Self::NoSample => {
                // Per RFC 6298 section 2,
                //   When the first RTT measurement R is made, the host MUST set
                //   SRTT <- R
                //   RTTVAR <- R/2
                *self = Self::Measured { srtt: rtt, rtt_var: rtt / 2 }
            }
            Self::Measured { srtt, rtt_var } => {
                // Per RFC 6298 section 2,
                //   When a subsequent RTT measurement R' is made, a host MUST set
                //     RTTVAR <- (1 - beta) * RTTVAR + beta * |SRTT - R'|
                //     SRTT <- (1 - alpha) * SRTT + alpha * R'
                //   ...
                //   The above SHOULD be computed using alpha=1/8 and beta=1/4.
                let diff = srtt.checked_sub(rtt).unwrap_or_else(|| rtt - *srtt);
                // Using fixed point integer division below rather than using
                // floating points just to define the exact constants.
                *rtt_var = ((*rtt_var * 3) + diff) / 4;
                *srtt = ((*srtt * 7) + rtt) / 8;
            }
        }
    }

    /// Returns the current retransmission timeout.
    pub(super) fn rto(&self) -> Duration {
        //   Until a round-trip time (RTT) measurement has been made for a
        //   segment sent between the sender and receiver, the sender SHOULD
        //   set RTO <- 1 second;
        //   ...
        //   RTO <- SRTT + max (G, K*RTTVAR)
        match *self {
            Estimator::NoSample => Self::RTO_INIT,
            Estimator::Measured { srtt, rtt_var } => {
                // `Duration::MAX` is 2^64 seconds which is about 6 * 10^11
                // years. If the following expression panics due to overflow,
                // we must have some serious errors in the estimator itself.
                srtt + Self::G.max(rtt_var * Self::K)
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use test_case::test_case;

    #[test_case(Estimator::NoSample, Duration::from_secs(2) => Estimator::Measured {
        srtt: Duration::from_secs(2),
        rtt_var: Duration::from_secs(1)
    })]
    #[test_case(Estimator::Measured {
        srtt: Duration::from_secs(1),
        rtt_var: Duration::from_secs(1)
    }, Duration::from_secs(2) => Estimator::Measured {
        srtt: Duration::from_millis(1125),
        rtt_var: Duration::from_secs(1)
    })]
    #[test_case(Estimator::Measured {
        srtt: Duration::from_secs(1),
        rtt_var: Duration::from_secs(2)
    }, Duration::from_secs(1) => Estimator::Measured {
        srtt: Duration::from_secs(1),
        rtt_var: Duration::from_millis(1500)
    })]
    fn sample_rtt(mut estimator: Estimator, rtt: Duration) -> Estimator {
        estimator.sample(rtt);
        estimator
    }

    #[test_case(Estimator::NoSample => Estimator::RTO_INIT)]
    #[test_case(Estimator::Measured {
        srtt: Duration::from_secs(1),
        rtt_var: Duration::from_secs(2),
    } => Duration::from_secs(9))]
    fn calculate_rto(estimator: Estimator) -> Duration {
        estimator.rto()
    }
}
