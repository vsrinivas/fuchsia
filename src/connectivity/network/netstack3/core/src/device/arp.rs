// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Address Resolution Protocol (ARP).

use std::collections::HashMap;
use std::hash::Hash;
use std::time::Duration;

use packet::{BufferMut, InnerSerializer, MtuError, Serializer};

use crate::device::ethernet::EthernetArpDevice;
use crate::device::DeviceLayerTimerId;
use crate::ip::Ipv4Addr;
use crate::wire::arp::{ArpPacket, ArpPacketBuilder, HType, PType};
use crate::{Context, EventDispatcher, TimerId, TimerIdInner};
use log::debug;

/// The type of an ARP operation.
#[derive(Debug, Eq, PartialEq)]
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

/// The identifier for timer events in the ARP layer.
///
/// This is used to retry sending ARP requests.
#[derive(Copy, Clone, PartialEq)]
pub(crate) struct ArpTimerId<P: PType> {
    device_id: u64,
    ip_addr: P,
}

impl ArpTimerId<Ipv4Addr> {
    fn new_ipv4_timer_id(device_id: u64, ip_addr: Ipv4Addr) -> TimerId {
        TimerId(TimerIdInner::DeviceLayer(DeviceLayerTimerId::ArpIpv4(ArpTimerId {
            device_id,
            ip_addr,
        })))
    }
}

/// A device layer protocol which can support ARP.
///
/// An `ArpDevice<P>` is a device layer protocol which can support ARP with the
/// network protocol `P` (e.g., IPv4, IPv6, etc).
pub(crate) trait ArpDevice<P: PType + Eq + Hash>: Sized {
    /// The hardware address type used by this protocol.
    type HardwareAddr: HType;

    /// The broadcast address.
    const BROADCAST: Self::HardwareAddr;

    /// Send an ARP packet in a device layer frame.
    ///
    /// `send_arp_frame` accepts a device ID, a destination hardware address,
    /// and a `Serializer`. It computes the routing information, serializes the
    /// request in a device layer frame, and sends it.
    fn send_arp_frame<D: EventDispatcher, S: Serializer>(
        ctx: &mut Context<D>,
        device_id: u64,
        dst: Self::HardwareAddr,
        body: S,
    ) -> Result<(), MtuError<S::InnerError>>;

    /// Get a mutable reference to a device's ARP state.
    fn get_arp_state<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> &mut ArpState<P, Self>;

    /// Get the protocol address of this interface.
    fn get_protocol_addr<D: EventDispatcher>(ctx: &mut Context<D>, device_id: u64) -> Option<P>;

    /// Get the hardware address of this interface.
    fn get_hardware_addr<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> Self::HardwareAddr;

    /// Notifies the device layer that the hardware address `hw_addr`
    /// was resolved for a the given protocol address `proto_addr`.
    fn address_resolved<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        proto_addr: P,
        hw_addr: Self::HardwareAddr,
    );

    /// Notifies the device layer that the hardware address resolution for
    /// the the given protocol address `proto_addr` failed.
    fn address_resolution_failed<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        proto_addr: P,
    );
}

/// Handle a ARP timer event
///
/// This currently only supports Ipv4/Ethernet ARP, since we know that that is
/// the only case that the netstack currently handles. In the future, this may
/// be extended to support other hardware or protocol types.
pub(crate) fn handle_timeout<D: EventDispatcher>(ctx: &mut Context<D>, id: ArpTimerId<Ipv4Addr>) {
    handle_timeout_inner::<D, Ipv4Addr, EthernetArpDevice>(ctx, id);
}

fn handle_timeout_inner<D: EventDispatcher, P: PType + Eq + Hash, AD: ArpDevice<P>>(
    ctx: &mut Context<D>,
    id: ArpTimerId<P>,
) {
    send_arp_request::<D, P, AD>(ctx, id.device_id, id.ip_addr);
}

