// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

use std::fmt::{self, Debug, Display, Formatter};

use log::debug;
use packet::{Buf, MtuError, ParseBuffer, Serializer};
use specialize_ip_macro::specialize_ip_address;
use zerocopy::{AsBytes, FromBytes, Unaligned};

use crate::device::arp::{ArpDevice, ArpHardwareType, ArpState};
use crate::device::{ndp, ndp::NdpState};
use crate::device::{DeviceId, FrameDestination};
use crate::ip::{AddrSubnet, Ip, IpAddr, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use crate::wire::arp::peek_arp_types;
use crate::wire::ethernet::{EthernetFrame, EthernetFrameBuilder};
use crate::{Context, EventDispatcher};
use std::collections::{HashMap, VecDeque};

/// A media access control (MAC) address.
#[derive(Copy, Clone, Eq, PartialEq, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(transparent)]
pub struct Mac([u8; 6]);

const ETHERNET_MAX_PENDING_FRAMES: usize = 10;

impl Mac {
    /// The broadcast MAC address.
    ///
    /// The broadcast MAC address, FF:FF:FF:FF:FF:FF, indicates that a frame should
    /// be received by all receivers regardless of their local MAC address.
    pub(crate) const BROADCAST: Mac = Mac([0xFF; 6]);

    const EUI_MAGIC: [u8; 2] = [0xff, 0xfe];

    /// Construct a new MAC address.
    pub const fn new(bytes: [u8; 6]) -> Mac {
        Mac(bytes)
    }

    /// Get the bytes of the MAC address.
    pub(crate) fn bytes(self) -> [u8; 6] {
        self.0
    }

    /// Return the RFC4291 EUI-64 interface identifier for this MAC address.
    ///
    /// `eui_magic` is the two bytes that are inserted between the MAC address
    /// to form the identifier. If None, the standard 0xfffe will be used.
    ///
    // TODO: remove `eui_magic` arg if/once it is unused.
    pub(crate) fn to_eui64(self, eui_magic: Option<[u8; 2]>) -> [u8; 8] {
        let mut eui = [0; 8];
        eui[0..3].copy_from_slice(&self.0[0..3]);
        eui[3..5].copy_from_slice(&eui_magic.unwrap_or(Self::EUI_MAGIC));
        eui[5..8].copy_from_slice(&self.0[3..6]);
        eui[0] ^= 0b0000_0010;
        eui
    }

    /// Return the link-local IPv6 address for this MAC address, as per RFC 4862.
    ///
    /// `eui_magic` is the two bytes that are inserted between the MAC address
    /// to form the identifier. If None, the standard 0xfffe will be used.
    ///
    /// TODO: remove `eui_magic` arg if/once it is unused.
    pub(crate) fn to_ipv6_link_local(self, eui_magic: Option<[u8; 2]>) -> Ipv6Addr {
        let mut ipv6_addr = [0; 16];
        ipv6_addr[0..2].copy_from_slice(&[0xfe, 0x80]);
        ipv6_addr[8..16].copy_from_slice(&self.to_eui64(eui_magic));
        Ipv6Addr::new(ipv6_addr)
    }

    /// Is this a unicast MAC address?
    ///
    /// Returns true if the least significant bit of the first byte of the
    /// address is 0.
    pub(crate) fn is_unicast(self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self.0[0] & 1 == 0
    }

    /// Is this a multicast MAC address?
    ///
    /// Returns true if the least significant bit of the first byte of the
    /// address is 1.
    pub(crate) fn is_multicast(self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self.0[0] & 1 == 1
    }

    /// Is this the broadcast MAC address?
    ///
    /// Returns true if this is the broadcast MAC address, FF:FF:FF:FF:FF:FF.
    pub(crate) fn is_broadcast(self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self == Mac::BROADCAST
    }
}

impl ndp::LinkLayerAddress for Mac {
    const BYTES_LENGTH: usize = 6;

    fn bytes(&self) -> &[u8] {
        &self.0
    }

    fn from_bytes(bytes: &[u8]) -> Self {
        // assert that contract is being held:
        debug_assert!(bytes.len() == Self::BYTES_LENGTH);
        let mut b = [0; Self::BYTES_LENGTH];
        b.copy_from_slice(bytes);
        Self::new(b)
    }
}

/// An EtherType number.
#[allow(missing_docs)]
#[derive(Copy, Clone, Hash, Eq, PartialEq)]
pub(crate) enum EtherType {
    Ipv4,
    Arp,
    Ipv6,
    Other(u16),
}

impl EtherType {
    const IPV4: u16 = 0x0800;
    const ARP: u16 = 0x0806;
    const IPV6: u16 = 0x86DD;
}

impl From<u16> for EtherType {
    fn from(u: u16) -> EtherType {
        match u {
            Self::IPV4 => EtherType::Ipv4,
            Self::ARP => EtherType::Arp,
            Self::IPV6 => EtherType::Ipv6,
            u => EtherType::Other(u),
        }
    }
}

impl Into<u16> for EtherType {
    fn into(self) -> u16 {
        match self {
            EtherType::Ipv4 => Self::IPV4,
            EtherType::Arp => Self::ARP,
            EtherType::Ipv6 => Self::IPV6,
            EtherType::Other(u) => u,
        }
    }
}

impl Display for EtherType {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(
            f,
            "{}",
            match self {
                EtherType::Ipv4 => "IPv4",
                EtherType::Arp => "ARP",
                EtherType::Ipv6 => "IPv6",
                EtherType::Other(u) => return write!(f, "EtherType {}", u),
            }
        )
    }
}

