// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Internet Group Management Protocol.
//!
//! The Internet Group Management Protocol (IGMP) is a communications protocol used
//! by hosts and adjacent routers on IPv4 networks to establish multicast group memberships.
//! IGMP is an integral part of IP multicast.

use std::collections::HashMap;
use std::time::Duration;

use failure::Fail;
use log::{debug, error};
use net_types::ip::{IpAddress, Ipv4Addr};
use net_types::MulticastAddr;
use packet::{Buf, BufferMut, InnerPacketBuilder};
use rand::Rng;
use specialize_ip_macro::specialize_ip_address;
use zerocopy::ByteSlice;

use crate::device::DeviceId;
use crate::ip::{
    gmp::{Action, Actions, GmpAction, GmpStateMachine, ProtocolSpecific},
    IpLayerTimerId,
};
use crate::wire::igmp::{
    messages::{IgmpLeaveGroup, IgmpMembershipReportV1, IgmpMembershipReportV2, IgmpPacket},
    IgmpMessage, IgmpPacketBuilder, MessageType,
};
use crate::{Context, EventDispatcher, Instant, TimerId, TimerIdInner};

/// Receive an IGMP message in an IP packet.
#[specialize_ip_address]
pub(crate) fn receive_igmp_packet<D: EventDispatcher, A: IpAddress, B: BufferMut>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: A,
    dst_ip: A,
    mut buffer: B,
) {
    #[ipv4addr]
    {
        let packet = match buffer.parse_with::<_, IgmpPacket<&[u8]>>(()) {
            Ok(packet) => packet,
            Err(err) => {
                debug!("Cannot parse the incoming IGMP packet, dropping.");
                return;
            } // TODO: Do something else here?
        };

        if let Err(e) = match packet {
            IgmpPacket::MembershipQueryV2(msg) => {
                let now = ctx.dispatcher.now();
                handle_igmp_message(ctx, device, msg, |rng, state, msg| {
                    state.query_received(rng, msg.max_response_time().into(), now)
                })
            }
            IgmpPacket::MembershipReportV1(msg) => {
                handle_igmp_message(ctx, device, msg, |_, state, _| state.report_received())
            }
            IgmpPacket::MembershipReportV2(msg) => {
                handle_igmp_message(ctx, device, msg, |_, state, _| state.report_received())
            }
            IgmpPacket::LeaveGroup(_) => {
                debug!("Hosts are not interested in Leave Group messages");
                return;
            }
            _ => {
                debug!("We do not support IGMPv3 yet");
                return;
            }
        } {
            error!("Error occurred when handling IGMPv2 message: {}", e);
        }
    }
    #[ipv6addr]
    // TODO: Once we support version-specific protocols, receive_igmp_packet
    // should not be generic over IpAddress; see NET-2233.
    unreachable!("igmp is not designed for ipv6, use MLD instead");
}

fn handle_igmp_message<D, B, M, F>(
    ctx: &mut Context<D>,
    device: DeviceId,
    msg: IgmpMessage<B, M>,
    handler: F,
) -> IgmpResult<()>
where
    D: EventDispatcher,
    B: ByteSlice,
    M: MessageType<B, FixedHeader = Ipv4Addr>,
    F: Fn(
        &mut D::Rng,
        &mut IgmpGroupState<D::Instant>,
        &IgmpMessage<B, M>,
    ) -> Actions<Igmpv2ProtocolSpecific>,
{
    let group_addr = msg.group_addr();
    let (state, dispatcher) = ctx.state_and_dispatcher();
    if group_addr.is_unspecified() {
        let mut addr_and_actions = state
            .ip
            .get_igmp_state_mut(device.id())
            .groups
            .iter_mut()
            .map(|(addr, state)| (addr.clone(), handler(dispatcher.rng(), state, &msg)))
            .collect::<Vec<_>>();
        // `addr` must be a multicast address, otherwise it will not have
        // an associated state in the first place
        for (addr, actions) in addr_and_actions {
            run_actions(ctx, device, actions, addr);
        }
        Ok(())
    } else if let Some(group_addr) = MulticastAddr::new(group_addr) {
        let actions = match state.ip.get_igmp_state_mut(device.id()).groups.get_mut(&group_addr) {
            Some(state) => handler(dispatcher.rng(), state, &msg),
            None => return Err(IgmpError::NotAMember { addr: *group_addr }),
        };
        // `group_addr` here must be a multicast address for similar reasons
        run_actions(ctx, device, actions, group_addr);
        Ok(())
    } else {
        Err(IgmpError::NotAMember { addr: group_addr })
    }
}

