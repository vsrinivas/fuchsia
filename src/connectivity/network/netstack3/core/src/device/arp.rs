// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Address Resolution Protocol (ARP).

use alloc::collections::hash_map::{Entry, HashMap};
use core::{hash::Hash, marker::PhantomData, time::Duration};

use log::{debug, error};
use net_types::{ip::Ipv4Addr, UnicastAddr, UnicastAddress, Witness as _};
use packet::{BufferMut, EmptyBuf, InnerPacketBuilder};
use packet_formats::arp::{ArpOp, ArpPacket, ArpPacketBuilder, HType};

use crate::{
    context::{CounterContext, FrameContext, StateContext, TimerContext, TimerHandler},
    device::link::LinkDevice,
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
pub(crate) trait ArpDevice {
    type HType: HType + UnicastAddress;
}

impl<L: LinkDevice> ArpDevice for L
where
    L::Address: HType + UnicastAddress,
{
    type HType = L::Address;
}

/// The identifier for timer events in the ARP layer.
///
/// This is used to retry sending ARP requests and to expire existing ARP table
/// entries. It is parametric on a device ID type, `D`, and a network protocol
/// type, `P`.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub(crate) struct ArpTimerId<D: ArpDevice, DeviceId> {
    device_id: DeviceId,
    proto_addr: Ipv4Addr,
    _marker: PhantomData<D>,
}

impl<D: ArpDevice, DeviceId> ArpTimerId<D, DeviceId> {
    fn new_request_retry(device_id: DeviceId, proto_addr: Ipv4Addr) -> ArpTimerId<D, DeviceId> {
        ArpTimerId { device_id, proto_addr, _marker: PhantomData }
    }

    pub(crate) fn get_device_id(&self) -> &DeviceId {
        &self.device_id
    }
}

/// The metadata associated with an ARP frame.
#[cfg_attr(test, derive(Debug, PartialEq))]
pub(crate) struct ArpFrameMetadata<D: ArpDevice, DeviceId> {
    /// The ID of the ARP device.
    pub(super) device_id: DeviceId,
    /// The destination hardware address.
    pub(super) dst_addr: D::HType,
}

/// Cleans up state associated with the device.
///
/// Equivalent to calling `sync_ctx.deinitialize`.
///
/// See [`ArpHandler::deinitialize`] for more details.
pub(super) fn deinitialize<D: ArpDevice, C, H: ArpHandler<D, C>>(
    sync_ctx: &mut H,
    ctx: &mut C,
    device_id: H::DeviceId,
) {
    sync_ctx.deinitialize(ctx, device_id)
}

/// An execution context for the ARP protocol when a buffer is provided.
///
/// `BufferArpContext` is like [`ArpContext`], except that it also requires that
/// the context be capable of receiving frames in buffers of type `B`. This is
/// used when a buffer of type `B` is provided to ARP (in particular, in
/// [`handle_packet`]), and allows ARP to reuse that buffer rather than needing
/// to always allocate a new one.
pub(crate) trait BufferArpContext<D: ArpDevice, C: ArpNonSyncCtx<D, Self::DeviceId>, B: BufferMut>:
    ArpContext<D, C>
    + FrameContext<C, B, ArpFrameMetadata<D, <Self as ArpDeviceIdContext<D>>::DeviceId>>
{
}

impl<
        D: ArpDevice,
        B: BufferMut,
        C: ArpNonSyncCtx<D, SC::DeviceId>,
        SC: ArpContext<D, C>
            + FrameContext<C, B, ArpFrameMetadata<D, <Self as ArpDeviceIdContext<D>>::DeviceId>>,
    > BufferArpContext<D, C, B> for SC
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

/// An execution context which provides a `DeviceId` type for ARP internals to
/// share.
pub(crate) trait ArpDeviceIdContext<D: ArpDevice> {
    /// An ID that identifies a particular device.
    type DeviceId: Copy + PartialEq;
}

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
    ArpDeviceIdContext<D>
    + StateContext<C, ArpState<D>, <Self as ArpDeviceIdContext<D>>::DeviceId>
    + FrameContext<C, EmptyBuf, ArpFrameMetadata<D, <Self as ArpDeviceIdContext<D>>::DeviceId>>
{
    /// Get a protocol address of this interface.
    ///
    /// If `device_id` does not have any addresses associated with it, return
    /// `None`.
    fn get_protocol_addr(&self, ctx: &mut C, device_id: Self::DeviceId) -> Option<Ipv4Addr>;

    /// Get the hardware address of this interface.
    fn get_hardware_addr(&self, ctx: &mut C, device_id: Self::DeviceId) -> UnicastAddr<D::HType>;

    /// Notifies the device layer that the hardware address `hw_addr` was
    /// resolved for the given protocol address `proto_addr`.
    fn address_resolved(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        proto_addr: Ipv4Addr,
        hw_addr: D::HType,
    );

    /// Notifies the device layer that the hardware address resolution for the
    /// given protocol address `proto_addr` failed.
    fn address_resolution_failed(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        proto_addr: Ipv4Addr,
    );
}

