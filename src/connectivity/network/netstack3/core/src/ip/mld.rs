// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Multicast Listener Discovery
//!
//! Multicast Listener Discovery (MLD) is derived from version 2 of IPv4's
//! Internet Group Management Protocol, IGMPv2. One important difference
//! to note is that MLD uses ICMPv6 (IP Protocol 58) message types,
//! rather than IGMP (IP Protocol 2) message types.

use std::time::Duration;

use crate::ip::gmp::{Actions, GmpStateMachine, ProtocolSpecific};

#[derive(PartialEq, Eq, Clone, Copy, Default, Debug)]
struct MldProtocolSpecific;

struct MldConfig {
    unsolicited_report_interval: Duration,
    send_leave_anyway: bool,
}

#[derive(PartialEq, Eq, Clone, Copy, Debug)]
struct ImmediateIdleState;

/// The default value for `unsolicited_report_interval` [RFC 2710 section 7.10]
///
/// [RFC 2710 section 7.10] https://tools.ietf.org/html/rfc2710#section-7.10
const DEFAULT_UNSOLICITED_REPORT_INTERVAL: Duration = Duration::from_secs(10);

impl Default for MldConfig {
    fn default() -> Self {
        MldConfig {
            unsolicited_report_interval: DEFAULT_UNSOLICITED_REPORT_INTERVAL,
            send_leave_anyway: false,
        }
    }
}

impl ProtocolSpecific for MldProtocolSpecific {
    /// The action to turn an MLD host to Idle state immediately.
    type Action = ImmediateIdleState;
    type Config = MldConfig;

    fn cfg_unsolicited_report_interval(cfg: &Self::Config) -> Duration {
        cfg.unsolicited_report_interval
    }

    fn cfg_send_leave_anyway(cfg: &Self::Config) -> bool {
        cfg.send_leave_anyway
    }

    fn get_max_resp_time(resp_time: Duration) -> Duration {
        resp_time
    }

    fn do_query_received_specific(
        cfg: &Self::Config,
        actions: &mut Actions<Self>,
        max_resp_time: Duration,
        old: Self,
    ) -> Self {
        if max_resp_time.as_millis() == 0 {
            actions.push_specific(ImmediateIdleState);
        }
        old
    }
}

type MldGroupState<I> = GmpStateMachine<I, MldProtocolSpecific>;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ip::gmp::{Action, GmpAction};
    use std::time::Instant;

    #[test]
    fn test_mld_immediate_report() {
        // Most of the test surface is covered by the GMP implementation,
        // MLD specific part is mostly passthrough. This test case is here
        // because MLD allows a router to ask for report immediately, by
        // specifying the `MaxRespDelay` to be 0. If this is the case, the
        // host should send the report immediately instead of setting a timer.
        let mut s = MldGroupState::default();
        s.join_group(Instant::now());
        let actions = s.query_received(Duration::from_secs(0), Instant::now());
        let vec = actions.into_iter().collect::<Vec<Action<_>>>();
        assert_eq!(vec.len(), 2);
        assert_eq!(vec[0], Action::Generic(GmpAction::SendReport(MldProtocolSpecific)));
        assert_eq!(vec[1], Action::Specific(ImmediateIdleState));
    }
}
