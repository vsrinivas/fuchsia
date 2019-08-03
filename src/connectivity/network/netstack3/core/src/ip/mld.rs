// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Multicast Listener Discovery
//!
//! Multicast Listener Discovery (MLD) is derived from version 2 of IPv4's
//! Internet Group Management Protocol, IGMPv2. One important difference
//! to note is that MLD uses ICMPv6 (IP Protocol 58) message types,
//! rather than IGMP (IP Protocol 2) message types.

use std::collections::HashMap;
use std::result::Result;
use std::time::Duration;

use failure::Fail;
use log::{debug, error};
use net_types::ip::AddrSubnet;
use net_types::{LinkLocalAddress, MulticastAddr};
use packet::serialize::Serializer;
use packet::InnerPacketBuilder;
use rand::Rng;
use zerocopy::ByteSlice;

use crate::ip::gmp::{Action, Actions, GmpAction, GmpStateMachine, ProtocolSpecific};
use crate::ip::types::IpProto;
use crate::ip::{Ip, IpAddress, IpLayerTimerId, Ipv6, Ipv6Addr};
use crate::wire::icmp::mld::{
    IcmpMldv1MessageType, Mldv1Body, Mldv1MessageBuilder, MulticastListenerDone,
    MulticastListenerReport,
};
use crate::wire::icmp::{IcmpPacketBuilder, IcmpUnusedCode, Icmpv6Packet};
use crate::wire::ipv6::Ipv6PacketBuilder;
use crate::Instant;
use crate::{Context, DeviceId, EventDispatcher};
use crate::{TimerId, TimerIdInner};

/// Receive an MLD message in an ICMPv6 packet.
pub(crate) fn receive_mld_packet<D: EventDispatcher, B: ByteSlice>(
    ctx: &mut Context<D>,
    device: DeviceId,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
    packet: Icmpv6Packet<B>,
) {
    if let Err(e) = match packet {
        Icmpv6Packet::MulticastListenerQuery(msg) => {
            let now = ctx.dispatcher().now();
            let max_response_delay: Duration = msg.body().max_response_delay();
            handle_mld_message(ctx, device, msg.body(), |rng, state| {
                state.query_received(rng, max_response_delay, now)
            })
        }
        Icmpv6Packet::MulticastListenerReport(msg) => {
            handle_mld_message(ctx, device, msg.body(), |_, state| state.report_received())
        }
        Icmpv6Packet::MulticastListenerDone(_) => {
            debug!("Hosts are not interested in Done messages");
            return;
        }
        _ => {
            unreachable!("It is not an MLD packet");
        }
    } {
        error!("Error occurred when handling MLD message: {}", e);
    }
}

fn handle_mld_message<B, D, F>(
    ctx: &mut Context<D>,
    device: DeviceId,
    body: &Mldv1Body<B>,
    handler: F,
) -> MldResult<()>
where
    B: ByteSlice,
    D: EventDispatcher,
    F: Fn(&mut D::Rng, &mut MldGroupState<D::Instant>) -> Actions<MldProtocolSpecific>,
{
    let (state, dispatcher) = ctx.state_and_dispatcher();
    let group_addr = body.group_addr;
    if group_addr.is_unspecified() {
        let addr_and_actions = state
            .ip
            .get_mld_state_mut(device.id())
            .groups
            .iter_mut()
            .map(|(addr, state)| (addr.clone(), handler(dispatcher.rng(), state)))
            .collect::<Vec<_>>();
        // `addr` must be a multicast address, otherwise it will not have
        // an associated state in the first place
        for (addr, actions) in addr_and_actions {
            run_actions(ctx, device, actions, addr);
        }
        Ok(())
    } else if let Some(group_addr) = MulticastAddr::new(group_addr) {
        let actions = match state.ip.get_mld_state_mut(device.id()).groups.get_mut(&group_addr) {
            Some(state) => handler(dispatcher.rng(), state),
            None => return Err(MldError::NotAMember { addr: group_addr.get() }),
        };
        run_actions(ctx, device, actions, group_addr);
        Ok(())
    } else {
        Err(MldError::NotAMember { addr: group_addr })
    }
}