/// An ARP handler for ARP Events.
///
/// `ArpHandler<D, C>` is implemented for any type which implements
/// [`ArpContext<D, C>`], and it can also be mocked for use in testing.
pub(super) trait ArpHandler<D: ArpDevice, C>:
    ArpDeviceIdContext<D> + TimerHandler<C, ArpTimerId<D, <Self as ArpDeviceIdContext<D>>::DeviceId>>
{
    /// Cleans up state associated with the device.
    ///
    /// The contract is that after `deinitialize` is called, nothing else should
    /// be done with the state.
    fn deinitialize(&mut self, ctx: &mut C, device_id: Self::DeviceId);

    /// Look up the hardware address for a network protocol address.
    fn lookup(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        local_addr: D::HType,
        lookup_addr: Ipv4Addr,
    ) -> Option<D::HType>;

    /// Insert a static entry into this device's ARP table.
    ///
    /// This will cause any conflicting dynamic entry to be removed, and any
    /// future conflicting gratuitous ARPs to be ignored.
    // TODO(rheacock): remove `cfg(test)` when this is used.
    #[cfg(test)]
    fn insert_static_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: Ipv4Addr,
        hw: D::HType,
    );
}

impl<D: ArpDevice, C: ArpNonSyncCtx<D, SC::DeviceId>, SC: ArpContext<D, C>> ArpHandler<D, C>
    for SC
{
    fn deinitialize(&mut self, ctx: &mut C, device_id: Self::DeviceId) {
        // Remove all timers associated with the device
        ctx.cancel_timers_with(|timer_id| *timer_id.get_device_id() == device_id);
        // TODO(rheacock): Send any immediate packets, and potentially flag the state as
        // uninitialized?
    }

    fn lookup(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        _local_addr: D::HType,
        lookup_addr: Ipv4Addr,
    ) -> Option<D::HType> {
        let result = self.get_state_with(device_id).table.lookup(lookup_addr).cloned();

        // Send an ARP Request if the address is not in our cache
        if result.is_none() {
            send_arp_request(self, ctx, device_id, lookup_addr);
        }

        result
    }

    #[cfg(test)]
    fn insert_static_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: Ipv4Addr,
        hw: D::HType,
    ) {
        // Cancel any outstanding timers for this entry; if none exist, these
        // will be no-ops.
        let outstanding_request =
            ctx.cancel_timer(ArpTimerId::new_request_retry(device_id, addr)).is_some();
        // If there was an outstanding resolution request, notify the device
        // layer that it's been resolved.
        if outstanding_request {
            self.address_resolved(ctx, device_id, addr, hw);
        }

        self.get_state_mut_with(device_id).table.insert_static(addr, hw);
    }
}

/// Handle an ARP timer firing.
///
/// Equivalent to calling `sync_ctx.handle_timer`.
pub(super) fn handle_timer<D: ArpDevice, C, H: ArpHandler<D, C>>(
    sync_ctx: &mut H,
    ctx: &mut C,
    id: ArpTimerId<D, H::DeviceId>,
) {
    sync_ctx.handle_timer(ctx, id)
}

impl<D: ArpDevice, C: ArpNonSyncCtx<D, SC::DeviceId>, SC: ArpContext<D, C>>
    TimerHandler<C, ArpTimerId<D, SC::DeviceId>> for SC
{
    fn handle_timer(
        &mut self,
        ctx: &mut C,
        ArpTimerId { device_id, proto_addr, _marker }: ArpTimerId<D, SC::DeviceId>,
    ) {
        send_arp_request(self, ctx, device_id, proto_addr)
    }
}

