// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Multicast Listener Discovery (MLD).
//!
//! MLD is derived from version 2 of IPv4's Internet Group Management Protocol,
//! IGMPv2. One important difference to note is that MLD uses ICMPv6 (IP
//! Protocol 58) message types, rather than IGMP (IP Protocol 2) message types.

use core::{convert::Infallible as Never, time::Duration};

use log::{debug, error};
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
    utils::NonZeroDuration,
};
use thiserror::Error;
use zerocopy::ByteSlice;

use crate::{
    context::{FrameContext, RngContext, TimerContext, TimerHandler},
    ip::{
        gmp::{
            gmp_handle_timer, handle_query_message, handle_report_message, GmpContext,
            GmpDelayedReportTimerId, GmpMessage, GmpMessageType, GmpState, GmpStateMachine, IpExt,
            ProtocolSpecific, QueryTarget,
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

/// The non-synchronized execution context for MLD.
pub(crate) trait MldNonSyncContext<DeviceId>:
    RngContext + TimerContext<MldDelayedReportTimerId<DeviceId>>
{
}
impl<DeviceId, C: RngContext + TimerContext<MldDelayedReportTimerId<DeviceId>>>
    MldNonSyncContext<DeviceId> for C
{
}

/// The execution context for the Multicast Listener Discovery (MLD) protocol.
pub(crate) trait MldContext<C: MldNonSyncContext<Self::DeviceId>>:
    IpDeviceIdContext<Ipv6> + FrameContext<C, EmptyBuf, MldFrameMetadata<Self::DeviceId>>
{
    /// Gets the IPv6 link local address on `device`.
    fn get_ipv6_link_local_addr(
        &self,
        device: Self::DeviceId,
    ) -> Option<LinkLocalUnicastAddr<Ipv6Addr>>;

    /// Calls the function with a mutable reference to the device's MLD state
    /// and whether or not MLD is enabled for the `device`.
    fn with_mld_state_mut<O, F: FnOnce(GmpState<'_, Ipv6Addr, MldGroupState<C::Instant>>) -> O>(
        &mut self,
        device: Self::DeviceId,
        cb: F,
    ) -> O;
}

/// A handler for incoming MLD packets.
///
/// A blanket implementation is provided for all `C: MldContext`.
pub(crate) trait MldPacketHandler<C, DeviceId> {
    /// Receive an MLD packet.
    fn receive_mld_packet<B: ByteSlice>(
        &mut self,
        ctx: &mut C,
        device: DeviceId,
        src_ip: Ipv6SourceAddr,
        dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: MldPacket<B>,
    );
}

impl<C: MldNonSyncContext<SC::DeviceId>, SC: MldContext<C>> MldPacketHandler<C, SC::DeviceId>
    for SC
{
    fn receive_mld_packet<B: ByteSlice>(
        &mut self,
        ctx: &mut C,
        device: SC::DeviceId,
        _src_ip: Ipv6SourceAddr,
        _dst_ip: SpecifiedAddr<Ipv6Addr>,
        packet: MldPacket<B>,
    ) {
        if let Err(e) = match packet {
            MldPacket::MulticastListenerQuery(msg) => {
                let body = msg.body();
                let addr = body.group_addr();
                SpecifiedAddr::new(addr)
                    .map_or(Some(QueryTarget::Unspecified), |addr| {
                        MulticastAddr::new(addr.get()).map(QueryTarget::Specified)
                    })
                    .map_or(Err(MldError::NotAMember { addr }), |group_addr| {
                        handle_query_message(
                            self,
                            ctx,
                            device,
                            group_addr,
                            body.max_response_delay(),
                        )
                    })
            }
            MldPacket::MulticastListenerReport(msg) => {
                let addr = msg.body().group_addr();
                MulticastAddr::new(msg.body().group_addr())
                    .map_or(Err(MldError::NotAMember { addr }), |group_addr| {
                        handle_report_message(self, ctx, device, group_addr)
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

impl IpExt for Ipv6 {
    fn should_perform_gmp(group_addr: MulticastAddr<Ipv6Addr>) -> bool {
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
        group_addr != Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS
            && ![Ipv6Scope::Reserved(Ipv6ReservedScope::Scope0), Ipv6Scope::InterfaceLocal]
                .contains(&group_addr.scope())
    }
}

impl<C: MldNonSyncContext<SC::DeviceId>, SC: MldContext<C>> GmpContext<Ipv6, C> for SC {
    type ProtocolSpecific = MldProtocolSpecific;
    type Err = MldError;
    type GroupState = MldGroupState<C::Instant>;

    fn send_message(
        &mut self,
        ctx: &mut C,
        device: Self::DeviceId,
        group_addr: MulticastAddr<Ipv6Addr>,
        msg_type: GmpMessageType<MldProtocolSpecific>,
    ) {
        let result = match msg_type {
            GmpMessageType::Report(MldProtocolSpecific) => send_mld_packet::<_, _, &[u8], _>(
                self,
                ctx,
                device,
                group_addr,
                MulticastListenerReport,
                group_addr,
                (),
            ),
            GmpMessageType::Leave => send_mld_packet::<_, _, &[u8], _>(
                self,
                ctx,
                device,
                Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS,
                MulticastListenerDone,
                group_addr,
                (),
            ),
        };

        match result {
            Ok(()) => {}
            Err(err) => error!(
                "error sending MLD message ({:?}) on device {} for group {}: {}",
                msg_type, device, group_addr, err
            ),
        }
    }

    fn run_actions(&mut self, _ctx: &mut C, device: SC::DeviceId, actions: Never) {
        unreachable!("actions ({:?} should not be constructable; device = {}", actions, device)
    }

    fn not_a_member_err(addr: Ipv6Addr) -> Self::Err {
        Self::Err::NotAMember { addr }
    }

    fn with_gmp_state_mut<O, F: FnOnce(GmpState<'_, Ipv6Addr, MldGroupState<C::Instant>>) -> O>(
        &mut self,
        device: Self::DeviceId,
        cb: F,
    ) -> O {
        self.with_mld_state_mut(device, cb)
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
    type Actions = Never;
    type Config = MldConfig;

    fn cfg_unsolicited_report_interval(cfg: &Self::Config) -> Duration {
        cfg.unsolicited_report_interval
    }

    fn cfg_send_leave_anyway(cfg: &Self::Config) -> bool {
        cfg.send_leave_anyway
    }

    fn get_max_resp_time(resp_time: Duration) -> Option<NonZeroDuration> {
        NonZeroDuration::new(resp_time)
    }

    fn do_query_received_specific(
        _cfg: &Self::Config,
        _max_resp_time: Duration,
        old: Self,
    ) -> (Self, Option<Never>) {
        (old, None)
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

impl<I: Instant> AsMut<GmpStateMachine<I, MldProtocolSpecific>> for MldGroupState<I> {
    fn as_mut(&mut self) -> &mut GmpStateMachine<I, MldProtocolSpecific> {
        let Self(s) = self;
        s
    }
}

/// An MLD timer to delay the sending of a report.
#[derive(PartialEq, Eq, Clone, Copy, Debug, Hash)]
pub(crate) struct MldDelayedReportTimerId<DeviceId>(
    pub(crate) GmpDelayedReportTimerId<Ipv6Addr, DeviceId>,
);

impl<DeviceId> From<GmpDelayedReportTimerId<Ipv6Addr, DeviceId>>
    for MldDelayedReportTimerId<DeviceId>
{
    fn from(id: GmpDelayedReportTimerId<Ipv6Addr, DeviceId>) -> MldDelayedReportTimerId<DeviceId> {
        MldDelayedReportTimerId(id)
    }
}

impl_timer_context!(
    DeviceId,
    MldDelayedReportTimerId<DeviceId>,
    GmpDelayedReportTimerId<Ipv6Addr, DeviceId>,
    MldDelayedReportTimerId(id),
    id
);

impl<C: MldNonSyncContext<SC::DeviceId>, SC: MldContext<C>>
    TimerHandler<C, MldDelayedReportTimerId<SC::DeviceId>> for SC
{
    fn handle_timer(&mut self, ctx: &mut C, timer: MldDelayedReportTimerId<SC::DeviceId>) {
        let MldDelayedReportTimerId(id) = timer;
        gmp_handle_timer(self, ctx, id);
    }
}

/// Send an MLD packet.
///
/// The MLD packet being sent should have its `hop_limit` to be 1 and a
/// `RouterAlert` option in its Hop-by-Hop Options extensions header.
fn send_mld_packet<
    C: MldNonSyncContext<SC::DeviceId>,
    SC: MldContext<C>,
    B: ByteSlice,
    M: IcmpMldv1MessageType<B>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device: SC::DeviceId,
    dst_ip: MulticastAddr<Ipv6Addr>,
    msg: M,
    group_addr: M::GroupAddr,
    max_resp_delay: M::MaxRespDelay,
) -> MldResult<()> {
    // According to https://tools.ietf.org/html/rfc3590#section-4, if a valid
    // link-local address is not available for the device (e.g., one has not
    // been configured), the message is sent with the unspecified address (::)
    // as the IPv6 source address.
    //
    // TODO(https://fxbug.dev/98534): Handle an IPv6 link-local address being
    // assigned when reports were sent with the unspecified source address.
    let src_ip =
        sync_ctx.get_ipv6_link_local_addr(device).map_or(Ipv6::UNSPECIFIED_ADDRESS, |x| x.get());

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
    sync_ctx
        .send_frame(ctx, MldFrameMetadata::new(device, dst_ip), body)
        .map_err(|_| MldError::SendFailure { addr: group_addr.into() })
}

#[cfg(test)]
mod tests {
    use core::convert::TryInto;

    use net_types::ethernet::Mac;
    use packet::ParseBuffer;
    use packet_formats::{
        icmp::{
            mld::{MulticastListenerDone, MulticastListenerQuery, MulticastListenerReport},
            IcmpParseArgs, Icmpv6MessageType, Icmpv6Packet,
        },
        testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame,
    };

    use super::*;
    use crate::{
        context::{
            testutil::{DummyInstant, DummyTimerCtxExt},
            InstantContext as _,
        },
        ip::{
            device::Ipv6DeviceTimerId,
            gmp::{
                GmpHandler as _, GroupJoinResult, GroupLeaveResult, MemberState, MulticastGroupSet,
                QueryReceivedActions, QueryReceivedGenericAction,
            },
            testutil::DummyDeviceId,
        },
        testutil::{
            assert_empty, new_rng, run_with_many_seeds, DummyEventDispatcherConfig, TestIpExt as _,
        },
        Ctx, StackStateBuilder, TimerId, TimerIdInner,
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

    type MockCtx = crate::context::testutil::DummyCtx<
        DummyMldCtx,
        MldDelayedReportTimerId<DummyDeviceId>,
        MldFrameMetadata<DummyDeviceId>,
        (),
        DummyDeviceId,
        (),
    >;
    type MockSyncCtx = crate::context::testutil::DummySyncCtx<
        DummyMldCtx,
        MldFrameMetadata<DummyDeviceId>,
        DummyDeviceId,
    >;
    type MockNonSyncCtx =
        crate::context::testutil::DummyNonSyncCtx<MldDelayedReportTimerId<DummyDeviceId>, (), ()>;

    impl MldContext<MockNonSyncCtx> for MockSyncCtx {
        fn get_ipv6_link_local_addr(
            &self,
            _device: DummyDeviceId,
        ) -> Option<LinkLocalUnicastAddr<Ipv6Addr>> {
            self.get_ref().ipv6_link_local
        }

        fn with_mld_state_mut<
            O,
            F: FnOnce(GmpState<'_, Ipv6Addr, MldGroupState<DummyInstant>>) -> O,
        >(
            &mut self,
            DummyDeviceId: DummyDeviceId,
            cb: F,
        ) -> O {
            let DummyMldCtx { groups, mld_enabled, ipv6_link_local: _ } = self.get_mut();
            cb(GmpState { enabled: *mld_enabled, groups })
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
                false,
            );
            assert_eq!(
                s.query_received(&mut rng, Duration::from_secs(0), DummyInstant::default()),
                QueryReceivedActions {
                    generic: Some(QueryReceivedGenericAction::StopTimerAndSendReport(
                        MldProtocolSpecific
                    )),
                    protocol_specific: None
                }
            );
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
    const TIMER_ID: MldDelayedReportTimerId<DummyDeviceId> =
        MldDelayedReportTimerId(GmpDelayedReportTimerId {
            device: DummyDeviceId,
            group_addr: GROUP_ADDR,
        });

    fn receive_mld_query(
        sync_ctx: &mut MockSyncCtx,
        non_sync_ctx: &mut MockNonSyncCtx,
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
            Icmpv6Packet::Mld(packet) => sync_ctx.receive_mld_packet(
                non_sync_ctx,
                DummyDeviceId,
                router_addr.try_into().unwrap(),
                MY_IP,
                packet,
            ),
            _ => panic!("serialized icmpv6 message is not an mld message"),
        }
    }

    fn receive_mld_report(
        sync_ctx: &mut MockSyncCtx,
        non_sync_ctx: &mut MockNonSyncCtx,
        group_addr: MulticastAddr<Ipv6Addr>,
    ) {
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
            Icmpv6Packet::Mld(packet) => sync_ctx.receive_mld_packet(
                non_sync_ctx,
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
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);

            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );

            receive_mld_query(
                &mut sync_ctx,
                &mut non_sync_ctx,
                Duration::from_secs(10),
                GROUP_ADDR,
            );
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                Some(TIMER_ID)
            );

            // We should get two MLD reports - one for the unsolicited one for
            // the host to turn into Delay Member state and the other one for
            // the timer being fired.
            assert_eq!(sync_ctx.frames().len(), 2);
            // The frames are all reports.
            for (_, frame) in sync_ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_immediate_query() {
        run_with_many_seeds(|seed| {
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);

            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );
            assert_eq!(sync_ctx.frames().len(), 1);

            receive_mld_query(&mut sync_ctx, &mut non_sync_ctx, Duration::from_secs(0), GROUP_ADDR);
            // The query says that it wants to hear from us immediately.
            assert_eq!(sync_ctx.frames().len(), 2);
            // There should be no timers set.
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                None
            );
            // The frames are all reports.
            for (_, frame) in sync_ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_integration_fallback_from_idle() {
        run_with_many_seeds(|seed| {
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);

            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );
            assert_eq!(sync_ctx.frames().len(), 1);

            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                Some(TIMER_ID)
            );
            assert_eq!(sync_ctx.frames().len(), 2);

            receive_mld_query(
                &mut sync_ctx,
                &mut non_sync_ctx,
                Duration::from_secs(10),
                GROUP_ADDR,
            );

            // We have received a query, hence we are falling back to Delay
            // Member state.
            let MldGroupState(group_state) = sync_ctx.get_ref().groups.get(&GROUP_ADDR).unwrap();
            match group_state.get_inner() {
                MemberState::Delaying(_) => {}
                _ => panic!("Wrong State!"),
            }

            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                Some(TIMER_ID)
            );
            assert_eq!(sync_ctx.frames().len(), 3);
            // The frames are all reports.
            for (_, frame) in sync_ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_integration_immediate_query_wont_fallback() {
        run_with_many_seeds(|seed| {
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);

            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );
            assert_eq!(sync_ctx.frames().len(), 1);

            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                Some(TIMER_ID)
            );
            assert_eq!(sync_ctx.frames().len(), 2);

            receive_mld_query(&mut sync_ctx, &mut non_sync_ctx, Duration::from_secs(0), GROUP_ADDR);

            // Since it is an immediate query, we will send a report immediately
            // and turn into Idle state again.
            let MldGroupState(group_state) = sync_ctx.get_ref().groups.get(&GROUP_ADDR).unwrap();
            match group_state.get_inner() {
                MemberState::Idle(_) => {}
                _ => panic!("Wrong State!"),
            }

            // No timers!
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                None
            );
            assert_eq!(sync_ctx.frames().len(), 3);
            // The frames are all reports.
            for (_, frame) in sync_ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_integration_delay_reset_timer() {
        let MockCtx { mut sync_ctx, mut non_sync_ctx } =
            MockCtx::with_sync_ctx(MockSyncCtx::default());
        // This seed was carefully chosen to produce a substantial duration
        // value below.
        non_sync_ctx.seed_rng(123456);
        assert_eq!(
            sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
            GroupJoinResult::Joined(())
        );
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            TIMER_ID,
            DummyInstant::from(Duration::from_micros(590_354)),
        )]);
        let instant1 = non_sync_ctx.timer_ctx().timers()[0].0.clone();
        let start = non_sync_ctx.now();
        let duration = instant1 - start;

        receive_mld_query(&mut sync_ctx, &mut non_sync_ctx, duration, GROUP_ADDR);
        assert_eq!(sync_ctx.frames().len(), 1);
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            TIMER_ID,
            DummyInstant::from(Duration::from_micros(34_751)),
        )]);
        let instant2 = non_sync_ctx.timer_ctx().timers()[0].0.clone();
        // This new timer should be sooner.
        assert!(instant2 <= instant1);
        assert_eq!(
            non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
            Some(TIMER_ID)
        );
        assert!(non_sync_ctx.now() - start <= duration);
        assert_eq!(sync_ctx.frames().len(), 2);
        // The frames are all reports.
        for (_, frame) in sync_ctx.frames() {
            ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        }
    }

    #[test]
    fn test_mld_integration_last_send_leave() {
        run_with_many_seeds(|seed| {
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);

            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );
            let now = non_sync_ctx.now();
            non_sync_ctx.timer_ctx().assert_timers_installed([(
                TIMER_ID,
                now..=(now + DEFAULT_UNSOLICITED_REPORT_INTERVAL),
            )]);
            // The initial unsolicited report.
            assert_eq!(sync_ctx.frames().len(), 1);
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                Some(TIMER_ID)
            );
            // The report after the delay.
            assert_eq!(sync_ctx.frames().len(), 2);
            assert_eq!(
                sync_ctx.gmp_leave_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupLeaveResult::Left(())
            );
            // Our leave message.
            assert_eq!(sync_ctx.frames().len(), 3);
            // The first two messages should be reports.
            ensure_frame(&sync_ctx.frames()[0].1, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&sync_ctx.frames()[0].1, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            ensure_frame(&sync_ctx.frames()[1].1, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(&sync_ctx.frames()[1].1, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            // The last one should be the done message whose destination is all
            // routers.
            ensure_frame(
                &sync_ctx.frames()[2].1,
                132,
                Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS,
                GROUP_ADDR,
            );
            ensure_slice_addr(&sync_ctx.frames()[2].1, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        });
    }

    #[test]
    fn test_mld_integration_not_last_does_not_send_leave() {
        run_with_many_seeds(|seed| {
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);

            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );
            let now = non_sync_ctx.now();
            non_sync_ctx.timer_ctx().assert_timers_installed([(
                TIMER_ID,
                now..=(now + DEFAULT_UNSOLICITED_REPORT_INTERVAL),
            )]);
            assert_eq!(sync_ctx.frames().len(), 1);
            receive_mld_report(&mut sync_ctx, &mut non_sync_ctx, GROUP_ADDR);
            non_sync_ctx.timer_ctx().assert_no_timers_installed();
            // The report should be discarded because we have received from someone
            // else.
            assert_eq!(sync_ctx.frames().len(), 1);
            assert_eq!(
                sync_ctx.gmp_leave_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupLeaveResult::Left(())
            );
            // A leave message is not sent.
            assert_eq!(sync_ctx.frames().len(), 1);
            // The frames are all reports.
            for (_, frame) in sync_ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
            }
        });
    }

    #[test]
    fn test_mld_with_link_local() {
        run_with_many_seeds(|seed| {
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);

            sync_ctx.get_mut().ipv6_link_local = Some(MY_MAC.to_ipv6_link_local().addr());
            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                Some(TIMER_ID)
            );
            for (_, frame) in sync_ctx.frames() {
                ensure_frame(&frame, 131, GROUP_ADDR, GROUP_ADDR);
                ensure_slice_addr(&frame, 8, 24, MY_MAC.to_ipv6_link_local().addr().get());
            }
        });
    }

    #[test]
    fn test_skip_mld() {
        run_with_many_seeds(|seed| {
            // Test that we do not perform MLD for addresses that we're supposed
            // to skip or when MLD is disabled.
            let test = |MockCtx { mut sync_ctx, mut non_sync_ctx }, group| {
                sync_ctx.get_mut().ipv6_link_local = Some(MY_MAC.to_ipv6_link_local().addr());

                // Assert that no observable effects have taken place.
                let assert_no_effect = |sync_ctx: &MockSyncCtx, non_sync_ctx: &MockNonSyncCtx| {
                    non_sync_ctx.timer_ctx().assert_no_timers_installed();
                    assert_empty(sync_ctx.frames());
                };

                assert_eq!(
                    sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, group),
                    GroupJoinResult::Joined(())
                );
                // We should join the group but left in the GMP's non-member
                // state.
                assert_gmp_state!(sync_ctx, &group, NonMember);
                assert_no_effect(&sync_ctx, &non_sync_ctx);

                receive_mld_report(&mut sync_ctx, &mut non_sync_ctx, group);
                // We should have done no state transitions/work.
                assert_gmp_state!(sync_ctx, &group, NonMember);
                assert_no_effect(&sync_ctx, &non_sync_ctx);

                receive_mld_query(&mut sync_ctx, &mut non_sync_ctx, Duration::from_secs(10), group);
                // We should have done no state transitions/work.
                assert_gmp_state!(sync_ctx, &group, NonMember);
                assert_no_effect(&sync_ctx, &non_sync_ctx);

                assert_eq!(
                    sync_ctx.gmp_leave_group(&mut non_sync_ctx, DummyDeviceId, group),
                    GroupLeaveResult::Left(())
                );
                // We should have left the group but not executed any `Actions`.
                assert!(sync_ctx.get_ref().groups.get(&group).is_none());
                assert_no_effect(&sync_ctx, &non_sync_ctx);
            };

            let new_ctx = || {
                let mut ctx = MockCtx::with_sync_ctx(MockSyncCtx::default());
                ctx.non_sync_ctx.seed_rng(seed);
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
            ctx.sync_ctx.get_mut().mld_enabled = false;
            test(ctx, GROUP_ADDR);
        });
    }

    #[test]
    fn test_mld_integration_with_local_join_leave() {
        run_with_many_seeds(|seed| {
            // Simple MLD integration test to check that when we call top-level
            // multicast join and leave functions, MLD is performed.
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);

            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );
            assert_gmp_state!(sync_ctx, &GROUP_ADDR, Delaying);
            assert_eq!(sync_ctx.frames().len(), 1);
            let now = non_sync_ctx.now();
            let range = now..=(now + DEFAULT_UNSOLICITED_REPORT_INTERVAL);
            non_sync_ctx.timer_ctx().assert_timers_installed([(TIMER_ID, range.clone())]);
            let frame = &sync_ctx.frames().last().unwrap().1;
            ensure_frame(frame, 131, GROUP_ADDR, GROUP_ADDR);
            ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);

            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::AlreadyMember
            );
            assert_gmp_state!(sync_ctx, &GROUP_ADDR, Delaying);
            assert_eq!(sync_ctx.frames().len(), 1);
            non_sync_ctx.timer_ctx().assert_timers_installed([(TIMER_ID, range.clone())]);

            assert_eq!(
                sync_ctx.gmp_leave_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupLeaveResult::StillMember
            );
            assert_gmp_state!(sync_ctx, &GROUP_ADDR, Delaying);
            assert_eq!(sync_ctx.frames().len(), 1);
            non_sync_ctx.timer_ctx().assert_timers_installed([(TIMER_ID, range)]);

            assert_eq!(
                sync_ctx.gmp_leave_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupLeaveResult::Left(())
            );
            assert_eq!(sync_ctx.frames().len(), 2);
            non_sync_ctx.timer_ctx().assert_no_timers_installed();
            let frame = &sync_ctx.frames().last().unwrap().1;
            ensure_frame(frame, 132, Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS, GROUP_ADDR);
            ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
        });
    }

    #[test]
    fn test_mld_enable_disable() {
        run_with_many_seeds(|seed| {
            let MockCtx { mut sync_ctx, mut non_sync_ctx } =
                MockCtx::with_sync_ctx(MockSyncCtx::default());
            non_sync_ctx.seed_rng(seed);
            assert_eq!(sync_ctx.take_frames(), []);

            // Should not perform MLD for the all-nodes address.
            //
            // As per RFC 3810 Section 6,
            //
            //   No MLD messages are ever sent regarding neither the link-scope,
            //   all-nodes multicast address, nor any multicast address of scope
            //   0 (reserved) or 1 (node-local).
            assert_eq!(
                sync_ctx.gmp_join_group(
                    &mut non_sync_ctx,
                    DummyDeviceId,
                    Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS
                ),
                GroupJoinResult::Joined(())
            );
            assert_gmp_state!(sync_ctx, &Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS, NonMember);
            assert_eq!(
                sync_ctx.gmp_join_group(&mut non_sync_ctx, DummyDeviceId, GROUP_ADDR),
                GroupJoinResult::Joined(())
            );
            assert_gmp_state!(sync_ctx, &GROUP_ADDR, Delaying);
            assert_matches::assert_matches!(
                &sync_ctx.take_frames()[..],
                [(MldFrameMetadata { device: DummyDeviceId, dst_ip }, frame)] => {
                    assert_eq!(dst_ip, &GROUP_ADDR);
                    ensure_frame(frame, Icmpv6MessageType::MulticastListenerReport.into(), GROUP_ADDR, GROUP_ADDR);
                    ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
                }
            );

            // Should do nothing.
            sync_ctx.gmp_handle_maybe_enabled(&mut non_sync_ctx, DummyDeviceId);
            assert_gmp_state!(sync_ctx, &Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS, NonMember);
            assert_gmp_state!(sync_ctx, &GROUP_ADDR, Delaying);
            assert_eq!(sync_ctx.take_frames(), []);

            // Should send done message.
            sync_ctx.gmp_handle_disabled(&mut non_sync_ctx, DummyDeviceId);
            assert_gmp_state!(sync_ctx, &Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS, NonMember);
            assert_gmp_state!(sync_ctx, &GROUP_ADDR, NonMember);
            assert_matches::assert_matches!(
                &sync_ctx.take_frames()[..],
                [(MldFrameMetadata { device: DummyDeviceId, dst_ip }, frame)] => {
                    assert_eq!(dst_ip, &Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS);
                    ensure_frame(frame, Icmpv6MessageType::MulticastListenerDone.into(), Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS, GROUP_ADDR);
                    ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
                }
            );

            // Should do nothing.
            sync_ctx.gmp_handle_disabled(&mut non_sync_ctx, DummyDeviceId);
            assert_gmp_state!(sync_ctx, &Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS, NonMember);
            assert_gmp_state!(sync_ctx, &GROUP_ADDR, NonMember);
            assert_eq!(sync_ctx.take_frames(), []);

            // Should send report message.
            sync_ctx.gmp_handle_maybe_enabled(&mut non_sync_ctx, DummyDeviceId);
            assert_gmp_state!(sync_ctx, &Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS, NonMember);
            assert_gmp_state!(sync_ctx, &GROUP_ADDR, Delaying);
            assert_matches::assert_matches!(
                &sync_ctx.take_frames()[..],
                [(MldFrameMetadata { device: DummyDeviceId, dst_ip }, frame)] => {
                    assert_eq!(dst_ip, &GROUP_ADDR);
                    ensure_frame(frame, Icmpv6MessageType::MulticastListenerReport.into(), GROUP_ADDR, GROUP_ADDR);
                    ensure_slice_addr(frame, 8, 24, Ipv6::UNSPECIFIED_ADDRESS);
                }
            );
        });
    }

    #[test]
    fn test_mld_enable_disable_integration() {
        let DummyEventDispatcherConfig {
            local_mac,
            remote_mac: _,
            local_ip: _,
            remote_ip: _,
            subnet: _,
        } = Ipv6::DUMMY_CONFIG;

        let crate::testutil::DummyCtx { sync_ctx, mut non_sync_ctx } =
            Ctx::new_with_builder(StackStateBuilder::default());
        let mut sync_ctx = &sync_ctx;
        let device_id =
            sync_ctx.state.device.add_ethernet_device(local_mac, Ipv6::MINIMUM_LINK_MTU.into());

        let now = non_sync_ctx.now();
        let ll_addr = local_mac.to_ipv6_link_local().addr();
        let snmc_addr = ll_addr.to_solicited_node_address();
        let snmc_timer_id = TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Mld(
            MldDelayedReportTimerId(GmpDelayedReportTimerId {
                device: device_id,
                group_addr: snmc_addr,
            })
            .into(),
        )));
        let range = now..=(now + DEFAULT_UNSOLICITED_REPORT_INTERVAL);
        struct TestConfig {
            ip_enabled: bool,
            gmp_enabled: bool,
        }
        let set_config = |sync_ctx: &mut &crate::testutil::DummySyncCtx,
                          non_sync_ctx: &mut crate::testutil::DummyNonSyncCtx,
                          TestConfig { ip_enabled, gmp_enabled }| {
            crate::ip::device::update_ipv6_configuration(
                sync_ctx,
                non_sync_ctx,
                device_id,
                |config| {
                    // TODO(https://fxbug.dev/98534): Make sure that DAD resolving
                    // for a link-local address results in reports sent with a
                    // specified source address.
                    config.dad_transmits = None;
                    config.max_router_solicitations = None;
                    config.ip_config.ip_enabled = ip_enabled;
                    config.ip_config.gmp_enabled = gmp_enabled;
                },
            );
        };
        let check_sent_report = |non_sync_ctx: &mut crate::testutil::DummyNonSyncCtx,
                                 specified_source: bool| {
            assert_matches::assert_matches!(
                &non_sync_ctx.take_frames()[..],
                [(egress_device, frame)] => {
                    assert_eq!(egress_device, &device_id);
                    let (src_mac, dst_mac, src_ip, dst_ip, ttl, _message, code) =
                        parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                            Ipv6,
                            _,
                            MulticastListenerReport,
                            _,
                        >(frame, |icmp| {
                            assert_eq!(icmp.body().group_addr, snmc_addr.get());
                        }).unwrap();
                    assert_eq!(src_mac, local_mac.get());
                    assert_eq!(dst_mac, Mac::from(&snmc_addr));
                    assert_eq!(
                        src_ip,
                        if specified_source {
                            ll_addr.get()
                        } else {
                            Ipv6::UNSPECIFIED_ADDRESS
                        }
                    );
                    assert_eq!(dst_ip, snmc_addr.get());
                    assert_eq!(ttl, 1);
                    assert_eq!(code, IcmpUnusedCode);
                }
            );
        };
        let check_sent_done = |non_sync_ctx: &mut crate::testutil::DummyNonSyncCtx,
                               specified_source: bool| {
            assert_matches::assert_matches!(
                &non_sync_ctx.take_frames()[..],
                [(egress_device, frame)] => {
                    assert_eq!(egress_device, &device_id);
                    let (src_mac, dst_mac, src_ip, dst_ip, ttl, _message, code) =
                        parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                            Ipv6,
                            _,
                            MulticastListenerDone,
                            _,
                        >(frame, |icmp| {
                            assert_eq!(icmp.body().group_addr, snmc_addr.get());
                        }).unwrap();
                    assert_eq!(src_mac, local_mac.get());
                    assert_eq!(dst_mac, Mac::from(&Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS));
                    assert_eq!(
                        src_ip,
                        if specified_source {
                            ll_addr.get()
                        } else {
                            Ipv6::UNSPECIFIED_ADDRESS
                        }
                    );
                    assert_eq!(dst_ip, Ipv6::ALL_ROUTERS_LINK_LOCAL_MULTICAST_ADDRESS.get());
                    assert_eq!(ttl, 1);
                    assert_eq!(code, IcmpUnusedCode);
                }
            );
        };

        // Enable IPv6 and MLD.
        //
        // MLD should be performed for the auto-generated link-local address's
        // solicited-node multicast address.
        set_config(
            &mut sync_ctx,
            &mut non_sync_ctx,
            TestConfig { ip_enabled: true, gmp_enabled: true },
        );
        non_sync_ctx.timer_ctx().assert_timers_installed([(snmc_timer_id, range.clone())]);
        check_sent_report(&mut non_sync_ctx, false);

        // Disable MLD.
        set_config(
            &mut sync_ctx,
            &mut non_sync_ctx,
            TestConfig { ip_enabled: true, gmp_enabled: false },
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        check_sent_done(&mut non_sync_ctx, true);

        // Enable MLD but disable IPv6.
        //
        // Should do nothing.
        set_config(
            &mut sync_ctx,
            &mut non_sync_ctx,
            TestConfig { ip_enabled: false, gmp_enabled: true },
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_matches::assert_matches!(&non_sync_ctx.take_frames()[..], []);

        // Disable MLD but enable IPv6.
        //
        // Should do nothing.
        set_config(
            &mut sync_ctx,
            &mut non_sync_ctx,
            TestConfig { ip_enabled: true, gmp_enabled: false },
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_matches::assert_matches!(&non_sync_ctx.take_frames()[..], []);

        // Enable MLD.
        set_config(
            &mut sync_ctx,
            &mut non_sync_ctx,
            TestConfig { ip_enabled: true, gmp_enabled: true },
        );
        non_sync_ctx.timer_ctx().assert_timers_installed([(snmc_timer_id, range.clone())]);
        check_sent_report(&mut non_sync_ctx, true);

        // Disable IPv6.
        set_config(
            &mut sync_ctx,
            &mut non_sync_ctx,
            TestConfig { ip_enabled: false, gmp_enabled: true },
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        check_sent_done(&mut non_sync_ctx, false);

        // Enable IPv6.
        set_config(
            &mut sync_ctx,
            &mut non_sync_ctx,
            TestConfig { ip_enabled: true, gmp_enabled: true },
        );
        non_sync_ctx.timer_ctx().assert_timers_installed([(snmc_timer_id, range)]);
        check_sent_report(&mut non_sync_ctx, false);
    }
}