#[derive(Debug, Fail)]
pub(crate) enum MldError {
    /// The host is trying to operate on an group address of which the host is not a member.
    #[fail(display = "the host has not already been a member of the address: {}", addr)]
    NotAMember { addr: Ipv6Addr },
    /// Failed to send an IGMP packet.
    #[fail(display = "failed to send out an IGMP packet to address: {}", addr)]
    SendFailure { addr: Ipv6Addr },
}

pub(crate) type MldResult<T> = Result<T, MldError>;

#[derive(PartialEq, Eq, Clone, Copy, Default, Debug)]
pub(crate) struct MldProtocolSpecific;

pub(crate) struct MldConfig {
    unsolicited_report_interval: Duration,
    send_leave_anyway: bool,
}

#[derive(PartialEq, Eq, Clone, Copy, Debug)]
pub(crate) struct ImmediateIdleState;

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

/// The state on a multicast address.
pub(crate) type MldGroupState<I> = GmpStateMachine<I, MldProtocolSpecific>;

/// The States on all the interested multicast address of an interface.
pub(crate) struct MldInterface<I: Instant> {
    groups: HashMap<MulticastAddr<Ipv6Addr>, MldGroupState<I>>,
}

impl<I: Instant> Default for MldInterface<I> {
    fn default() -> Self {
        MldInterface { groups: HashMap::new() }
    }
}

impl<I: Instant> MldInterface<I> {
    fn join_group<R: Rng>(
        &mut self,
        rng: &mut R,
        addr: MulticastAddr<Ipv6Addr>,
        now: I,
    ) -> Actions<MldProtocolSpecific> {
        self.groups.entry(addr).or_insert(MldGroupState::default()).join_group(rng, now)
    }

    fn leave_group(
        &mut self,
        addr: MulticastAddr<Ipv6Addr>,
    ) -> MldResult<Actions<MldProtocolSpecific>> {
        match self.groups.remove(&addr).as_mut() {
            Some(state) => Ok(state.leave_group()),
            None => Err(MldError::NotAMember { addr: addr.get() }),
        }
    }

    fn report_timer_expired(&mut self, addr: MulticastAddr<Ipv6Addr>) -> MldResult<()> {
        match self.groups.get_mut(&addr) {
            Some(state) => {
                state.report_timer_expired();
                Ok(())
            }
            None => Err(MldError::NotAMember { addr: addr.get() }),
        }
    }
}

/// Make our host join a multicast group.
pub(crate) fn mld_join_group<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    group_addr: MulticastAddr<Ipv6Addr>,
) {
    let (state, dispatcher) = ctx.state_and_dispatcher();
    let now = dispatcher.now();
    let actions =
        state.ip.get_mld_state_mut(device.id()).join_group(dispatcher.rng(), group_addr, now);
    // actions will be `Nothing` if the the host is not in the `NonMember` state.
    run_actions(ctx, device, actions, group_addr);
}

/// Make our host leave the multicast group.
///
/// If our host is not already a member of the given address, this will result
/// in the `IgmpError::NotAMember` error.
pub(crate) fn mld_leave_group<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    group_addr: MulticastAddr<Ipv6Addr>,
) -> MldResult<()> {
    let actions = ctx.state_mut().ip.get_mld_state_mut(device.id()).leave_group(group_addr)?;
    run_actions(ctx, device, actions, group_addr);
    Ok(())
}

/// The timer to delay the MLD report.
#[derive(PartialEq, Eq, Clone, Copy, Debug, Hash)]
pub(crate) struct MldReportDelay {
    device: DeviceId,
    group_addr: MulticastAddr<Ipv6Addr>,
}

