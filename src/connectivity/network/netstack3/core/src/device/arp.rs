// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Address Resolution Protocol (ARP).

use core::time::Duration;

use log::{debug, trace, warn};
use net_types::{
    ip::{Ipv4, Ipv4Addr},
    SpecifiedAddr, UnicastAddr, UnicastAddress, Witness as _,
};
use packet::{BufferMut, EmptyBuf, InnerPacketBuilder, Serializer};
use packet_formats::{
    arp::{ArpOp, ArpPacket, ArpPacketBuilder, HType},
    utils::NonZeroDuration,
};

use crate::{
    context::{CounterContext, FrameContext, TimerContext},
    device::{link::LinkDevice, DeviceIdContext},
    ip::device::nud::{
        BufferNudContext, DynamicNeighborUpdateSource, NudContext, NudHandler, NudState, NudTimerId,
    },
};

// NOTE(joshlf): This may seem a bit odd. Why not just say that `ArpDevice` is a
// sub-trait of `L: LinkDevice` where `L::Address: HType`? Unfortunately, rustc
// is still pretty bad at reasoning about where clauses. In a (never published)
// earlier version of this code, I tried that approach. Even simple function
// signatures like `fn foo<D: ArpDevice, P: PType, C, SC: ArpContext<D, P, C>>()` were
// too much for rustc to handle. Even without trying to actually use the
// associated `Address` type, that function signature alone would cause rustc to
// complain that it wasn't guaranteed that `D::Address: HType`.
//
// Doing it this way instead sidesteps the problem by taking the `where` clause
// out of the definition of `ArpDevice`. It's still present in the blanket impl,
// but rustc seems OK with that.

/// A link device whose addressing scheme is supported by ARP.
///
/// `ArpDevice` is implemented for all `L: LinkDevice where L::Address: HType`.
pub(crate) trait ArpDevice: LinkDevice<Address = Self::HType> {
    type HType: HType + UnicastAddress;
}

impl<L: LinkDevice> ArpDevice for L
where
    L::Address: HType + UnicastAddress,
{
    type HType = L::Address;
}

/// The identifier for timer events in the ARP layer.
pub(crate) type ArpTimerId<D, DeviceId> = NudTimerId<Ipv4, D, DeviceId>;

/// The metadata associated with an ARP frame.
#[cfg_attr(test, derive(Debug, PartialEq))]
pub(crate) struct ArpFrameMetadata<D: ArpDevice, DeviceId> {
    /// The ID of the ARP device.
    pub(super) device_id: DeviceId,
    /// The destination hardware address.
    pub(super) dst_addr: D::HType,
}

/// An execution context for the ARP protocol when a buffer is provided.
///
/// `BufferArpContext` is like [`ArpContext`], except that it also requires that
/// the context be capable of receiving frames in buffers of type `B`. This is
/// used when a buffer of type `B` is provided to ARP (in particular, in
/// [`handle_packet`]), and allows ARP to reuse that buffer rather than needing
/// to always allocate a new one.
pub(crate) trait BufferArpContext<D: ArpDevice, C: ArpNonSyncCtx<D, Self::DeviceId>, B: BufferMut>:
    ArpContext<D, C> + FrameContext<C, B, ArpFrameMetadata<D, Self::DeviceId>>
{
    /// Send an IP packet to the neighbor with address `dst_link_address`.
    fn send_ip_packet_to_neighbor_link_addr<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        device_id: &Self::DeviceId,
        dst_link_address: D::HType,
        body: S,
    ) -> Result<(), S>;
}

// NOTE(joshlf): The `ArpDevice` parameter may seem unnecessary. We only ever
// use the associated `HType` type, so why not just take that directly? By the
// same token, why have it as a parameter on `ArpState`, `ArpTimerId`, and
// `ArpFrameMetadata`? The answer is that, if we did, there would be no way to
// distinguish between different link device protocols that all happened to use
// the same hardware addressing scheme.
//
// Consider that the way that we implement context traits is via blanket impls.
// Even though each module's code _feels_ isolated from the rest of the system,
// in reality, all context impls end up on the same context type. In particular,
// all impls are of the form `impl<C: SomeContextTrait> SomeOtherContextTrait
// for C`. The `C` is the same throughout the whole stack.
//
// Thus, for two different link device protocols with the same `HType` and
// `PType`, if we used an `HType` parameter rather than an `ArpDevice`
// parameter, the `ArpContext` impls would conflict (in fact, the
// `StateContext`, `TimerContext`, and `FrameContext` impls would all conflict
// for similar reasons).

