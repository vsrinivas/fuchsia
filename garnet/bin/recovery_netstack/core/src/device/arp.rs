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

    fn get_hardware_addr<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> Self::HardwareAddr;
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
    // TODO(wesleyac) Add support for gratuitous ARP and probe/announce.
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
    }
    if addressed_to_me && packet.operation() == ArpOp::Request {
        let self_hw_addr = AD::get_hardware_addr(ctx, device_id);
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

fn send_arp_request<D: EventDispatcher, P: PType + Eq + Hash, AD: ArpDevice<P>>(
    ctx: &mut Context<D>,
    device_id: u64,
    lookup_addr: P,
) {
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

        // TODO(wesleyac): Configurable timeout.
        // Currently at 20 seconds because that's what FreeBSD does.
        // TODO(wesleyac): maxretries
        ctx.dispatcher.schedule_timeout(
            Duration::from_secs(20),
            ArpTimerId::new_ipv4_timer_id(device_id, lookup_addr.addr()),
        );

        AD::get_arp_state(ctx, device_id).table.set_waiting(lookup_addr);
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
    Known(H),
    Waiting,
}

impl<H, P: Hash + Eq> ArpTable<H, P> {
    fn insert(&mut self, net: P, hw: H) {
        self.table.insert(net, ArpValue::Known(hw));
    }

    fn set_waiting(&mut self, net: P) {
        self.table.insert(net, ArpValue::Waiting);
    }

    fn lookup(&self, addr: P) -> Option<&H> {
        match self.table.get(&addr) {
            Some(ArpValue::Known(x)) => Some(x),
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
    use crate::testutil;
    use crate::testutil::DummyEventDispatcher;
    use crate::wire::arp::{peek_arp_types, ArpPacketBuilder};
    use crate::wire::ethernet::EthernetFrame;
    use crate::StackState;

    const TEST_LOCAL_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_REMOTE_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_LOCAL_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_REMOTE_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);

    fn receive_arp_response(
        ctx: &mut Context<DummyEventDispatcher>,
        device_id: u64,
        local_ipv4: Ipv4Addr,
        remote_ipv4: Ipv4Addr,
        local_mac: Mac,
        remote_mac: Mac,
    ) {
        let mut buf =
            ArpPacketBuilder::new(ArpOp::Request, remote_mac, remote_ipv4, local_mac, local_ipv4)
                .serialize_outer()
                .unwrap();
        let (hw, proto) = peek_arp_types(buf.as_ref()).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);

        receive_arp_packet::<DummyEventDispatcher, Ipv4Addr, EthernetArpDevice, _>(
            ctx, device_id, remote_mac, local_mac, buf,
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
    fn test_send_arp_request_on_cache_miss() {
        const NUM_ARP_REQUESTS: usize = 4;
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);
        set_ip_addr_subnet(&mut ctx, dev_id.id, AddrSubnet::new(TEST_LOCAL_IPV4, 24).unwrap());
        let device_id = dev_id.id();

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
        for _ in 1..NUM_ARP_REQUESTS {
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
        assert_eq!(arp_request_num, NUM_ARP_REQUESTS - 1);

        receive_arp_response(
            &mut ctx,
            device_id,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            TEST_REMOTE_MAC,
        );

        // Once an arp response is received, the arp timer event will be cancelled.
        assert!(!ctx.dispatcher.timer_events().any(|(t, id)| id == &arp_timer_id))
    }

    #[test]
    fn test_handle_arp_request() {
        let mut state = StackState::default();
        let dev_id = state.device.add_ethernet_device(TEST_LOCAL_MAC, IPV6_MIN_MTU);
        let dispatcher = DummyEventDispatcher::default();
        let mut ctx: Context<DummyEventDispatcher> = Context::new(state, dispatcher);
        let device_id = dev_id.id();
        set_ip_addr_subnet(&mut ctx, device_id, AddrSubnet::new(TEST_LOCAL_IPV4, 24).unwrap());

        receive_arp_response(
            &mut ctx,
            device_id,
            TEST_LOCAL_IPV4,
            TEST_REMOTE_IPV4,
            TEST_LOCAL_MAC,
            TEST_REMOTE_MAC,
        );

        assert_eq!(
            lookup::<DummyEventDispatcher, Ipv4Addr, EthernetArpDevice>(
                &mut ctx,
                device_id,
                TEST_LOCAL_MAC,
                TEST_REMOTE_IPV4
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
}
