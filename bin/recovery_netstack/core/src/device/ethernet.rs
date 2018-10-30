// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

use std::fmt::{self, Display, Formatter};

use log::debug;
use zerocopy::{AsBytes, FromBytes, Unaligned};

use crate::device::arp::{ArpDevice, ArpHardwareType, ArpState};
use crate::device::DeviceId;
use crate::ip::{Ip, IpAddr, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Subnet};
use crate::wire::arp::peek_arp_types;
use crate::wire::ethernet::{EthernetFrame, EthernetFrameSerializer};
use crate::wire::{BufferAndRange, SerializationRequest};
use crate::{Context, EventDispatcher};

/// A media access control (MAC) address.
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
#[repr(transparent)]
pub struct Mac([u8; 6]);

unsafe impl FromBytes for Mac {}
unsafe impl AsBytes for Mac {}
unsafe impl Unaligned for Mac {}

impl Mac {
    /// The broadcast MAC address.
    ///
    /// The broadcast MAC address, FF:FF:FF:FF:FF:FF, indicates that a frame should
    /// be received by all receivers regardless of their local MAC address.
    pub const BROADCAST: Mac = Mac([0xFF; 6]);

    /// Construct a new MAC address.
    pub const fn new(bytes: [u8; 6]) -> Mac {
        Mac(bytes)
    }

    /// Get the bytes of the MAC address.
    pub fn bytes(&self) -> [u8; 6] {
        self.0
    }

    /// Is this a unicast MAC address?
    ///
    /// Returns true if the least significant bit of the first byte of the
    /// address is 0.
    pub fn is_unicast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self.0[0] & 1 == 0
    }

    /// Is this a multicast MAC address?
    ///
    /// Returns true if the least significant bit of the first byte of the
    /// address is 1.
    pub fn is_multicast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self.0[0] & 1 == 1
    }

    /// Is this the broadcast MAC address?
    ///
    /// Returns true if this is the broadcast MAC address, FF:FF:FF:FF:FF:FF.
    pub fn is_broadcast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        *self == Mac::BROADCAST
    }
}

/// An EtherType number.
#[allow(missing_docs)]
#[derive(Eq, PartialEq, Debug)]
#[repr(u16)]
pub enum EtherType {
    Ipv4 = EtherType::IPV4,
    Arp = EtherType::ARP,
    Ipv6 = EtherType::IPV6,
}

impl EtherType {
    const IPV4: u16 = 0x0800;
    const ARP: u16 = 0x0806;
    const IPV6: u16 = 0x86DD;

    /// Construct an `EtherType` from a `u16`.
    ///
    /// `from_u16` returns the `EtherType` with the numerical value `u`, or
    /// `None` if the value is unrecognized.
    pub fn from_u16(u: u16) -> Option<EtherType> {
        match u {
            Self::IPV4 => Some(EtherType::Ipv4),
            Self::ARP => Some(EtherType::Arp),
            Self::IPV6 => Some(EtherType::Ipv6),
            _ => None,
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
            }
        )
    }
}

/// The state associated with an Ethernet device.
pub struct EthernetDeviceState {
    mac: Mac,
    ipv4_addr: Option<(Ipv4Addr, Subnet<Ipv4Addr>)>,
    ipv6_addr: Option<(Ipv6Addr, Subnet<Ipv6Addr>)>,
    ipv4_arp: ArpState<Ipv4Addr, EthernetArpDevice>,
}

impl EthernetDeviceState {
    /// Construct a new `EthernetDeviceState`.
    pub fn new(mac: Mac) -> EthernetDeviceState {
        EthernetDeviceState {
            mac,
            ipv4_addr: None,
            ipv6_addr: None,
            ipv4_arp: ArpState::default(),
        }
    }
}

/// An extension trait adding IP-related functionality to `Ipv4` and `Ipv6`.
trait EthernetIpExt: Ip {
    const EtherType: EtherType;
}

impl<I: Ip> EthernetIpExt for I {
    default const EtherType: EtherType = EtherType::Ipv4;
}

impl EthernetIpExt for Ipv4 {
    const EtherType: EtherType = EtherType::Ipv4;
}

impl EthernetIpExt for Ipv6 {
    const EtherType: EtherType = EtherType::Ipv6;
}