/// The non-synchronized execution context for the ARP protocol.
pub(crate) trait ArpNonSyncCtx<D: ArpDevice, DeviceId>:
    TimerContext<ArpTimerId<D, DeviceId>> + CounterContext
{
}
impl<DeviceId, D: ArpDevice, C: TimerContext<ArpTimerId<D, DeviceId>> + CounterContext>
    ArpNonSyncCtx<D, DeviceId> for C
{
}

/// An execution context for the ARP protocol.
pub(crate) trait ArpContext<D: ArpDevice, C: ArpNonSyncCtx<D, Self::DeviceId>>:
    DeviceIdContext<D> + FrameContext<C, EmptyBuf, ArpFrameMetadata<D, Self::DeviceId>>
{
    /// Get a protocol address of this interface.
    ///
    /// If `device_id` does not have any addresses associated with it, return
    /// `None`.
    fn get_protocol_addr(&self, ctx: &mut C, device_id: &Self::DeviceId) -> Option<Ipv4Addr>;

    /// Get the hardware address of this interface.
    fn get_hardware_addr(&self, ctx: &mut C, device_id: &Self::DeviceId) -> UnicastAddr<D::HType>;

    /// Calls the function with a mutable reference to ARP state.
    fn with_arp_state_mut<O, F: FnOnce(&mut ArpState<D>) -> O>(
        &mut self,
        device_id: &Self::DeviceId,
        cb: F,
    ) -> O;
}

impl<D: ArpDevice, C: ArpNonSyncCtx<D, SC::DeviceId>, SC: ArpContext<D, C>> NudContext<Ipv4, D, C>
    for SC
{
    fn retrans_timer(&self, _device_id: &SC::DeviceId) -> NonZeroDuration {
        NonZeroDuration::new(DEFAULT_ARP_REQUEST_PERIOD).unwrap()
    }

    fn with_nud_state_mut<O, F: FnOnce(&mut NudState<Ipv4, D::Address>) -> O>(
        &mut self,
        device_id: &SC::DeviceId,
        cb: F,
    ) -> O {
        self.with_arp_state_mut(device_id, |ArpState { nud }| cb(nud))
    }

    fn send_neighbor_solicitation(
        &mut self,
        ctx: &mut C,
        device_id: &SC::DeviceId,
        lookup_addr: SpecifiedAddr<Ipv4Addr>,
    ) {
        send_arp_request(self, ctx, device_id, lookup_addr.get())
    }
}

impl<
        B: BufferMut,
        D: ArpDevice,
        C: ArpNonSyncCtx<D, SC::DeviceId>,
        SC: BufferArpContext<D, C, B>,
    > BufferNudContext<B, Ipv4, D, C> for SC
{
    fn send_ip_packet_to_neighbor_link_addr<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        device_id: &SC::DeviceId,
        dst_mac: D::HType,
        body: S,
    ) -> Result<(), S> {
        BufferArpContext::send_ip_packet_to_neighbor_link_addr(self, ctx, device_id, dst_mac, body)
    }
}

pub(crate) trait ArpPacketHandler<B: BufferMut, D: ArpDevice, C>:
    DeviceIdContext<D>
{
    fn handle_packet(&mut self, ctx: &mut C, device_id: Self::DeviceId, buffer: B);
}

impl<
        B: BufferMut,
        D: ArpDevice,
        C: ArpNonSyncCtx<D, SC::DeviceId>,
        SC: BufferArpContext<D, C, B> + NudHandler<Ipv4, D, C>,
    > ArpPacketHandler<B, D, C> for SC
{
    /// Handles an inbound ARP packet.
    fn handle_packet(&mut self, ctx: &mut C, device_id: Self::DeviceId, buffer: B) {
        handle_packet(self, ctx, device_id, buffer)
    }
}