impl Debug for EtherType {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        Display::fmt(self, f)
    }
}

/// The state associated with an Ethernet device.
pub(crate) struct EthernetDeviceState {
    mac: Mac,
    mtu: u32,
    ipv4_addr_sub: Option<AddrSubnet<Ipv4Addr>>,
    ipv6_addr_sub: Option<AddrSubnet<Ipv6Addr>>,
    ipv4_arp: ArpState<Ipv4Addr, EthernetArpDevice>,
    ndp: ndp::NdpState<EthernetNdpDevice>,
    // pending_frames stores a list of serialized frames indexed by their
    // desintation IP addresses. The frames contain an entire EthernetFrame
    // body and the MTU check is performed before queueing them here.
    pending_frames: HashMap<IpAddr, VecDeque<Vec<u8>>>,
}

impl EthernetDeviceState {
    /// Construct a new `EthernetDeviceState`.
    ///
    /// `new` constructs a new `EthernetDeviceState` with the given MAC address
    /// and MTU. The MTU will be taken as a limit on the size of Ethernet
    /// payloads - the Ethernet header is not counted towards the MTU.
    pub(crate) fn new(mac: Mac, mtu: u32) -> EthernetDeviceState {
        // TODO(joshlf): Add a minimum MTU for all Ethernet devices such that
        //  you cannot create an `EthernetDeviceState` with an MTU smaller than
        //  the minimum. The absolute minimum needs to be at least the minimum
        //  body size of an Ethernet frame. For IPv6-capable devices, the
        //  minimum needs to be higher - the IPv6 minimum MTU. The easy path is
        //  to simply use the IPv6 minimum MTU as the minimum in all cases,
        //  although we may at some point want to figure out how to configure
        //  devices which don't support IPv6, and allow smaller MTUs for those
        //  devices.
        //  A few questions:
        //  - How do we wire error information back up the call stack? Should
        //  this just return a Result or something?

        EthernetDeviceState {
            mac,
            mtu,
            ipv4_addr_sub: None,
            ipv6_addr_sub: None,
            ipv4_arp: ArpState::default(),
            ndp: NdpState::default(),
            pending_frames: HashMap::new(),
        }
    }

    /// Adds a pending frame `frame` associated with `local_addr` to the list
    /// of pending frames in the current device state.
    ///
    /// If an older frame had to be dropped because it exceeds the maximum
    /// allowed number of pending frames, it is returned.
    fn add_pending_frame(&mut self, local_addr: IpAddr, frame: Vec<u8>) -> Option<Vec<u8>> {
        let buff = self.pending_frames.entry(local_addr).or_insert_with(Default::default);
        buff.push_back(frame);
        if buff.len() > ETHERNET_MAX_PENDING_FRAMES {
            buff.pop_front()
        } else {
            None
        }
    }

    /// Takes all pending frames associated with address `local_addr`.
    fn take_pending_frames(&mut self, local_addr: IpAddr) -> Option<impl Iterator<Item = Vec<u8>>> {
        match self.pending_frames.remove(&local_addr) {
            Some(mut buff) => Some(buff.into_iter()),
            None => None,
        }
    }
}

/// An extension trait adding IP-related functionality to `Ipv4` and `Ipv6`.
pub(crate) trait EthernetIpExt: Ip {
    const ETHER_TYPE: EtherType;
}

impl<I: Ip> EthernetIpExt for I {
    default const ETHER_TYPE: EtherType = EtherType::Ipv4;
}

