// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Address Resolution Protocol (ARP).

use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::hash::Hash;
use std::marker::PhantomData;
use std::mem;
use std::time::Duration;

use log::{debug, error};
use net_types::{ethernet::Mac, ip::Ipv4Addr};
use never::Never;
use packet::{BufferMut, EmptyBuf, InnerPacketBuilder};
use zerocopy::{AsBytes, FromBytes, Unaligned};

use crate::context::{
    CounterContext, FrameContext, FrameHandler, StateContext, TimerContext, TimerHandler,
};
use crate::device::ethernet::EtherType;
use crate::device::link::{BroadcastLinkAddress, LinkDevice};
use crate::wire::arp::{ArpPacket, ArpPacketBuilder};

/// The type of an ARP operation.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
#[allow(missing_docs)]
#[repr(u16)]
pub(crate) enum ArpOp {
    Request = ArpOp::REQUEST,
    Response = ArpOp::RESPONSE,
}

impl ArpOp {
    const REQUEST: u16 = 0x0001;
    const RESPONSE: u16 = 0x0002;

    /// Construct an `ArpOp` from a `u16`.
    ///
    /// `from_u16` returns the `ArpOp` with the numerical value `u`, or `None`
    /// if the value is unrecognized.
    pub(crate) fn from_u16(u: u16) -> Option<ArpOp> {
        match u {
            Self::REQUEST => Some(ArpOp::Request),
            Self::RESPONSE => Some(ArpOp::Response),
            _ => None,
        }
    }
}

/// A trait to represent an ARP hardware type.
pub(crate) trait HType: BroadcastLinkAddress + Hash + Eq {
    /// The hardware type.
    const HTYPE: ArpHardwareType;
    /// The in-memory size of an instance of the type.
    const HLEN: u8;
}

/// A trait to represent an ARP protocol type.
pub(crate) trait PType: FromBytes + AsBytes + Unaligned + Copy + Clone + Hash + Eq {
    /// The protocol type.
    const PTYPE: EtherType;
    /// The in-memory size of an instance of the type.
    const PLEN: u8;
}

impl HType for Mac {
    const HTYPE: ArpHardwareType = ArpHardwareType::Ethernet;
    const HLEN: u8 = mem::size_of::<Mac>() as u8;
}

impl PType for Ipv4Addr {
    const PTYPE: EtherType = EtherType::Ipv4;
    const PLEN: u8 = mem::size_of::<Ipv4Addr>() as u8;
}

/// An ARP hardware protocol.
#[derive(Debug, PartialEq)]
#[allow(missing_docs)]
#[repr(u16)]
pub(crate) enum ArpHardwareType {
    Ethernet = ArpHardwareType::ETHERNET,
}

impl ArpHardwareType {
    const ETHERNET: u16 = 0x0001;

    /// Construct an `ArpHardwareType` from a `u16`.
    ///
    /// `from_u16` returns the `ArpHardwareType` with the numerical value `u`,
    /// or `None` if the value is unrecognized.
    pub(crate) fn from_u16(u: u16) -> Option<ArpHardwareType> {
        match u {
            Self::ETHERNET => Some(ArpHardwareType::Ethernet),
            _ => None,
        }
    }
}

// NOTE(joshlf): This may seem a bit odd. Why not just say that `ArpDevice` is a
// sub-trait of `L: LinkDevice` where `L::Address: HType`? Unfortunately, rustc
// is still pretty bad at reasoning about where clauses. In a (never published)
// earlier version of this code, I tried that approach. Even simple function
// signatures like `fn foo<D: ArpDevice, P: PType, C: ArpContext<D, P>>()` were
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
pub(crate) trait ArpDevice {
    type HType: HType;
}

impl<L: LinkDevice> ArpDevice for L
where
    L::Address: HType,
{
    type HType = L::Address;
}

/// The identifier for timer events in the ARP layer.
///
/// This is used to retry sending ARP requests and to expire existing ARP table
/// entries. It is parametric on a device ID type, `D`, and a network protocol
/// type, `P`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub(crate) struct ArpTimerId<D: ArpDevice, P: PType, DeviceId> {
    device_id: DeviceId,
    inner: ArpTimerIdInner<P>,
    _marker: PhantomData<D>,
}

impl<D: ArpDevice, P: PType, DeviceId> ArpTimerId<D, P, DeviceId> {
    fn new(device_id: DeviceId, inner: ArpTimerIdInner<P>) -> ArpTimerId<D, P, DeviceId> {
        ArpTimerId { device_id, inner, _marker: PhantomData }
    }

    fn new_request_retry(device_id: DeviceId, proto_addr: P) -> ArpTimerId<D, P, DeviceId> {
        ArpTimerId::new(device_id, ArpTimerIdInner::RequestRetry { proto_addr })
    }

    fn new_entry_expiration(device_id: DeviceId, proto_addr: P) -> ArpTimerId<D, P, DeviceId> {
        ArpTimerId::new(device_id, ArpTimerIdInner::EntryExpiration { proto_addr })
    }

    pub(crate) fn get_device_id(&self) -> &DeviceId {
        &self.device_id
    }
}

/// The metadata associated with an ARP frame.
pub(super) struct ArpFrameMetadata<D: ArpDevice, DeviceId> {
    /// The ID of the ARP device.
    pub(super) device_id: DeviceId,
    /// The destination hardware address.
    pub(super) dst_addr: D::HType,
}

/// Cleans up state associated with the device.
///
/// Equivalent to calling `ctx.deinitialize`.
///
/// See [`ArpHandler::deinitialize`] for more details.
pub(super) fn deinitialize<D: ArpDevice, P: PType, H: ArpHandler<D, P>>(
    ctx: &mut H,
    device_id: H::DeviceId,
) {
    ctx.deinitialize(device_id)
}

/// An execution context for the ARP protocol when a buffer is provided.
///
/// `BufferArpContext` is like [`ArpContext`], except that it also requires that
/// the context be capable of receiving frames in buffers of type `B`. This is
/// used when a buffer of type `B` is provided to ARP (in particular, in
/// [`receive_arp_packet`]), and allows ARP to reuse that buffer rather than
/// needing to always allocate a new one.
pub(super) trait BufferArpContext<D: ArpDevice, P: PType, B: BufferMut>:
    ArpContext<D, P> + FrameContext<B, ArpFrameMetadata<D, <Self as ArpDeviceIdContext<D>>::DeviceId>>
{
}