#[derive(Debug, Fail)]
pub(crate) enum IgmpError {
    /// The host is trying to operate on an group address of which the host is not a member.
    #[fail(display = "the host has not already been a member of the address: {}", addr)]
    NotAMember { addr: Ipv4Addr },
    /// Failed to send an IGMP packet.
    #[fail(display = "failed to send out an IGMP packet to address: {}", addr)]
    SendFailure { addr: Ipv4Addr },
    /// The given device does not have an assigned IP address.
    #[fail(display = "no ip address is associated with the device: {}", device)]
    NoIpAddress { device: DeviceId },
}

pub(crate) type IgmpResult<T> = Result<T, IgmpError>;

#[derive(Debug, PartialEq, Eq, Clone, Copy, Hash)]
pub(crate) enum IgmpTimerId {
    /// The timer used to switch a host from Delay Member state to Idle Member state
    ReportDelay { device: DeviceId, group_addr: MulticastAddr<Ipv4Addr> },
    /// The timer used to determine whether there is a router speaking IGMPv1
    V1RouterPresent { device: DeviceId },
}

impl IgmpTimerId {
    fn new_report_delay_timer(device: DeviceId, group_addr: MulticastAddr<Ipv4Addr>) -> TimerId {
        IgmpTimerId::ReportDelay { device, group_addr }.into()
    }

    fn new_v1_router_present_timer(device: DeviceId) -> TimerId {
        IgmpTimerId::V1RouterPresent { device }.into()
    }
}

impl From<IgmpTimerId> for TimerId {
    fn from(id: IgmpTimerId) -> Self {
        TimerId(TimerIdInner::IpLayer(IpLayerTimerId::IgmpTimer(id)))
    }
}

pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, timer: IgmpTimerId) {
    match timer {
        IgmpTimerId::ReportDelay { device, group_addr } => {
            let actions =
                match ctx.state.ip.get_igmp_state_mut(device.id()).groups.get_mut(&group_addr) {
                    Some(state) => state.report_timer_expired(),
                    None => {
                        error!("Not already a member");
                        return;
                    }
                };
            run_actions(ctx, device, actions, group_addr);
        }
        IgmpTimerId::V1RouterPresent { device } => {
            for (_, state) in ctx.state.ip.get_igmp_state_mut(device.id()).groups.iter_mut() {
                state.v1_router_present_timer_expired();
            }
        }
    }
}