/// Handles an inbound ARP packet.
pub(crate) fn handle_packet<
    D: ArpDevice,
    C: ArpNonSyncCtx<D, SC::DeviceId>,
    B: BufferMut,
    SC: BufferArpContext<D, C, B>,
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

    let addressed_to_me =
        Some(packet.target_protocol_address()) == sync_ctx.get_protocol_addr(ctx, device_id);

    // The following logic is equivalent to the "ARP, Proxy ARP, and Gratuitous
    // ARP" section of RFC 2002.

    // Gratuitous ARPs, which have the same sender and target address, need to
    // be handled separately since they do not send a response.
    if packet.sender_protocol_address() == packet.target_protocol_address() {
        insert_dynamic(
            sync_ctx,
            ctx,
            device_id,
            packet.sender_protocol_address(),
            packet.sender_hardware_address(),
        );

        // If we have an outstanding retry timer for this host, we should cancel
        // it since we now have the mapping in cache.
        let _: Option<C::Instant> = ctx.cancel_timer(ArpTimerId::new_request_retry(
            device_id,
            packet.sender_protocol_address(),
        ));

        ctx.increment_counter("arp::rx_gratuitous_resolve");
        // Notify device layer:
        sync_ctx.address_resolved(
            ctx,
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
        || sync_ctx
            .get_state_with(device_id)
            .table
            .lookup(packet.sender_protocol_address())
            .is_some()
    {
        insert_dynamic(
            sync_ctx,
            ctx,
            device_id,
            packet.sender_protocol_address(),
            packet.sender_hardware_address(),
        );
        // Since we just got the protocol -> hardware address mapping, we can
        // cancel a timer to resend a request.
        let _: Option<C::Instant> = ctx.cancel_timer(ArpTimerId::new_request_retry(
            device_id,
            packet.sender_protocol_address(),
        ));

        ctx.increment_counter("arp::rx_resolve");
        // Notify device layer:
        sync_ctx.address_resolved(
            ctx,
            device_id,
            packet.sender_protocol_address(),
            packet.sender_hardware_address(),
        );
    }
    if addressed_to_me && packet.operation() == ArpOp::Request {
        let self_hw_addr = sync_ctx.get_hardware_addr(ctx, device_id);
        ctx.increment_counter("arp::rx_request");
        // TODO(joshlf): Do something if send_frame returns an error?
        let _ = sync_ctx.send_frame(
            ctx,
            ArpFrameMetadata { device_id, dst_addr: packet.sender_hardware_address() },
            ArpPacketBuilder::new(
                ArpOp::Response,
                self_hw_addr.get(),
                packet.target_protocol_address(),
                packet.sender_hardware_address(),
                packet.sender_protocol_address(),
            )
            .into_serializer_with(buffer),
        );
    }
}

/// Insert a static entry into this device's ARP table.
///
/// Equivalent to calling `ctx.insert_static_neighbor`.
///
/// See [`ArpHandler::insert_static_neighbor`] for more details.
// TODO(rheacock): remove `cfg(test)` when this is used.
#[cfg(test)]
pub(super) fn insert_static_neighbor<D: ArpDevice, C, H: ArpHandler<D, C>>(
    sync_ctx: &mut H,
    ctx: &mut C,
    device_id: H::DeviceId,
    net: Ipv4Addr,
    hw: D::HType,
) {
    sync_ctx.insert_static_neighbor(ctx, device_id, net, hw)
}

/// Insert a dynamic entry into this device's ARP table.
///
/// The entry will potentially be overwritten by any future static entry and the
/// entry will not be successfully added into the table if there currently is a
/// static entry.
fn insert_dynamic<D: ArpDevice, C: ArpNonSyncCtx<D, SC::DeviceId>, SC: ArpContext<D, C>>(
    sync_ctx: &mut SC,
    _ctx: &mut C,
    device_id: SC::DeviceId,
    net: Ipv4Addr,
    hw: D::HType,
) {
    // Let's extend the expiration deadline by rescheduling the timer. It is
    // assumed that `schedule_timer` will first cancel the timer that is already
    // there.
    let _: bool = sync_ctx.get_state_mut_with(device_id).table.insert_dynamic(net, hw);
}

/// Look up the hardware address for a network protocol address.
///
/// Equivalent to calling `ctx.lookup`.
pub(super) fn lookup<D: ArpDevice, C, H: ArpHandler<D, C>>(
    sync_ctx: &mut H,
    ctx: &mut C,
    device_id: H::DeviceId,
    local_addr: D::HType,
    lookup_addr: Ipv4Addr,
) -> Option<D::HType> {
    sync_ctx.lookup(ctx, device_id, local_addr, lookup_addr)
}

// Since BSD resends ARP requests every 20 seconds and sets the default time
// limit to establish a TCP connection as 75 seconds, 4 is used as the max
// number of tries, which is the initial remaining_tries.
const DEFAULT_ARP_REQUEST_MAX_TRIES: usize = 4;
// Currently at 20 seconds because that's what FreeBSD does.
const DEFAULT_ARP_REQUEST_PERIOD: Duration = Duration::from_secs(20);

fn send_arp_request<D: ArpDevice, C: ArpNonSyncCtx<D, SC::DeviceId>, SC: ArpContext<D, C>>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    lookup_addr: Ipv4Addr,
) {
    let tries_remaining = sync_ctx
        .get_state_mut_with(device_id)
        .table
        .get_remaining_tries(lookup_addr)
        .unwrap_or(DEFAULT_ARP_REQUEST_MAX_TRIES);

    if let Some(sender_protocol_addr) = sync_ctx.get_protocol_addr(ctx, device_id) {
        let self_hw_addr = sync_ctx.get_hardware_addr(ctx, device_id);
        // TODO(joshlf): Do something if send_frame returns an error?
        let _ = sync_ctx.send_frame(
            ctx,
            ArpFrameMetadata { device_id, dst_addr: D::HType::BROADCAST },
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

        let id = ArpTimerId::new_request_retry(device_id, lookup_addr);
        if tries_remaining > 1 {
            // TODO(wesleyac): Configurable timer.
            let _: Option<C::Instant> = ctx.schedule_timer(DEFAULT_ARP_REQUEST_PERIOD, id);
            sync_ctx
                .get_state_mut_with(device_id)
                .table
                .set_waiting(lookup_addr, tries_remaining - 1);
        } else {
            let _: Option<C::Instant> = ctx.cancel_timer(id);
            sync_ctx.get_state_mut_with(device_id).table.remove(lookup_addr);
            sync_ctx.address_resolution_failed(ctx, device_id, lookup_addr);
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
pub(crate) struct ArpState<D: ArpDevice> {
    table: ArpTable<D::HType>,
}

impl<D: ArpDevice> Default for ArpState<D> {
    fn default() -> Self {
        ArpState { table: ArpTable::default() }
    }
}

struct ArpTable<H> {
    table: HashMap<Ipv4Addr, ArpValue<H>>,
}

#[derive(Debug, Eq, PartialEq)] // for testing
enum ArpValue<H> {
    // Invariant: no timers exist for this entry.
    _Static { hardware_addr: H },
    // Invariant: a single entry expiration timer exists for this entry.
    Dynamic { hardware_addr: H },
    // Invariant: a single request retry timer exists for this entry.
    Waiting { remaining_tries: usize },
}

impl<H> ArpTable<H> {
    // TODO(rheacock): remove `cfg(test)` when this is used
    #[cfg(test)]
    fn insert_static(&mut self, net: Ipv4Addr, hw: H) {
        // A static entry overrides everything, so just insert it.
        let _: Option<_> = self.table.insert(net, ArpValue::_Static { hardware_addr: hw });
    }

    /// This function tries to insert a dynamic entry into the ArpTable, the
    /// bool returned from the function is used to indicate whether the
    /// insertion is successful.
    fn insert_dynamic(&mut self, net: Ipv4Addr, hw: H) -> bool {
        // A dynamic entry should not override a static one, if that happens,
        // don't do it. if we want to handle this kind of situation in the
        // future, we can make this function return a `Result`.
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
                let _: &mut _ = entry.insert(new_val);
            }
        }
        true
    }

    fn remove(&mut self, net: Ipv4Addr) {
        let _: Option<_> = self.table.remove(&net);
    }

    fn get_remaining_tries(&self, net: Ipv4Addr) -> Option<usize> {
        if let Some(ArpValue::Waiting { remaining_tries }) = self.table.get(&net) {
            Some(*remaining_tries)
        } else {
            None
        }
    }

    fn set_waiting(&mut self, net: Ipv4Addr, remaining_tries: usize) {
        // TODO(https://fxbug.dev/81368): Remove this type.
        //
        // This type (ArpTable) is type system-defeating. Instead of interacting with entries in
        // the hash map, we do these point-access, which is both suboptimal and impossible to
        // reason about because the type information is far away. This entire module needs a
        // rewrite, so I am just sprinkling `let _` here for now.
        let _: Option<_> = self.table.insert(net, ArpValue::Waiting { remaining_tries });
    }

    fn lookup(&self, addr: Ipv4Addr) -> Option<&H> {
        match self.table.get(&addr) {
            Some(ArpValue::_Static { hardware_addr })
            | Some(ArpValue::Dynamic { hardware_addr }) => Some(hardware_addr),
            _ => None,
        }
    }
}

impl<H> Default for ArpTable<H> {
    fn default() -> Self {
        ArpTable { table: HashMap::default() }
    }
}

#[cfg(test)]
mod tests {
    use alloc::{vec, vec::Vec};
    use core::iter;

    use net_types::{ethernet::Mac, ip::Ipv4Addr};
    use packet::{ParseBuffer, Serializer};
    use packet_formats::arp::{peek_arp_types, ArpHardwareType, ArpNetworkType, ArpPacketBuilder};
    use test_case::test_case;

    use super::*;
    use crate::{
        context::{
            testutil::{
                DummyCtx, DummyInstant, DummyNetwork, DummyNonSyncCtx, DummySyncCtx,
                DummyTimerCtxExt,
            },
            InstantContext,
        },
        device::ethernet::EthernetLinkDevice,
        ip::DummyDeviceId,
        testutil::assert_empty,
    };

    const TEST_LOCAL_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_REMOTE_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_ANOTHER_REMOTE_IPV4: Ipv4Addr = Ipv4Addr::new([9, 10, 11, 12]);
    const TEST_LOCAL_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_REMOTE_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);
    const TEST_INVALID_MAC: Mac = Mac::new([0, 0, 0, 0, 0, 0]);
    const TEST_REQUEST_RETRY_TIMER_ID: ArpTimerId<EthernetLinkDevice, ()> =
        ArpTimerId { device_id: (), proto_addr: TEST_REMOTE_IPV4, _marker: PhantomData };

    /// A dummy `ArpContext` that stores frames, address resolution events, and
    /// address resolution failure events.
    struct DummyArpCtx {
        proto_addr: Option<Ipv4Addr>,
        hw_addr: UnicastAddr<Mac>,
        addr_resolved: Vec<(Ipv4Addr, Mac)>,
        addr_resolution_failed: Vec<Ipv4Addr>,
        arp_state: ArpState<EthernetLinkDevice>,
    }

    impl Default for DummyArpCtx {
        fn default() -> DummyArpCtx {
            DummyArpCtx {
                proto_addr: Some(TEST_LOCAL_IPV4),
                hw_addr: UnicastAddr::new(TEST_LOCAL_MAC).unwrap(),
                addr_resolved: Vec::new(),
                addr_resolution_failed: Vec::new(),
                arp_state: ArpState::default(),
            }
        }
    }

    type MockNonSyncCtx = DummyNonSyncCtx<ArpTimerId<EthernetLinkDevice, ()>, (), ()>;

    type MockCtx =
        DummySyncCtx<DummyArpCtx, ArpFrameMetadata<EthernetLinkDevice, ()>, DummyDeviceId>;

    impl ArpDeviceIdContext<EthernetLinkDevice> for MockCtx {
        type DeviceId = ();
    }

    impl ArpContext<EthernetLinkDevice, MockNonSyncCtx> for MockCtx {
        fn get_protocol_addr(&self, _ctx: &mut MockNonSyncCtx, _device_id: ()) -> Option<Ipv4Addr> {
            self.get_ref().proto_addr
        }

        fn get_hardware_addr(&self, _ctx: &mut MockNonSyncCtx, _device_id: ()) -> UnicastAddr<Mac> {
            self.get_ref().hw_addr
        }

        fn address_resolved(
            &mut self,
            _ctx: &mut MockNonSyncCtx,
            _device_id: (),
            proto_addr: Ipv4Addr,
            hw_addr: Mac,
        ) {
            self.get_mut().addr_resolved.push((proto_addr, hw_addr));
        }

        fn address_resolution_failed(
            &mut self,
            _ctx: &mut MockNonSyncCtx,
            _device_id: (),
            proto_addr: Ipv4Addr,
        ) {
            self.get_mut().addr_resolution_failed.push(proto_addr);
        }
    }

    impl StateContext<MockNonSyncCtx, ArpState<EthernetLinkDevice>> for MockCtx {
        fn get_state_with(&self, _id: ()) -> &ArpState<EthernetLinkDevice> {
            &self.get_ref().arp_state
        }

        fn get_state_mut_with(&mut self, _id: ()) -> &mut ArpState<EthernetLinkDevice> {
            &mut self.get_mut().arp_state
        }
    }

    fn send_arp_packet(
        sync_ctx: &mut MockCtx,
        ctx: &mut MockNonSyncCtx,
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

        handle_packet::<_, _, _, _>(sync_ctx, ctx, (), buf);
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
        sync_ctx: &MockCtx,
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

    // Validate that `sync_ctx` contains exactly one installed timer with the given
    // instant and ID.
    fn validate_single_timer(
        non_sync_ctx: &MockNonSyncCtx,
        instant: Duration,
        id: ArpTimerId<EthernetLinkDevice, ()>,
    ) {
        non_sync_ctx.timer_ctx().assert_timers_installed([(id, DummyInstant::from(instant))]);
    }

    fn validate_single_retry_timer(
        non_sync_ctx: &MockNonSyncCtx,
        instant: Duration,
        addr: Ipv4Addr,
    ) {
        validate_single_timer(non_sync_ctx, instant, ArpTimerId::new_request_retry((), addr))
    }

    #[test]
    fn test_receive_gratuitous_arp_request() {
        // Test that, when we receive a gratuitous ARP request, we cache the
        // sender's address information, and we do not send a response.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());
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
        assert_eq!(
            sync_ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4),
            Some(&TEST_REMOTE_MAC)
        );
        // Gratuitous ARPs should not prompt a response.
        assert_empty(sync_ctx.frames().iter());
    }

    #[test]
    fn test_receive_gratuitous_arp_response() {
        // Test that, when we receive a gratuitous ARP response, we cache the
        // sender's address information, and we do not send a response.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());
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
        assert_eq!(
            sync_ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4),
            Some(&TEST_REMOTE_MAC)
        );
        // Gratuitous ARPs should not send a response.
        assert_empty(sync_ctx.frames().iter());
    }

    #[test]
    fn test_receive_gratuitous_arp_response_existing_request() {
        // Test that, if we have an outstanding request retry timer and receive
        // a gratuitous ARP for the same host, we cancel the timer and notify
        // the device layer.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());

        assert_eq!(
            lookup(&mut sync_ctx, &mut non_sync_ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4),
            None
        );

        // We should have installed a single retry timer.
        validate_single_retry_timer(&non_sync_ctx, DEFAULT_ARP_REQUEST_PERIOD, TEST_REMOTE_IPV4);

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
        assert_eq!(
            sync_ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4),
            Some(&TEST_REMOTE_MAC)
        );

        // We should have notified the device layer.
        assert_eq!(
            sync_ctx.get_ref().addr_resolved.as_slice(),
            [(TEST_REMOTE_IPV4, TEST_REMOTE_MAC)]
        );

        // Gratuitous ARPs should not send a response (the 1 frame is for the
        // original request).
        assert_eq!(sync_ctx.frames().len(), 1);
    }

    #[test]
    fn test_cancel_timers_on_deinitialize() {
        // Test that associated timers are cancelled when the arp device
        // is deinitialized.

        // Cancelling timers matches on the DeviceId, so setup a context that
        // uses IDs. The test doesn't use the context functions, so it's okay
        // that they return the same info.
        type MockNonSyncCtx2 = DummyNonSyncCtx<ArpTimerId<EthernetLinkDevice, usize>, (), ()>;

        type MockCtx2 = crate::context::testutil::DummySyncCtx<
            DummyArpCtx,
            ArpFrameMetadata<EthernetLinkDevice, usize>,
            DummyDeviceId,
        >;

        impl ArpDeviceIdContext<EthernetLinkDevice> for MockCtx2 {
            type DeviceId = usize;
        }

        impl ArpContext<EthernetLinkDevice, MockNonSyncCtx2> for MockCtx2 {
            fn get_protocol_addr(
                &self,
                _ctx: &mut MockNonSyncCtx2,
                _device_id: usize,
            ) -> Option<Ipv4Addr> {
                self.get_ref().proto_addr
            }

            fn get_hardware_addr(
                &self,
                _ctx: &mut MockNonSyncCtx2,
                _device_id: usize,
            ) -> UnicastAddr<Mac> {
                self.get_ref().hw_addr
            }

            fn address_resolved(
                &mut self,
                _ctx: &mut MockNonSyncCtx2,
                _device_id: usize,
                proto_addr: Ipv4Addr,
                hw_addr: Mac,
            ) {
                self.get_mut().addr_resolved.push((proto_addr, hw_addr));
            }

            fn address_resolution_failed(
                &mut self,
                _ctx: &mut MockNonSyncCtx2,
                _device_id: usize,
                proto_addr: Ipv4Addr,
            ) {
                self.get_mut().addr_resolution_failed.push(proto_addr);
            }
        }

        impl StateContext<MockNonSyncCtx2, ArpState<EthernetLinkDevice>, usize> for MockCtx2 {
            fn get_state_with(&self, _id: usize) -> &ArpState<EthernetLinkDevice> {
                &self.get_ref().arp_state
            }

            fn get_state_mut_with(&mut self, _id: usize) -> &mut ArpState<EthernetLinkDevice> {
                &mut self.get_mut().arp_state
            }
        }

        // Setup up a dummy context and trigger a timer with a lookup
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx2::default());

        let device_id_0: usize = 0;
        let device_id_1: usize = 1;

        assert_eq!(
            lookup(&mut sync_ctx, &mut non_sync_ctx, device_id_0, TEST_LOCAL_MAC, TEST_REMOTE_IPV4),
            None
        );

        // We should have installed a single retry timer.
        let deadline = non_sync_ctx.now() + DEFAULT_ARP_REQUEST_PERIOD;
        let timer = ArpTimerId::new_request_retry(device_id_0, TEST_REMOTE_IPV4);
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer, deadline)]);

        // Deinitializing a different ID should not impact the current timer.
        deinitialize(&mut sync_ctx, &mut non_sync_ctx, device_id_1);
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer, deadline)]);

        // Deinitializing the correct ID should cancel the timer.
        deinitialize(&mut sync_ctx, &mut non_sync_ctx, device_id_0);
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }

    #[test]
    fn test_send_arp_request_on_cache_miss() {
        // Test that, when we perform a lookup that fails, we send an ARP
        // request and install a timer to retry.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());

        // Perform the lookup.
        assert_eq!(
            lookup(&mut sync_ctx, &mut non_sync_ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4),
            None
        );

        // We should have sent a single ARP request.
        validate_last_arp_packet(
            &sync_ctx,
            1,
            Mac::BROADCAST,
            ArpOp::Request,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            Mac::BROADCAST,
        );

        // We should have installed a single retry timer.
        validate_single_retry_timer(&non_sync_ctx, DEFAULT_ARP_REQUEST_PERIOD, TEST_REMOTE_IPV4);

        // Test that, when we receive an ARP response, we cancel the timer.
        send_arp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_MAC,
            TEST_LOCAL_MAC,
        );

        // The response should now be in our cache.
        assert_eq!(
            sync_ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4),
            Some(&TEST_REMOTE_MAC)
        );

        // We should have notified the device layer.
        assert_eq!(
            sync_ctx.get_ref().addr_resolved.as_slice(),
            [(TEST_REMOTE_IPV4, TEST_REMOTE_MAC)]
        );
    }

    #[test]
    fn test_no_arp_request_on_cache_hit() {
        // Test that, when we perform a lookup that succeeds, we do not send an
        // ARP request or install a retry timer.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());

        // Perform a gratuitous ARP to populate the cache.
        send_arp_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_REMOTE_MAC,
        );

        // Perform the lookup.
        assert_eq!(
            lookup(&mut sync_ctx, &mut non_sync_ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4),
            Some(TEST_REMOTE_MAC)
        );

        // We should not have sent any ARP request.
        assert_empty(sync_ctx.frames().iter());
        // We should not have set a retry timer.
        assert_eq!(
            non_sync_ctx.cancel_timer(ArpTimerId::new_request_retry((), TEST_REMOTE_IPV4)),
            None
        );
    }

    #[test]
    fn test_exhaust_retries_arp_request() {
        // Test that, after performing a certain number of ARP request retries,
        // we give up and don't install another retry timer.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());

        assert_eq!(
            lookup(&mut sync_ctx, &mut non_sync_ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4),
            None
        );

        // `i` represents the `i`th request, so we start it at 1 since we
        // already sent one request during the call to `lookup`.
        for i in 1..DEFAULT_ARP_REQUEST_MAX_TRIES {
            // We should have sent i requests total. We have already validated
            // all the rest, so only validate the most recent one.
            validate_last_arp_packet(
                &sync_ctx,
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
                sync_ctx.get_ref().arp_state.table.get_remaining_tries(TEST_REMOTE_IPV4),
                Some(DEFAULT_ARP_REQUEST_MAX_TRIES - i)
            );

            // There should be a single ARP request retry timer installed.
            validate_single_retry_timer(
                &non_sync_ctx,
                // Duration only implements Mul<u32>
                DEFAULT_ARP_REQUEST_PERIOD * (i as u32),
                TEST_REMOTE_IPV4,
            );

            // Trigger the ARP request retry timer.
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, TimerHandler::handle_timer),
                Some(TEST_REQUEST_RETRY_TIMER_ID)
            );
        }

        // We should have sent DEFAULT_ARP_REQUEST_MAX_TRIES requests total. We
        // have already validated all the rest, so only validate the most recent
        // one.
        validate_last_arp_packet(
            &sync_ctx,
            DEFAULT_ARP_REQUEST_MAX_TRIES,
            Mac::BROADCAST,
            ArpOp::Request,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            Mac::BROADCAST,
        );

        // There shouldn't be any timers installed.
        non_sync_ctx.timer_ctx().assert_no_timers_installed();

        // The table entry should have been completely removed.
        assert_eq!(sync_ctx.get_ref().arp_state.table.table.get(&TEST_REMOTE_IPV4), None);

        // We should have notified the device layer of the failure.
        assert_eq!(sync_ctx.get_ref().addr_resolution_failed.as_slice(), [TEST_REMOTE_IPV4]);
    }

    #[test]
    fn test_handle_arp_request() {
        // Test that, when we receive an ARP request, we cache the sender's
        // address information and send an ARP response.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());

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
        assert_eq!(
            lookup(&mut sync_ctx, &mut non_sync_ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4),
            Some(TEST_REMOTE_MAC)
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

    #[test]
    fn test_arp_table() {
        let mut t: ArpTable<Mac> = ArpTable::default();
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 1])), None);
        let mac = Mac::new([1, 2, 3, 4, 5, 6]);
        assert!(t.insert_dynamic(Ipv4Addr::new([10, 0, 0, 1]), mac));
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 1])), Some(&mac));
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 2])), None);
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

        let mut network = DummyNetwork::new(
            {
                host_iter.clone().map(|cfg| {
                    let ArpHostConfig { name, proto_addr, hw_addr } = cfg;
                    let mut ctx = DummyCtx::with_sync_ctx(MockCtx::default());
                    let DummyCtx { sync_ctx, non_sync_ctx: _ } = &mut ctx;
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
                            Some((*name, (), None))
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

        // The lookup should fail.
        network.with_context(local_name, |DummyCtx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                lookup(sync_ctx, non_sync_ctx, (), local_hw_addr, requested_remote_proto_addr),
                None
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
            // We should have installed a retry timer.
            validate_single_retry_timer(
                non_sync_ctx,
                DEFAULT_ARP_REQUEST_PERIOD,
                requested_remote_proto_addr,
            );
        });
        // Step once to deliver the ARP request to the remotes.
        let res = network.step(
            |DummyCtx { sync_ctx, non_sync_ctx }, device_id, buf| {
                handle_packet(sync_ctx, non_sync_ctx, device_id, buf)
            },
            |DummyCtx { sync_ctx, non_sync_ctx }, _ctx, id| {
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
        assert_eq!(
            network
                .sync_ctx(requested_remote_name)
                .get_ref()
                .arp_state
                .table
                .lookup(local_proto_addr),
            Some(&LOCAL_HOST_CFG.hw_addr)
        );
        // The requested remote should have sent an ARP response.
        validate_last_arp_packet(
            network.sync_ctx(requested_remote_name),
            1,
            local_hw_addr,
            ArpOp::Response,
            requested_remote_proto_addr,
            local_proto_addr,
            requested_remote_hw_addr,
            local_hw_addr,
        );

        other_remote_cfgs.iter().for_each(|non_requested_remote| {
            let ArpHostConfig { name: unrequested_remote_name, proto_addr: _, hw_addr: _ } =
                non_requested_remote;
            // The non-requested_remote should not have populated its ARP cache.
            assert_eq!(
                network
                    .sync_ctx(*unrequested_remote_name)
                    .get_ref()
                    .arp_state
                    .table
                    .lookup(local_proto_addr),
                None
            );

            // The non-requested_remote should not have sent an ARP response.
            assert_empty(network.sync_ctx(*unrequested_remote_name).frames().iter());
        });

        // Step once to deliver the ARP response to the local.
        let res = network.step(
            |DummyCtx { sync_ctx, non_sync_ctx }, device_id, buf| {
                handle_packet(sync_ctx, non_sync_ctx, device_id, buf)
            },
            |DummyCtx { sync_ctx, non_sync_ctx }, _ctx, id| {
                TimerHandler::handle_timer(sync_ctx, non_sync_ctx, id)
            },
        );
        assert_eq!(res.timers_fired, 0);
        assert_eq!(res.frames_sent, expected_frames_sent_bcast);

        // The local should have populated its cache with the remote's
        // information.
        assert_eq!(
            network
                .sync_ctx(local_name)
                .get_ref()
                .arp_state
                .table
                .lookup(requested_remote_proto_addr),
            Some(&requested_remote_hw_addr)
        );
        // The device layer should have been notified.
        assert_eq!(
            network.sync_ctx(local_name).get_ref().addr_resolved.as_slice(),
            [(requested_remote_proto_addr, requested_remote_hw_addr)]
        );
    }

    #[test]
    fn test_arp_table_static_override_dynamic() {
        let mut table: ArpTable<Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac0 = Mac::new([1, 2, 3, 4, 5, 6]);
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        assert!(table.insert_dynamic(ip, mac0));
        assert_eq!(table.lookup(ip), Some(&mac0));
        table.insert_static(ip, mac1);
        assert_eq!(table.lookup(ip), Some(&mac1));
    }

    #[test]
    fn test_arp_table_static_override_static() {
        let mut table: ArpTable<Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac0 = Mac::new([1, 2, 3, 4, 5, 6]);
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.insert_static(ip, mac0);
        assert_eq!(table.lookup(ip), Some(&mac0));
        table.insert_static(ip, mac1);
        assert_eq!(table.lookup(ip), Some(&mac1));
    }

    #[test]
    fn test_arp_table_static_override_waiting() {
        let mut table: ArpTable<Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.set_waiting(ip, 4);
        assert_eq!(table.lookup(ip), None);
        table.insert_static(ip, mac1);
        assert_eq!(table.lookup(ip), Some(&mac1));
    }

    #[test]
    fn test_arp_table_dynamic_override_waiting() {
        let mut table: ArpTable<Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.set_waiting(ip, 4);
        assert_eq!(table.lookup(ip), None);
        assert!(table.insert_dynamic(ip, mac1));
        assert_eq!(table.lookup(ip), Some(&mac1));
    }

    #[test]
    fn test_arp_table_dynamic_override_dynamic() {
        let mut table: ArpTable<Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac0 = Mac::new([1, 2, 3, 4, 5, 6]);
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        assert!(table.insert_dynamic(ip, mac0));
        assert_eq!(table.lookup(ip), Some(&mac0));
        assert!(table.insert_dynamic(ip, mac1));
        assert_eq!(table.lookup(ip), Some(&mac1));
    }

    #[test]
    fn test_arp_table_dynamic_should_not_override_static() {
        let mut table: ArpTable<Mac> = ArpTable::default();
        let ip = TEST_REMOTE_IPV4;
        let mac0 = Mac::new([1, 2, 3, 4, 5, 6]);
        let mac1 = Mac::new([6, 5, 4, 3, 2, 1]);
        table.insert_static(ip, mac0);
        assert_eq!(table.lookup(ip), Some(&mac0));
        assert!(!table.insert_dynamic(ip, mac1));
        assert_eq!(table.lookup(ip), Some(&mac0));
    }

    #[test]
    fn test_arp_table_static_cancel_waiting_timer() {
        // Test that, if we insert a static entry while a request retry timer is
        // installed, that timer is canceled, and the device layer is notified.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());

        // Perform a lookup in order to kick off a request and install a retry
        // timer.
        assert_eq!(
            lookup(&mut sync_ctx, &mut non_sync_ctx, (), TEST_LOCAL_MAC, TEST_REMOTE_IPV4),
            None
        );

        // We should be in the Waiting state.
        assert_eq!(
            sync_ctx.get_ref().arp_state.table.get_remaining_tries(TEST_REMOTE_IPV4),
            Some(DEFAULT_ARP_REQUEST_MAX_TRIES - 1)
        );
        // We should have an ARP request retry timer set.
        validate_single_retry_timer(&non_sync_ctx, DEFAULT_ARP_REQUEST_PERIOD, TEST_REMOTE_IPV4);

        // Now insert a static entry.
        insert_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            (),
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
        );

        // The timer should have been canceled.
        non_sync_ctx.timer_ctx().assert_no_timers_installed();

        // We should have notified the device layer.
        assert_eq!(
            sync_ctx.get_ref().addr_resolved.as_slice(),
            [(TEST_REMOTE_IPV4, TEST_REMOTE_MAC)]
        );
    }

    #[test]
    fn test_arp_table_static_cancel_expiration_timer() {
        // Test that, if we insert a static entry that overrides an existing
        // dynamic entry, the dynamic entry's expiration timer is canceled.

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());

        insert_dynamic(&mut sync_ctx, &mut non_sync_ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);

        // We should have the address in cache.
        assert_eq!(
            sync_ctx.get_ref().arp_state.table.lookup(TEST_REMOTE_IPV4),
            Some(&TEST_REMOTE_MAC)
        );

        // Now insert a static entry.
        insert_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            (),
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
        );

        // The timer should have been canceled.
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }

    #[test]
    fn test_arp_table_dynamic_after_static_should_not_set_timer() {
        // Test that, if a static entry exists, attempting to insert a dynamic
        // entry for the same address will not cause a timer to be scheduled.
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::default());

        insert_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            (),
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();

        insert_dynamic(&mut sync_ctx, &mut non_sync_ctx, (), TEST_REMOTE_IPV4, TEST_REMOTE_MAC);
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }
}