impl MldReportDelay {
    fn new(device: DeviceId, group_addr: MulticastAddr<Ipv6Addr>) -> TimerId {
        MldReportDelay { device, group_addr }.into()
    }
}

impl From<MldReportDelay> for TimerId {
    fn from(id: MldReportDelay) -> Self {
        TimerId(TimerIdInner::IpLayer(IpLayerTimerId::MldTimer(id)))
    }
}

fn run_actions<D>(
    ctx: &mut Context<D>,
    device: DeviceId,
    actions: Actions<MldProtocolSpecific>,
    group_addr: MulticastAddr<Ipv6Addr>,
) where
    D: EventDispatcher,
{
    for action in actions {
        if let Err(err) = run_action(ctx, device, action, group_addr) {
            error!("Error performing action on {} on device {}: {}", group_addr, device, err);
        }
    }
}

/// Interpret the actions generated by the state machine.
fn run_action<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device: DeviceId,
    action: Action<MldProtocolSpecific>,
    group_addr: MulticastAddr<Ipv6Addr>,
) -> MldResult<()> {
    match action {
        Action::Generic(GmpAction::ScheduleReportTimer(delay)) => {
            ctx.dispatcher.schedule_timeout(delay, MldReportDelay::new(device, group_addr));
            Ok(())
        }
        Action::Generic(GmpAction::StopReportTimer) => {
            ctx.dispatcher.cancel_timeout(MldReportDelay::new(device, group_addr));
            Ok(())
        }
        Action::Generic(GmpAction::SendLeave) => send_mld_packet::<&[u8], _, _>(
            ctx,
            device,
            MulticastAddr::new(crate::ip::IPV6_ALL_ROUTERS).unwrap(),
            MulticastListenerDone,
            group_addr,
            (),
        ),
        Action::Generic(GmpAction::SendReport(_)) => send_mld_packet::<&[u8], _, _>(
            ctx,
            device,
            group_addr,
            MulticastListenerReport,
            group_addr,
            (),
        ),
        Action::Specific(ImmediateIdleState) => {
            ctx.dispatcher.cancel_timeout(MldReportDelay::new(device, group_addr));
            ctx.state_mut().ip.get_mld_state_mut(device.id()).report_timer_expired(group_addr)
        }
    }
}

/// Send an MLD packet.
///
/// The MLD packet being sent should have its `hop_limit` to be 1 and
/// a `RouterAlert` option in its Hop-by-Hop Options extensions header.
fn send_mld_packet<B: ByteSlice, D: EventDispatcher, M: IcmpMldv1MessageType<B>>(
    ctx: &mut Context<D>,
    device: DeviceId,
    dst_ip: MulticastAddr<Ipv6Addr>,
    msg: M,
    group_addr: M::GroupAddr,
    max_resp_delay: M::MaxRespDelay,
) -> MldResult<()> {
    // According to https://tools.ietf.org/html/rfc3590#section-4,
    // if a valid link-local address is not available for the device (e.g., one
    // has not been configured), the message is sent with the unspecified
    // address (::) as the IPv6 source address.
    // NOTE: currently `get_ip_addr_subnet` is not returning the MAC generated link
    // local address, so we're probably just always using UNSPECIFIED_ADDRESS.
    let src_ip = crate::device::get_ip_addr_subnet(ctx, device).map_or(
        Ipv6::UNSPECIFIED_ADDRESS,
        |x: AddrSubnet<Ipv6Addr>| {
            let addr = x.addr();
            if addr.is_linklocal() {
                addr
            } else {
                Ipv6::UNSPECIFIED_ADDRESS
            }
        },
    );
    let body = Mldv1MessageBuilder::<M>::new_with_max_resp_delay(group_addr, max_resp_delay)
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::new(src_ip, dst_ip.get(), IcmpUnusedCode, msg))
        .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip.get(), 1, IpProto::Icmpv6));
    // TODO: set a Hop-by-hop Router Alert option.
    crate::device::send_ip_frame(ctx, device, dst_ip.get(), body)
        .map_err(|_| MldError::SendFailure { addr: group_addr.into() })
}

pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, timer: MldReportDelay) {
    let MldReportDelay { device, group_addr } = timer;
    send_mld_packet::<&[u8], _, _>(
        ctx,
        device,
        group_addr,
        MulticastListenerReport,
        group_addr,
        (),
    );
    if let Err(e) =
        ctx.state_mut().ip.get_mld_state_mut(device.id()).report_timer_expired(group_addr)
    {
        error!("MLD timer fired, but an error has occurred: {}", e);
    }
}

#[cfg(test)]
mod tests {

    use std::convert::TryInto;
    use std::time::Instant;

    use net_types::ethernet::Mac;

    use super::*;
    use crate::device::set_ip_addr_subnet;
    use crate::ip::gmp::{Action, GmpAction, MemberState};
    use crate::ip::icmp::receive_icmp_packet;
    use crate::ip::IPV6_ALL_ROUTERS;
    use crate::testutil;
    use crate::testutil::new_rng;
    use crate::testutil::{DummyEventDispatcher, DummyEventDispatcherBuilder};
    use crate::wire::icmp::mld::MulticastListenerQuery;

    #[test]
    fn test_mld_immediate_report() {
        // Most of the test surface is covered by the GMP implementation,
        // MLD specific part is mostly passthrough. This test case is here
        // because MLD allows a router to ask for report immediately, by
        // specifying the `MaxRespDelay` to be 0. If this is the case, the
        // host should send the report immediately instead of setting a timer.
        let mut s = MldGroupState::default();
        let mut rng = new_rng(0);
        s.join_group(&mut rng, Instant::now());
        let actions = s.query_received(&mut rng, Duration::from_secs(0), Instant::now());
        let vec = actions.into_iter().collect::<Vec<Action<_>>>();
        assert_eq!(vec.len(), 2);
        assert_eq!(vec[0], Action::Generic(GmpAction::SendReport(MldProtocolSpecific)));
        assert_eq!(vec[1], Action::Specific(ImmediateIdleState));
    }