/// Receive an ARP packet from a device.
///
/// The protocol and hardware types (`P` and `D::HardwareAddr` respectively)
/// must be set statically. Unless there is only one valid pair of protocol and
/// hardware types in a given context, it is the caller's responsibility to call
/// `peek_arp_types` in order to determine which types to use in calling this
/// function.
pub(crate) fn receive_arp_packet<
    D: EventDispatcher,
    P: PType + Eq + Hash,
    AD: ArpDevice<P>,
    B: BufferMut,
>(
    ctx: &mut Context<D>,
    device_id: u64,
    src_addr: AD::HardwareAddr,
    dst_addr: AD::HardwareAddr,
    mut buffer: B,
) {
    // TODO(wesleyac) Add support for probe.
    let packet = match buffer.parse::<ArpPacket<_, AD::HardwareAddr, P>>() {
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
        Some(packet.target_protocol_address()) == AD::get_protocol_addr(ctx, device_id);
    let table = &mut AD::get_arp_state(ctx, device_id).table;

    // The following logic is equivalent to the "ARP, Proxy ARP, and Gratuitous ARP"
    // section of RFC 2002.

    // Gratuitous ARPs, which have the same sender and target address, need to
    // be handled separately since they do not send a response.
    if packet.sender_protocol_address() == packet.target_protocol_address() {
        table.insert(packet.sender_protocol_address(), packet.sender_hardware_address());

        increment_counter!(ctx, "arp::rx_gratuitous_resolve");
        // Notify device layer:
        AD::address_resolved(
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

    if addressed_to_me || table.lookup(packet.sender_protocol_address()).is_some() {
        table.insert(packet.sender_protocol_address(), packet.sender_hardware_address());
        // Since we just got the protocol -> hardware address mapping, we can
        // cancel a timeout to resend a request.
        ctx.dispatcher.cancel_timeout(ArpTimerId::new_ipv4_timer_id(
            device_id,
            packet.sender_protocol_address().addr(),
        ));

        increment_counter!(ctx, "arp::rx_resolve");
        // Notify device layer:
        AD::address_resolved(
            ctx,
            device_id,
            packet.sender_protocol_address(),
            packet.sender_hardware_address(),
        );
    }
    if addressed_to_me && packet.operation() == ArpOp::Request {
        let self_hw_addr = AD::get_hardware_addr(ctx, device_id);
        increment_counter!(ctx, "arp::rx_request");
        AD::send_arp_frame(
            ctx,
            device_id,
            packet.sender_hardware_address(),
            InnerSerializer::new_vec(
                ArpPacketBuilder::new(
                    ArpOp::Response,
                    self_hw_addr,
                    packet.target_protocol_address(),
                    packet.sender_hardware_address(),
                    packet.sender_protocol_address(),
                ),
                buffer,
            ),
        );
    }
}

/// Insert an entry into the ARP table.
pub(crate) fn insert<D: EventDispatcher, P: PType + Eq + Hash, AD: ArpDevice<P>>(
    ctx: &mut Context<D>,
    device_id: u64,
    net: P,
    hw: AD::HardwareAddr,
) {
    AD::get_arp_state(ctx, device_id).table.insert(net, hw);
}

/// Look up the hardware address for a network protocol address.
pub(crate) fn lookup<D: EventDispatcher, P: PType + Eq + Hash, AD: ArpDevice<P>>(
    ctx: &mut Context<D>,
    device_id: u64,
    local_addr: AD::HardwareAddr,
    lookup_addr: P,
) -> Option<AD::HardwareAddr> {
    // TODO(joshlf): Figure out what to do if a frame can't be sent right now
    // because it needs to wait for an ARP reply. Where do we put those frames?
    // How do we associate them with the right ARP reply? How do we retreive
    // them when we get that ARP reply? How do we time out so we don't hold onto
    // a stale frame forever?
    let result = AD::get_arp_state(ctx, device_id).table.lookup(lookup_addr).cloned();

    // Send an ARP Request if the address is not in our cache
    if result.is_none() {
        send_arp_request::<D, P, AD>(ctx, device_id, lookup_addr);
    }

    result
}

// Since BSD resends ARP requests every 20 seconds and sets the default
// time limit to establish a TCP connection as 75 seconds, 4 is used as
// the max number of tries, which is the initial remaining_tries.
const DEFAULT_ARP_REQUEST_MAX_TRIES: usize = 4;
// Currently at 20 seconds because that's what FreeBSD does.
const DEFAULT_ARP_REQUEST_PERIOD: u64 = 20;

fn send_arp_request<D: EventDispatcher, P: PType + Eq + Hash, AD: ArpDevice<P>>(
    ctx: &mut Context<D>,
    device_id: u64,
    lookup_addr: P,
) {
    let tries_remaining = AD::get_arp_state(ctx, device_id)
        .table
        .get_remaining_tries(lookup_addr)
        .unwrap_or(DEFAULT_ARP_REQUEST_MAX_TRIES);

    if let Some(sender_protocol_addr) = AD::get_protocol_addr(ctx, device_id) {
        let self_hw_addr = AD::get_hardware_addr(ctx, device_id);
        // TODO(joshlf): Do something if send_arp_frame returns an error?
        AD::send_arp_frame(
            ctx,
            device_id,
            AD::BROADCAST,
            ArpPacketBuilder::new(
                ArpOp::Request,
                self_hw_addr,
                sender_protocol_addr,
                // This is meaningless, since RFC 826 does not specify the behaviour.
                // However, the broadcast address is sensible, as this is the actual
                // address we are sending the packet to.
                AD::BROADCAST,
                lookup_addr,
            ),
        );

        let id = ArpTimerId::new_ipv4_timer_id(device_id, lookup_addr.addr());
        if tries_remaining > 1 {
            // TODO(wesleyac): Configurable timeout.
            ctx.dispatcher.schedule_timeout(Duration::from_secs(DEFAULT_ARP_REQUEST_PERIOD), id);
            AD::get_arp_state(ctx, device_id).table.set_waiting(lookup_addr, tries_remaining - 1);
        } else {
            ctx.dispatcher.cancel_timeout(id);
            AD::get_arp_state(ctx, device_id).table.remove(lookup_addr);
            AD::address_resolution_failed(ctx, device_id, lookup_addr);
        }
    } else {
        // RFC 826 does not specify what to do if we don't have a local address, but there is no
        // reasonable way to send an ARP request without one (as the receiver will cache our local
        // address on receiving the packet. So, if this is the case, we do not send an ARP request.
        // TODO(wesleyac): Should we cache these, and send packets once we have an address?
        debug!("Not sending ARP request, since we don't know our local protocol address");
    }
}

/// The state associated with an instance of the Address Resolution Protocol
/// (ARP).
///
/// Each device will contain an `ArpState` object for each of the network
/// protocols that it supports.
pub(crate) struct ArpState<P: PType + Hash + Eq, D: ArpDevice<P>> {
    // NOTE(joshlf): Taking an ArpDevice type parameter is technically
    // unnecessary here; we could instead just be parametric on a hardware type
    // and a network protocol type. However, doing it this way ensure that
    // device layer code doesn't accidentally invoke receive_arp_packet with
    // different ArpDevice implementations in different places (this would fail
    // to compile because the get_arp_state method on ArpDevice returns an
    // ArpState<_, Self>, which requires that the ArpDevice implementation
    // matches the type of the ArpState stored in that device's state).
    table: ArpTable<D::HardwareAddr, P>,
}

impl<P: PType + Hash + Eq, D: ArpDevice<P>> Default for ArpState<P, D> {
    fn default() -> Self {
        ArpState { table: ArpTable::default() }
    }
}

struct ArpTable<H, P: Hash + Eq> {
    table: HashMap<P, ArpValue<H>>,
}

#[derive(Debug, Eq, PartialEq)] // for testing
enum ArpValue<H> {
    Known { hardware_addr: H },
    Waiting { remaining_tries: usize },
}

impl<H, P: Hash + Eq> ArpTable<H, P> {
    fn insert(&mut self, net: P, hw: H) {
        self.table.insert(net, ArpValue::Known { hardware_addr: hw });
    }

    fn remove(&mut self, net: P) {
        self.table.remove(&net);
    }

    fn get_remaining_tries(&mut self, net: P) -> Option<usize> {
        let remaining_tries =
            if let Some(ArpValue::Waiting { remaining_tries }) = self.table.get(&net) {
                Some(*remaining_tries)
            } else {
                None
            };
        remaining_tries
    }

    fn set_waiting(&mut self, net: P, remaining_tries: usize) {
        self.table.insert(net, ArpValue::Waiting { remaining_tries });
    }

    fn lookup(&self, addr: P) -> Option<&H> {
        match self.table.get(&addr) {
            Some(ArpValue::Known { hardware_addr }) => Some(hardware_addr),
            _ => None,
        }
    }
}

impl<H, P: Hash + Eq> Default for ArpTable<H, P> {
    fn default() -> Self {
        ArpTable { table: HashMap::default() }
    }
}

#[cfg(test)]
mod tests {
    use packet::ParseBuffer;

    use super::*;
    use crate::device::ethernet::{set_ip_addr_subnet, EtherType, Mac};
    use crate::ip::{AddrSubnet, Ipv4Addr, IPV6_MIN_MTU};
    use crate::testutil::{set_logger_for_test, DummyEventDispatcher, DummyEventDispatcherBuilder};
    use crate::wire::arp::{peek_arp_types, ArpPacketBuilder};
    use crate::wire::ethernet::EthernetFrame;
    use crate::{testutil, Subnet};
    use crate::{DeviceId, StackState};

    const TEST_LOCAL_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_REMOTE_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_LOCAL_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_REMOTE_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);
    const TEST_INVALID_MAC: Mac = Mac::new([0, 0, 0, 0, 0, 0]);

    fn send_arp_packet(
        ctx: &mut Context<DummyEventDispatcher>,
        device_id: u64,
        op: ArpOp,
        sender_ipv4: Ipv4Addr,
        target_ipv4: Ipv4Addr,
        sender_mac: Mac,
        target_mac: Mac,
    ) {
        let mut buf = ArpPacketBuilder::new(op, sender_mac, sender_ipv4, target_mac, target_ipv4)
            .serialize_outer()
            .unwrap();
        let (hw, proto) = peek_arp_types(buf.as_ref()).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);

        receive_arp_packet::<DummyEventDispatcher, Ipv4Addr, EthernetArpDevice, _>(
            ctx, device_id, sender_mac, target_mac, buf,
        );
    }

    // validate if buf is an IPv4 Ethernet frame that encapsulates an ARP
    // packet with the specific op, local_ipv4, remote_ipv4, local_mac and
    // remote_mac.
    fn validate_ipv4_arp_packet(
        mut buf: &[u8],
        op: ArpOp,
        local_ipv4: Ipv4Addr,
        remote_ipv4: Ipv4Addr,
        local_mac: Mac,
        remote_mac: Mac,
    ) -> bool {
        if let Ok(frame) = buf.parse::<EthernetFrame<_>>() {
            if frame.ethertype() == Some(EtherType::Arp)
                && frame.src_mac() == local_mac
                && frame.dst_mac() == remote_mac
            {
                if let Ok((hw, proto)) = peek_arp_types(frame.body()) {
                    if hw == ArpHardwareType::Ethernet && proto == EtherType::Ipv4 {
                        if let Ok(arp) = buf.parse::<ArpPacket<_, Mac, Ipv4Addr>>() {
                            return arp.operation() == op
                                && arp.sender_hardware_address() == local_mac
                                && arp.target_hardware_address() == remote_mac
                                && arp.sender_protocol_address() == local_ipv4
                                && arp.target_protocol_address() == remote_ipv4;
                        }
                    }
                }
            }
        }
        false
    }

    #[test]
    fn test_receive_gratuitous_arp_request() {
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);
        let device_id = dev_id.id();
        set_ip_addr_subnet(&mut ctx, device_id, AddrSubnet::new(TEST_LOCAL_IPV4, 24).unwrap());

        send_arp_packet(
            &mut ctx,
            device_id,
            ArpOp::Request,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_INVALID_MAC,
        );

        assert_eq!(
            *EthernetArpDevice::get_arp_state(&mut ctx, device_id)
                .table
                .lookup(TEST_REMOTE_IPV4)
                .unwrap(),
            TEST_REMOTE_MAC
        );

        // Gratuitous ARPs should not send a response.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 0);
    }

    #[test]
    fn test_receive_gratuitous_arp_response() {
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);
        let device_id = dev_id.id();
        set_ip_addr_subnet(&mut ctx, device_id, AddrSubnet::new(TEST_LOCAL_IPV4, 24).unwrap());

        send_arp_packet(
            &mut ctx,
            device_id,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_IPV4,
            TEST_REMOTE_MAC,
            TEST_REMOTE_MAC,
        );

        assert_eq!(
            *EthernetArpDevice::get_arp_state(&mut ctx, device_id)
                .table
                .lookup(TEST_REMOTE_IPV4)
                .unwrap(),
            TEST_REMOTE_MAC
        );

        // Gratuitous ARPs should not send a response.
        assert_eq!(ctx.dispatcher.frames_sent().len(), 0);
    }

    #[test]
    fn test_send_arp_request_on_cache_miss() {
        const NUM_ARP_REQUESTS: usize = DEFAULT_ARP_REQUEST_MAX_TRIES;
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);
        let device_id = dev_id.id();

        set_ip_addr_subnet(&mut ctx, device_id, AddrSubnet::new(TEST_LOCAL_IPV4, 24).unwrap());

        lookup::<DummyEventDispatcher, Ipv4Addr, EthernetArpDevice>(
            &mut ctx,
            device_id,
            TEST_LOCAL_MAC,
            TEST_REMOTE_IPV4,
        );

        let arp_timer_id = ArpTimerId::new_ipv4_timer_id(device_id, TEST_REMOTE_IPV4);

        // Check that we send the original packet, then resend a few times if
        // we don't receive a response.
        let mut cur_frame_num: usize = 0;
        let mut arp_request_num = 0;
        for _ in 0..NUM_ARP_REQUESTS {
            for frame_num in cur_frame_num..ctx.dispatcher.frames_sent().len() {
                let mut buf = &ctx.dispatcher.frames_sent()[frame_num].1[..];
                if validate_ipv4_arp_packet(
                    buf,
                    ArpOp::Request,
                    TEST_LOCAL_IPV4,
                    TEST_REMOTE_IPV4,
                    TEST_LOCAL_MAC,
                    EthernetArpDevice::BROADCAST,
                ) {
                    cur_frame_num = frame_num + 1;
                    arp_request_num = arp_request_num + 1;
                    testutil::trigger_timers_until(&mut ctx, |id| id == &arp_timer_id);
                    break;
                }
            }
        }
        assert_eq!(arp_request_num, NUM_ARP_REQUESTS);

        send_arp_packet(
            &mut ctx,
            device_id,
            ArpOp::Response,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_MAC,
            TEST_LOCAL_MAC,
        );

        // Once an arp response is received, the arp timer event will be cancelled.
        assert!(!ctx.dispatcher.timer_events().any(|(t, id)| id == &arp_timer_id))
    }

    #[test]
    fn test_exhaust_retries_arp_request() {
        //To fully test max retries of APR request, NUM_ARP_REQUEST should be
        // set >= DEFAULT_ARP_REQUEST_MAX_TRIES.
        const NUM_ARP_REQUESTS: usize = DEFAULT_ARP_REQUEST_MAX_TRIES + 1;
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);
        let device_id = dev_id.id();

        set_ip_addr_subnet(&mut ctx, device_id, AddrSubnet::new(TEST_LOCAL_IPV4, 24).unwrap());

        lookup::<DummyEventDispatcher, Ipv4Addr, EthernetArpDevice>(
            &mut ctx,
            device_id,
            TEST_LOCAL_MAC,
            TEST_REMOTE_IPV4,
        );

        let arp_timer_id = ArpTimerId::new_ipv4_timer_id(device_id, TEST_REMOTE_IPV4);

        // The above loopup sent one arp request already.
        let mut num_requests_sent: usize = 1;

        let mut cur_frame_num: usize = 0;
        let mut arp_request_num = 0;

        for _ in 0..NUM_ARP_REQUESTS {
            // Check that ARP request timer event is cancelled after the number
            // of DEFAULT_ARP_REQUEST_MAX_TRIES ARP requests are sent.
            if num_requests_sent < DEFAULT_ARP_REQUEST_MAX_TRIES {
                assert_eq!(
                    EthernetArpDevice::get_arp_state(&mut ctx, device_id)
                        .table
                        .get_remaining_tries(TEST_REMOTE_IPV4),
                    Some(DEFAULT_ARP_REQUEST_MAX_TRIES - num_requests_sent)
                );
                assert!(ctx.dispatcher.timer_events().any(|(t, id)| id == &arp_timer_id));
            } else {
                assert_eq!(
                    EthernetArpDevice::get_arp_state(&mut ctx, device_id)
                        .table
                        .get_remaining_tries(TEST_REMOTE_IPV4),
                    None
                );
                assert!(!ctx.dispatcher.timer_events().any(|(t, id)| id == &arp_timer_id));
            }

            for frame_num in cur_frame_num..ctx.dispatcher.frames_sent().len() {
                let mut buf = &ctx.dispatcher.frames_sent()[frame_num].1[..];
                if validate_ipv4_arp_packet(
                    buf,
                    ArpOp::Request,
                    TEST_LOCAL_IPV4,
                    TEST_REMOTE_IPV4,
                    TEST_LOCAL_MAC,
                    EthernetArpDevice::BROADCAST,
                ) {
                    cur_frame_num = frame_num + 1;
                    arp_request_num = arp_request_num + 1;

                    testutil::trigger_timers_until(&mut ctx, |id| id == &arp_timer_id);
                    num_requests_sent += 1;
                    break;
                }
            }
        }
    }

    #[test]
    fn test_handle_arp_request() {
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);
        let device_id = dev_id.id();
        set_ip_addr_subnet(&mut ctx, device_id, AddrSubnet::new(TEST_LOCAL_IPV4, 24).unwrap());

        send_arp_packet(
            &mut ctx,
            device_id,
            ArpOp::Request,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_MAC,
            TEST_LOCAL_MAC,
        );

        assert_eq!(
            lookup::<DummyEventDispatcher, Ipv4Addr, EthernetArpDevice>(
                &mut ctx,
                device_id,
                TEST_LOCAL_MAC,
                TEST_REMOTE_IPV4,
            )
            .unwrap(),
            TEST_REMOTE_MAC
        );

        assert_eq!(ctx.dispatcher.frames_sent().len(), 1);

        let mut buf = &ctx.dispatcher.frames_sent()[0].1[..];
        assert!(validate_ipv4_arp_packet(
            buf,
            ArpOp::Response,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            TEST_REMOTE_MAC,
        ));
    }

    #[test]
    fn test_arp_table() {
        let mut t: ArpTable<Mac, Ipv4Addr> = ArpTable::default();
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 1])), None);
        t.insert(Ipv4Addr::new([10, 0, 0, 1]), Mac::new([1, 2, 3, 4, 5, 6]));
        assert_eq!(*t.lookup(Ipv4Addr::new([10, 0, 0, 1])).unwrap(), Mac::new([1, 2, 3, 4, 5, 6]));
        assert_eq!(t.lookup(Ipv4Addr::new([10, 0, 0, 2])), None);
    }

    #[test]
    fn test_address_resolution() {
        use crate::device::ethernet::EthernetArpDevice;
        use crate::ip::{send_ip_packet_from_device, IpProto, Ipv4};
        use crate::wire::icmp::{IcmpEchoRequest, IcmpPacketBuilder, IcmpUnusedCode};
        use packet::{serialize::BufferSerializer, Buf};

        set_logger_for_test();

        // We set up two contexts (local and remote) and add them to a
        // DummyNetwork. We are not using the DUMMY_CONFIG_V4 configuration here
        // because we *don't want* the ARP table to be pre-populated by the
        // builder. The DummyNetwork is set up so that all frames from local are
        // forwarded to remote and vice-versa.
        let local_ip = Ipv4Addr::new([10, 0, 0, 1]);
        let remote_ip = Ipv4Addr::new([10, 0, 0, 2]);
        let subnet = Subnet::new(Ipv4Addr::new([10, 0, 0, 0]), 24).unwrap();

        let mut local = DummyEventDispatcherBuilder::default();
        local.add_device_with_ip(TEST_LOCAL_MAC, local_ip, subnet);
        local.add_device_route(subnet, 0);

        let mut remote = DummyEventDispatcherBuilder::default();
        remote.add_device_with_ip(TEST_REMOTE_MAC, remote_ip, subnet);
        remote.add_device_route(subnet, 0);

        let device_id = DeviceId::new_ethernet(1);
        let mut net = testutil::DummyNetwork::new(
            vec![("local", local.build()), ("remote", remote.build())].into_iter(),
            |ctx, device| {
                if *ctx == "local" {
                    ("remote", device_id, None)
                } else {
                    ("local", device_id, None)
                }
            },
        );

        // let's try to ping the remote device from the local device:
        let req = IcmpEchoRequest::new(0, 0);
        let req_body = &[1, 2, 3, 4];
        let body = BufferSerializer::new_vec(Buf::new(req_body.to_vec(), ..)).encapsulate(
            IcmpPacketBuilder::<Ipv4, &[u8], _>::new(local_ip, remote_ip, IcmpUnusedCode, req),
        );
        send_ip_packet_from_device(
            net.context("local"),
            device_id,
            local_ip,
            remote_ip,
            remote_ip,
            IpProto::Icmp,
            body,
        );
        // this should've triggered an ARP request to come out of local
        assert_eq!(net.context("local").dispatcher.frames_sent().len(), 1);
        // and a timer should've been started.
        assert_eq!(net.context("local").dispatcher.timer_events().count(), 1);

        net.step();

        assert_eq!(
            *net.context("remote").state().test_counters.get("arp::rx_request"),
            1,
            "remote received arp request"
        );
        assert_eq!(net.context("remote").dispatcher.frames_sent().len(), 1);

        net.step();

        assert_eq!(
            *net.context("local").state().test_counters.get("arp::rx_resolve"),
            1,
            "local received arp response"
        );

        // at the end of the exchange, both sides should have each other on
        // their arp tables:
        assert_eq!(
            *EthernetArpDevice::get_arp_state::<_>(net.context("local"), device_id.id())
                .table
                .lookup(remote_ip)
                .unwrap(),
            TEST_REMOTE_MAC
        );
        assert_eq!(
            *EthernetArpDevice::get_arp_state::<_>(net.context("remote"), device_id.id())
                .table
                .lookup(local_ip)
                .unwrap(),
            TEST_LOCAL_MAC
        );
        // and the local timer should've been unscheduled:
        assert_eq!(net.context("local").dispatcher.timer_events().count(), 0);

        // upon link layer resolution, the original ping request should've been
        // sent out:
        net.step();
        assert_eq!(
            *net.context("remote").state().test_counters.get("receive_icmp_packet::echo_request"),
            1
        );

        // and the response should come back:
        net.step();
        assert_eq!(
            *net.context("local").state().test_counters.get("receive_icmp_packet::echo_reply"),
            1
        );
    }
}