/// Send an IP packet in an Ethernet frame.
///
/// `send_ip_frame` accepts a device ID, a local IP address, and a
/// `SerializationRequest`. It computes the routing information and serializes
/// the request in a new Ethernet frame and sends it.
pub fn send_ip_frame<D: EventDispatcher, A, S>(
    ctx: &mut Context<D>, device_id: u64, local_addr: A, body: S,
) where
    A: IpAddr,
    S: SerializationRequest,
{
    specialize_ip_addr!(
        fn lookup_dst_mac<D>(ctx: &mut Context<D>, device_id: u64, local_addr: Self) -> Option<Mac>
        where
            D: EventDispatcher,
        {
            Ipv4Addr => {
                let src_mac = get_device_state(ctx, device_id).mac;
                if let Some(dst_mac) = crate::device::arp::lookup::<_, _, EthernetArpDevice>(
                    ctx, device_id, src_mac, local_addr,
                ) {
                    Some(dst_mac)
                } else {
                    log_unimplemented!(
                        None,
                        "device::ethernet::send_ip_frame: unimplemented on arp cache miss"
                    )
                }
            }
            Ipv6Addr => { log_unimplemented!(None, "device::ethernet::send_ip_frame: IPv6 unimplemented") }
        }
    );

    if let Some(dst_mac) = A::lookup_dst_mac(ctx, device_id, local_addr) {
        let src_mac = get_device_state(ctx, device_id).mac;
        let buffer = body
            .encapsulate(EthernetFrameSerializer::new(
                src_mac,
                dst_mac,
                A::Version::EtherType,
            ))
            .serialize_outer();
        ctx.dispatcher()
            .send_frame(DeviceId::new_ethernet(device_id), buffer.as_ref());
    }
}

/// Receive an Ethernet frame from the network.
pub fn receive_frame<D: EventDispatcher>(ctx: &mut Context<D>, device_id: u64, bytes: &mut [u8]) {
    let (frame, body_range) = if let Ok(frame) = EthernetFrame::parse(&mut bytes[..]) {
        frame
    } else {
        // TODO(joshlf): Do something else?
        return;
    };

    if let Some(Ok(ethertype)) = frame.ethertype() {
        let (src, dst) = (frame.src_mac(), frame.dst_mac());
        let device = DeviceId::new_ethernet(device_id);
        let buffer = BufferAndRange::new_from(bytes, body_range);
        match ethertype {
            EtherType::Arp => {
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
            EtherType::Ipv4 => crate::ip::receive_ip_packet::<D, Ipv4>(ctx, device, buffer),
            EtherType::Ipv6 => crate::ip::receive_ip_packet::<D, Ipv6>(ctx, device, buffer),
        }
    } else {
        // TODO(joshlf): Do something else?
        return;
    }
}

/// Get the IP address associated with this device.
pub fn get_ip_addr<D: EventDispatcher, A: IpAddr>(
    ctx: &mut Context<D>, device_id: u64,
) -> Option<(A, Subnet<A>)> {
    specialize_ip_addr!(
        fn get_ip_addr(state: &EthernetDeviceState) -> Option<(Self, Subnet<Self>)> {
            Ipv4Addr => { state.ipv4_addr }
            Ipv6Addr => { state.ipv6_addr }
        }
    );
    A::get_ip_addr(get_device_state(ctx, device_id))
}

/// Set the IP address associated with this device.
pub fn set_ip_addr<D: EventDispatcher, A: IpAddr>(
    ctx: &mut Context<D>, device_id: u64, addr: A, subnet: Subnet<A>,
) {
    // TODO(joshlf): Perform any other necessary setup
    specialize_ip_addr!(
        fn set_ip_addr(state: &mut EthernetDeviceState, addr: Self, subnet: Subnet<Self>) {
            Ipv4Addr => { state.ipv4_addr = Some((addr, subnet)) }
            Ipv6Addr => { state.ipv6_addr = Some((addr, subnet)) }
        }
    );
    A::set_ip_addr(get_device_state(ctx, device_id), addr, subnet)
}

fn get_device_state<D: EventDispatcher>(
    ctx: &mut Context<D>, device_id: u64,
) -> &mut EthernetDeviceState {
    ctx.state()
        .device
        .ethernet
        .get_mut(&device_id)
        .expect(&format!("no such Ethernet device: {}", device_id))
}

// Dummy type used to implement ArpDevice.
pub struct EthernetArpDevice;

impl ArpDevice<Ipv4Addr> for EthernetArpDevice {
    type HardwareAddr = Mac;
    const BROADCAST: Mac = Mac::BROADCAST;

    fn send_arp_frame<D: EventDispatcher, S: SerializationRequest>(
        ctx: &mut Context<D>, device_id: u64, dst: Self::HardwareAddr, body: S,
    ) {
        let src = get_device_state(ctx, device_id).mac;
        let buffer = body
            .encapsulate(EthernetFrameSerializer::new(src, dst, EtherType::Arp))
            .serialize_outer();
        ctx.dispatcher()
            .send_frame(DeviceId::new_ethernet(device_id), buffer.as_ref());
    }

    fn get_arp_state<D: EventDispatcher>(
        ctx: &mut Context<D>, device_id: u64,
    ) -> &mut ArpState<Ipv4Addr, Self> {
        &mut get_device_state(ctx, device_id).ipv4_arp
    }

    fn get_protocol_addr<D: EventDispatcher>(
        ctx: &mut Context<D>, device_id: u64,
    ) -> Option<Ipv4Addr> {
        get_device_state(ctx, device_id).ipv4_addr.map(|x| x.0)
    }

    fn get_hardware_addr<D: EventDispatcher>(ctx: &mut Context<D>, device_id: u64) -> Mac {
        get_device_state(ctx, device_id).mac
    }
}
