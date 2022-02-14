// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Multicast Listener Discovery (MLD).
//!
//! MLD is derived from version 2 of IPv4's Internet Group Management Protocol,
//! IGMPv2. One important difference to note is that MLD uses ICMPv6 (IP
//! Protocol 58) message types, rather than IGMP (IP Protocol 2) message types.

use core::time::Duration;

use log::{debug, error, trace};
use net_types::{
    ip::{Ip, Ipv6, Ipv6Addr, Ipv6ReservedScope, Ipv6Scope, Ipv6SourceAddr},
    LinkLocalUnicastAddr, MulticastAddr, ScopeableAddress, SpecifiedAddr, Witness,
};
use packet::{serialize::Serializer, EmptyBuf, InnerPacketBuilder};
use packet_formats::{
    icmp::{
        mld::{
            IcmpMldv1MessageType, MldPacket, Mldv1Body, Mldv1MessageBuilder, MulticastListenerDone,
            MulticastListenerReport,
        },
        IcmpPacketBuilder, IcmpUnusedCode,
    },
    ip::Ipv6Proto,
    ipv6::{
        ext_hdrs::{ExtensionHeaderOptionAction, HopByHopOption, HopByHopOptionData},
        Ipv6PacketBuilder, Ipv6PacketBuilderWithHbhOptions,
    },
};
use thiserror::Error;
use zerocopy::ByteSlice;

use crate::{
    context::{FrameContext, InstantContext, RngContext, TimerContext, TimerHandler},
    ip::{
        gmp::{
            gmp_join_group, gmp_leave_group, handle_gmp_message, Action, Actions, GmpAction,
            GmpContext, GmpHandler, GmpMessage, GmpStateMachine, GroupJoinResult, GroupLeaveResult,
            MulticastGroupSet, ProtocolSpecific,
        },
        IpDeviceIdContext,
    },
    Instant,
};

/// Metadata for sending an MLD packet in an IP packet.
///
/// `MldFrameMetadata` is used by [`MldContext`]'s [`FrameContext`] bound. When
/// [`FrameContext::send_frame`] is called with an `MldFrameMetadata`, the body
/// contains an MLD packet in an IP packet. It is encapsulated in a link-layer
/// frame, and sent to the link-layer address corresponding to the given local
/// IP address.
#[cfg_attr(test, derive(Debug, PartialEq))]
pub(crate) struct MldFrameMetadata<D> {
    pub(crate) device: D,
    pub(crate) dst_ip: MulticastAddr<Ipv6Addr>,
}

impl<D> MldFrameMetadata<D> {
    fn new(device: D, dst_ip: MulticastAddr<Ipv6Addr>) -> MldFrameMetadata<D> {
        MldFrameMetadata { device, dst_ip }
    }
}

/// The execution context for the Multicast Listener Discovery (MLD) protocol.
pub(crate) trait MldContext:
    IpDeviceIdContext<Ipv6>
    + RngContext
    + TimerContext<MldReportDelay<Self::DeviceId>>
    + FrameContext<EmptyBuf, MldFrameMetadata<Self::DeviceId>>
{
    /// Gets the IPv6 link local address on `device`.
    fn get_ipv6_link_local_addr(
        &self,
        device: Self::DeviceId,
    ) -> Option<LinkLocalUnicastAddr<Ipv6Addr>>;

    /// Is MLD enabled for `device`?
    ///
    /// If `mld_enabled` returns false, then [`GmpHandler::gmp_join_group`] and
    /// [`GmpHandler::gmp_leave_group`] will still join/leave multicast groups
    /// locally, and inbound MLD packets will still be processed, but no timers
    /// will be installed, and no outbound MLD traffic will be generated.
    fn mld_enabled(&self, device: Self::DeviceId) -> bool;

    /// Gets mutable access to the device's MLD state and RNG.
    fn get_state_mut_and_rng(
        &mut self,
        device: Self::DeviceId,
    ) -> (
        &mut MulticastGroupSet<Ipv6Addr, MldGroupState<<Self as InstantContext>::Instant>>,
        &mut Self::Rng,
    );

    /// Gets mutable access to the device's MLD state.
    fn get_state_mut(
        &mut self,
        device: Self::DeviceId,
    ) -> &mut MulticastGroupSet<Ipv6Addr, MldGroupState<<Self as InstantContext>::Instant>> {
        let (state, _rng): (_, &mut Self::Rng) = self.get_state_mut_and_rng(device);
        state
    }
}

