// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Internet Group Management Protocol.
//!
//! The Internet Group Management Protocol (IGMP) is a communications protocol used
//! by hosts and adjacent routers on IPv4 networks to establish multicast group memberships.
//! IGMP is an integral part of IP multicast.

use std::time::Duration;

use log::trace;
use net_types::ip::IpAddress;
use packet::BufferMut;

use crate::ip::gmp::{Actions, GmpStateMachine, ProtocolSpecific};
use crate::{Context, EventDispatcher};

/// Receive an IGMP message in an IP packet.
pub(crate) fn receive_igmp_packet<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    src_ip: A,
    dst_ip: A,
    buffer: B,
) {
    log_unimplemented!((), "ip::igmp::receive_igmp_packet: Not implemented.")
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Igmpv2ProtocolSpecific {
    v1_router_present: bool,
}

impl Default for Igmpv2ProtocolSpecific {
    fn default() -> Self {
        Igmpv2ProtocolSpecific { v1_router_present: false }
    }
}

#[derive(PartialEq, Eq, Debug)]
enum Igmpv2Actions {
    ScheduleV1RouterPresentTimer(Duration),
}

struct Igmpv2HostConfig {
    // When a host wants to send a report not because
    // of a query, this value is used as the delay timer.
    unsolicited_report_interval: Duration,
    // When this option is true, the host can send a leave
    // message even when it is not the last one in the multicast
    // group.
    send_leave_anyway: bool,
    // Default timer value for Version 1 Router Present Timeout.
    v1_router_present_timeout: Duration,
}

/// The default value for `unsolicited_report_interval` as per [RFC 2236 section 8.10].
///
/// [RFC 2236 section 8.10]: https://tools.ietf.org/html/rfc2236#section-8.10
const DEFAULT_UNSOLICITED_REPORT_INTERVAL: Duration = Duration::from_secs(10);
/// The default value for `v1_router_present_timeout` as per [RFC 2236 section 8.11].
///
/// [RFC 2236 section 8.11]: https://tools.ietf.org/html/rfc2236#section-8.11
const DEFAULT_V1_ROUTER_PRESENT_TIMEOUT: Duration = Duration::from_secs(400);
/// The default value for the `MaxRespTime` if the query is a V1 query, whose
/// `MaxRespTime` field is 0 in the packet. Please refer to [RFC 2236 section 4]
///
/// [RFCC 2236 section 4]: https://tools.ietf.org/html/rfc2236#section-4
const DEFAULT_V1_QUERY_MAX_RESP_TIME: Duration = Duration::from_secs(10);

impl Default for Igmpv2HostConfig {
    fn default() -> Self {
        Igmpv2HostConfig {
            unsolicited_report_interval: DEFAULT_UNSOLICITED_REPORT_INTERVAL,
            send_leave_anyway: false,
            v1_router_present_timeout: DEFAULT_V1_ROUTER_PRESENT_TIMEOUT,
        }
    }
}

impl ProtocolSpecific for Igmpv2ProtocolSpecific {
    type Action = Igmpv2Actions;
    type Config = Igmpv2HostConfig;

    fn cfg_unsolicited_report_interval(cfg: &Self::Config) -> Duration {
        cfg.unsolicited_report_interval
    }

    fn cfg_send_leave_anyway(cfg: &Self::Config) -> bool {
        cfg.send_leave_anyway
    }

    fn get_max_resp_time(resp_time: Duration) -> Duration {
        if resp_time.as_micros() == 0 {
            DEFAULT_V1_QUERY_MAX_RESP_TIME
        } else {
            resp_time
        }
    }

    fn do_query_received_specific(
        cfg: &Self::Config,
        actions: &mut Actions<Self>,
        max_resp_time: Duration,
        old: Igmpv2ProtocolSpecific,
    ) -> Igmpv2ProtocolSpecific {
        // IGMPv2 hosts should be compatible with routers that only speak IGMPv1.
        // When an IGMPv2 host receives an IGMPv1 query (whose `MaxRespCode` is 0),
        // it should set up a timer and only respond with IGMPv1 responses before
        // the timer expires. Please refer to https://tools.ietf.org/html/rfc2236#section-4
        // for details.
        let new_ps = Igmpv2ProtocolSpecific { v1_router_present: max_resp_time.as_micros() == 0 };
        if new_ps.v1_router_present {
            actions.push_specific(Igmpv2Actions::ScheduleV1RouterPresentTimer(
                cfg.v1_router_present_timeout,
            ));
        }
        new_ps
    }
}

type IgmpGroupState<I> = GmpStateMachine<I, Igmpv2ProtocolSpecific>;

#[cfg(test)]
mod tests {
    use super::*;
    use std::time;

    use crate::ip::gmp::{Action, GmpAction, MemberState};
    use crate::Instant;

    fn at_least_one_action(
        actions: Actions<Igmpv2ProtocolSpecific>,
        action: Action<Igmpv2ProtocolSpecific>,
    ) -> bool {
        actions.into_iter().any(|a| a == action)
    }

    impl<I: Instant> IgmpGroupState<I> {
        fn v1_router_present_timer_expired(&mut self) {
            self.update_with_protocol_specific(Igmpv2ProtocolSpecific { v1_router_present: false });
        }
    }

    #[test]
    fn test_igmp_state_with_igmpv1_router() {
        let mut s = IgmpGroupState::default();
        s.join_group(time::Instant::now());
        s.query_received(Duration::from_secs(0), time::Instant::now());
        let actions = s.report_timer_expired();
        at_least_one_action(
            actions,
            Action::<Igmpv2ProtocolSpecific>::Generic(GmpAction::SendReport(
                Igmpv2ProtocolSpecific { v1_router_present: true },
            )),
        );
    }

    #[test]
    fn test_igmp_state_igmpv1_router_present_timer_expires() {
        let mut s = IgmpGroupState::default();
        s.join_group(time::Instant::now());
        s.query_received(Duration::from_secs(0), time::Instant::now());
        match s.get_inner() {
            MemberState::Delaying(state) => {
                assert!(state.get_protocol_specific().v1_router_present);
            }
            _ => panic!("Wrong State!"),
        }
        s.v1_router_present_timer_expired();
        match s.get_inner() {
            MemberState::Delaying(state) => {
                assert!(!state.get_protocol_specific().v1_router_present);
            }
            _ => panic!("Wrong State!"),
        }
        s.query_received(Duration::from_secs(0), time::Instant::now());
        s.report_received();
        s.v1_router_present_timer_expired();
        match s.get_inner() {
            MemberState::Idle(state) => {
                assert!(!state.get_protocol_specific().v1_router_present);
            }
            _ => panic!("Wrong State!"),
        }
    }
}