impl EthernetIpExt for Ipv4 {
    const ETHER_TYPE: EtherType = EtherType::Ipv4;
}

impl EthernetIpExt for Ipv6 {
    const ETHER_TYPE: EtherType = EtherType::Ipv6;
}

/// Send an IP packet in an Ethernet frame.
///
/// `send_ip_frame` accepts a device ID, a local IP address, and a
/// `SerializationRequest`. It computes the routing information and serializes
/// the request in a new Ethernet frame and sends it.
#[specialize_ip_address]
pub(crate) fn send_ip_frame<D: EventDispatcher, A: IpAddress, S: Serializer>(
    ctx: &mut Context<D>,
    device_id: u64,
    local_addr: A,
    body: S,
) -> Result<(), (MtuError<S::InnerError>, S)> {
    let state = get_device_state(ctx, device_id);
    let (local_mac, mtu) = (state.mac, state.mtu);

    #[ipv4addr]
    let dst_mac = {
        if let Some(dst_mac) = crate::device::arp::lookup::<_, _, EthernetArpDevice>(
            ctx, device_id, local_mac, local_addr,
        ) {
            Ok(dst_mac)
        } else {
            Err(IpAddr::V4(local_addr))
        }
    };

    #[ipv6addr]
    let dst_mac = {
        if let Some(dst_mac) = ndp::lookup::<_, EthernetNdpDevice>(ctx, device_id, local_addr) {
            Ok(dst_mac)
        } else {
            Err(IpAddr::V6(local_addr))
        }
    };

    match dst_mac {
        Ok(dst_mac) => {
            let buffer = body
                .with_mtu(mtu as usize)
                .encapsulate(EthernetFrameBuilder::new(local_mac, dst_mac, A::Version::ETHER_TYPE))
                .serialize_outer()
                .map_err(|(err, ser)| (err, ser.into_serializer().into_serializer()))?;
            ctx.dispatcher().send_frame(DeviceId::new_ethernet(device_id), buffer.as_ref());
        }
        Err(local_addr) => {
            let state = get_device_state(ctx, device_id);
            let dropped = state.add_pending_frame(
                local_addr,
                body.with_mtu(mtu as usize)
                    .serialize_outer()
                    .map_err(|(err, ser)| (err, ser.into_serializer()))?
                    .as_ref()
                    .to_vec(),
            );
            if let Some(dropped) = dropped {
                // TODO(brunodalbo): Is it ok to silently just let this drop? Or
                //  should the IP layer be notified in any way?
                log_unimplemented!((), "Ethernet dropped frame because ran out of allowable space");
            }
        }
    }

    Ok(())
}

/// Receive an Ethernet frame from the network.
pub(crate) fn receive_frame<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: u64,
    bytes: &mut [u8],
) {
    let mut buffer = Buf::new(bytes, ..);
    let frame = if let Ok(frame) = buffer.parse::<EthernetFrame<_>>() {
        frame
    } else {
        // TODO(joshlf): Do something else?
        return;
    };

    let (src, dst) = (frame.src_mac(), frame.dst_mac());
    let device = DeviceId::new_ethernet(device_id);
    let frame_dst = if dst == get_device_state(ctx, device_id).mac {
        FrameDestination::Unicast
    } else if dst.is_broadcast() {
        FrameDestination::Broadcast
    } else {
        return;
    };

    match frame.ethertype() {
        Some(EtherType::Arp) => {
            let types = if let Ok(types) = peek_arp_types(buffer.as_ref()) {
                types
            } else {
                // TODO(joshlf): Do something else here?
                return;
            };
            match types {
                (ArpHardwareType::Ethernet, EtherType::Ipv4) => {
                    crate::device::arp::receive_arp_packet::<D, Ipv4Addr, EthernetArpDevice, _>(
                        ctx, device_id, src, dst, buffer,
                    )
                }
                types => debug!("got ARP packet for unsupported types: {:?}", types),
            }
        }
        Some(EtherType::Ipv4) => {
            crate::ip::receive_ip_packet::<D, _, Ipv4>(ctx, device, frame_dst, buffer)
        }
        Some(EtherType::Ipv6) => {
            crate::ip::receive_ip_packet::<D, _, Ipv6>(ctx, device, frame_dst, buffer)
        }
        Some(EtherType::Other(_)) | None => {} // TODO(joshlf)
    }
}