impl<C: MldContext> GmpHandler<Ipv6> for C {
    fn gmp_join_group(
        &mut self,
        device: Self::DeviceId,
        group_addr: MulticastAddr<Ipv6Addr>,
    ) -> GroupJoinResult {
        gmp_join_group(self, device, group_addr)
    }

    fn gmp_leave_group(
        &mut self,
        device: Self::DeviceId,
        group_addr: MulticastAddr<Ipv6Addr>,
    ) -> GroupLeaveResult {
        gmp_leave_group(self, device, group_addr)
    }
}

/// A handler for incoming MLD packets.
///
/// A blanket implementation is provided for all `C: MldContext`.
pub(crate) trait MldPacketHandler<DeviceId> {
    /// Receive an MLD packet.
    fn receive_mld_packet<B: ByteSlice>(
        &mut self,
        device: DeviceId,
        src_ip: Ipv6SourceAddr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: MldPacket<B>,
    );
}

impl<C: MldContext> MldPacketHandler<C::DeviceId> for C {
    fn receive_mld_packet<B: ByteSlice>(
        &mut self,
        device: C::DeviceId,
        _src_ip: Ipv6SourceAddr,
        _dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: MldPacket<B>,
    ) {
        if let Err(e) = match packet {
            MldPacket::MulticastListenerQuery(msg) => {
                let now = self.now();
                let max_response_delay: Duration = msg.body().max_response_delay();
                handle_gmp_message(self, device, msg.body(), |rng, MldGroupState(state)| {
                    state.query_received(rng, max_response_delay, now)
                })
            }
            MldPacket::MulticastListenerReport(msg) => {
                handle_gmp_message(self, device, msg.body(), |_, MldGroupState(state)| {
                    state.report_received()
                })
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

impl<B: ByteSlice> GmpMessage<Ipv6> for Mldv1Body<B> {
    fn group_addr(&self) -> Ipv6Addr {
        self.group_addr
    }
}

impl<C: MldContext> GmpContext<Ipv6, MldProtocolSpecific> for C {
    type Err = MldError;
    type GroupState = MldGroupState<Self::Instant>;
    fn run_actions(
        &mut self,
        device: C::DeviceId,
        actions: Actions<MldProtocolSpecific>,
        group_addr: MulticastAddr<Ipv6Addr>,
    ) {
        // Per [RFC 3810 Section 6]:
        //
        // > No MLD messages are ever sent regarding neither the link-scope
        // > all-nodes multicast address, nor any multicast address of scope 0
        // > (reserved) or 1 (node-local).
        //
        // We abide by this requirement by not executing [`Actions`] on these
        // addresses. Executing [`Actions`] only produces externally-visible side
        // effects, and is not required to maintain the correctness of the MLD state
        // machines.
        //
        // [RFC 3810 Section 6]: https://tools.ietf.org/html/rfc3810#section-6
        let skip_mld = group_addr == Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS
            || group_addr.scope() == Ipv6Scope::Reserved(Ipv6ReservedScope::Scope0)
            || group_addr.scope() == Ipv6Scope::InterfaceLocal;

        if skip_mld {
            trace!("skipping executing MLD actions for group {}: MLD prohibited on this group by RFC 3810 Section 6", group_addr);
        } else if !self.mld_enabled(device) {
            trace!("skipping executing MLD actions on device {}: MLD disabled on device", device);
        } else {
            for action in actions {
                if let Err(err) = run_action(self, device, action, group_addr) {
                    error!(
                        "Error performing action on {} on device {}: {}",
                        group_addr, device, err
                    );
                }
            }
        }
    }

    fn not_a_member_err(addr: Ipv6Addr) -> Self::Err {
        Self::Err::NotAMember { addr }
    }

    fn get_state_mut_and_rng(
        &mut self,
        device: C::DeviceId,
    ) -> (&mut MulticastGroupSet<Ipv6Addr, Self::GroupState>, &mut Self::Rng) {
        self.get_state_mut_and_rng(device)
    }
}

#[derive(Debug, Error)]
pub(crate) enum MldError {
    /// The host is trying to operate on an group address of which the host is
    /// not a member.
    #[error("the host has not already been a member of the address: {}", addr)]
    NotAMember { addr: Ipv6Addr },
    /// Failed to send an IGMP packet.
    #[error("failed to send out an IGMP packet to address: {}", addr)]
    SendFailure { addr: Ipv6Addr },
}

pub(crate) type MldResult<T> = Result<T, MldError>;

#[derive(PartialEq, Eq, Clone, Copy, Default, Debug)]
pub(crate) struct MldProtocolSpecific;

#[derive(Debug)]
pub(crate) struct MldConfig {
    unsolicited_report_interval: Duration,
    send_leave_anyway: bool,
}

#[derive(PartialEq, Eq, Clone, Copy, Debug)]
pub(crate) struct ImmediateIdleState;

/// The default value for `unsolicited_report_interval` [RFC 2710 Section 7.10]
///
/// [RFC 2710 Section 7.10]: https://tools.ietf.org/html/rfc2710#section-7.10
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
#[cfg_attr(test, derive(Debug))]
pub(crate) struct MldGroupState<I: Instant>(GmpStateMachine<I, MldProtocolSpecific>);

impl<I: Instant> From<GmpStateMachine<I, MldProtocolSpecific>> for MldGroupState<I> {
    fn from(state: GmpStateMachine<I, MldProtocolSpecific>) -> MldGroupState<I> {
        MldGroupState(state)
    }
}

impl<I: Instant> From<MldGroupState<I>> for GmpStateMachine<I, MldProtocolSpecific> {
    fn from(MldGroupState(state): MldGroupState<I>) -> GmpStateMachine<I, MldProtocolSpecific> {
        state
    }
}

impl<I: Instant> MulticastGroupSet<Ipv6Addr, MldGroupState<I>> {
    fn report_timer_expired(
        &mut self,
        addr: MulticastAddr<Ipv6Addr>,
    ) -> MldResult<Actions<MldProtocolSpecific>> {
        match self.get_mut(&addr) {
            Some(MldGroupState(state)) => Ok(state.report_timer_expired()),
            None => Err(MldError::NotAMember { addr: addr.get() }),
        }
    }
}

/// An MLD timer to delay an MLD report for the link device type `D`.
#[derive(PartialEq, Eq, Clone, Copy, Debug, Hash)]
pub(crate) struct MldReportDelay<DeviceId> {
    device: DeviceId,
    group_addr: MulticastAddr<Ipv6Addr>,
}

impl<DeviceId> MldReportDelay<DeviceId> {
    fn new(device: DeviceId, group_addr: MulticastAddr<Ipv6Addr>) -> MldReportDelay<DeviceId> {
        MldReportDelay { device, group_addr }
    }
}

impl<C: MldContext> TimerHandler<MldReportDelay<C::DeviceId>> for C {
    fn handle_timer(&mut self, timer: MldReportDelay<C::DeviceId>) {
        let MldReportDelay { device, group_addr } = timer;
        match self.get_state_mut(device).report_timer_expired(group_addr) {
            Ok(actions) => self.run_actions(device, actions, group_addr),
            Err(e) => error!("MLD timer fired, but an error has occurred: {}", e),
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
            let _: Option<C::Instant> =
                ctx.schedule_timer(delay, MldReportDelay::new(device, group_addr));
            Ok(())
        }
        Action::Generic(GmpAction::StopReportTimer) => {
            let _: Option<C::Instant> = ctx.cancel_timer(MldReportDelay::new(device, group_addr));
            Ok(())
        }
        Action::Generic(GmpAction::SendLeave) => send_mld_packet::<_, &[u8], _>(
            ctx,
            device,
            Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS,
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
            let _: Option<C::Instant> = ctx.cancel_timer(MldReportDelay::new(device, group_addr));
            let actions = ctx.get_state_mut(device).report_timer_expired(group_addr)?;
            Ok(ctx.run_actions(device, actions, group_addr))
        }
    }
}

/// Send an MLD packet.
///
/// The MLD packet being sent should have its `hop_limit` to be 1 and a
/// `RouterAlert` option in its Hop-by-Hop Options extensions header.
fn send_mld_packet<C: MldContext, B: ByteSlice, M: IcmpMldv1MessageType<B>>(
    ctx: &mut C,
    device: C::DeviceId,
    dst_ip: MulticastAddr<Ipv6Addr>,
    msg: M,
    group_addr: M::GroupAddr,
    max_resp_delay: M::MaxRespDelay,
) -> MldResult<()> {
    // According to https://tools.ietf.org/html/rfc3590#section-4, if a valid
    // link-local address is not available for the device (e.g., one has not
    // been configured), the message is sent with the unspecified address (::)
    // as the IPv6 source address.

    let src_ip =
        ctx.get_ipv6_link_local_addr(device).map_or(Ipv6::UNSPECIFIED_ADDRESS, |x| x.get());

    let body = Mldv1MessageBuilder::<M>::new_with_max_resp_delay(group_addr, max_resp_delay)
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::new(src_ip, dst_ip.get(), IcmpUnusedCode, msg))
        .encapsulate(
            Ipv6PacketBuilderWithHbhOptions::new(
                Ipv6PacketBuilder::new(src_ip, dst_ip.get(), 1, Ipv6Proto::Icmpv6),
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

#[cfg(test)]
mod tests {
    use alloc::vec::Vec;
    use core::convert::TryInto;

    use net_types::ethernet::Mac;
    use packet::ParseBuffer;
    use packet_formats::icmp::{mld::MulticastListenerQuery, IcmpParseArgs, Icmpv6Packet};
    use rand_xorshift::XorShiftRng;

    use super::*;
    use crate::{
        assert_empty,
        context::{
            testutil::{DummyInstant, DummyTimerCtxExt},
            DualStateContext,
        },
        ip::{
            gmp::{Action, MemberState},
            DummyDeviceId,
        },
        testutil::{new_rng, run_with_many_seeds, FakeCryptoRng},
    };

    /// A dummy [`MldContext`] that stores the [`MldInterface`] and an optional
    /// IPv6 link-local address that may be returned in calls to
    /// [`MldContext::get_ipv6_link_local_addr`].
    struct DummyMldCtx {
        groups: MulticastGroupSet<Ipv6Addr, MldGroupState<DummyInstant>>,
        mld_enabled: bool,
        ipv6_link_local: Option<LinkLocalUnicastAddr<Ipv6Addr>>,
    }

    impl Default for DummyMldCtx {
        fn default() -> DummyMldCtx {
            DummyMldCtx {
                groups: MulticastGroupSet::default(),
                mld_enabled: true,
                ipv6_link_local: None,
            }
        }
    }

    type DummyCtx = crate::context::testutil::DummyCtx<
        DummyMldCtx,
        MldReportDelay<DummyDeviceId>,
        MldFrameMetadata<DummyDeviceId>,
    >;

    impl MldContext for DummyCtx {
        fn get_ipv6_link_local_addr(
            &self,
            _device: DummyDeviceId,
        ) -> Option<LinkLocalUnicastAddr<Ipv6Addr>> {
            self.get_ref().ipv6_link_local
        }

        fn mld_enabled(&self, _device: DummyDeviceId) -> bool {
            self.get_ref().mld_enabled
        }

        fn get_state_mut_and_rng(
            &mut self,
            _id0: DummyDeviceId,
        ) -> (
            &mut MulticastGroupSet<Ipv6Addr, MldGroupState<DummyInstant>>,
            &mut FakeCryptoRng<XorShiftRng>,
        ) {
            let (state, rng) = self.get_states_mut();
            (&mut state.groups, rng)
        }
    }

    #[test]
    fn test_mld_immediate_report() {
        run_with_many_seeds(|seed| {
            // Most of the test surface is covered by the GMP implementation,
            // MLD specific part is mostly passthrough. This test case is here
            // because MLD allows a router to ask for report immediately, by
            // specifying the `MaxRespDelay` to be 0. If this is the case, the
            // host should send the report immediately instead of setting a
            // timer.
            let mut rng = new_rng(seed);
            let (mut s, _actions) = GmpStateMachine::<_, MldProtocolSpecific>::join_group(
                &mut rng,
                DummyInstant::default(),
            );
            let actions =
                s.query_received(&mut rng, Duration::from_secs(0), DummyInstant::default());
            let vec = actions.into_iter().collect::<Vec<Action<_>>>();
            assert_eq!(vec, [Action::Specific(ImmediateIdleState)]);
        });
    }

    const MY_IP: SpecifiedAddr<Ipv6Addr> = unsafe {
        SpecifiedAddr::new_unchecked(Ipv6Addr::from_bytes([
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 3,
        ]))
    };
    const MY_MAC: Mac = Mac::new([1, 2, 3, 4, 5, 6]);
    const ROUTER_MAC: Mac = Mac::new([6, 5, 4, 3, 2, 1]);
    const GROUP_ADDR: MulticastAddr<Ipv6Addr> =
        unsafe { MulticastAddr::new_unchecked(Ipv6Addr::new([0xff02, 0, 0, 0, 0, 0, 0, 3])) };
    const TIMER_ID: MldReportDelay<DummyDeviceId> =
        MldReportDelay { device: DummyDeviceId, group_addr: GROUP_ADDR };

    fn receive_mld_query(
        ctx: &mut DummyCtx,
        resp_time: Duration,
        group_addr: MulticastAddr<Ipv6Addr>,
    ) {
        let router_addr: Ipv6Addr = ROUTER_MAC.to_ipv6_link_local().addr().get();
        let mut buffer = Mldv1MessageBuilder::<MulticastListenerQuery>::new_with_max_resp_delay(
            group_addr.get(),
            resp_time.try_into().unwrap(),
        )
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::<_, &[u8], _>::new(
            router_addr,
            MY_IP,
            IcmpUnusedCode,
            MulticastListenerQuery,
        ))
        .serialize_vec_outer()
        .unwrap();
        match buffer
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(router_addr, MY_IP))
            .unwrap()
        {
            Icmpv6Packet::Mld(packet) => ctx.receive_mld_packet(
                DummyDeviceId,
                router_addr.try_into().unwrap(),
                MY_IP,
                packet,
            ),
            _ => panic!("serialized icmpv6 message is not an mld message"),
        }
    }

    fn receive_mld_report(ctx: &mut DummyCtx, group_addr: MulticastAddr<Ipv6Addr>) {
        let router_addr: Ipv6Addr = ROUTER_MAC.to_ipv6_link_local().addr().get();
        let mut buffer = Mldv1MessageBuilder::<MulticastListenerReport>::new(group_addr)
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<_, &[u8], _>::new(
                router_addr,
                MY_IP,
                IcmpUnusedCode,
                MulticastListenerReport,
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_b();
        match buffer
            .parse_with::<_, Icmpv6Packet<_>>(IcmpParseArgs::new(router_addr, MY_IP))
            .unwrap()
        {
            Icmpv6Packet::Mld(packet) => ctx.receive_mld_packet(
                DummyDeviceId,
                router_addr.try_into().unwrap(),
                MY_IP,
                packet,
            ),
            _ => panic!("serialized icmpv6 message is not an mld message"),
        }
    }

    // Ensure the ttl is 1.
    fn ensure_ttl(frame: &[u8]) {
        assert_eq!(frame[7], 1);
    }

    fn ensure_slice_addr(frame: &[u8], start: usize, end: usize, ip: Ipv6Addr) {
        let mut bytes = [0u8; 16];
        bytes.copy_from_slice(&frame[start..end]);
        assert_eq!(Ipv6Addr::from_bytes(bytes), ip);
    }

    // Ensure the destination address field in the ICMPv6 packet is correct.
    fn ensure_dst_addr(frame: &[u8], ip: Ipv6Addr) {
        ensure_slice_addr(frame, 24, 40, ip);
    }

    // Ensure the multicast address field in the MLD packet is correct.
    fn ensure_multicast_addr(frame: &[u8], ip: Ipv6Addr) {
        ensure_slice_addr(frame, 56, 72, ip);
    }

    // Ensure a sent frame meets the requirement.
    fn ensure_frame(
        frame: &[u8],
        op: u8,
        dst: MulticastAddr<Ipv6Addr>,
        multicast: MulticastAddr<Ipv6Addr>,
    ) {
        ensure_ttl(frame);
        assert_eq!(frame[48], op);
        // Ensure the length our payload is 32 = 8 (hbh_ext_hdr) + 24 (mld)
        assert_eq!(frame[5], 32);
        // Ensure the next header is our HopByHop Extension Header.
        assert_eq!(frame[6], 0);
        // Ensure there is a RouterAlert HopByHopOption in our sent frame
        assert_eq!(&frame[40..48], &[58, 0, 5, 2, 0, 0, 1, 0]);
        ensure_ttl(&frame[..]);
        ensure_dst_addr(&frame[..], dst.get());
        ensure_multicast_addr(&frame[..], multicast.get());
    }

    #[test]
    fn test_mld_simple_integration() {
        run_with_many_seeds(|seed| {
            let mut ctx = DummyCtx::default();
            ctx.seed_rng(seed);

            assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));

            receive_mld_query(&mut ctx, Duration::from_secs(10), GROUP_ADDR);
            assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), Some(TIMER_ID));

            // We should get two MLD reports - one for the unsolicited one for
            // the host to turn into Delay Member state and the other one for
            // the timer being fired.
            assert_eq!(ctx.frames().len(), 2);
            // The frames are all reports.
            for (_, frame) in ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_immediate_query() {
        run_with_many_seeds(|seed| {
            let mut ctx = DummyCtx::default();
            ctx.seed_rng(seed);

            assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
            assert_eq!(ctx.frames().len(), 1);

            receive_mld_query(&mut ctx, Duration::from_secs(0), GROUP_ADDR);
            // The query says that it wants to hear from us immediately.
            assert_eq!(ctx.frames().len(), 2);
            // There should be no timers set.
            assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), None);
            // The frames are all reports.
            for (_, frame) in ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_integration_fallback_from_idle() {
        run_with_many_seeds(|seed| {
            let mut ctx = DummyCtx::default();
            ctx.seed_rng(seed);

            assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
            assert_eq!(ctx.frames().len(), 1);

            assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), Some(TIMER_ID));
            assert_eq!(ctx.frames().len(), 2);

            receive_mld_query(&mut ctx, Duration::from_secs(10), GROUP_ADDR);

            // We have received a query, hence we are falling back to Delay
            // Member state.
            let MldGroupState(group_state) =
                MldContext::get_state_mut(&mut ctx, DummyDeviceId).get(&GROUP_ADDR).unwrap();
            match group_state.get_inner() {
                MemberState::Delaying(_) => {}
                _ => panic!("Wrong State!"),
            }

            assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), Some(TIMER_ID));
            assert_eq!(ctx.frames().len(), 3);
            // The frames are all reports.
            for (_, frame) in ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_integration_immediate_query_wont_fallback() {
        run_with_many_seeds(|seed| {
            let mut ctx = DummyCtx::default();
            ctx.seed_rng(seed);

            assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
            assert_eq!(ctx.frames().len(), 1);

            assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), Some(TIMER_ID));
            assert_eq!(ctx.frames().len(), 2);

            receive_mld_query(&mut ctx, Duration::from_secs(0), GROUP_ADDR);

            // Since it is an immediate query, we will send a report immediately
            // and turn into Idle state again.
            let MldGroupState(group_state) =
                MldContext::get_state_mut(&mut ctx, DummyDeviceId).get(&GROUP_ADDR).unwrap();
            match group_state.get_inner() {
                MemberState::Idle(_) => {}
                _ => panic!("Wrong State!"),
            }

            // No timers!
            assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), None);
            assert_eq!(ctx.frames().len(), 3);
            // The frames are all reports.
            for (_, frame) in ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_integration_delay_reset_timer() {
        let mut ctx = DummyCtx::default();
        // This seed was carefully chosen to produce a substantial duration
        // value below.
        ctx.seed_rng(123456);
        assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        ctx.timer_ctx().assert_timers_installed([(
            TIMER_ID,
            DummyInstant::from(Duration::from_micros(590_354)),
        )]);
        let instant1 = ctx.timer_ctx().timers()[0].0.clone();
        let start = ctx.now();
        let duration = instant1 - start;

        receive_mld_query(&mut ctx, duration, GROUP_ADDR);
        assert_eq!(ctx.frames().len(), 1);
        ctx.timer_ctx().assert_timers_installed([(
            TIMER_ID,
            DummyInstant::from(Duration::from_micros(34_751)),
        )]);
        let instant2 = ctx.timer_ctx().timers()[0].0.clone();
        // This new timer should be sooner.
        assert!(instant2 <= instant1);
        assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), Some(TIMER_ID));
        assert!(ctx.now() - start <= duration);
        assert_eq!(ctx.frames().len(), 2);
        // The frames are all reports.
        for (_, frame) in ctx.frames() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }

    #[test]
    fn test_mld_integration_last_send_leave() {
        run_with_many_seeds(|seed| {
            let mut ctx = DummyCtx::default();
            ctx.seed_rng(seed);

            assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
            let now = ctx.now();
            ctx.timer_ctx().assert_timers_installed([(
                TIMER_ID,
                now..=(now + DEFAULT_UNSOLICITED_REPORT_INTERVAL),
            )]);
            // The initial unsolicited report.
            assert_eq!(ctx.frames().len(), 1);
            assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), Some(TIMER_ID));
            // The report after the delay.
            assert_eq!(ctx.frames().len(), 2);
            assert_eq!(ctx.gmp_leave_group(DummyDeviceId, GROUP_ADDR), GroupLeaveResult::Left(()));
            // Our leave message.
            assert_eq!(ctx.frames().len(), 3);
            // The first two messages should be reports.
            ensure_frame(&ctx.frames()[0].1, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&ctx.frames()[0].1, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            ensure_frame(&ctx.frames()[1].1, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&ctx.frames()[1].1, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            // The last one should be the done message whose destination is all
            // routers.
            ensure_frame(
                &ctx.frames()[2].1,
                132,
                Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS,
                GROUP_ADDR,
            );
            ensure_slice_addr(&ctx.frames()[2].1, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        });
    }

    #[test]
    fn test_mld_integration_not_last_does_not_send_leave() {
        run_with_many_seeds(|seed| {
            let mut ctx = DummyCtx::default();
            ctx.seed_rng(seed);

            assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
            let now = ctx.now();
            ctx.timer_ctx().assert_timers_installed([(
                TIMER_ID,
                now..=(now + DEFAULT_UNSOLICITED_REPORT_INTERVAL),
            )]);
            assert_eq!(ctx.frames().len(), 1);
            receive_mld_report(&mut ctx, GROUP_ADDR);
            ctx.timer_ctx().assert_no_timers_installed();
            // The report should be discarded because we have received from someone
            // else.
            assert_eq!(ctx.frames().len(), 1);
            assert_eq!(ctx.gmp_leave_group(DummyDeviceId, GROUP_ADDR), GroupLeaveResult::Left(()));
            // A leave message is not sent.
            assert_eq!(ctx.frames().len(), 1);
            // The frames are all reports.
            for (_, frame) in ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_with_link_local() {
        run_with_many_seeds(|seed| {
            let mut ctx = DummyCtx::default();
            ctx.seed_rng(seed);

            ctx.get_mut().ipv6_link_local = Some(MY_MAC.to_ipv6_link_local().addr());
            assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
            assert_eq!(ctx.trigger_next_timer(TimerHandler::handle_timer), Some(TIMER_ID));
            for (_, frame) in ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, MY_MAC.to_ipv6_link_local().addr().get());
            }
        });
    }

    #[test]
    fn test_skip_mld() {
        run_with_many_seeds(|seed| {
            // Test that we properly skip executing any `Actions` for addresses
            // that we're supposed to skip (see the comment in `run_actions`)
            // and when MLD is disabled for the device.
            let test = |mut ctx: DummyCtx, group| {
                ctx.get_mut().ipv6_link_local = Some(MY_MAC.to_ipv6_link_local().addr());

                // Assert that no observable effects have taken place.
                let assert_no_effect = |ctx: &DummyCtx| {
                    ctx.timer_ctx().assert_no_timers_installed();
                    assert_empty(ctx.frames());
                };

                assert_eq!(ctx.gmp_join_group(DummyDeviceId, group), GroupJoinResult::Joined(()));
                // We should have joined the group but not executed any
                // `Actions`.
                assert_gmp_state!(ctx, &group, Delaying);
                assert_no_effect(&ctx);

                receive_mld_report(&mut ctx, group);
                // We should have executed the transition but not executed any
                // `Actions`.
                assert_gmp_state!(ctx, &group, Idle);
                assert_no_effect(&ctx);

                receive_mld_query(&mut ctx, Duration::from_secs(10), group);
                // We should have executed the transition but not executed any
                // `Actions`.
                assert_gmp_state!(ctx, &group, Delaying);
                assert_no_effect(&ctx);

                assert_eq!(ctx.gmp_leave_group(DummyDeviceId, group), GroupLeaveResult::Left(()));
                // We should have left the group but not executed any `Actions`.
                assert!(ctx.get_ref().groups.get(&group).is_none());
                assert_no_effect(&ctx);
            };

            let new_ctx = || {
                let mut ctx = DummyCtx::default();
                ctx.seed_rng(seed);
                ctx
            };

            // Test that we skip executing `Actions` for addresses we're
            // supposed to skip.
            test(new_ctx(), Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS);
            let mut bytes = Ipv6::MULTICAST_SUBNET.network().ipv6_bytes();
            // Manually set the "scope" field to 0.
            bytes[1] = bytes[1] & 0xF0;
            let reserved0 = MulticastAddr::new(Ipv6Addr::from_bytes(bytes)).unwrap();
            // Manually set the "scope" field to 1 (interface-local).
            bytes[1] = (bytes[1] & 0xF0) | 1;
            let iface_local = MulticastAddr::new(Ipv6Addr::from_bytes(bytes)).unwrap();
            test(new_ctx(), reserved0);
            test(new_ctx(), iface_local);

            // Test that we skip executing `Actions` when MLD is disabled on the
            // device.
            let mut ctx = new_ctx();
            ctx.get_mut().mld_enabled = false;
            test(ctx, GROUP_ADDR);
        });
    }

    #[test]
    fn test_mld_integration_with_local_join_leave() {
        run_with_many_seeds(|seed| {
            // Simple MLD integration test to check that when we call top-level
            // multicast join and leave functions, MLD is performed.
            let mut ctx = DummyCtx::default();
            ctx.seed_rng(seed);

            assert_eq!(ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
            assert_gmp_state!(ctx, &GROUP_ADDR, Delaying);
            assert_eq!(ctx.frames().len(), 1);
            let now = ctx.now();
            let range = now..=(now + DEFAULT_UNSOLICITED_REPORT_INTERVAL);
            ctx.timer_ctx().assert_timers_installed([(TIMER_ID, range.clone())]);
            let frame = &ctx.frames().last().unwrap().1;
            ensure_frame(frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);

            assert_eq!(
                ctx.gmp_join_group(DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::AlreadyMember
            );
            assert_gmp_state!(ctx, &GROUP_ADDR, Delaying);
            assert_eq!(ctx.frames().len(), 1);
            ctx.timer_ctx().assert_timers_installed([(TIMER_ID, range.clone())]);

            assert_eq!(
                ctx.gmp_leave_group(DummyDeviceId, GROUP_ADDR),
                GroupLeaveResult::StillMember
            );
            assert_gmp_state!(ctx, &GROUP_ADDR, Delaying);
            assert_eq!(ctx.frames().len(), 1);
            ctx.timer_ctx().assert_timers_installed([(TIMER_ID, range)]);

            assert_eq!(ctx.gmp_leave_group(DummyDeviceId, GROUP_ADDR), GroupLeaveResult::Left(()));
            assert_eq!(ctx.frames().len(), 2);
            ctx.timer_ctx().assert_no_timers_installed();
            let frame = &ctx.frames().last().unwrap().1;
            ensure_frame(frame, 132, Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS, GROUP_ADDR);
            ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        });
    }
}