fn handle_packet<
    D: ArpDevice,
    C: ArpNonSyncCtx<D, SC::DeviceId>,
    B: BufferMut,
    SC: BufferArpContext<D, C, B> + NudHandler<Ipv4, D, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    mut buffer: B,
) {
    // TODO(wesleyac) Add support for probe.
    let packet = match buffer.parse::<ArpPacket<_, D::HType, Ipv4Addr>>() {
        Ok(packet) => packet,
        Err(err) => {
            // If parse failed, it's because either the packet was malformed, or
            // it was for an unexpected hardware or network protocol. In either
            // case, we just drop the packet and move on. RFC 826's "Packet
            // Reception" section says of packet processing algorithm, "Negative
            // conditionals indicate an end of processing and a discarding of
            // the packet."
            debug!("discarding malformed ARP packet: {}", err);
            return;
        }
    };

    let source = match packet.operation() {
        ArpOp::Request => DynamicNeighborUpdateSource::Probe,
        ArpOp::Response => DynamicNeighborUpdateSource::Confirmation,
        ArpOp::Other(o) => {
            debug!("dropping arp packet with op = {:?}", o);
            return;
        }
    };

    enum PacketKind {
        Gratuitous,
        AddressedToMe,
    }

    // The following logic is equivalent to the "Packet Reception" section of
    // RFC 826.
    //
    // We statically know that the hardware type and protocol type are correct,
    // so we do not need to have additional code to check that. The remainder of
    // the algorithm is:
    //
    // Merge_flag := false
    // If the pair <protocol type, sender protocol address> is
    //     already in my translation table, update the sender
    //     hardware address field of the entry with the new
    //     information in the packet and set Merge_flag to true.
    // ?Am I the target protocol address?
    // Yes:
    //   If Merge_flag is false, add the triplet <protocol type,
    //       sender protocol address, sender hardware address> to
    //       the translation table.
    //   ?Is the opcode ares_op$REQUEST?  (NOW look at the opcode!!)
    //   Yes:
    //     Swap hardware and protocol fields, putting the local
    //         hardware and protocol addresses in the sender fields.
    //     Set the ar$op field to ares_op$REPLY
    //     Send the packet to the (new) target hardware address on
    //         the same hardware on which the request was received.
    //
    // This can be summed up as follows:
    //
    // +----------+---------------+---------------+-----------------------------+
    // | opcode   | Am I the TPA? | SPA in table? | action                      |
    // +----------+---------------+---------------+-----------------------------+
    // | REQUEST  | yes           | yes           | Update table, Send response |
    // | REQUEST  | yes           | no            | Update table, Send response |
    // | REQUEST  | no            | yes           | Update table                |
    // | REQUEST  | no            | no            | NOP                         |
    // | RESPONSE | yes           | yes           | Update table                |
    // | RESPONSE | yes           | no            | Update table                |
    // | RESPONSE | no            | yes           | Update table                |
    // | RESPONSE | no            | no            | NOP                         |
    // +----------+---------------+---------------+-----------------------------+

    let sender_addr = packet.sender_protocol_address();
    let target_addr = packet.target_protocol_address();
    let (source, kind) = match (
        sender_addr == target_addr,
        Some(target_addr) == sync_ctx.get_protocol_addr(ctx, &device_id),
    ) {
        (true, false) => {
            // Treat all GARP messages as neighbor probes as GARPs are not
            // responses for previously sent requests, even if the packet
            // operation is a response OP code.
            //
            // Per RFC 5944 section 4.6,
            //
            //   A Gratuitous ARP [45] is an ARP packet sent by a node in order
            //   to spontaneously cause other nodes to update an entry in their
            //   ARP cache. A gratuitous ARP MAY use either an ARP Request or an
            //   ARP Reply packet. In either case, the ARP Sender Protocol
            //   Address and ARP Target Protocol Address are both set to the IP
            //   address of the cache entry to be updated, and the ARP Sender
            //   Hardware Address is set to the link-layer address to which this
            //   cache entry should be updated. When using an ARP Reply packet,
            //   the Target Hardware Address is also set to the link-layer
            //   address to which this cache entry should be updated (this field
            //   is not used in an ARP Request packet).
            //
            //   In either case, for a gratuitous ARP, the ARP packet MUST be
            //   transmitted as a local broadcast packet on the local link. As
            //   specified in [16], any node receiving any ARP packet (Request
            //   or Reply) MUST update its local ARP cache with the Sender
            //   Protocol and Hardware Addresses in the ARP packet, if the
            //   receiving node has an entry for that IP address already in its
            //   ARP cache. This requirement in the ARP protocol applies even
            //   for ARP Request packets, and for ARP Reply packets that do not
            //   match any ARP Request transmitted by the receiving node [16].
            (DynamicNeighborUpdateSource::Probe, PacketKind::Gratuitous)
        }
        (false, true) => (source, PacketKind::AddressedToMe),
        (false, false) => {
            trace!(
                "non-gratuitous ARP packet not targetting us; sender = {}, target={}",
                sender_addr,
                target_addr
            );
            return;
        }
        (true, true) => {
            warn!(
                "got gratuitous ARP packet with our address {} on device {}, dropping...",
                target_addr, device_id
            );
            return;
        }
    };

    let sender_hw_addr = packet.sender_hardware_address();
    if let Some(addr) = SpecifiedAddr::new(sender_addr) {
        NudHandler::<Ipv4, D, _>::set_dynamic_neighbor(
            sync_ctx,
            ctx,
            &device_id,
            addr,
            sender_hw_addr,
            source,
        )
    };

    match kind {
        PacketKind::Gratuitous => return,
        PacketKind::AddressedToMe => match source {
            DynamicNeighborUpdateSource::Probe => {
                let self_hw_addr = sync_ctx.get_hardware_addr(ctx, &device_id);

                // TODO(joshlf): Do something if send_frame returns an error?
                let _: Result<(), _> = FrameContext::send_frame(
                    sync_ctx,
                    ctx,
                    ArpFrameMetadata { device_id, dst_addr: sender_hw_addr },
                    ArpPacketBuilder::new(
                        ArpOp::Response,
                        self_hw_addr.get(),
                        target_addr,
                        sender_hw_addr,
                        sender_addr,
                    )
                    .into_serializer_with(buffer),
                );
            }
            DynamicNeighborUpdateSource::Confirmation => {}
        },
    }
}