/// Get the IP address and subnet associated with this device.
#[specialize_ip_address]
pub(crate) fn get_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: u64,
) -> Option<AddrSubnet<A>> {
    #[ipv4addr]
    return get_device_state(ctx, device_id).ipv4_addr_sub;
    #[ipv6addr]
    return get_device_state(ctx, device_id).ipv6_addr_sub;
}

/// Get the IPv6 link-local address associated with this device.
///
/// The IPv6 link-local address returned is constructed from this device's MAC
/// address.
pub(crate) fn get_ipv6_link_local_addr<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: u64,
) -> Ipv6Addr {
    // TODO(brunodalbo) the link local address is subject to the same collision
    //  verifications as prefix global addresses, we should keep a state machine
    //  about that check and cache the adopted address. For now, we just compose
    //  the link-local from the ethernet MAC.
    get_device_state(ctx, device_id).mac.to_ipv6_link_local(None)
}

/// Set the IP address and subnet associated with this device.
#[specialize_ip_address]
pub(crate) fn set_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Context<D>,
    device_id: u64,
    addr_sub: AddrSubnet<A>,
) {
    #[ipv4addr]
    get_device_state(ctx, device_id).ipv4_addr_sub = Some(addr_sub);
    #[ipv6addr]
    get_device_state(ctx, device_id).ipv6_addr_sub = Some(addr_sub);
}

/// Get the MTU associated with this device.
pub(crate) fn get_mtu<D: EventDispatcher>(ctx: &mut Context<D>, device_id: u64) -> u32 {
    get_device_state(ctx, device_id).mtu
}

/// Insert an entry into this device's ARP table.
pub(crate) fn insert_arp_table_entry<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: u64,
    addr: Ipv4Addr,
    mac: Mac,
) {
    crate::device::arp::insert::<D, Ipv4Addr, EthernetArpDevice>(ctx, device_id, addr, mac);
}

/// Insert an entry into this device's NDP table.
pub(crate) fn insert_ndp_table_entry<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: u64,
    addr: Ipv6Addr,
    mac: Mac,
) {
    ndp::insert_neighbor::<D, EthernetNdpDevice>(ctx, device_id, addr, mac)
}

fn get_device_state<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: u64,
) -> &mut EthernetDeviceState {
    // TODO(joshlf): Sometimes we want lookups to be infallible (if we know that
    // the device exists), but sometimes we want to report an error to the user.
    // Right now, this is a DoS vector.
    ctx.state_mut()
        .device
        .ethernet
        .get_mut(&device_id)
        .unwrap_or_else(|| panic!("no such Ethernet device: {}", device_id))
}

/// Dummy type used to implement ArpDevice.
pub(super) struct EthernetArpDevice;

impl ArpDevice<Ipv4Addr> for EthernetArpDevice {
    type HardwareAddr = Mac;
    const BROADCAST: Mac = Mac::BROADCAST;

    fn send_arp_frame<D: EventDispatcher, S: Serializer>(
        ctx: &mut Context<D>,
        device_id: u64,
        dst: Self::HardwareAddr,
        body: S,
    ) -> Result<(), MtuError<S::InnerError>> {
        let src = get_device_state(ctx, device_id).mac;
        let buffer = body
            .encapsulate(EthernetFrameBuilder::new(src, dst, EtherType::Arp))
            .serialize_outer()
            .map_err(|(err, _)| err)?;
        ctx.dispatcher().send_frame(DeviceId::new_ethernet(device_id), buffer.as_ref());
        Ok(())
    }

    fn get_arp_state<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> &mut ArpState<Ipv4Addr, Self> {
        &mut get_device_state(ctx, device_id).ipv4_arp
    }

    fn get_protocol_addr<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> Option<Ipv4Addr> {
        get_device_state(ctx, device_id).ipv4_addr_sub.map(AddrSubnet::into_addr)
    }

    fn get_hardware_addr<D: EventDispatcher>(ctx: &mut Context<D>, device_id: u64) -> Mac {
        get_device_state(ctx, device_id).mac
    }

    fn address_resolved<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        proto_addr: Ipv4Addr,
        hw_addr: Mac,
    ) {
        mac_resolved(ctx, device_id, IpAddr::V4(proto_addr), hw_addr);
    }

    fn address_resolution_failed<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        proto_addr: Ipv4Addr,
    ) {
        mac_resolution_failed(ctx, device_id, IpAddr::V4(proto_addr));
    }
}

/// Dummy type used to implement NdpDevice
pub(crate) struct EthernetNdpDevice;