impl<
        D: ArpDevice,
        P: PType,
        B: BufferMut,
        C: ArpContext<D, P>
            + FrameContext<B, ArpFrameMetadata<D, <Self as ArpDeviceIdContext<D>>::DeviceId>>,
    > BufferArpContext<D, P, B> for C
{
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

/// An execution context which provides a `DeviceId` type for ARP internals to share.
pub(super) trait ArpDeviceIdContext<D: ArpDevice> {
    /// An ID that identifies a particular device.
    type DeviceId: Copy + PartialEq;
}

/// An execution context for the ARP protocol.
pub(super) trait ArpContext<D: ArpDevice, P: PType>:
    ArpDeviceIdContext<D>
    + StateContext<ArpState<D, P>, <Self as ArpDeviceIdContext<D>>::DeviceId>
    + TimerContext<ArpTimerId<D, P, <Self as ArpDeviceIdContext<D>>::DeviceId>>
    + FrameContext<EmptyBuf, ArpFrameMetadata<D, <Self as ArpDeviceIdContext<D>>::DeviceId>>
    + CounterContext
{
    /// Get a protocol address of this interface.
    ///
    /// If `device_id` does not have any addresses associated with it, return `None`.
    fn get_protocol_addr(&self, device_id: Self::DeviceId) -> Option<P>;

    /// Get the hardware address of this interface.
    fn get_hardware_addr(&self, device_id: Self::DeviceId) -> D::HType;

    /// Notifies the device layer that the hardware address `hw_addr` was
    /// resolved for the given protocol address `proto_addr`.
    fn address_resolved(&mut self, device_id: Self::DeviceId, proto_addr: P, hw_addr: D::HType);

    /// Notifies the device layer that the hardware address resolution for the
    /// given protocol address `proto_addr` failed.
    fn address_resolution_failed(&mut self, device_id: Self::DeviceId, proto_addr: P);

    /// Notifies the device layer that a previously-cached resolution entry has
    /// expired, and is no longer valid.
    fn address_resolution_expired(&mut self, device_id: Self::DeviceId, proto_addr: P);
}

/// An ARP packet handler.
///
/// The protocol and device types (`P` and `D` respectively) must be set
/// statically. Unless there is only one valid pair of protocol and hardware
/// types in a given context, it is the caller's responsibility to call
/// `peek_arp_types` in order to determine which types to use when referencing
/// a specific `ArpPacketHandler` implementation.
pub(super) trait ArpPacketHandler<D: ArpDevice, P: PType, B: BufferMut>:
    ArpHandler<D, P>
{
    /// Receive an ARP packet from a device.
    fn receive_arp_packet(&mut self, device_id: Self::DeviceId, buffer: B);
}

impl<D: ArpDevice, P: PType, B: BufferMut, C: BufferArpContext<D, P, B>> ArpPacketHandler<D, P, B>
    for C
{
    fn receive_arp_packet(&mut self, device_id: Self::DeviceId, buffer: B) {
        ArpTimerFrameHandler::handle_frame(self, device_id, (), buffer)
    }
}

/// An ARP handler for ARP Events.
///
/// `ArpHandler<D, P>` is implemented for any type which implements [`ArpContext<D, P>`],
/// and it can also be mocked for use in testing.
pub(super) trait ArpHandler<D: ArpDevice, P: PType>: ArpDeviceIdContext<D> {
    /// Cleans up state associated with the device.
    ///
    /// The contract is that after `deinitialize` is called, nothing else should be done
    /// with the state.
    fn deinitialize(&mut self, device_id: Self::DeviceId);

    /// Look up the hardware address for a network protocol address.
    fn lookup(
        &mut self,
        device_id: Self::DeviceId,
        local_addr: D::HType,
        lookup_addr: P,
    ) -> Option<D::HType>;

    /// Handle an ARP timer firing.
    fn handle_timer(&mut self, id: ArpTimerId<D, P, Self::DeviceId>);

    /// Insert a static entry into this device's ARP table.
    ///
    /// This will cause any conflicting dynamic entry to be removed, and any future
    /// conflicting gratuitous ARPs to be ignored.
    // TODO(rheacock): remove `cfg(test)` when this is used.
    #[cfg(test)]
    fn insert_static_neighbor(&mut self, device_id: Self::DeviceId, addr: P, hw: D::HType);
}

impl<D: ArpDevice, P: PType, C: ArpContext<D, P>> ArpHandler<D, P> for C {
    fn deinitialize(&mut self, device_id: Self::DeviceId) {
        // Remove all timers associated with the device
        self.cancel_timers_with(|timer_id| *timer_id.get_device_id() == device_id);
        // TODO(rheacock): Send any immediate packets, and potentially flag the state as
        // uninitialized?
    }

    fn lookup(
        &mut self,
        device_id: Self::DeviceId,
        _local_addr: D::HType,
        lookup_addr: P,
    ) -> Option<D::HType> {
        let result = self.get_state_with(device_id).table.lookup(lookup_addr).cloned();

        // Send an ARP Request if the address is not in our cache
        if result.is_none() {
            send_arp_request(self, device_id, lookup_addr);
        }

        result
    }

    fn handle_timer(&mut self, id: ArpTimerId<D, P, Self::DeviceId>) {
        ArpTimerFrameHandler::handle_timer(self, id)
    }

    #[cfg(test)]
    fn insert_static_neighbor(&mut self, device_id: Self::DeviceId, addr: P, hw: D::HType) {
        // Cancel any outstanding timers for this entry; if none exist, these will
        // be no-ops.
        let outstanding_request =
            self.cancel_timer(ArpTimerId::new_request_retry(device_id, addr)).is_some();
        self.cancel_timer(ArpTimerId::new_entry_expiration(device_id, addr));

        // If there was an outstanding resolution request, notify the device layer
        // that it's been resolved.
        if outstanding_request {
            self.address_resolved(device_id, addr, hw);
        }

        self.get_state_mut_with(device_id).table.insert_static(addr, hw);
    }
}

/// Handle an ARP timer firing.
///
/// Equivalent to calling `ctx.handle_timer`.
pub(super) fn handle_timer<D: ArpDevice, P: PType, H: ArpHandler<D, P>>(
    ctx: &mut H,
    id: ArpTimerId<D, P, H::DeviceId>,
) {
    ctx.handle_timer(id)
}

/// A handler for ARP events.
///
/// This type cannot be constructed, and is only meant to be used at the type
/// level. We implement `TimerHandler` and `FrameHandler` for `ArpTimerFrameHandler`
/// rather than just provide the top-level `handle_timer` and `receive_frame`
/// functions so that `ArpTimerFrameHandler` can be used in tests with the
/// `DummyTimerContextExt` trait and with the `DummyNetwork` type.
struct ArpTimerFrameHandler<D: ArpDevice, P> {
    _marker: PhantomData<(D, P)>,
    _never: Never,
}

impl<D: ArpDevice, P: PType, C: ArpContext<D, P>> TimerHandler<C, ArpTimerId<D, P, C::DeviceId>>
    for ArpTimerFrameHandler<D, P>
{
    fn handle_timer(ctx: &mut C, id: ArpTimerId<D, P, C::DeviceId>) {
        match id.inner {
            ArpTimerIdInner::RequestRetry { proto_addr } => {
                send_arp_request(ctx, id.device_id, proto_addr)
            }
            ArpTimerIdInner::EntryExpiration { proto_addr } => {
                ctx.get_state_mut_with(id.device_id).table.remove(proto_addr);
                ctx.address_resolution_expired(id.device_id, proto_addr);

                // There are several things to notice:
                // - Unlike when we send an ARP request in response to a lookup,
                //   here we don't schedule a retry timer, so the request will
                //   be sent only once.
                // - This is best-effort in the sense that the protocol is still
                //   correct if we don't manage to send an ARP request or
                //   receive an ARP response.
                // - The point of doing this is just to make it more likely for
                //   our ARP cache to stay up to date; it's not actually a
                //   requirement of the protocol. Note that the RFC does say "It
                //   may be desirable to have table aging and/or timers".
                if let Some(sender_protocol_addr) = ctx.get_protocol_addr(id.device_id) {
                    let self_hw_addr = ctx.get_hardware_addr(id.device_id);
                    // TODO(joshlf): Do something if send_frame returns an error?
                    let _ = ctx.send_frame(
                        ArpFrameMetadata { device_id: id.device_id, dst_addr: D::HType::BROADCAST },
                        ArpPacketBuilder::new(
                            ArpOp::Request,
                            self_hw_addr,
                            sender_protocol_addr,
                            // This is meaningless, since RFC 826 does not
                            // specify the behaviour. However, the broadcast
                            // address is sensible, as this is the actual
                            // address we are sending the packet to.
                            D::HType::BROADCAST,
                            proto_addr,
                        )
                        .into_serializer(),
                    );
                }
            }
        }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
enum ArpTimerIdInner<P: PType> {
    RequestRetry { proto_addr: P },
    EntryExpiration { proto_addr: P },
}

/// Receive an ARP packet from a device.
///
/// Equivalent to calling `ctx.receive_arp_packet`.
pub(super) fn receive_arp_packet<
    D: ArpDevice,
    P: PType,
    B: BufferMut,
    H: ArpPacketHandler<D, P, B>,
>(
    ctx: &mut H,
    device_id: H::DeviceId,
    buffer: B,
) {
    ctx.receive_arp_packet(device_id, buffer)
}

impl<D: ArpDevice, P: PType, B: BufferMut, C: BufferArpContext<D, P, B>>
    FrameHandler<C, C::DeviceId, (), B> for ArpTimerFrameHandler<D, P>
{
    fn handle_frame(ctx: &mut C, device_id: C::DeviceId, _meta: (), mut buffer: B) {
        // TODO(wesleyac) Add support for probe.
        let packet = match buffer.parse::<ArpPacket<_, D::HType, P>>() {
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

        let addressed_to_me =
            Some(packet.target_protocol_address()) == ctx.get_protocol_addr(device_id);

        // The following logic is equivalent to the "ARP, Proxy ARP, and Gratuitous
        // ARP" section of RFC 2002.

        // Gratuitous ARPs, which have the same sender and target address, need to
        // be handled separately since they do not send a response.
        if packet.sender_protocol_address() == packet.target_protocol_address() {
            insert_dynamic(
                ctx,
                device_id,
                packet.sender_protocol_address(),
                packet.sender_hardware_address(),
            );

            // If we have an outstanding retry timer for this host, we should cancel
            // it since we now have the mapping in cache.
            ctx.cancel_timer(ArpTimerId::new_request_retry(
                device_id,
                packet.sender_protocol_address(),
            ));

            ctx.increment_counter("arp::rx_gratuitous_resolve");
            // Notify device layer:
            ctx.address_resolved(
                device_id,
                packet.sender_protocol_address(),
                packet.sender_hardware_address(),
            );
            return;
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
        //
        // Given that the semantics of ArpTable is that inserting and updating an
        // entry are the same, this can be implemented with two if statements (one
        // to update the table, and one to send a response).

        if addressed_to_me
            || ctx
                .get_state_with(device_id)
                .table
                .lookup(packet.sender_protocol_address())
                .is_some()
        {
            insert_dynamic(
                ctx,
                device_id,
                packet.sender_protocol_address(),
                packet.sender_hardware_address(),
            );
            // Since we just got the protocol -> hardware address mapping, we can
            // cancel a timer to resend a request.
            ctx.cancel_timer(ArpTimerId::new_request_retry(
                device_id,
                packet.sender_protocol_address(),
            ));

            ctx.increment_counter("arp::rx_resolve");
            // Notify device layer:
            ctx.address_resolved(
                device_id,
                packet.sender_protocol_address(),
                packet.sender_hardware_address(),
            );
        }
        if addressed_to_me && packet.operation() == ArpOp::Request {
            let self_hw_addr = ctx.get_hardware_addr(device_id);
            ctx.increment_counter("arp::rx_request");
            // TODO(joshlf): Do something if send_frame returns an error?
            let _ = ctx.send_frame(
                ArpFrameMetadata { device_id, dst_addr: packet.sender_hardware_address() },
                ArpPacketBuilder::new(
                    ArpOp::Response,
                    self_hw_addr,
                    packet.target_protocol_address(),
                    packet.sender_hardware_address(),
                    packet.sender_protocol_address(),
                )
                .into_serializer_with(buffer),
            );
        }
    }
}

/// Insert a static entry into this device's ARP table.
///
/// Equivalent to calling `ctx.insert_static_neighbor`.
///
/// See [`ArpHandler::insert_static_neighbor`] for more details.
// TODO(rheacock): remove `cfg(test)` when this is used.
#[cfg(test)]
pub(super) fn insert_static_neighbor<D: ArpDevice, P: PType, H: ArpHandler<D, P>>(
    ctx: &mut H,
    device_id: H::DeviceId,
    net: P,
    hw: D::HType,
) {
    ctx.insert_static_neighbor(device_id, net, hw)
}

/// Insert a dynamic entry into this device's ARP table.
///
/// The entry will potentially be overwritten by any future static entry and the
/// entry will not be successfully added into the table if there currently is a
/// static entry.
fn insert_dynamic<D: ArpDevice, P: PType, C: ArpContext<D, P>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    net: P,
    hw: D::HType,
) {
    // Let's extend the expiration deadline by rescheduling the timer. It is
    // assumed that `schedule_timer` will first cancel the timer that is already
    // there.
    let expiration = ArpTimerId::new_entry_expiration(device_id, net);
    if ctx.get_state_mut_with(device_id).table.insert_dynamic(net, hw) {
        ctx.schedule_timer(DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD, expiration);
    }
}

/// Look up the hardware address for a network protocol address.
///
/// Equivalent to calling `ctx.lookup`.
pub(super) fn lookup<D: ArpDevice, P: PType, H: ArpHandler<D, P>>(
    ctx: &mut H,
    device_id: H::DeviceId,
    local_addr: D::HType,
    lookup_addr: P,
) -> Option<D::HType> {
    ctx.lookup(device_id, local_addr, lookup_addr)
}

// Since BSD resends ARP requests every 20 seconds and sets the default time
// limit to establish a TCP connection as 75 seconds, 4 is used as the max
// number of tries, which is the initial remaining_tries.
const DEFAULT_ARP_REQUEST_MAX_TRIES: usize = 4;
// Currently at 20 seconds because that's what FreeBSD does.
const DEFAULT_ARP_REQUEST_PERIOD: Duration = Duration::from_secs(20);
// Based on standard implementations, 60 seconds is quite usual to expire an ARP
// entry.
const DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD: Duration = Duration::from_secs(60);

fn send_arp_request<D: ArpDevice, P: PType, C: ArpContext<D, P>>(
    ctx: &mut C,
    device_id: C::DeviceId,
    lookup_addr: P,
) {
    let tries_remaining = ctx
        .get_state_mut_with(device_id)
        .table
        .get_remaining_tries(lookup_addr)
        .unwrap_or(DEFAULT_ARP_REQUEST_MAX_TRIES);

    if let Some(sender_protocol_addr) = ctx.get_protocol_addr(device_id) {
        let self_hw_addr = ctx.get_hardware_addr(device_id);
        // TODO(joshlf): Do something if send_frame returns an error?
        let _ = ctx.send_frame(
            ArpFrameMetadata { device_id, dst_addr: D::HType::BROADCAST },
            ArpPacketBuilder::new(
                ArpOp::Request,
                self_hw_addr,
                sender_protocol_addr,
                // This is meaningless, since RFC 826 does not specify the
                // behaviour. However, the broadcast address is sensible, as
                // this is the actual address we are sending the packet to.
                D::HType::BROADCAST,
                lookup_addr,
            )
            .into_serializer(),
        );

        let id = ArpTimerId::new_request_retry(device_id, lookup_addr);
        if tries_remaining > 1 {
            // TODO(wesleyac): Configurable timer.
            ctx.schedule_timer(DEFAULT_ARP_REQUEST_PERIOD, id);
            ctx.get_state_mut_with(device_id).table.set_waiting(lookup_addr, tries_remaining - 1);
        } else {
            ctx.cancel_timer(id);
            ctx.get_state_mut_with(device_id).table.remove(lookup_addr);
            ctx.address_resolution_failed(device_id, lookup_addr);
        }
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
pub(crate) struct ArpState<D: ArpDevice, P: PType + Hash + Eq> {
    table: ArpTable<P, D::HType>,
}

impl<D: ArpDevice, P: PType + Hash + Eq> Default for ArpState<D, P> {
    fn default() -> Self {
        ArpState { table: ArpTable::default() }
    }
}

struct ArpTable<P: Hash + Eq, H> {
    table: HashMap<P, ArpValue<H>>,
}

#[derive(Debug, Eq, PartialEq)] // for testing
enum ArpValue<H> {
    // invariant: no timers exist for this entry
    _Static { hardware_addr: H },
    // invariant: a single entry expiration timer exists for this entry
    Dynamic { hardware_addr: H },
    // invariant: a single request retry timer exists for this entry
    Waiting { remaining_tries: usize },
}

impl<P: Hash + Eq, H> ArpTable<P, H> {
    // TODO(rheacock): remove `cfg(test)` when this is used
    #[cfg(test)]
    fn insert_static(&mut self, net: P, hw: H) {
        // a static entry overrides everything, so just insert it.
        self.table.insert(net, ArpValue::_Static { hardware_addr: hw });
    }

    /// This function tries to insert a dynamic entry into the ArpTable, the
    /// bool returned from the function is used to indicate whether the
    /// insertion is successful.
    fn insert_dynamic(&mut self, net: P, hw: H) -> bool {
        // a dynamic entry should not override a static one, if that happens,
        // don't do it. if we want to handle this kind of situation in the
        // future, we can make this function return a `Result`
        let new_val = ArpValue::Dynamic { hardware_addr: hw };

        match self.table.entry(net) {
            Entry::Occupied(ref mut entry) => {
                let old_val = entry.get_mut();
                match old_val {
                    ArpValue::_Static { .. } => {
                        error!("Conflicting ARP entries: please check your manual configuration and hosts in your local network");
                        return false;
                    }
                    _ => *old_val = new_val,
                }
            }
            Entry::Vacant(entry) => {
                entry.insert(new_val);
            }
        }
        true
    }

    fn remove(&mut self, net: P) {
        self.table.remove(&net);
    }

    fn get_remaining_tries(&self, net: P) -> Option<usize> {
        if let Some(ArpValue::Waiting { remaining_tries }) = self.table.get(&net) {
            Some(*remaining_tries)
        } else {
            None
        }
    }

    fn set_waiting(&mut self, net: P, remaining_tries: usize) {
        self.table.insert(net, ArpValue::Waiting { remaining_tries });
    }

    fn lookup(&self, addr: P) -> Option<&H> {
        match self.table.get(&addr) {
            Some(ArpValue::_Static { hardware_addr })
            | Some(ArpValue::Dynamic { hardware_addr }) => Some(hardware_addr),
            _ => None,
        }
    }
}

impl<P: Hash + Eq, H> Default for ArpTable<P, H> {
    fn default() -> Self {
        ArpTable { table: HashMap::default() }
    }
}

#[cfg(test)]
mod tests {
    use net_types::ethernet::Mac;
    use net_types::ip::Ipv4Addr;
    use packet::{ParseBuffer, Serializer};

    use super::*;
    use crate::context::testutil::{DummyInstant, DummyNetwork, DummyTimerContextExt};
    use crate::context::InstantContext;
    use crate::device::ethernet::{EtherType, EthernetLinkDevice};
    use crate::wire::arp::{peek_arp_types, ArpPacketBuilder};

    const TEST_LOCAL_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_REMOTE_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_LOCAL_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_REMOTE_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);
    const TEST_INVALID_MAC: Mac = Mac::new([0, 0, 0, 0, 0, 0]);

    /// A dummy `ArpContext` that stores frames, address resolution events, and
    /// address resolution failure events.
    struct DummyArpContext {
        proto_addr: Option<Ipv4Addr>,
        hw_addr: Mac,
        addr_resolved: Vec<(Ipv4Addr, Mac)>,
        addr_resolution_failed: Vec<Ipv4Addr>,
        addr_resolution_expired: Vec<Ipv4Addr>,
        arp_state: ArpState<EthernetLinkDevice, Ipv4Addr>,
    }

    impl Default for DummyArpContext {
        fn default() -> DummyArpContext {
            DummyArpContext {
                proto_addr: Some(TEST_LOCAL_IPV4),
                hw_addr: TEST_LOCAL_MAC,
                addr_resolved: vec![],
                addr_resolution_failed: vec![],
                addr_resolution_expired: vec![],
                arp_state: ArpState::default(),
            }
        }
    }

    type DummyContext = crate::context::testutil::DummyContext<
        DummyArpContext,
        ArpTimerId<EthernetLinkDevice, Ipv4Addr, ()>,
        ArpFrameMetadata<EthernetLinkDevice, ()>,
    >;

    impl ArpDeviceIdContext<EthernetLinkDevice> for DummyContext {
        type DeviceId = ();
    }

    impl ArpContext<EthernetLinkDevice, Ipv4Addr> for DummyContext {
        fn get_protocol_addr(&self, _device_id: ()) -> Option<Ipv4Addr> {
            self.get_ref().proto_addr
        }

        fn get_hardware_addr(&self, _device_id: ()) -> Mac {
            self.get_ref().hw_addr
        }

        fn address_resolved(&mut self, _device_id: (), proto_addr: Ipv4Addr, hw_addr: Mac) {
            self.get_mut().addr_resolved.push((proto_addr, hw_addr));
        }

        fn address_resolution_failed(&mut self, _device_id: (), proto_addr: Ipv4Addr) {
            self.get_mut().addr_resolution_failed.push(proto_addr);
        }

        fn address_resolution_expired(&mut self, _device_id: (), proto_addr: Ipv4Addr) {
            self.get_mut().addr_resolution_expired.push(proto_addr);
        }
    }

    impl StateContext<ArpState<EthernetLinkDevice, Ipv4Addr>> for DummyContext {
        fn get_state_with(&self, _id: ()) -> &ArpState<EthernetLinkDevice, Ipv4Addr> {
            &self.get_ref().arp_state
        }

        fn get_state_mut_with(&mut self, _id: ()) -> &mut ArpState<EthernetLinkDevice, Ipv4Addr> {
            &mut self.get_mut().arp_state
        }
    }

    fn send_arp_packet(
        ctx: &mut DummyContext,
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
        assert_eq!(hw, crate::device::arp::ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);

        receive_arp_packet::<_, Ipv4Addr, _, _>(ctx, (), buf);
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
        ctx: &DummyContext,
        total_frames: usize,
        dst: Mac,
        op: ArpOp,
        local_ipv4: Ipv4Addr,
        remote_ipv4: Ipv4Addr,
        local_mac: Mac,
        remote_mac: Mac,
    ) {
        assert_eq!(ctx.frames().len(), total_frames);
        let (meta, frame) = &ctx.frames()[total_frames - 1];
        assert_eq!(meta.dst_addr, dst);
        validate_arp_packet(frame, op, local_ipv4, remote_ipv4, local_mac, remote_mac);
    }

    // Validate that `ctx` contains exactly one installed timer with the given
    // instant and ID.
    fn validate_single_timer(
        ctx: &DummyContext,
        instant: Duration,
        id: ArpTimerId<EthernetLinkDevice, Ipv4Addr, ()>,
    ) {
        assert_eq!(ctx.timers().as_slice(), [(DummyInstant::from(instant), id)]);
    }

    fn validate_single_retry_timer(ctx: &DummyContext, instant: Duration, addr: Ipv4Addr) {
        validate_single_timer(ctx, instant, ArpTimerId::new_request_retry((), addr))
    }

    fn validate_single_entry_timer(ctx: &DummyContext, instant: Duration, addr: Ipv4Addr) {
        validate_single_timer(ctx, instant, ArpTimerId::new_entry_expiration((), addr))
    }

    #[test]
    fn test_receive_gratuitous_arp_request() {
        // Test that, when we receive a gratuitous ARP request, we cache the
        // sender's address information, and we do not send a response.

        let mut ctx = DummyContext::default();
        send_arp_packet(
            &mut ctx,
            ArpOp::Request,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_INVALID_MAC,
        );

        // We should have cached the sender's address information.
        assert_eq!(
            ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );
        // Gratuitous ARPs should not prompt a response.
        assert_eq!(ctx.frames().len(), 0);
    }

    #[test]
    fn test_receive_gratuitous_arp_response() {
        // Test that, when we receive a gratuitous ARP request, we cache the
        // sender's address information, and we do not send a response.

        let mut ctx = DummyContext::default();
        send_arp_packet(
            &mut ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_REMOTE_MAC,
        );

        // We should have cached the sender's address information.
        assert_eq!(
            ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );
        // Gratuitous ARPs should not send a response.
        assert_eq!(ctx.frames().len(), 0);
    }

    #[test]
    fn test_receive_gratuitous_arp_response_existing_request() {
        // Test that, if we have an outstanding request retry timer and receive
        // a gratuitous ARP for the same host, we cancel the timer and notify
        // the device layer.

        let mut ctx = DummyContext::default();

        lookup(&mut ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4);

        // We should have installed a single retry timer.
        validate_single_retry_timer(&ctx, DEFAULT_ARP_REQUEST_PERIOD, TEST_REMOTE_IPV4);

        send_arp_packet(
            &mut ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_REMOTE_MAC,
        );

        // The response should now be in our cache.
        assert_eq!(
            ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );

        // The retry timer should be canceled, and replaced by an entry
        // expiration timer.
        validate_single_entry_timer(&ctx, DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD, TEST_REMOTE_IPV4);

        // We should have notified the device layer.
        assert_eq!(ctx.get_ref().addr_resolved.as_slice(), [(TEST_REMOTE_IPV4, TEST_REMOTE_MAC)]);

        // Gratuitous ARPs should not send a response (the 1 frame is for the
        // original request).
        assert_eq!(ctx.frames().len(), 1);
    }

    #[test]
    fn test_cancel_timers_on_deinitialize() {
        // Test that associated timers are cancelled when the arp device
        // is deinitialized.

        // Cancelling timers matches on the DeviceId, so setup a context that
        // uses IDs. The test doesn't use the context functions, so it's okay
        // that they return the same info.
        type DummyContext2 = crate::context::testutil::DummyContext<
            DummyArpContext,
            ArpTimerId<EthernetLinkDevice, Ipv4Addr, usize>,
            ArpFrameMetadata<EthernetLinkDevice, usize>,
        >;

        impl ArpDeviceIdContext<EthernetLinkDevice> for DummyContext2 {
            type DeviceId = usize;
        }

        impl ArpContext<EthernetLinkDevice, Ipv4Addr> for DummyContext2 {
            fn get_protocol_addr(&self, _device_id: usize) -> Option<Ipv4Addr> {
                self.get_ref().proto_addr
            }

            fn get_hardware_addr(&self, _device_id: usize) -> Mac {
                self.get_ref().hw_addr
            }

            fn address_resolved(&mut self, _device_id: usize, proto_addr: Ipv4Addr, hw_addr: Mac) {
                self.get_mut().addr_resolved.push((proto_addr, hw_addr));
            }

            fn address_resolution_failed(&mut self, _device_id: usize, proto_addr: Ipv4Addr) {
                self.get_mut().addr_resolution_failed.push(proto_addr);
            }

            fn address_resolution_expired(&mut self, _device_id: usize, proto_addr: Ipv4Addr) {
                self.get_mut().addr_resolution_expired.push(proto_addr);
            }
        }

        impl StateContext<ArpState<EthernetLinkDevice, Ipv4Addr>, usize> for DummyContext2 {
            fn get_state_with(&self, _id: usize) -> &ArpState<EthernetLinkDevice, Ipv4Addr> {
                &self.get_ref().arp_state
            }

            fn get_state_mut_with(
                &mut self,
                _id: usize,
            ) -> &mut ArpState<EthernetLinkDevice, Ipv4Addr> {
                &mut self.get_mut().arp_state
            }
        }

        // Setup up a dummy context and trigger a timer with a lookup
        let mut ctx = DummyContext2::default();

        let device_id_0: usize = 0;
        let device_id_1: usize = 1;

        lookup(&mut ctx, device_id_0, TEST_LOCAL_MAC, TEST_REMOTE_IPV4);

        // We should have installed a single retry timer.
        assert_eq!(ctx.timers().len(), 1);

        // Deinitializing a different ID should not impact the current timer.
        deinitialize(&mut ctx, device_id_1);
        assert_eq!(ctx.timers().len(), 1);

        // Deinitializing the correct ID should cancel the timer.
        deinitialize(&mut ctx, device_id_0);
        assert_eq!(ctx.timers().len(), 0);
    }

    #[test]
    fn test_send_arp_request_on_cache_miss() {
        // Test that, when we perform a lookup that fails, we send an ARP
        // request and install a timer to retry.

        let mut ctx = DummyContext::default();

        // Perform the lookup.
        lookup(&mut ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4);

        // We should have sent a single ARP request.
        validate_last_arp_packet(
            &ctx,
            1,
            Mac::BROADCAST,
            ArpOp::Request,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            Mac::BROADCAST,
        );

        // We should have installed a single retry timer.
        validate_single_retry_timer(&ctx, DEFAULT_ARP_REQUEST_PERIOD, TEST_REMOTE_IPV4);

        // Test that, when we receive an ARP response, we cancel the timer.
        send_arp_packet(
            &mut ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_MAC,
            TEST_LOCAL_MAC,
        );

        // The response should now be in our cache.
        assert_eq!(
            ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );

        // We should have notified the device layer.
        assert_eq!(ctx.get_ref().addr_resolved.as_slice(), [(TEST_REMOTE_IPV4, TEST_REMOTE_MAC)]);

        // The retry timer should be canceled, and replaced by an entry
        // expiration timer.
        validate_single_entry_timer(&ctx, DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD, TEST_REMOTE_IPV4);
    }

    #[test]
    fn test_no_arp_request_on_cache_hit() {
        // Test that, when we perform a lookup that succeeds, we do not send an
        // ARP request or install a retry timer.

        let mut ctx = DummyContext::default();

        // Perform a gratuitous ARP to populate the cache.
        send_arp_packet(
            &mut ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_REMOTE_MAC,
        );

        // Perform the lookup.
        lookup(&mut ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4);

        // We should not have sent any ARP request.
        assert_eq!(ctx.frames().len(), 0);
        // We should not have set a retry timer.
        assert!(ctx.cancel_timer(ArpTimerId::new_request_retry((), TEST_REMOTE_IPV4)).is_none());
    }

    #[test]
    fn test_exhaust_retries_arp_request() {
        // Test that, after performing a certain number of ARP request retries,
        // we give up and don't install another retry timer.

        let mut ctx = DummyContext::default();

        lookup(&mut ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4);

        // `i` represents the `i`th request, so we start it at 1 since we
        // already sent one request during the call to `lookup`.
        for i in 1..DEFAULT_ARP_REQUEST_MAX_TRIES {
            // We should have sent i requests total. We have already validated
            // all the rest, so only validate the most recent one.
            validate_last_arp_packet(
                &ctx,
                i,
                Mac::BROADCAST,
                ArpOp::Request,
                TEST_LOCAL_IPV4,
                TEST_REMOTE_IPV4,
                TEST_LOCAL_MAC,
                Mac::BROADCAST,
            );

            // Check the number of remaining tries.
            assert_eq!(
                ctx.get_ref().arp_state.table.get_remaining_tries(TEST_REMOTE_IPV4).unwrap(),
                DEFAULT_ARP_REQUEST_MAX_TRIES - i
            );

            // There should be a single ARP request retry timer installed.
            validate_single_retry_timer(
                &ctx,
                // Duration only implements Mul<u32>
                DEFAULT_ARP_REQUEST_PERIOD * (i as u32),
                TEST_REMOTE_IPV4,
            );

            // Trigger the ARP request retry timer.
            assert!(ctx.trigger_next_timer::<ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>>());
        }

        // We should have sent DEFAULT_ARP_REQUEST_MAX_TRIES requests total. We
        // have already validated all the rest, so only validate the most recent
        // one.
        validate_last_arp_packet(
            &ctx,
            DEFAULT_ARP_REQUEST_MAX_TRIES,
            Mac::BROADCAST,
            ArpOp::Request,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            Mac::BROADCAST,
        );

        // There shouldn't be any timers installed.
        assert_eq!(ctx.timers().len(), 0);

        // The table entry should have been completely removed.
        assert!(ctx.get_ref().arp_state.table.table.get(&TEST_REMOTE_IPV4).is_none());

        // We should have notified the device layer of the failure.
        assert_eq!(ctx.get_ref().addr_resolution_failed.as_slice(), [TEST_REMOTE_IPV4]);
    }

    #[test]
    fn test_handle_arp_request() {
        // Test that, when we receive an ARP request, we cache the sender's
        // address information and send an ARP response.

        let mut ctx = DummyContext::default();

        send_arp_packet(
            &mut ctx,
            ArpOp::Request,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_MAC,
            TEST_LOCAL_MAC,
        );

        // Make sure we cached the sender's address information.
        assert_eq!(
            lookup(&mut ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4).unwrap(),
            TEST_REMOTE_MAC
        );

        // We should have sent an ARP response.
        validate_last_arp_packet(
            &ctx,
            1,
            TEST_REMOTE_MAC,
            ArpOp::Response,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            TEST_REMOTE_MAC,
        );
    }

    #[test]
    fn test_arp_table() {
        let mut t: ArpTable<Ipv4Addr, Mac> = ArpTable::default();
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 1])), None);
        t.insert_dynamic(Ipv4Addr::new([10, 0, 0, 1]), Mac::new([1, 2, 3, 4, 5, 6]));
        assert_eq!(*t.lookup(Ipv4Addr::new([10, 0, 0, 1])).unwrap(), Mac::new([1, 2, 3, 4, 5, 6]));
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 2])), None);
    }

    #[test]
    fn test_address_resolution() {
        // Test a basic ARP resolution scenario with both sides participating.
        // We expect the following steps:
        // 1. When a lookup is performed and results in a cache miss, we send an
        //    ARP request and set a request retry timer.
        // 2. When the remote receives the request, it populates its cache with
        //    the local's information, and sends an ARP reply.
        // 3. When the reply is received, the timer is canceled, the table is
        //    updated, a new entry expiration timer is installed, and the device
        //    layer is notified of the resolution.

        let local = DummyContext::default();
        let mut remote = DummyContext::default();
        remote.get_mut().hw_addr = TEST_REMOTE_MAC;
        remote.get_mut().proto_addr = Some(TEST_REMOTE_IPV4);

        let mut network = DummyNetwork::new(
            vec![("local", local), ("remote", remote)],
            |ctx, _state: &DummyArpContext, _meta| {
                if ctx == "local" {
                    ("remote", (), (), None)
                } else {
                    ("local", (), (), None)
                }
            },
        );

        // The lookup should fail.
        assert!(lookup(network.context("local"), (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4).is_none());
        // We should have sent an ARP request.
        validate_last_arp_packet(
            network.context("local"),
            1,
            Mac::BROADCAST,
            ArpOp::Request,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            Mac::BROADCAST,
        );
        // We should have installed a retry timer.
        validate_single_retry_timer(
            network.context("local"),
            DEFAULT_ARP_REQUEST_PERIOD,
            TEST_REMOTE_IPV4,
        );

        // Step once to deliver the ARP request to the remote.
        let res = network.step::<ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>, ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>>();
        assert_eq!(res.timers_fired(), 0);
        assert_eq!(res.frames_sent(), 1);

        // The remote should have populated its ARP cache with the local's
        // information.
        assert_eq!(
            network.context("remote").get_ref().arp_state.table.lookup(TEST_LOCAL_IPV4).unwrap(),
            &TEST_LOCAL_MAC
        );
        // The remote should have sent an ARP response.
        validate_last_arp_packet(
            network.context("remote"),
            1,
            TEST_LOCAL_MAC,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_MAC,
            TEST_LOCAL_MAC,
        );

        // Step once to deliver the ARP response to the local.
        let res = network.step::<ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>, ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>>();
        assert_eq!(res.timers_fired(), 0);
        assert_eq!(res.frames_sent(), 1);

        // The local should have populated its cache with the remote's
        // information.
        assert_eq!(
            network.context("local").get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );
        // The retry timer should be canceled, and replaced by an entry
        // expiration timer.
        validate_single_entry_timer(
            network.context("local"),
            DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD,
            TEST_REMOTE_IPV4,
        );
        // The device layer should have been notified.
        assert_eq!(
            network.context("local").get_ref().addr_resolved.as_slice(),
            [(TEST_REMOTE_IPV4, TEST_REMOTE_MAC)]
        );
    }

    #[test]
    fn test_arp_table_static_override_dynamic() {
        let mut table: ArpTable<Ipv4Addr, Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac0 = Mac::new([1, 2, 3, 4, 5, 6]);
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.insert_dynamic(ip, mac0);
        assert_eq!(*table.lookup(ip).unwrap(), mac0);
        table.insert_static(ip, mac1);
        assert_eq!(*table.lookup(ip).unwrap(), mac1);
    }

    #[test]
    fn test_arp_table_static_override_static() {
        let mut table: ArpTable<Ipv4Addr, Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac0 = Mac::new([1, 2, 3, 4, 5, 6]);
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.insert_static(ip, mac0);
        assert_eq!(*table.lookup(ip).unwrap(), mac0);
        table.insert_static(ip, mac1);
        assert_eq!(*table.lookup(ip).unwrap(), mac1);
    }

    #[test]
    fn test_arp_table_static_override_waiting() {
        let mut table: ArpTable<Ipv4Addr, Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.set_waiting(ip, 4);
        assert_eq!(table.lookup(ip), None);
        table.insert_static(ip, mac1);
        assert_eq!(*table.lookup(ip).unwrap(), mac1);
    }

    #[test]
    fn test_arp_table_dynamic_override_waiting() {
        let mut table: ArpTable<Ipv4Addr, Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.set_waiting(ip, 4);
        assert_eq!(table.lookup(ip), None);
        table.insert_dynamic(ip, mac1);
        assert_eq!(*table.lookup(ip).unwrap(), mac1);
    }

    #[test]
    fn test_arp_table_dynamic_override_dynamic() {
        let mut table: ArpTable<Ipv4Addr, Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac0 = Mac::new([1, 2, 3, 4, 5, 6]);
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.insert_dynamic(ip, mac0);
        assert_eq!(*table.lookup(ip).unwrap(), mac0);
        table.insert_dynamic(ip, mac1);
        assert_eq!(*table.lookup(ip).unwrap(), mac1);
    }

    #[test]
    fn test_arp_table_dynamic_should_not_override_static() {
        let mut table: ArpTable<Ipv4Addr, Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac0 = Mac::new([1, 2, 3, 4, 5, 6]);
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.insert_static(ip, mac0);
        assert_eq!(*table.lookup(ip).unwrap(), mac0);
        table.insert_dynamic(ip, mac1);
        assert_eq!(*table.lookup(ip).unwrap(), mac0);
    }

    #[test]
    fn test_arp_table_static_cancel_waiting_timer() {
        // Test that, if we insert a static entry while a request retry timer is
        // installed, that timer is canceled, and the device layer is notified.

        let mut ctx = DummyContext::default();

        // Perform a lookup in order to kick off a request and install a retry
        // timer.
        lookup(&mut ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4);

        // We should be in the Waiting state.
        assert!(ctx.get_ref().arp_state.table.get_remaining_tries(TEST_REMOTE_IPV4).is_some());
        // We should have an ARP request retry timer set.
        validate_single_retry_timer(&ctx, DEFAULT_ARP_REQUEST_PERIOD, TEST_REMOTE_IPV4);

        // Now insert a static entry.
        insert_static_neighbor(&mut ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);

        // The timer should have been canceled.
        assert_eq!(ctx.timers().len(), 0);

        // We should have notified the device layer.
        assert_eq!(ctx.get_ref().addr_resolved.as_slice(), [(TEST_REMOTE_IPV4, TEST_REMOTE_MAC)]);
    }

    #[test]
    fn test_arp_table_static_cancel_expiration_timer() {
        // Test that, if we insert a static entry that overrides an existing
        // dynamic entry, the dynamic entry's expiration timer is canceled.

        let mut ctx = DummyContext::default();

        insert_dynamic(&mut ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);

        // We should have the address in cache.
        assert_eq!(
            ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );
        // We should have an ARP entry expiration timer set.
        validate_single_entry_timer(&ctx, DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD, TEST_REMOTE_IPV4);

        // Now insert a static entry.
        insert_static_neighbor(&mut ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);

        // The timer should have been canceled.
        assert_eq!(ctx.timers().len(), 0);
    }

    #[test]
    fn test_arp_entry_expiration() {
        // Test that, if a dynamic entry is installed, it is removed after the
        // appropriate amount of time.

        let mut ctx = DummyContext::default();

        insert_dynamic(&mut ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);

        // We should have the address in cache.
        assert_eq!(
            ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );
        // We should have an ARP entry expiration timer set.
        validate_single_entry_timer(&ctx, DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD, TEST_REMOTE_IPV4);

        // Trigger the entry expiration timer.
        assert!(ctx.trigger_next_timer::<ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>>());

        // The right amount of time should have elapsed.
        assert_eq!(ctx.now(), DummyInstant::from(DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD));
        // The entry should have been removed.
        assert!(ctx.get_ref().arp_state.table.table.get(&TEST_REMOTE_IPV4).is_none());
        // The timer should have been canceled.
        assert_eq!(ctx.timers().len(), 0);
        // The device layer should have been notified.
        assert_eq!(ctx.get_ref().addr_resolution_expired, [TEST_REMOTE_IPV4]);
    }

    #[test]
    fn test_gratuitous_arp_resets_entry_timer() {
        // Test that a gratuitous ARP resets the entry expiration timer by
        // performing the following steps:
        // 1. An arp entry is installed with default timer at instant t
        // 2. A gratuitous arp message is sent after 5 seconds
        // 3. Check at instant DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD whether the
        //    entry is there (it should be)
        // 4. Check whether the entry disappears at instant
        //    (DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD + 5)

        let mut ctx = DummyContext::default();

        insert_dynamic(&mut ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);

        // Let 5 seconds elapse.
        assert_eq!(
            ctx.trigger_timers_until_instant::<ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>>(
                DummyInstant::from(Duration::from_secs(5))
            ),
            0
        );

        // The entry should still be there.
        assert_eq!(
            ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );

        // Receive the gratuitous ARP response.
        send_arp_packet(
            &mut ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_REMOTE_MAC,
        );

        // Let the remaining time elapse to the first entry expiration timer.
        assert_eq!(
            ctx.trigger_timers_until_instant::<ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>>(
                DummyInstant::from(DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD)
            ),
            0
        );
        // The entry should still be there.
        assert_eq!(
            ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).unwrap(),
            &TEST_REMOTE_MAC
        );

        // Trigger the entry expiration timer.
        assert!(ctx.trigger_next_timer::<ArpTimerFrameHandler<EthernetLinkDevice, Ipv4Addr>>());
        // The right amount of time should have elapsed.
        assert_eq!(
            ctx.now(),
            DummyInstant::from(Duration::from_secs(5) + DEFAULT_ARP_ENTRY_EXPIRATION_PERIOD)
        );
        // The entry should be gone.
        assert!(ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4).is_none());
        // The device layer should have been notified.
        assert_eq!(ctx.get_ref().addr_resolution_expired, [TEST_REMOTE_IPV4]);
    }

    #[test]
    fn test_arp_table_dynamic_after_static_should_not_set_timer() {
        // Test that, if a static entry exists, attempting to insert a dynamic
        // entry for the same address will not cause a timer to be scheduled.
        let mut ctx = DummyContext::default();

        insert_static_neighbor(&mut ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);
        assert_eq!(ctx.timers().len(), 0);

        insert_dynamic(&mut ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);
        assert_eq!(ctx.timers().len(), 0);
    }
}