// Currently at 20 seconds because that's what FreeBSD does.
const DEFAULT_ARP_REQUEST_PERIOD: Duration = Duration::from_secs(20);

fn send_arp_request<D: ArpDevice, C: ArpNonSyncCtx<D, SC::DeviceId>, SC: ArpContext<D, C>>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: &SC::DeviceId,
    lookup_addr: Ipv4Addr,
) {
    if let Some(sender_protocol_addr) = sync_ctx.get_protocol_addr(ctx, device_id) {
        let self_hw_addr = sync_ctx.get_hardware_addr(ctx, device_id);
        // TODO(joshlf): Do something if send_frame returns an error?
        let _ = FrameContext::send_frame(
            sync_ctx,
            ctx,
            ArpFrameMetadata { device_id: device_id.clone(), dst_addr: D::HType::BROADCAST },
            ArpPacketBuilder::new(
                ArpOp::Request,
                self_hw_addr.get(),
                sender_protocol_addr,
                // This is meaningless, since RFC 826 does not specify the
                // behaviour. However, the broadcast address is sensible, as
                // this is the actual address we are sending the packet to.
                D::HType::BROADCAST,
                lookup_addr,
            )
            .into_serializer(),
        );
    } else {
        // RFC 826 does not specify what to do if we don't have a local address,
        // but there is no reasonable way to send an ARP request without one (as
        // the receiver will cache our local address on receiving the packet.
        // So, if this is the case, we do not send an ARP request.
        // TODO(wesleyac): Should we cache these, and send packets once we have
        // an address?
        debug!("Not sending ARP request, since we don't know our local protocol address");
    }
}

/// The state associated with an instance of the Address Resolution Protocol
/// (ARP).
///
/// Each device will contain an `ArpState` object for each of the network
/// protocols that it supports.
pub(crate) struct ArpState<D: ArpDevice> {
    nud: NudState<Ipv4, D::HType>,
}

impl<D: ArpDevice> Default for ArpState<D> {
    fn default() -> Self {
        ArpState { nud: Default::default() }
    }
}

#[cfg(test)]
mod tests {
    use alloc::{vec, vec::Vec};
    use core::iter;

    use net_types::{ethernet::Mac, ip::Ipv4Addr};
    use packet::{Buf, ParseBuffer, Serializer};
    use packet_formats::arp::{peek_arp_types, ArpHardwareType, ArpNetworkType, ArpPacketBuilder};
    use test_case::test_case;