impl ndp::NdpDevice for EthernetNdpDevice {
    type LinkAddress = Mac;
    const BROADCAST: Mac = Mac::BROADCAST;

    fn get_ndp_state<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> &mut ndp::NdpState<Self> {
        &mut get_device_state(ctx, device_id).ndp
    }

    fn get_link_layer_addr<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
    ) -> Self::LinkAddress {
        get_device_state(ctx, device_id).mac
    }

    fn get_ipv6_addr<D: EventDispatcher>(ctx: &mut Context<D>, device_id: u64) -> Option<Ipv6Addr> {
        let state = get_device_state(ctx, device_id);
        // TODO(brunodalbo) just returning either the configured or EUI
        //  link_local address for now, we need a better structure to keep
        //  a list of IPv6 addresses.
        match state.ipv6_addr_sub {
            Some(addr_sub) => Some(addr_sub.into_addr()),
            None => Some(state.mac.to_ipv6_link_local(None)),
        }
    }

    fn has_ipv6_addr<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        address: &Ipv6Addr,
    ) -> bool {
        let state = get_device_state(ctx, device_id);
        state.ipv6_addr_sub.map_or(false, |addr_sub| addr_sub.into_addr() == *address)
            || state.mac.to_ipv6_link_local(None) == *address
    }

    fn send_ipv6_frame_to<D: EventDispatcher, S: Serializer>(
        ctx: &mut Context<D>,
        device_id: u64,
        dst: Mac,
        body: S,
    ) -> Result<(), MtuError<S::InnerError>> {
        let src = get_device_state(ctx, device_id).mac;
        let buffer = body
            .encapsulate(EthernetFrameBuilder::new(src, dst, EtherType::Ipv6))
            .serialize_outer()
            .map_err(|(err, _)| err)?;
        ctx.dispatcher().send_frame(DeviceId::new_ethernet(device_id), buffer.as_ref());
        Ok(())
    }

    fn send_ipv6_frame<D: EventDispatcher, S: Serializer>(
        ctx: &mut Context<D>,
        device_id: u64,
        next_hop: Ipv6Addr,
        body: S,
    ) -> Result<(), MtuError<S::InnerError>> {
        send_ip_frame(ctx, device_id, next_hop, body).map_err(|e| e.0)
    }

    fn get_device_id(id: u64) -> DeviceId {
        DeviceId::new_ethernet(id)
    }

    fn address_resolved<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        address: &Ipv6Addr,
        link_address: Self::LinkAddress,
    ) {
        mac_resolved(ctx, device_id, IpAddr::V6(*address), link_address);
    }

    fn address_resolution_failed<D: EventDispatcher>(
        ctx: &mut Context<D>,
        device_id: u64,
        address: &Ipv6Addr,
    ) {
        mac_resolution_failed(ctx, device_id, IpAddr::V6(*address));
    }
}

/// Sends out any pending frames that are waiting for link layer address
/// resolution.
///
/// `mac_resolved` is the common logic used when a link layer address is
/// resolved either by ARP or NDP.
fn mac_resolved<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: u64,
    address: IpAddr,
    dst_mac: Mac,
) {
    let state = get_device_state(ctx, device_id);
    let device_id = DeviceId::new_ethernet(device_id);
    let src_mac = state.mac;
    let ether_type = match &address {
        IpAddr::V4(_) => EtherType::Ipv4,
        IpAddr::V6(_) => EtherType::Ipv6,
    };
    if let Some(pending) = state.take_pending_frames(address) {
        for frame in pending {
            // NOTE(brunodalbo): We already performed MTU checking when we
            //  saved the buffer waiting for address resolution. It should
            //  be noted that the MTU check back then didn't account for
            //  ethernet frame padding required by EthernetFrameBuilder,
            //  but that's fine (as it stands right now) because the MTU
            //  is guaranteed to be larger than an Ethernet minimum frame
            //  body size.
            let serialized = frame
                .encapsulate(EthernetFrameBuilder::new(src_mac, dst_mac, ether_type))
                .serialize_outer()
                .map_err(|(err, _)| err);

            match serialized {
                Ok(buffer) => ctx.dispatcher().send_frame(device_id, buffer.as_ref()),
                Err(e) => debug!("Failed to serialize pending frame {:?}", e),
            }
        }
    }
}

