// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Multicast Listener Discovery (MLD).
//!
//! MLD is derived from version 2 of IPv4's Internet Group Management Protocol,
//! IGMPv2. One important difference to note is that MLD uses ICMPv6 (IP
//! Protocol 58) message types, rather than IGMP (IP Protocol 2) message types.

use alloc::vec::Vec;
use core::marker::PhantomData;
use core::time::Duration;

use log::{debug, error, trace};
use net_types::ip::{Ip, Ipv6, Ipv6Addr, Ipv6ReservedScope, Ipv6Scope, Ipv6SourceAddr};
use net_types::{
    LinkLocalUnicastAddr, MulticastAddr, ScopeableAddress, SpecifiedAddr, SpecifiedAddress, Witness,
};
use packet::serialize::Serializer;
use packet::{EmptyBuf, InnerPacketBuilder};
use packet_formats::icmp::mld::{
    IcmpMldv1MessageType, Mldv1Body, Mldv1MessageBuilder, MulticastListenerDone,
    MulticastListenerReport,
};
use packet_formats::icmp::{mld::MldPacket, IcmpPacketBuilder, IcmpUnusedCode};
use packet_formats::ip::Ipv6Proto;
use packet_formats::ipv6::ext_hdrs::{
    ExtensionHeaderOptionAction, HopByHopOption, HopByHopOptionData,
};
use packet_formats::ipv6::{Ipv6PacketBuilder, Ipv6PacketBuilderWithHbhOptions};
use thiserror::Error;
use zerocopy::ByteSlice;

use crate::context::{FrameContext, InstantContext, RngStateContext, TimerContext, TimerHandler};
use crate::device::link::LinkDevice;
use crate::device::DeviceIdContext;
use crate::ip::gmp::{
    Action, Actions, GmpAction, GmpHandler, GmpStateMachine, GroupJoinResult, GroupLeaveResult,
    MulticastGroupSet, ProtocolSpecific,
};
use crate::Instant;

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
pub(crate) trait MldContext<D: LinkDevice>:
    DeviceIdContext<D>
    + TimerContext<MldReportDelay<D, <Self as DeviceIdContext<D>>::DeviceId>>
    + RngStateContext<
        MulticastGroupSet<Ipv6Addr, MldGroupState<<Self as InstantContext>::Instant>>,
        <Self as DeviceIdContext<D>>::DeviceId,
    > + FrameContext<EmptyBuf, MldFrameMetadata<<Self as DeviceIdContext<D>>::DeviceId>>
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
}

impl<D: LinkDevice, C: MldContext<D>> GmpHandler<D, Ipv6> for C {
    fn gmp_join_group(
        &mut self,
        device: Self::DeviceId,
        group_addr: MulticastAddr<Ipv6Addr>,
    ) -> GroupJoinResult {
        let now = self.now();
        let (state, rng) = self.get_state_rng_with(device);
        state
            .join_group_gmp(group_addr, rng, now)
            .map(|actions| run_actions(self, device, actions, group_addr))
    }

    fn gmp_leave_group(
        &mut self,
        device: Self::DeviceId,
        group_addr: MulticastAddr<Ipv6Addr>,
    ) -> GroupLeaveResult {
        self.get_state_mut_with(device)
            .leave_group_gmp(group_addr)
            .map(|actions| run_actions(self, device, actions, group_addr))
    }
}

/// A handler for incoming MLD packets.
///
/// This trait is designed to be implemented in two situations. First, just like
/// [`GmpHandler`], a blanket implementation is provided for all `C:
/// MldContext`. This provides separate implementations for each link device
/// protocol that supports MLD.
///
/// Second, unlike `GmpHandler`, a single impl is provided by the device layer
/// itself, setting the `D` parameter to `()`. This is used by the `icmp` module
/// to dispatch inbound MLD packets. This impl is responsible for dispatching to
/// the appropriate link device-specific implementation.
pub(crate) trait MldPacketHandler<D, DeviceId> {
    /// Receive an MLD packet.
    fn receive_mld_packet<B: ByteSlice>(
        &mut self,
        device: DeviceId,
        src_ip: Ipv6SourceAddr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: MldPacket<B>,
    );
}