    use super::*;
    use crate::{
        context::{
            testutil::{FakeCtx, FakeNetwork, FakeNonSyncCtx, FakeSyncCtx},
            TimerHandler,
        },
        device::{ethernet::EthernetLinkDevice, link::testutil::FakeLinkDeviceId},
        ip::{
            device::nud::{
                testutil::{assert_dynamic_neighbor_with_addr, assert_neighbor_unknown},
                BufferNudHandler,
            },
            testutil::FakeDeviceId,
        },
        testutil::assert_empty,
    };

    const TEST_LOCAL_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_REMOTE_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_ANOTHER_REMOTE_IPV4: Ipv4Addr = Ipv4Addr::new([9, 10, 11, 12]);
    const TEST_LOCAL_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_REMOTE_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);
    const TEST_INVALID_MAC: Mac = Mac::new([0, 0, 0, 0, 0, 0]);

    /// A fake `ArpContext` that stores frames, address resolution events, and
    /// address resolution failure events.
    struct FakeArpCtx {
        proto_addr: Option<Ipv4Addr>,
        hw_addr: UnicastAddr<Mac>,
        arp_state: ArpState<EthernetLinkDevice>,
    }

    impl Default for FakeArpCtx {
        fn default() -> FakeArpCtx {
            FakeArpCtx {
                proto_addr: Some(TEST_LOCAL_IPV4),
                hw_addr: UnicastAddr::new(TEST_LOCAL_MAC).unwrap(),
                arp_state: ArpState::default(),
            }
        }
    }

    type FakeNonSyncCtxImpl =
        FakeNonSyncCtx<ArpTimerId<EthernetLinkDevice, FakeLinkDeviceId>, (), ()>;

    type FakeCtxImpl = FakeSyncCtx<
        FakeArpCtx,
        ArpFrameMetadata<EthernetLinkDevice, FakeLinkDeviceId>,
        FakeDeviceId,
    >;

    impl DeviceIdContext<EthernetLinkDevice> for FakeCtxImpl {
        type DeviceId = FakeLinkDeviceId;
    }

    impl ArpContext<EthernetLinkDevice, FakeNonSyncCtxImpl> for FakeCtxImpl {
        fn get_protocol_addr(
            &self,
            _ctx: &mut FakeNonSyncCtxImpl,
            _device_id: &FakeLinkDeviceId,
        ) -> Option<Ipv4Addr> {
            self.get_ref().proto_addr
        }

        fn get_hardware_addr(
            &self,
            _ctx: &mut FakeNonSyncCtxImpl,
            _device_id: &FakeLinkDeviceId,
        ) -> UnicastAddr<Mac> {
            self.get_ref().hw_addr
        }

        fn with_arp_state_mut<O, F: FnOnce(&mut ArpState<EthernetLinkDevice>) -> O>(
            &mut self,
            FakeLinkDeviceId: &FakeLinkDeviceId,
            cb: F,
        ) -> O {
            cb(&mut self.get_mut().arp_state)
        }
    }

    impl<B: BufferMut> BufferArpContext<EthernetLinkDevice, FakeNonSyncCtxImpl, B> for FakeCtxImpl {
        fn send_ip_packet_to_neighbor_link_addr<S: Serializer<Buffer = B>>(
            &mut self,
            _ctx: &mut FakeNonSyncCtxImpl,
            _device_id: &FakeLinkDeviceId,
            _dst_link_address: Mac,
            _body: S,
        ) -> Result<(), S> {
            Ok(())
        }
    }

    fn send_arp_packet(
        sync_ctx: &mut FakeCtxImpl,
        ctx: &mut FakeNonSyncCtxImpl,
        op: ArpOp,
        sender_ipv4: Ipv4Addr,
        target_ipv4: Ipv4Addr,
        sender_mac: Mac,
        target_mac: Mac,
    ) {
        let buf = ArpPacketBuilder::new(op, sender_mac, sender_ipv4, target_mac, target_ipv4)
            .into_serializer()
            .serialize_vec_outer()
            .unwrap();
        let (hw, proto) = peek_arp_types(buf.as_ref()).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, ArpNetworkType::Ipv4);

        handle_packet::<_, _, _, _>(sync_ctx, ctx, FakeLinkDeviceId, buf);
    }

    // Validate that buf is an ARP packet with the specific op, local_ipv4,
    // remote_ipv4, local_mac and remote_mac.
    fn validate_arp_packet(
        mut buf: &[u8],
        op: ArpOp,
        local_ipv4: Ipv4Addr,
        remote_ipv4: Ipv4Addr,
        local_mac: Mac,
        remote_mac: Mac,
    ) {
        let packet = buf.parse::<ArpPacket<_, Mac, Ipv4Addr>>().unwrap();
        assert_eq!(packet.sender_hardware_address(), local_mac);
        assert_eq!(packet.target_hardware_address(), remote_mac);
        assert_eq!(packet.sender_protocol_address(), local_ipv4);
        assert_eq!(packet.target_protocol_address(), remote_ipv4);
        assert_eq!(packet.operation(), op);
    }

    // Validate that we've sent `total_frames` frames in total, and that the
    // most recent one was sent to `dst` with the given ARP packet contents.
    fn validate_last_arp_packet(
        sync_ctx: &FakeCtxImpl,
        total_frames: usize,
        dst: Mac,
        op: ArpOp,
        local_ipv4: Ipv4Addr,
        remote_ipv4: Ipv4Addr,
        local_mac: Mac,
        remote_mac: Mac,
    ) {
        assert_eq!(sync_ctx.frames().len(), total_frames);
        let (meta, frame) = &sync_ctx.frames()[total_frames - 1];
        assert_eq!(meta.dst_addr, dst);
        validate_arp_packet(frame, op, local_ipv4, remote_ipv4, local_mac, remote_mac);
    }

    #[test]
    fn test_receive_gratuitous_arp_request() {
        // Test that, when we receive a gratuitous ARP request, we cache the
        // sender's address information, and we do not send a response.

        let FakeCtx { mut sync_ctx, mut non_sync_ctx } =
            FakeCtx::with_sync_ctx(FakeCtxImpl::default());
        send_arp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            ArpOp::Request,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_INVALID_MAC,
        );

        // We should have cached the sender's address information.
        assert_dynamic_neighbor_with_addr(
            &mut sync_ctx,
            FakeLinkDeviceId,
            SpecifiedAddr::new(TEST_REMOTE_IPV4).unwrap(),
            TEST_REMOTE_MAC,
        );
        // Gratuitous ARPs should not prompt a response.
        assert_empty(sync_ctx.frames().iter());
    }

    #[test]
    fn test_receive_gratuitous_arp_response() {
        // Test that, when we receive a gratuitous ARP response, we cache the
        // sender's address information, and we do not send a response.

        let FakeCtx { mut sync_ctx, mut non_sync_ctx } =
            FakeCtx::with_sync_ctx(FakeCtxImpl::default());
        send_arp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_REMOTE_MAC,
        );

        // We should have cached the sender's address information.
        assert_dynamic_neighbor_with_addr(
            &mut sync_ctx,
            FakeLinkDeviceId,
            SpecifiedAddr::new(TEST_REMOTE_IPV4).unwrap(),
            TEST_REMOTE_MAC,
        );
        // Gratuitous ARPs should not send a response.
        assert_empty(sync_ctx.frames().iter());
    }

    #[test]
    fn test_receive_gratuitous_arp_response_existing_request() {
        // Test that, if we have an outstanding request retry timer and receive
        // a gratuitous ARP for the same host, we cancel the timer and notify
        // the device layer.

        let FakeCtx { mut sync_ctx, mut non_sync_ctx } =
            FakeCtx::with_sync_ctx(FakeCtxImpl::default());

        // Trigger link resolution.
        assert_neighbor_unknown(
            &mut sync_ctx,
            FakeLinkDeviceId,
            SpecifiedAddr::new(TEST_REMOTE_IPV4).unwrap(),
        );
        assert_eq!(
            BufferNudHandler::send_ip_packet_to_neighbor(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &FakeLinkDeviceId,
                SpecifiedAddr::new(TEST_REMOTE_IPV4).unwrap(),
                Buf::new([1], ..),
            ),
            Ok(())
        );

        send_arp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_REMOTE_MAC,
        );

        // The response should now be in our cache.
        assert_dynamic_neighbor_with_addr(
            &mut sync_ctx,
            FakeLinkDeviceId,
            SpecifiedAddr::new(TEST_REMOTE_IPV4).unwrap(),
            TEST_REMOTE_MAC,
        );

        // Gratuitous ARPs should not send a response (the 1 frame is for the
        // original request).
        assert_eq!(sync_ctx.frames().len(), 1);
    }

    #[test]
    fn test_handle_arp_request() {
        // Test that, when we receive an ARP request, we cache the sender's
        // address information and send an ARP response.

        let FakeCtx { mut sync_ctx, mut non_sync_ctx } =
            FakeCtx::with_sync_ctx(FakeCtxImpl::default());

        send_arp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            ArpOp::Request,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_MAC,
            TEST_LOCAL_MAC,
        );

        // Make sure we cached the sender's address information.
        assert_dynamic_neighbor_with_addr(
            &mut sync_ctx,
            FakeLinkDeviceId,
            SpecifiedAddr::new(TEST_REMOTE_IPV4).unwrap(),
            TEST_REMOTE_MAC,
        );

        // We should have sent an ARP response.
        validate_last_arp_packet(
            &sync_ctx,
            1,
            TEST_REMOTE_MAC,
            ArpOp::Response,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            TEST_REMOTE_MAC,
        );
    }

    struct ArpHostConfig<'a> {
        name: &'a str,
        proto_addr: Ipv4Addr,
        hw_addr: Mac,
    }

    #[test_case(ArpHostConfig {
                    name: "remote",
                    proto_addr: TEST_REMOTE_IPV4,
                    hw_addr: TEST_REMOTE_MAC
                },
                vec![]
    )]
    #[test_case(ArpHostConfig {
                    name: "requested_remote",
                    proto_addr: TEST_REMOTE_IPV4,
                    hw_addr: TEST_REMOTE_MAC
                },
                vec![
                    ArpHostConfig {
                        name: "non_requested_remote",
                        proto_addr: TEST_ANOTHER_REMOTE_IPV4,
                        hw_addr: TEST_REMOTE_MAC
                    }
                ]
    )]
    fn test_address_resolution(
        requested_remote_cfg: ArpHostConfig<'_>,
        other_remote_cfgs: Vec<ArpHostConfig<'_>>,
    ) {
        // Test a basic ARP resolution scenario.
        // We expect the following steps:
        // 1. When a lookup is performed and results in a cache miss, we send an
        //    ARP request and set a request retry timer.
        // 2. When the requested remote receives the request, it populates its cache with
        //    the local's information, and sends an ARP reply.
        // 3. Any non-requested remotes will neither populate their caches nor send ARP replies.
        // 4. When the reply is received, the timer is canceled, the table is
        //    updated, a new entry expiration timer is installed, and the device
        //    layer is notified of the resolution.

        const LOCAL_HOST_CFG: ArpHostConfig<'_> =
            ArpHostConfig { name: "local", proto_addr: TEST_LOCAL_IPV4, hw_addr: TEST_LOCAL_MAC };
        let host_iter = other_remote_cfgs
            .iter()
            .chain(iter::once(&requested_remote_cfg))
            .chain(iter::once(&LOCAL_HOST_CFG));

        let mut network = FakeNetwork::new(
            {
                host_iter.clone().map(|cfg| {
                    let ArpHostConfig { name, proto_addr, hw_addr } = cfg;
                    let mut ctx = FakeCtx::with_sync_ctx(FakeCtxImpl::default());
                    let FakeCtx { sync_ctx, non_sync_ctx: _ } = &mut ctx;
                    sync_ctx.get_mut().hw_addr = UnicastAddr::new(*hw_addr).unwrap();
                    sync_ctx.get_mut().proto_addr = Some(*proto_addr);
                    (*name, ctx)
                })
            },
            |ctx: &str, _meta| {
                host_iter
                    .clone()
                    .filter_map(|cfg| {
                        let ArpHostConfig { name, proto_addr: _, hw_addr: _ } = cfg;
                        if !ctx.eq(*name) {
                            Some((*name, FakeLinkDeviceId, None))
                        } else {
                            None
                        }
                    })
                    .collect::<Vec<_>>()
            },
        );

        let ArpHostConfig {
            name: local_name,
            proto_addr: local_proto_addr,
            hw_addr: local_hw_addr,
        } = LOCAL_HOST_CFG;

        let ArpHostConfig {
            name: requested_remote_name,
            proto_addr: requested_remote_proto_addr,
            hw_addr: requested_remote_hw_addr,
        } = requested_remote_cfg;

        // Trigger link resolution.
        network.with_context(local_name, |FakeCtx { sync_ctx, non_sync_ctx }| {
            assert_neighbor_unknown(
                sync_ctx,
                FakeLinkDeviceId,
                SpecifiedAddr::new(requested_remote_proto_addr).unwrap(),
            );
            assert_eq!(
                BufferNudHandler::send_ip_packet_to_neighbor(
                    sync_ctx,
                    non_sync_ctx,
                    &FakeLinkDeviceId,
                    SpecifiedAddr::new(requested_remote_proto_addr).unwrap(),
                    Buf::new([1], ..),
                ),
                Ok(())
            );

            // We should have sent an ARP request.
            validate_last_arp_packet(
                sync_ctx,
                1,
                Mac::BROADCAST,
                ArpOp::Request,
                local_proto_addr,
                requested_remote_proto_addr,
                local_hw_addr,
                Mac::BROADCAST,
            );
        });
        // Step once to deliver the ARP request to the remotes.
        let res = network.step(
            |FakeCtx { sync_ctx, non_sync_ctx }, device_id, buf| {
                handle_packet(sync_ctx, non_sync_ctx, device_id, buf)
            },
            |FakeCtx { sync_ctx, non_sync_ctx }, _ctx, id| {
                TimerHandler::handle_timer(sync_ctx, non_sync_ctx, id)
            },
        );
        assert_eq!(res.timers_fired, 0);

        // Our faked broadcast network should deliver frames to every host other
        // than the sender itself. These should include all non-participating remotes
        // and either the local or the participating remote, depending on who is
        // sending the packet.
        let expected_frames_sent_bcast = other_remote_cfgs.len() + 1;
        assert_eq!(res.frames_sent, expected_frames_sent_bcast);

        // The requested remote should have populated its ARP cache with the local's
        // information.
        network.with_context(requested_remote_name, |FakeCtx { sync_ctx, non_sync_ctx: _ }| {
            assert_dynamic_neighbor_with_addr(
                sync_ctx,
                FakeLinkDeviceId,
                SpecifiedAddr::new(local_proto_addr).unwrap(),
                LOCAL_HOST_CFG.hw_addr,
            );

            // The requested remote should have sent an ARP response.
            validate_last_arp_packet(
                sync_ctx,
                1,
                local_hw_addr,
                ArpOp::Response,
                requested_remote_proto_addr,
                local_proto_addr,
                requested_remote_hw_addr,
                local_hw_addr,
            );
        });

        // Step once to deliver the ARP response to the local.
        let res = network.step(
            |FakeCtx { sync_ctx, non_sync_ctx }, device_id, buf| {
                handle_packet(sync_ctx, non_sync_ctx, device_id, buf)
            },
            |FakeCtx { sync_ctx, non_sync_ctx }, _ctx, id| {
                TimerHandler::handle_timer(sync_ctx, non_sync_ctx, id)
            },
        );
        assert_eq!(res.timers_fired, 0);
        assert_eq!(res.frames_sent, expected_frames_sent_bcast);

        // The local should have populated its cache with the remote's
        // information.
        network.with_context(local_name, |FakeCtx { sync_ctx, non_sync_ctx: _ }| {
            assert_dynamic_neighbor_with_addr(
                sync_ctx,
                FakeLinkDeviceId,
                SpecifiedAddr::new(requested_remote_proto_addr).unwrap(),
                requested_remote_hw_addr,
            );
        });

        other_remote_cfgs.iter().for_each(
            |ArpHostConfig { name: unrequested_remote_name, proto_addr: _, hw_addr: _ }| {
                // The non-requested_remote should not have populated its ARP cache.
                network.with_context(
                    *unrequested_remote_name,
                    |FakeCtx { sync_ctx, non_sync_ctx: _ }| {
                        // The non-requested_remote should not have sent an ARP response.
                        assert_empty(sync_ctx.frames().iter());

                        assert_neighbor_unknown(
                            sync_ctx,
                            FakeLinkDeviceId,
                            SpecifiedAddr::new(local_proto_addr).unwrap(),
                        );
                    },
                )
            },
        );
    }
}