/// Clears out any pending frames that are waiting for link layer address
/// resolution.
///
/// `mac_resolution_failed` is the common logic used when a link layer address
/// fails to resolve either by ARP or NDP.
fn mac_resolution_failed<D: EventDispatcher>(
    ctx: &mut Context<D>,
    device_id: u64,
    address: IpAddr,
) {
    // TODO(brunodalbo) what do we do here in regards to the pending frames?
    //  NDP's RFC explicitly states unreachable ICMP messages must be generated:
    //  "If no Neighbor Advertisement is received after MAX_MULTICAST_SOLICIT
    //  solicitations, address resolution has failed. The sender MUST return
    //  ICMP destination unreachable indications with code 3
    //  (Address Unreachable) for each packet queued awaiting address
    //  resolution."
    //  For ARP, we don't have such a clear statement on the RFC, it would make
    //  sense to do the same thing though.
    let state = get_device_state(ctx, device_id);
    if let Some(pending) = state.take_pending_frames(address) {
        log_unimplemented!((), "ethernet mac resolution failed not implemented");
    }
}

#[cfg(test)]
mod tests {
    use packet::{Buf, BufferSerializer};

    use super::*;
    use crate::testutil::{DummyEventDispatcher, DummyEventDispatcherBuilder, DUMMY_CONFIG_V4};

    #[test]
    fn test_mac_to_eui() {
        assert_eq!(
            Mac::new([0x00, 0x1a, 0xaa, 0x12, 0x34, 0x56]).to_eui64(None),
            [0x02, 0x1a, 0xaa, 0xff, 0xfe, 0x12, 0x34, 0x56]
        );
        assert_eq!(
            Mac::new([0x00, 0x1a, 0xaa, 0x12, 0x34, 0x56]).to_eui64(Some([0xfe, 0xfe])),
            [0x02, 0x1a, 0xaa, 0xfe, 0xfe, 0x12, 0x34, 0x56]
        );
    }

    #[test]
    fn test_slaac() {
        assert_eq!(
            Mac::new([0x00, 0x1a, 0xaa, 0x12, 0x34, 0x56]).to_ipv6_link_local(None),
            Ipv6Addr::new([
                0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x1a, 0xaa, 0xff, 0xfe, 0x12, 0x34, 0x56
            ])
        );
        assert_eq!(
            Mac::new([0x00, 0x1a, 0xaa, 0x12, 0x34, 0x56]).to_ipv6_link_local(Some([0xfe, 0xfe])),
            Ipv6Addr::new([
                0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x02, 0x1a, 0xaa, 0xfe, 0xfe, 0x12, 0x34, 0x56
            ])
        );
    }

    #[test]
    fn test_mtu() {
        // Test that we send an Ethernet frame whose size is less than the MTU,
        // and that we don't send an Ethernet frame whose size is greater than
        // the MTU.
        fn test(size: usize, expect_frames_sent: usize) {
            let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V4)
                .build::<DummyEventDispatcher>();
            send_ip_frame(
                &mut ctx,
                1,
                DUMMY_CONFIG_V4.remote_ip,
                BufferSerializer::new_vec(Buf::new(&mut vec![0; size], ..)),
            );
            assert_eq!(ctx.dispatcher().frames_sent().len(), expect_frames_sent);
        }

        // The Ethernet device MTU currently defaults to IPV6_MIN_MTU.
        test(crate::ip::IPV6_MIN_MTU as usize, 1);
        test(crate::ip::IPV6_MIN_MTU as usize + 1, 0);
    }

    #[test]
    fn test_pending_frames() {
        let mut state =
            EthernetDeviceState::new(DUMMY_CONFIG_V4.local_mac, crate::ip::IPV6_MIN_MTU);
        let ip = IpAddr::V4(DUMMY_CONFIG_V4.local_ip);
        state.add_pending_frame(ip, vec![1]);
        state.add_pending_frame(ip, vec![2]);
        state.add_pending_frame(ip, vec![3]);

        // check that we're accumulating correctly...
        assert_eq!(3, state.take_pending_frames(ip).unwrap().count());
        // ...and that take_pending_frames clears all the buffered data.
        assert!(state.take_pending_frames(ip).is_none());

        for i in 0..ETHERNET_MAX_PENDING_FRAMES {
            assert!(state.add_pending_frame(ip, vec![i as u8]).is_none());
        }
        // check that adding more than capacity will drop the older buffers as
        // a proper FIFO queue.
        assert_eq!(0, state.add_pending_frame(ip, vec![255]).unwrap()[0]);
        assert_eq!(1, state.add_pending_frame(ip, vec![255]).unwrap()[0]);
        assert_eq!(2, state.add_pending_frame(ip, vec![255]).unwrap()[0]);
    }
}
