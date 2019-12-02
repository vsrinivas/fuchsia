// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Multicast Listener Discovery (MLD).
//!
//! MLD is derived from version 2 of IPv4's Internet Group Management Protocol,
//! IGMPv2. One important difference to note is that MLD uses ICMPv6 (IP
//! Protocol 58) message types, rather than IGMP (IP Protocol 2) message types.

use std::collections::HashMap;
use std::time::Duration;

use failure::Fail;
use log::{debug, error};
use net_types::ip::{Ip, Ipv6, Ipv6Addr};
use net_types::{LinkLocalAddr, MulticastAddr, SpecifiedAddr, SpecifiedAddress, Witness};
use packet::serialize::Serializer;
use packet::{EmptyBuf, InnerPacketBuilder};
#[cfg(test)]
use rand::Rng;
use rand_xorshift::XorShiftRng;
use zerocopy::ByteSlice;

use crate::context::{
    FrameContext, InstantContext, RngContext, RngContextExt, StateContext, TimerContext,
};
use crate::ip::gmp::{Action, Actions, GmpAction, GmpStateMachine, ProtocolSpecific};
use crate::ip::{IpDeviceIdContext, IpProto};
use crate::wire::icmp::mld::{
    IcmpMldv1MessageType, Mldv1Body, Mldv1MessageBuilder, MulticastListenerDone,
    MulticastListenerReport,
};
use crate::wire::icmp::{mld::MldPacket, IcmpPacketBuilder, IcmpUnusedCode};
use crate::wire::ipv6::ext_hdrs::{
    ExtensionHeaderOptionAction, HopByHopOption, HopByHopOptionData,
};
use crate::wire::ipv6::{Ipv6PacketBuilder, Ipv6PacketBuilderWithHBHOptions};
use crate::Instant;

/// Metadata for sending an MLD packet in an IP packet.
///
/// `MldFrameMetadata` is used by [`MldContext`]'s [`FrameContext`] bound. When
/// [`FrameContext::send_frame`] is called with an `MldFrameMetadata`, the body
/// contains an MLD packet in an IP packet. It is encapsulated in a link-layer
/// frame, and sent to the link-layer address corresponding to the given local
/// IP address.
pub(crate) struct MldFrameMetadata<D> {
    pub(crate) device: D,
    pub(crate) local_ip: MulticastAddr<Ipv6Addr>,
}

impl<D> MldFrameMetadata<D> {
    fn new(device: D, local_ip: MulticastAddr<Ipv6Addr>) -> MldFrameMetadata<D> {
        MldFrameMetadata { device, local_ip }
    }
}

/// The execution context for the Multicast Listener Discovery (MLD) protocol.
pub(crate) trait MldContext:
    IpDeviceIdContext
    + TimerContext<MldReportDelay<<Self as IpDeviceIdContext>::DeviceId>>
    + RngContext
    + StateContext<
        MldInterface<<Self as InstantContext>::Instant>,
        <Self as IpDeviceIdContext>::DeviceId,
    > + FrameContext<EmptyBuf, MldFrameMetadata<<Self as IpDeviceIdContext>::DeviceId>>
{
    /// Gets the IPv6 link local address on `device`.
    fn get_ipv6_link_local_addr(&self, device: Self::DeviceId) -> Option<LinkLocalAddr<Ipv6Addr>>;
}

/// A handler for incoming MLD packets.
///
/// `MldHandler` is implemented by any type which also implements
/// [`MldContext`], and it can also be mocked for use in testing.
pub(crate) trait MldHandler: IpDeviceIdContext {
    /// Receive an MLD packet.
    fn receive_mld_packet<B: ByteSlice>(
        &mut self,
        device: Self::DeviceId,
        src_ip: Ipv6Addr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: MldPacket<B>,
    );
}