    const MY_MAC: Mac = Mac::new([1, 2, 3, 4, 5, 6]);
    const ROUTER_MAC: Mac = Mac::new([6, 5, 4, 3, 2, 1]);
    const GROUP_ADDR: Ipv6Addr =
        Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3]);

    fn receive_mld_query(
        ctx: &mut Context<DummyEventDispatcher>,
        device: DeviceId,
        resp_time: Duration,
    ) {
        let my_addr = MY_MAC.to_ipv6_link_local().get();
        let router_addr = ROUTER_MAC.to_ipv6_link_local().get();
        let buffer = Mldv1MessageBuilder::<MulticastListenerQuery>::new_with_max_resp_delay(
            GROUP_ADDR,
            resp_time.try_into().unwrap(),
        )
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::<_, &[u8], _>::new(
            router_addr,
            my_addr,
            IcmpUnusedCode,
            MulticastListenerQuery,
        ))
        .serialize_vec_outer()
        .unwrap();
        receive_icmp_packet(ctx, Some(device), router_addr, my_addr, buffer);
    }

    fn receive_mld_report(ctx: &mut Context<DummyEventDispatcher>, device: DeviceId) {
        let my_addr = MY_MAC.to_ipv6_link_local().get();
        let router_addr = ROUTER_MAC.to_ipv6_link_local().get();
        let buffer = Mldv1MessageBuilder::<MulticastListenerReport>::new(
            MulticastAddr::new(GROUP_ADDR).unwrap(),
        )
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::<_, &[u8], _>::new(
            router_addr,
            my_addr,
            IcmpUnusedCode,
            MulticastListenerReport,
        ))
        .serialize_vec_outer()
        .unwrap();
        receive_icmp_packet(ctx, Some(device), router_addr, my_addr, buffer);
    }

    fn setup_simple_test_environment() -> (Context<DummyEventDispatcher>, DeviceId) {
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let dev_id = ctx.state.add_ethernet_device(MY_MAC, 1500);
        crate::device::initialize_device(&mut ctx, dev_id);
        set_ip_addr_subnet(
            &mut ctx,
            dev_id,
            AddrSubnet::new(MY_MAC.to_ipv6_link_local().get(), 128).unwrap(),
        );
        (ctx, dev_id)
    }

    // ensure the ttl is 1.
    fn ensure_ttl(frame: &[u8]) {
        assert_eq!(frame[21], 1);
    }

    fn ensure_slice_addr(frame: &[u8], start: usize, end: usize, ip: Ipv6Addr) {
        let mut bytes = [0u8; 16];
        bytes.copy_from_slice(&frame[start..end]);
        assert_eq!(Ipv6Addr::new(bytes), ip);
    }

    // ensure the destination address field in the ICMPv6 packet is correct.
    fn ensure_dst_addr(frame: &[u8], ip: Ipv6Addr) {
        ensure_slice_addr(frame, 38, 54, ip);
    }

    // ensure the multicast address field in the MLD packet is correct.
    fn ensure_multicast_addr(frame: &[u8], ip: Ipv6Addr) {
        ensure_slice_addr(frame, 62, 78, ip);
    }

    // ensure a sent frame meets the requirement
    fn ensure_frame(frame: &[u8], op: u8, dst: Ipv6Addr, multicast: Ipv6Addr) {
        ensure_ttl(frame);
        assert_eq!(frame[54], op);
        ensure_ttl(&frame[..]);
        ensure_dst_addr(&frame[..], dst);
        ensure_multicast_addr(&frame[..], multicast);
    }

    #[test]
    fn test_mld_simple_integration() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());

        receive_mld_query(&mut ctx, dev_id, Duration::from_secs(10));
        assert!(testutil::trigger_next_timer(&mut ctx));

        let frame = &ctx.dispatcher.frames_sent().get(0).unwrap().1;

        // we should get two MLD reports, one for the unsolicited one for the host
        // to turn into Delay Member state and the other one for the timer being fired.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
        // The frames are all reports.
        for (_, frame) in ctx.dispatcher.frames_sent() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
        }
    }

    #[test]
    fn test_mld_immediate_query() {
        testutil::set_logger_for_test();
        let (mut ctx, dev_id) = setup_simple_test_environment();
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);

        receive_mld_query(&mut ctx, dev_id, Duration::from_secs(0));
        // the query says that it wants to hear from us immediately
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
        // there should be no timers set
        assert!(!testutil::trigger_next_timer(&mut ctx));
        // The frames are all reports.
        for (_, frame) in ctx.dispatcher.frames_sent() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
        }
    }

    #[test]
    fn test_mld_integration_fallback_from_idle() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);

        assert!(testutil::trigger_next_timer(&mut ctx));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);

        receive_mld_query(&mut ctx, dev_id, Duration::from_secs(10));

        // We have received a query, hence we are falling back to Delay Member state.
        let group_state = ctx
            .state
            .ip
            .get_mld_state_mut(dev_id.id())
            .groups
            .get(&MulticastAddr::new(GROUP_ADDR).unwrap())
            .unwrap();
        match group_state.get_inner() {
            MemberState::Delaying(_) => {}
            _ => panic!("Wrong State!"),
        }

        assert!(testutil::trigger_next_timer(&mut ctx));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 3);
        // The frames are all reports.
        for (_, frame) in ctx.dispatcher.frames_sent() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
        }
    }

    #[test]
    fn test_mld_integration_immediate_query_wont_fallback() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);

        assert!(testutil::trigger_next_timer(&mut ctx));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);

        receive_mld_query(&mut ctx, dev_id, Duration::from_secs(0));

        // Since it is an immediate query, we will send a report immediately and turn
        // into Idle state again
        let group_state = ctx
            .state
            .ip
            .get_mld_state_mut(dev_id.id())
            .groups
            .get(&MulticastAddr::new(GROUP_ADDR).unwrap())
            .unwrap();
        match group_state.get_inner() {
            MemberState::Idle(_) => {}
            _ => panic!("Wrong State!"),
        }

        // No timers!
        assert!(!testutil::trigger_next_timer(&mut ctx));
        assert_eq!(ctx.dispatcher.frames_sent().len(), 3);
        // The frames are all reports.
        for (_, frame) in ctx.dispatcher.frames_sent() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
        }
    }

    // TODO: It seems like the RNG is always giving us quite small values, so the test logic
    // for the timers doesn't hold. Ignore the test for now until we have a way to reliably
    // generate a larger value for duration.
    #[test]
    #[ignore]
    fn test_mld_integration_delay_reset_timer() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);
        let instant1 = ctx.dispatcher.timer_events().nth(0).unwrap().0.clone();
        let start = ctx.dispatcher.now();
        let duration = instant1 - start;

        receive_mld_query(&mut ctx, dev_id, duration);
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
        // The frames are all reports.
        for (_, frame) in ctx.dispatcher.frames_sent() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
        }
    }

    #[test]
    fn test_mld_integration_last_send_leave() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);
        // The initial unsolicited report
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        assert!(testutil::trigger_next_timer(&mut ctx));
        // The report after the delay
        assert_eq!(ctx.dispatcher.frames_sent().len(), 2);
        assert!(mld_leave_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap()).is_ok());
        // Our leave message
        assert_eq!(ctx.dispatcher.frames_sent().len(), 3);
        // The first two messages should be reports
        ensure_frame(&ctx.dispatcher().frames_sent()[0].1, 131, GROUP_ADDR, GROUP_ADDR);
        ensure_frame(&ctx.dispatcher().frames_sent()[1].1, 131, GROUP_ADDR, GROUP_ADDR);
        // The last one should be the done message whose destination is all routers.
        ensure_frame(&ctx.dispatcher().frames_sent()[2].1, 132, IPV6_ALL_ROUTERS, GROUP_ADDR);
    }

    #[test]
    fn test_mld_integration_not_last_dont_send_leave() {
        let (mut ctx, dev_id) = setup_simple_test_environment();
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert_eq!(ctx.dispatcher.timer_events().count(), 1);
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        receive_mld_report(&mut ctx, dev_id);
        assert_eq!(ctx.dispatcher.timer_events().count(), 0);
        // The report should be discarded because we have received from someone else.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        assert!(mld_leave_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap()).is_ok());
        // A leave message is not sent
        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);
        // The frames are all reports.
        for (_, frame) in ctx.dispatcher.frames_sent() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
        }
    }

    #[test]
    fn test_mld_unspecified_src_no_addr() {
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let dev_id = ctx.state.add_ethernet_device(MY_MAC, 1500);
        crate::device::initialize_device(&mut ctx, dev_id);
        // The IP address of the device is intentionally unspecified.
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert!(testutil::trigger_next_timer(&mut ctx));
        for (_, frame) in ctx.dispatcher.frames_sent() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 22, 38, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }

    #[test]
    fn test_mld_unspecified_src_not_link_local() {
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let dev_id = ctx.state.add_ethernet_device(MY_MAC, 1500);
        crate::device::initialize_device(&mut ctx, dev_id);
        set_ip_addr_subnet(
            &mut ctx,
            dev_id,
            AddrSubnet::new(
                // set up a site-local address
                Ipv6Addr::new([0xfe, 0xf0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]),
                128,
            )
            .unwrap(),
        );
        // The IP address of the device is intentionally unspecified.
        mld_join_group(&mut ctx, dev_id, MulticastAddr::new(GROUP_ADDR).unwrap());
        assert!(testutil::trigger_next_timer(&mut ctx));
        for (_, frame) in ctx.dispatcher.frames_sent() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 22, 38, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }
}