impl<D: LinkDevice, C: MldContext<D>> MldPacketHandler<D, C::DeviceId> for C {
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
                handle_mld_message(self, device, msg.body(), |rng, MldGroupState(state)| {
                    state.query_received(rng, max_response_delay, now)
                })
            }
            MldPacket::MulticastListenerReport(msg) => {
                handle_mld_message(self, device, msg.body(), |_, MldGroupState(state)| {
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

fn handle_mld_message<D: LinkDevice, C: MldContext<D>, B: ByteSlice, F>(
    ctx: &mut C,
    device: C::DeviceId,
    body: &Mldv1Body<B>,
    handler: F,
) -> MldResult<()>
where
    F: Fn(&mut C::Rng, &mut MldGroupState<C::Instant>) -> Actions<MldProtocolSpecific>,
{
    let (state, rng) = ctx.get_state_rng_with(device);
    let group_addr = body.group_addr;
    if !group_addr.is_specified() {
        let addr_and_actions = state
            .iter_mut()
            .map(|(addr, state)| (addr.clone(), handler(rng, state)))
            .collect::<Vec<_>>();
        for (addr, actions) in addr_and_actions {
            run_actions(ctx, device, actions, addr);
        }
        Ok(())
    } else if let Some(group_addr) = MulticastAddr::new(group_addr) {
        let actions = match state.get_mut(&group_addr) {
            Some(state) => handler(rng, state),
            None => return Err(MldError::NotAMember { addr: group_addr.get() }),
        };
        run_actions(ctx, device, actions, group_addr);
        Ok(())
    } else {
        Err(MldError::NotAMember { addr: group_addr })
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
pub(crate) struct MldReportDelay<D, DeviceId> {
    device: DeviceId,
    group_addr: MulticastAddr<Ipv6Addr>,
    _marker: PhantomData<D>,
}

impl<D, DeviceId> MldReportDelay<D, DeviceId> {
    fn new(device: DeviceId, group_addr: MulticastAddr<Ipv6Addr>) -> MldReportDelay<D, DeviceId> {
        MldReportDelay { device, group_addr, _marker: PhantomData }
    }
}

impl<D: LinkDevice, C: MldContext<D>> TimerHandler<MldReportDelay<D, C::DeviceId>> for C {
    fn handle_timer(&mut self, timer: MldReportDelay<D, C::DeviceId>) {
        let MldReportDelay { device, group_addr, _marker } = timer;
        match self.get_state_mut_with(device).report_timer_expired(group_addr) {
            Ok(actions) => run_actions(self, device, actions, group_addr),
            Err(e) => error!("MLD timer fired, but an error has occurred: {}", e),
        }
    }
}

fn run_actions<D: LinkDevice, C: MldContext<D>>(
    ctx: &mut C,
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
    } else if !ctx.mld_enabled(device) {
        trace!("skipping executing MLD actions on device {}: MLD disabled on device", device);
    } else {
        for action in actions {
            if let Err(err) = run_action(ctx, device, action, group_addr) {
                error!("Error performing action on {} on device {}: {}", group_addr, device, err);
            }
        }
    }
}

/// Interpret the actions generated by the state machine.
fn run_action<D: LinkDevice, C: MldContext<D>>(
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
        Action::Generic(GmpAction::SendLeave) => send_mld_packet::<_, _, &[u8], _>(
            ctx,
            device,
            Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS,
            MulticastListenerDone,
            group_addr,
            (),
        ),
        Action::Generic(GmpAction::SendReport(_)) => send_mld_packet::<_, _, &[u8], _>(
            ctx,
            device,
            group_addr,
            MulticastListenerReport,
            group_addr,
            (),
        ),
        Action::Specific(ImmediateIdleState) => {
            let _: Option<C::Instant> = ctx.cancel_timer(MldReportDelay::new(device, group_addr));
            let actions = ctx.get_state_mut_with(device).report_timer_expired(group_addr)?;
            Ok(run_actions(ctx, device, actions, group_addr))
        }
    }
}

/// Send an MLD packet.
///
/// The MLD packet being sent should have its `hop_limit` to be 1 and a
/// `RouterAlert` option in its Hop-by-Hop Options extensions header.
fn send_mld_packet<D: LinkDevice, C: MldContext<D>, B: ByteSlice, M: IcmpMldv1MessageType<B>>(
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
    use core::convert::TryInto;

    use net_types::ethernet::Mac;
    use packet::ParseBuffer;
    use packet_formats::icmp::mld::MulticastListenerQuery;
    use packet_formats::icmp::{IcmpParseArgs, Icmpv6Packet};
    use rand_xorshift::XorShiftRng;

    use super::*;
    use crate::assert_empty;
    use crate::context::testutil::{DummyInstant, DummyTimerCtxExt};
    use crate::context::DualStateContext;
    use crate::device::link::testutil::{DummyLinkDevice, DummyLinkDeviceId};
    use crate::ip::gmp::{Action, MemberState};
    use crate::testutil;
    use crate::testutil::{new_rng, FakeCryptoRng};

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
        MldReportDelay<DummyLinkDevice, DummyLinkDeviceId>,
        MldFrameMetadata<DummyLinkDeviceId>,
    >;

    impl
        DualStateContext<
            MulticastGroupSet<Ipv6Addr, MldGroupState<DummyInstant>>,
            FakeCryptoRng<XorShiftRng>,
            DummyLinkDeviceId,
        > for DummyCtx
    {
        fn get_states_with(
            &self,
            _id0: DummyLinkDeviceId,
            _id1: (),
        ) -> (&MulticastGroupSet<Ipv6Addr, MldGroupState<DummyInstant>>, &FakeCryptoRng<XorShiftRng>)
        {
            let (state, rng) = self.get_states();
            (&state.groups, rng)
        }

        fn get_states_mut_with(
            &mut self,
            _id0: DummyLinkDeviceId,
            _id1: (),
        ) -> (
            &mut MulticastGroupSet<Ipv6Addr, MldGroupState<DummyInstant>>,
            &mut FakeCryptoRng<XorShiftRng>,
        ) {
            let (state, rng) = self.get_states_mut();
            (&mut state.groups, rng)
        }
    }

    impl MldContext<DummyLinkDevice> for DummyCtx {
        fn get_ipv6_link_local_addr(
            &self,
            _device: DummyLinkDeviceId,
        ) -> Option<LinkLocalUnicastAddr<Ipv6Addr>> {
            self.get_ref().ipv6_link_local
        }

        fn mld_enabled(&self, _device: DummyLinkDeviceId) -> bool {
            self.get_ref().mld_enabled
        }
    }

    #[test]
    fn test_mld_immediate_report() {
        // Most of the test surface is covered by the GMP implementation, MLD
        // specific part is mostly passthrough. This test case is here because
        // MLD allows a router to ask for report immediately, by specifying the
        // `MaxRespDelay` to be 0. If this is the case, the host should send the
        // report immediately instead of setting a timer.
        let mut rng = new_rng(0);
        let (mut s, _actions) = GmpStateMachine::<_, MldProtocolSpecific>::join_group(
            &mut rng,
            DummyInstant::default(),
        );
        let actions = s.query_received(&mut rng, Duration::from_secs(0), DummyInstant::default());
        let vec = actions.into_iter().collect::<Vec<Action<_>>>();
        assert_eq!(vec, [Action::Specific(ImmediateIdleState)]);
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
                DummyLinkDeviceId,
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
                DummyLinkDeviceId,
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
        let mut ctx = DummyCtx::default();
        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));

        receive_mld_query(&mut ctx, Duration::from_secs(10), GROUP_ADDR);
        assert!(ctx.trigger_next_timer());

        // We should get two MLD reports - one for the unsolicited one for the
        // host to turn into Delay Member state and the other one for the timer
        // being fired.
        assert_eq!(ctx.frames().len(), 2);
        // The frames are all reports.
        for (_, frame) in ctx.frames() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }

    #[test]
    fn test_mld_immediate_query() {
        testutil::set_logger_for_test();
        let mut ctx = DummyCtx::default();
        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        assert_eq!(ctx.frames().len(), 1);

        receive_mld_query(&mut ctx, Duration::from_secs(0), GROUP_ADDR);
        // The query says that it wants to hear from us immediately.
        assert_eq!(ctx.frames().len(), 2);
        // There should be no timers set.
        assert!(!ctx.trigger_next_timer());
        // The frames are all reports.
        for (_, frame) in ctx.frames() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }

    #[test]
    fn test_mld_integration_fallback_from_idle() {
        let mut ctx = DummyCtx::default();
        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        assert_eq!(ctx.frames().len(), 1);

        assert!(ctx.trigger_next_timer());
        assert_eq!(ctx.frames().len(), 2);

        receive_mld_query(&mut ctx, Duration::from_secs(10), GROUP_ADDR);

        // We have received a query, hence we are falling back to Delay Member
        // state.
        let MldGroupState(group_state) =
            ctx.get_state_with(DummyLinkDeviceId).get(&GROUP_ADDR).unwrap();
        match group_state.get_inner() {
            MemberState::Delaying(_) => {}
            _ => panic!("Wrong State!"),
        }

        assert!(ctx.trigger_next_timer());
        assert_eq!(ctx.frames().len(), 3);
        // The frames are all reports.
        for (_, frame) in ctx.frames() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }

    #[test]
    fn test_mld_integration_immediate_query_wont_fallback() {
        let mut ctx = DummyCtx::default();
        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        assert_eq!(ctx.frames().len(), 1);

        assert!(ctx.trigger_next_timer());
        assert_eq!(ctx.frames().len(), 2);

        receive_mld_query(&mut ctx, Duration::from_secs(0), GROUP_ADDR);

        // Since it is an immediate query, we will send a report immediately and
        // turn into Idle state again.
        let MldGroupState(group_state) =
            ctx.get_state_with(DummyLinkDeviceId).get(&GROUP_ADDR).unwrap();
        match group_state.get_inner() {
            MemberState::Idle(_) => {}
            _ => panic!("Wrong State!"),
        }

        // No timers!
        assert!(!ctx.trigger_next_timer());
        assert_eq!(ctx.frames().len(), 3);
        // The frames are all reports.
        for (_, frame) in ctx.frames() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }

    #[test]
    fn test_mld_integration_delay_reset_timer() {
        let mut ctx = DummyCtx::default();
        // This seed was carefully chosen to produce a substantial duration
        // value below.
        ctx.seed_rng(123456);
        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        assert_eq!(ctx.timers().len(), 1);
        let instant1 = ctx.timers()[0].0.clone();
        let start = ctx.now();
        let duration = instant1 - start;

        receive_mld_query(&mut ctx, duration, GROUP_ADDR);
        assert_eq!(ctx.frames().len(), 1);
        assert_eq!(ctx.timers().len(), 1);
        let instant2 = ctx.timers()[0].0.clone();
        // This new timer should be sooner.
        assert!(instant2 <= instant1);
        assert!(ctx.trigger_next_timer());
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
        let mut ctx = DummyCtx::default();
        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        assert_eq!(ctx.timers().len(), 1);
        // The initial unsolicited report.
        assert_eq!(ctx.frames().len(), 1);
        assert!(ctx.trigger_next_timer());
        // The report after the delay.
        assert_eq!(ctx.frames().len(), 2);
        assert_eq!(ctx.gmp_leave_group(DummyLinkDeviceId, GROUP_ADDR), GroupLeaveResult::Left(()));
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
    }

    #[test]
    fn test_mld_integration_not_last_does_not_send_leave() {
        let mut ctx = DummyCtx::default();
        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        assert_eq!(ctx.timers().len(), 1);
        assert_eq!(ctx.frames().len(), 1);
        receive_mld_report(&mut ctx, GROUP_ADDR);
        assert_empty(ctx.timers().iter());
        // The report should be discarded because we have received from someone
        // else.
        assert_eq!(ctx.frames().len(), 1);
        assert_eq!(ctx.gmp_leave_group(DummyLinkDeviceId, GROUP_ADDR), GroupLeaveResult::Left(()));
        // A leave message is not sent.
        assert_eq!(ctx.frames().len(), 1);
        // The frames are all reports.
        for (_, frame) in ctx.frames() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }

    #[test]
    fn test_mld_with_link_local() {
        let mut ctx = DummyCtx::default();
        ctx.get_mut().ipv6_link_local = Some(MY_MAC.to_ipv6_link_local().addr());
        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        assert!(ctx.trigger_next_timer());
        for (_, frame) in ctx.frames() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 8, 24, MY_MAC.to_ipv6_link_local().addr().get());
        }
    }

    #[test]
    fn test_skip_mld() {
        // Test that we properly skip executing any `Actions` for addresses that
        // we're supposed to skip (see the comment in `run_actions`) and when
        // MLD is disabled for the device.
        let test = |mut ctx: DummyCtx, group| {
            ctx.get_mut().ipv6_link_local = Some(MY_MAC.to_ipv6_link_local().addr());

            // Assert that no observable effects have taken place.
            let assert_no_effect = |ctx: &DummyCtx| {
                assert_empty(ctx.timers());
                assert_empty(ctx.frames());
            };

            assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, group), GroupJoinResult::Joined(()));
            // We should have joined the group but not executed any `Actions`.
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

            assert_eq!(ctx.gmp_leave_group(DummyLinkDeviceId, group), GroupLeaveResult::Left(()));
            // We should have left the group but not executed any `Actions`.
            assert!(ctx.get_ref().groups.get(&group).is_none());
            assert_no_effect(&ctx);
        };

        // Test that we skip executing `Actions` for addresses we're supposed to
        // skip.
        test(DummyCtx::default(), Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS);
        let mut bytes = Ipv6::MULTICAST_SUBNET.network().ipv6_bytes();
        // Manually set the "scope" field to 0.
        bytes[1] = bytes[1] & 0xF0;
        let reserved0 = MulticastAddr::new(Ipv6Addr::from_bytes(bytes)).unwrap();
        // Manually set the "scope" field to 1 (interface-local).
        bytes[1] = (bytes[1] & 0xF0) | 1;
        let iface_local = MulticastAddr::new(Ipv6Addr::from_bytes(bytes)).unwrap();
        test(DummyCtx::default(), reserved0);
        test(DummyCtx::default(), iface_local);

        // Test that we skip executing `Actions` when MLD is disabled on the
        // device.
        let mut ctx = DummyCtx::default();
        ctx.get_mut().mld_enabled = false;
        test(ctx, GROUP_ADDR);
    }

    #[test]
    fn test_mld_integration_with_local_join_leave() {
        // Simple MLD integration test to check that when we call top-level
        // multicast join and leave functions, MLD is performed.

        let mut ctx = DummyCtx::default();

        assert_eq!(ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR), GroupJoinResult::Joined(()));
        assert_gmp_state!(ctx, &GROUP_ADDR, Delaying);
        assert_eq!(ctx.frames().len(), 1);
        assert_eq!(ctx.timers().len(), 1);
        let frame = &ctx.frames().last().unwrap().1;
        ensure_frame(frame, 131, GROUP_ADDR, GROUP_ADDR);
        ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);

        assert_eq!(
            ctx.gmp_join_group(DummyLinkDeviceId, GROUP_ADDR),
            GroupJoinResult::AlreadyMember
        );
        assert_gmp_state!(ctx, &GROUP_ADDR, Delaying);
        assert_eq!(ctx.frames().len(), 1);
        assert_eq!(ctx.timers().len(), 1);

        assert_eq!(
            ctx.gmp_leave_group(DummyLinkDeviceId, GROUP_ADDR),
            GroupLeaveResult::StillMember
        );
        assert_gmp_state!(ctx, &GROUP_ADDR, Delaying);
        assert_eq!(ctx.frames().len(), 1);
        assert_eq!(ctx.timers().len(), 1);

        assert_eq!(ctx.gmp_leave_group(DummyLinkDeviceId, GROUP_ADDR), GroupLeaveResult::Left(()));
        assert_eq!(ctx.frames().len(), 2);
        assert_empty(ctx.timers().iter());
        let frame = &ctx.frames().last().unwrap().1;
        ensure_frame(frame, 132, Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS, GROUP_ADDR);
        ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
    }
}