fn send_igmp_message<B, D, M>(
    ctx: &mut Context<D>,
    device: DeviceId,
    group_addr: MulticastAddr<Ipv4Addr>,
    dst_ip: MulticastAddr<Ipv4Addr>,
    max_resp_time: M::MaxRespTime,
) -> IgmpResult<()>
where
    D: EventDispatcher,
    M: MessageType<B, FixedHeader = Ipv4Addr, VariableBody = ()>,
{
    let src_ip = match crate::device::get_ip_addr_subnet(ctx, device) {
        Some(addr_subnet) => addr_subnet.addr(),
        None => return Err(IgmpError::NoIpAddress { device }),
    };
    let body = IgmpPacketBuilder::<B, M>::new_with_resp_time(group_addr.get(), max_resp_time);
    crate::ip::send_igmp_packet(ctx, device, src_ip, dst_ip.get(), body.into_serializer())
        .map_err(|_| IgmpError::SendFailure { addr: *group_addr })
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

impl<I: Instant> IgmpGroupState<I> {
    fn v1_router_present_timer_expired(&mut self) {
        self.update_with_protocol_specific(Igmpv2ProtocolSpecific { v1_router_present: false });
    }
}

/// This is used to represent the groups that an IGMP host is interested in.
pub(crate) struct IgmpInterface<I: Instant> {
    groups: HashMap<MulticastAddr<Ipv4Addr>, IgmpGroupState<I>>,
}

impl<I: Instant> Default for IgmpInterface<I> {
    fn default() -> Self {
        IgmpInterface { groups: HashMap::new() }
    }
}

fn run_actions<D>(
    ctx: &mut Context<D>,
    device: DeviceId,
    actions: Actions<Igmpv2ProtocolSpecific>,
    group_addr: MulticastAddr<Ipv4Addr>,
) where
    D: EventDispatcher,
{
    for action in actions {
        if let Err(err) = run_action(ctx, device, action, group_addr) {
            error!("Error performing action on {} on device {}: {}", group_addr, device, err);
        }
    }
}

// TODO: change this once we have a better strategy for buffer allocation.
type MessageBuffer = Buf<Vec<u8>>;

/// Interpret the actions
fn run_action<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    action: Action<Igmpv2ProtocolSpecific>,
    group_addr: MulticastAddr<Ipv4Addr>,
) -> IgmpResult<()> {
    match action {
        Action::Generic(GmpAction::ScheduleReportTimer(duration)) => {
            ctx.dispatcher.schedule_timeout(
                duration,
                IgmpTimerId::new_report_delay_timer(device, group_addr),
            );
        }
        Action::Generic(GmpAction::StopReportTimer) => {
            ctx.dispatcher.cancel_timeout(IgmpTimerId::new_report_delay_timer(device, group_addr));
        }
        Action::Generic(GmpAction::SendLeave) => {
            send_igmp_message::<MessageBuffer, D, IgmpLeaveGroup>(
                ctx,
                device,
                group_addr,
                MulticastAddr::new(crate::ip::IPV4_ALL_ROUTERS).unwrap(),
                (),
            )?;
        }
        Action::Generic(GmpAction::SendReport(Igmpv2ProtocolSpecific { v1_router_present })) => {
            if v1_router_present {
                send_igmp_message::<MessageBuffer, D, IgmpMembershipReportV1>(
                    ctx,
                    device,
                    group_addr,
                    group_addr,
                    (),
                )?;
            } else {
                send_igmp_message::<MessageBuffer, D, IgmpMembershipReportV2>(
                    ctx,
                    device,
                    group_addr,
                    group_addr,
                    (),
                )?;
            }
        }
        Action::Specific(Igmpv2Actions::ScheduleV1RouterPresentTimer(duration)) => {
            ctx.dispatcher
                .schedule_timeout(duration, IgmpTimerId::new_v1_router_present_timer(device));
        }
    }
    Ok(())
}

impl<I: Instant> IgmpInterface<I> {
    fn join_group<R: Rng>(
        &mut self,
        rng: &mut R,
        addr: MulticastAddr<Ipv4Addr>,
        now: I,
    ) -> Actions<Igmpv2ProtocolSpecific> {
        self.groups.entry(addr).or_insert(GmpStateMachine::default()).join_group(rng, now)
    }

    fn leave_group(
        &mut self,
        addr: MulticastAddr<Ipv4Addr>,
    ) -> IgmpResult<Actions<Igmpv2ProtocolSpecific>> {
        match self.groups.remove(&addr).as_mut() {
            Some(state) => Ok(state.leave_group()),
            None => Err(IgmpError::NotAMember { addr: addr.get() }),
        }
    }
}

/// Make our host join a multicast group.
pub(crate) fn igmp_join_group<D>(
    ctx: &mut Context<D>,
    device: DeviceId,
    group_addr: MulticastAddr<Ipv4Addr>,
) where
    D: EventDispatcher,
{
    let (state, dispatcher) = ctx.state_and_dispatcher();
    let now = dispatcher.now();
    let rng = dispatcher.rng();
    let actions = state.ip.get_igmp_state_mut(device.id()).join_group(rng, group_addr, now);
    // actions will be `Nothing` if the the host is not in the `NonMember` state.
    run_actions(ctx, device, actions, group_addr);
}