impl<C: MldContext> MldHandler for C {
    fn receive_mld_packet<B: ByteSlice>(
        &mut self,
        device: Self::DeviceId,
        _src_ip: Ipv6Addr,
        _dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: MldPacket<B>,
    ) {
        if let Err(e) = match packet {
            MldPacket::MulticastListenerQuery(msg) => {
                let now = self.now();
                let max_response_delay: Duration = msg.body().max_response_delay();
                handle_mld_message(self, device, msg.body(), |rng, state| {
                    state.query_received(rng, max_response_delay, now)
                })
            }
            MldPacket::MulticastListenerReport(msg) => {
                handle_mld_message(self, device, msg.body(), |_, state| state.report_received())
            }
            MldPacket::MulticastListenerDone(_) => {
                debug!("Hosts are not interested in Done messages");
                return;
            }
        } {
            error!("Error occurred when handling MLD message: {}", e);
        }
    }
}

fn handle_mld_message<C: MldContext, B: ByteSlice, F>(
    ctx: &mut C,
    device: C::DeviceId,
    body: &Mldv1Body<B>,
    handler: F,
) -> MldResult<()>
where
    F: Fn(&mut XorShiftRng, &mut MldGroupState<C::Instant>) -> Actions<MldProtocolSpecific>,
{
    // TODO(joshlf): Once we figure out how to access the RNG and the state at
    // the same time, get rid of this hack. For the time being, this is probably
    // fine because, while the `XorShiftRng` isn't cryptographically secure, its
    // seed is, which means that, at worst, an attacker will be able to
    // correlate events generated during this one function call.
    let mut rng = ctx.new_xorshift_rng();
    let group_addr = body.group_addr;
    if !group_addr.is_specified() {
        let addr_and_actions = ctx
            .get_state_mut_with(device)
            .groups
            .iter_mut()
            .map(|(addr, state)| (addr.clone(), handler(&mut rng, state)))
            .collect::<Vec<_>>();
        // `addr` must be a multicast address, otherwise it will not have
        // an associated state in the first place
        for (addr, actions) in addr_and_actions {
            run_actions(ctx, device, actions, addr);
        }
        Ok(())
    } else if let Some(group_addr) = MulticastAddr::new(group_addr) {
        let actions = match ctx.get_state_mut_with(device).groups.get_mut(&group_addr) {
            Some(state) => handler(&mut rng, state),
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
        _cfg: &Self::Config,
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
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    fn join_group<R: Rng>(
        &mut self,
        rng: &mut R,
        addr: MulticastAddr<Ipv6Addr>,
        now: I,
    ) -> Actions<MldProtocolSpecific> {
        self.groups.entry(addr).or_insert(MldGroupState::default()).join_group(rng, now)
    }

    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
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
// TODO(rheacock): remove `#[cfg(test)]` when this is used.
#[cfg(test)]
pub(crate) fn mld_join_group<C: MldContext>(
    ctx: &mut C,
    device: C::DeviceId,
    group_addr: MulticastAddr<Ipv6Addr>,
) {
    // TODO(joshlf): Once we figure out how to access the RNG and the state at
    // the same time, get rid of this hack. For the time being, this is probably
    // fine because, while the `XorShiftRng` isn't cryptographically secure, its
    // seed is, which means that, at worst, an attacker will be able to
    // correlate events generated during this one function call.
    let mut rng = ctx.new_xorshift_rng();
    let now = ctx.now();
    let actions = ctx.get_state_mut_with(device).join_group(&mut rng, group_addr, now);
    // actions will be `Nothing` if the the host is not in the `NonMember` state.
    run_actions(ctx, device, actions, group_addr);
}

/// Make our host leave the multicast group.
///
/// If our host is not already a member of the given address, this will result
/// in the `IgmpError::NotAMember` error.
// TODO(rheacock): remove `#[cfg(test)]` when this is used.
#[cfg(test)]
pub(crate) fn mld_leave_group<C: MldContext>(
    ctx: &mut C,
    device: C::DeviceId,
    group_addr: MulticastAddr<Ipv6Addr>,
) -> MldResult<()> {
    let actions = ctx.get_state_mut_with(device).leave_group(group_addr)?;
    run_actions(ctx, device, actions, group_addr);
    Ok(())
}

/// The timer to delay the MLD report.
#[derive(PartialEq, Eq, Clone, Copy, Debug, Hash)]
pub(crate) struct MldReportDelay<D> {
    device: D,
    group_addr: MulticastAddr<Ipv6Addr>,
}

impl<D> MldReportDelay<D> {
    fn new(device: D, group_addr: MulticastAddr<Ipv6Addr>) -> MldReportDelay<D> {
        MldReportDelay { device, group_addr }.into()
    }
}

fn run_actions<C: MldContext>(
    ctx: &mut C,
    device: C::DeviceId,
    actions: Actions<MldProtocolSpecific>,
    group_addr: MulticastAddr<Ipv6Addr>,
) {
    for action in actions {
        if let Err(err) = run_action(ctx, device, action, group_addr) {
            error!("Error performing action on {} on device {}: {}", group_addr, device, err);
        }
    }
}

/// Interpret the actions generated by the state machine.
fn run_action<C: MldContext>(
    ctx: &mut C,
    device: C::DeviceId,
    action: Action<MldProtocolSpecific>,
    group_addr: MulticastAddr<Ipv6Addr>,
) -> MldResult<()> {
    match action {
        Action::Generic(GmpAction::ScheduleReportTimer(delay)) => {
            ctx.schedule_timer(delay, MldReportDelay::new(device, group_addr));
            Ok(())
        }
        Action::Generic(GmpAction::StopReportTimer) => {
            ctx.cancel_timer(MldReportDelay::new(device, group_addr));
            Ok(())
        }
        Action::Generic(GmpAction::SendLeave) => send_mld_packet::<_, &[u8], _>(
            ctx,
            device,
            MulticastAddr::new(crate::ip::IPV6_ALL_ROUTERS).unwrap(),
            MulticastListenerDone,
            group_addr,
            (),
        ),
        Action::Generic(GmpAction::SendReport(_)) => send_mld_packet::<_, &[u8], _>(
            ctx,
            device,
            group_addr,
            MulticastListenerReport,
            group_addr,
            (),
        ),
        Action::Specific(ImmediateIdleState) => {
            ctx.cancel_timer(MldReportDelay::new(device, group_addr));
            ctx.get_state_mut_with(device).report_timer_expired(group_addr)
        }
    }
}

/// Send an MLD packet.
///
/// The MLD packet being sent should have its `hop_limit` to be 1 and
/// a `RouterAlert` option in its Hop-by-Hop Options extensions header.
fn send_mld_packet<C: MldContext, B: ByteSlice, M: IcmpMldv1MessageType<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    dst_ip: MulticastAddr<Ipv6Addr>,
    msg: M,
    group_addr: M::GroupAddr,
    max_resp_delay: M::MaxRespDelay,
) -> MldResult<()> {
    // According to https://tools.ietf.org/html/rfc3590#section-4,
    // if a valid link-local address is not available for the device (e.g., one
    // has not been configured), the message is sent with the unspecified
    // address (::) as the IPv6 source address.

    let src_ip =
        ctx.get_ipv6_link_local_addr(device).map_or(Ipv6::UNSPECIFIED_ADDRESS, |x| x.get());

    let body = Mldv1MessageBuilder::<M>::new_with_max_resp_delay(group_addr, max_resp_delay)
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::new(src_ip, dst_ip.get(), IcmpUnusedCode, msg))
        .encapsulate(
            Ipv6PacketBuilderWithHBHOptions::new(
                Ipv6PacketBuilder::new(src_ip, dst_ip.get(), 1, IpProto::Icmpv6),
                &[HopByHopOption {
                    action: ExtensionHeaderOptionAction::SkipAndContinue,
                    mutable: false,
                    data: HopByHopOptionData::RouterAlert { data: 0 },
                }],
            )
            .unwrap(),
        );
    ctx.send_frame(MldFrameMetadata::new(device, dst_ip), body)
        .map_err(|_| MldError::SendFailure { addr: group_addr.into() })
}

pub(crate) fn handle_timeout<C: MldContext>(ctx: &mut C, timer: MldReportDelay<C::DeviceId>) {
    let MldReportDelay { device, group_addr } = timer;
    // TODO(rheacock): Handle the case where this returns an error.
    let _ = send_mld_packet::<_, &[u8], _>(
        ctx,
        device,
        group_addr,
        MulticastListenerReport,
        group_addr,
        (),
    );
    if let Err(e) = ctx.get_state_mut_with(device).report_timer_expired(group_addr) {
        error!("MLD timer fired, but an error has occurred: {}", e);
    }
}

#[cfg(test)]
mod tests {
    use std::convert::TryInto;
    use std::time::Instant;

    use net_types::ethernet::Mac;
    use net_types::ip::AddrSubnet;

    use super::*;
    use crate::device::{add_ip_addr_subnet, DeviceId};
    use crate::ip::gmp::{Action, GmpAction, MemberState};
    use crate::ip::icmp::receive_icmpv6_packet;
    use crate::ip::IPV6_ALL_ROUTERS;
    use crate::testutil;
    use crate::testutil::new_rng;
    use crate::testutil::{DummyEventDispatcher, DummyEventDispatcherBuilder};
    use crate::wire::icmp::mld::MulticastListenerQuery;
    use crate::{Context, EventDispatcher};

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

    const MY_IP: SpecifiedAddr<Ipv6Addr> = unsafe {
        SpecifiedAddr::new_unchecked(Ipv6Addr::new([
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 3,
        ]))
    };
    const MY_MAC: Mac = Mac::new([1, 2, 3, 4, 5, 6]);
    const ROUTER_MAC: Mac = Mac::new([6, 5, 4, 3, 2, 1]);
    const GROUP_ADDR: Ipv6Addr =
        Ipv6Addr::new([0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3]);

    fn receive_mld_query(
        ctx: &mut Context<DummyEventDispatcher>,
        device: DeviceId,
        resp_time: Duration,
    ) {
        let my_addr = MY_IP;
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
        receive_icmpv6_packet(ctx, Some(device), router_addr, my_addr, buffer);
    }

    fn receive_mld_report(ctx: &mut Context<DummyEventDispatcher>, device: DeviceId) {
        let my_addr = MY_IP;
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
        receive_icmpv6_packet(ctx, Some(device), router_addr, my_addr, buffer);
    }

    fn setup_simple_test_environment() -> (Context<DummyEventDispatcher>, DeviceId) {
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let dev_id = ctx.state.add_ethernet_device(MY_MAC, 1500);
        crate::device::initialize_device(&mut ctx, dev_id);
        let _ = add_ip_addr_subnet(&mut ctx, dev_id, AddrSubnet::new(MY_IP.get(), 128).unwrap());
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
        ensure_slice_addr(frame, 70, 86, ip);
    }

    // ensure a sent frame meets the requirement
    fn ensure_frame(frame: &[u8], op: u8, dst: Ipv6Addr, multicast: Ipv6Addr) {
        ensure_ttl(frame);
        assert_eq!(frame[62], op);
        // Ensure the length our payload is 32 = 8 (hbh_ext_hdr) + 24 (mld)
        assert_eq!(frame[19], 32);
        // Ensure the next header is our HopByHop Extension Header.
        assert_eq!(frame[20], 0);
        // Ensure the hop limit is 1.
        assert_eq!(frame[21], 1);
        // Ensure there is a RouterAlert HopByHopOption in our sent frame
        assert_eq!(&frame[54..62], &[58, 0, 5, 2, 0, 0, 1, 0]);
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

        let _frame = &ctx.dispatcher.frames_sent().get(0).unwrap().1;

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
        let group_state =
            <Context<_> as StateContext<MldInterface<_>, _>>::get_state_with(&ctx, dev_id)
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
        let group_state =
            <Context<_> as StateContext<MldInterface<_>, _>>::get_state_with(&ctx, dev_id)
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
            ensure_slice_addr(&frame, 22, 38, MY_MAC.to_ipv6_link_local().get());
        }
    }

    #[test]
    fn test_mld_unspecified_src_not_link_local() {
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let dev_id = ctx.state.add_ethernet_device(MY_MAC, 1500);
        crate::device::initialize_device(&mut ctx, dev_id);
        let _ = add_ip_addr_subnet(
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
            ensure_slice_addr(&frame, 22, 38, MY_MAC.to_ipv6_link_local().get());
        }
    }
}