/// Make our host leave the multicast group.
///
/// If our host is not already a member of the given address, this will result
/// in the `IgmpError::NotAMember` error.
pub(crate) fn igmp_leave_group<D>(
    ctx: &mut Context<D>,
    device: DeviceId,
    group_addr: MulticastAddr<Ipv4Addr>,
) -> IgmpResult<()>
where
    D: EventDispatcher,
{
    let actions = ctx.state_mut().ip.get_igmp_state_mut(device.id()).leave_group(group_addr)?;
    run_actions(ctx, device, actions, group_addr);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::convert::TryInto;
    use std::time;

    use net_types::ethernet::Mac;
    use net_types::ip::AddrSubnet;
    use packet::serialize::{Buf, Serializer};

    use crate::device::ethernet::*;
    use crate::device::DeviceId;
    use crate::ip::gmp::{Action, GmpAction, MemberState};
    use crate::testutil::{self, *};
    use crate::wire::igmp::messages::IgmpMembershipQueryV2;

    fn at_least_one_action(
        actions: Actions<Igmpv2ProtocolSpecific>,
        action: Action<Igmpv2ProtocolSpecific>,
    ) -> bool {
        actions.into_iter().any(|a| a == action)
    }

    #[test]
    fn test_igmp_state_with_igmpv1_router() {
        let mut rng = new_rng(0);
        let mut s = IgmpGroupState::default();
        s.join_group(&mut rng, time::Instant::now());
        s.query_received(&mut rng, Duration::from_secs(0), time::Instant::now());
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
        let mut rng = new_rng(0);
        s.join_group(&mut rng, time::Instant::now());
        s.query_received(&mut rng, Duration::from_secs(0), time::Instant::now());
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
        s.query_received(&mut rng, Duration::from_secs(0), time::Instant::now());
        s.report_received();
        s.v1_router_present_timer_expired();
        match s.get_inner() {
            MemberState::Idle(state) => {
                assert!(!state.get_protocol_specific().v1_router_present);
            }
            _ => panic!("Wrong State!"),
        }
    }

    const MY_ADDR: Ipv4Addr = Ipv4Addr::new([192, 168, 0, 2]);
    const ROUTER_ADDR: Ipv4Addr = Ipv4Addr::new([192, 168, 0, 1]);
    const OTHER_HOST_ADDR: Ipv4Addr = Ipv4Addr::new([192, 168, 0, 3]);
    const GROUP_ADDR: Ipv4Addr = Ipv4Addr::new([224, 0, 0, 3]);
    const GROUP_ADDR_2: Ipv4Addr = Ipv4Addr::new([224, 0, 0, 4]);

    fn receive_igmp_query<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device: DeviceId,
        resp_time: Duration,
    ) {
        let ser = IgmpPacketBuilder::<Buf<Vec<u8>>, IgmpMembershipQueryV2>::new_with_resp_time(
            GROUP_ADDR,
            resp_time.try_into().unwrap(),
        );
        let mut buff = ser.into_serializer().serialize_vec_outer().unwrap();
        receive_igmp_packet(ctx, device, ROUTER_ADDR, MY_ADDR, buff);
    }

    fn receive_igmp_general_query<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device: DeviceId,
        resp_time: Duration,
    ) {
        let ser = IgmpPacketBuilder::<Buf<Vec<u8>>, IgmpMembershipQueryV2>::new_with_resp_time(
            Ipv4Addr::new([0, 0, 0, 0]),
            resp_time.try_into().unwrap(),
        );
        let mut buff = ser.into_serializer().serialize_vec_outer().unwrap();
        receive_igmp_packet(ctx, device, ROUTER_ADDR, MY_ADDR, buff);
    }

    fn receive_igmp_report<D: EventDispatcher>(ctx: &mut Context<D>, device: DeviceId) {
        let ser = IgmpPacketBuilder::<Buf<Vec<u8>>, IgmpMembershipReportV2>::new(GROUP_ADDR);
        let mut buff = ser.into_serializer().serialize_vec_outer().unwrap();
        receive_igmp_packet(ctx, device, OTHER_HOST_ADDR, MY_ADDR, buff);
    }

    fn setup_simple_test_environment() -> (Context<DummyEventDispatcher>, DeviceId) {
        let mut ctx = Context::with_default_state(DummyEventDispatcher::default());
        let dev_id = ctx.state.add_ethernet_device(Mac::new([1, 2, 3, 4, 5, 6]), 1500);
        crate::device::initialize_device(&mut ctx, dev_id);
        set_ip_addr_subnet(&mut ctx, dev_id.id(), AddrSubnet::new(MY_ADDR, 24).unwrap());
        (ctx, dev_id)
    }

    fn ensure_ttl_ihl_rtr(ctx: &mut Context<DummyEventDispatcher>) {
        for (_, frame) in ctx.dispatcher.frames_sent() {
            assert_eq!(frame[22], 1); // TTL,
            assert_eq!(&frame[34..38], &[148, 4, 0, 0]); // RTR
            assert_eq!(frame[14], 0x46); // IHL
        }
    }

    #[test]
    fn test_igmp_simple_integration() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        igmp_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());

        receive_igmp_query(&mut ctx, dev_id, Duration::from_secs(10));
        assert!(testutil::trigger_next_timer(&mut ctx));

        // we should get two Igmpv2 reports, one for the unsolicited one for the host
        // to turn into Delay Member state and the other one for the timer being fired.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
    }

    #[test]
    fn test_igmp_integration_fallback_from_idle() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        igmp_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);

        assert!(testutil::trigger_next_timer(&mut ctx));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);

        receive_igmp_query(&mut ctx, dev_id, Duration::from_secs(10));

        // We have received a query, hence we are falling back to Delay Member state.
        let group_state = ctx
            .state
            .ip
            .get_igmp_state_mut(dev_id.id())
            .groups
            .get(&MulticastAddr::new(GROUP_ADDR).unwrap())
            .unwrap();
        match group_state.get_inner() {
            MemberState::Delaying(_) => {}
            _ => panic!("Wrong State!"),
        }

        assert!(testutil::trigger_next_timer(&mut ctx));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 3);

        let mac: [u8; 6] = [0x01, 0x00, 0x5e, 0, 0, 3];
        for (_, frame) in ctx.dispatcher.frames_sent() {
            assert_eq!(frame[0..6], mac[..]);
        }
        ensure_ttl_ihl_rtr(&mut ctx);
    }

    #[test]
    fn test_igmp_integration_igmpv1_router_present() {
        let (mut ctx, dev_id) = setup_simple_test_environment();

        igmp_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);
        let instant1 = ctx.dispatcher.timer_events().nth(0).unwrap().0.clone();

        receive_igmp_query(&mut ctx, dev_id, Duration::from_secs(0));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);

        // Since we have heard from the v1 router, we should have set our flag
        let group_state = ctx
            .state
            .ip
            .get_igmp_state_mut(dev_id.id())
            .groups
            .get(&MulticastAddr::new(GROUP_ADDR).unwrap())
            .unwrap();
        match group_state.get_inner() {
            MemberState::Delaying(state) => {
                assert!(state.get_protocol_specific().v1_router_present)
            }
            _ => panic!("Wrong State!"),
        }

        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        // Two timers: one for the delayed report, one for the v1 router timer
        assert_eq!(ctx.dispatcher.timer_events().count(), 2);
        let instant2 = ctx.dispatcher.timer_events().nth(0).unwrap().0.clone();
        assert_eq!(instant1, instant2);

        assert!(testutil::trigger_next_timer(&mut ctx));
        // After the first timer, we send out our V1 report.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
        // the last frame being sent should be a V1 report.
        let (_, frame) = ctx.dispatcher.frames_sent().last().unwrap();
        // 34 and 0x12 are hacky but they can quickly tell it is a V1 report.
        assert_eq!(frame[38], 0x12);

        assert!(testutil::trigger_next_timer(&mut ctx));
        // After the second timer, we should reset our flag for v1 routers.
        let group_state = ctx
            .state
            .ip
            .get_igmp_state_mut(dev_id.id())
            .groups
            .get(&MulticastAddr::new(GROUP_ADDR).unwrap())
            .unwrap();
        match group_state.get_inner() {
            MemberState::Idle(state) => assert!(!state.get_protocol_specific().v1_router_present),
            _ => panic!("Wrong State!"),
        }

        receive_igmp_query(&mut ctx, dev_id, Duration::from_secs(10));
        assert!(testutil::trigger_next_timer(&mut ctx));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 3);
        // Now we should get V2 report
        assert_eq!(ctx.dispatcher.frames_sent().last().unwrap().1[38], 0x16);
        ensure_ttl_ihl_rtr(&mut ctx);
    }

    // TODO(zeling): add this test back once we can have a reliable and
    // deterministic way to make `duration` longer than 100ms.
    #[test]
    #[ignore]
    fn test_igmp_integration_delay_reset_timer() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        igmp_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);
        let instant1 = ctx.dispatcher.timer_events().nth(0).unwrap().0.clone();
        let start = ctx.dispatcher.now();
        let duration = Duration::from_micros(((instant1 - start).as_micros() / 2) as u64);
        if duration.as_millis() < 100 {
            return;
        }
        receive_igmp_query(&mut ctx, dev_id, duration);
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        let instant2 = ctx.dispatcher.timer_events().nth(0).unwrap().0.clone();
        // because of the message, our timer should be reset to a nearer future
        assert!(instant2 <= instant1);
        assert!(trigger_next_timer(&mut ctx));
        assert!(ctx.dispatcher.now() - start <= duration);
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
        // make sure it is a V2 report
        assert_eq!(ctx.dispatcher.frames_sent().last().unwrap().1[38], 0x16);
        ensure_ttl_ihl_rtr(&mut ctx);
    }

    #[test]
    fn test_igmp_integration_last_send_leave() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        igmp_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);
        // The initial unsolicited report
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        assert!(testutil::trigger_next_timer(&mut ctx));
        // The report after the delay
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
        assert!(igmp_leave_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap()).is_ok());
        // our leave message
        assert_eq!(ctx.dispatcher.frames_sent().len(), 3);

        let leave_frame = &ctx.dispatcher.frames_sent().last().unwrap().1;

        // make sure it is a leave message
        assert_eq!(leave_frame[38], 0x17);
        // and the destination is ALL-ROUTERS (224.0.0.2)
        assert_eq!(leave_frame[30], 224);
        assert_eq!(leave_frame[31], 0);
        assert_eq!(leave_frame[32], 0);
        assert_eq!(leave_frame[33], 2);
        ensure_ttl_ihl_rtr(&mut ctx);
    }

    #[test]
    fn test_igmp_integration_not_last_dont_send_leave() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        igmp_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        receive_igmp_report(&mut ctx, dev_id);
        assert_eq!(ctx.dispatcher.timer_events().count(), 0);
        // The report should be discarded because we have received from someone else.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        assert!(igmp_leave_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap()).is_ok());
        // A leave message is not sent
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        ensure_ttl_ihl_rtr(&mut ctx);
    }

    #[test]
    fn test_receive_general_query() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        igmp_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        igmp_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR_2).unwrap());
        assert_eq!(ctx.dispatcher.timer_events().count(), 2);
        // The initial unsolicited report
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
        assert!(trigger_next_timer(&mut ctx));
        assert!(trigger_next_timer(&mut ctx));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 4);
        receive_igmp_general_query(&mut ctx, dev_id, Duration::from_secs(10));
        // Two new timers should be there.
        assert_eq!(ctx.dispatcher.timer_events().count(), 2);
        assert!(trigger_next_timer(&mut ctx));
        assert!(trigger_next_timer(&mut ctx));
        // Two new reports should be sent
        assert_eq!(ctx.dispatcher.frames_sent().len(), 6);
        ensure_ttl_ihl_rtr(&mut ctx);
    }
}
