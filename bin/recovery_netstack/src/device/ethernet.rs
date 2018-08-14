// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Ethernet protocol.

use std::fmt::{self, Display, Formatter};

use log::{debug, log, trace};
use zerocopy::{AsBytes, FromBytes, Unaligned};

use crate::device::DeviceId;
use crate::ip::{IpAddr, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Subnet};
use crate::wire::ethernet::EthernetFrame;
use crate::wire::{BufferAndRange, SerializationCallback};
use crate::StackState;

/// The broadcast MAC address.
///
/// The broadcast MAC address, FF:FF:FF:FF:FF:FF, indicates that a frame should
/// be received by all receivers regardless of their local MAC address.
pub const BROADCAST_MAC: Mac = Mac([0xFF; 6]);

/// A media access control (MAC) address.
#[derive(Copy, Clone, Eq, PartialEq, Debug)]
#[repr(transparent)]
pub struct Mac([u8; 6]);

unsafe impl FromBytes for Mac {}
unsafe impl AsBytes for Mac {}
unsafe impl Unaligned for Mac {}

impl Mac {
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
        *self == BROADCAST_MAC
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
#[derive(Default)]
pub struct EthernetDeviceState {
    ipv4_addr: Option<(Ipv4Addr, Subnet<Ipv4Addr>)>,
    ipv6_addr: Option<(Ipv6Addr, Subnet<Ipv6Addr>)>,
}

/// Send an IP packet in an Ethernet frame.
///
/// `send_ip_frame` accepts a device ID, a local IP address, and a callback. It
/// computes the routing information and invokes the callback with the number of
/// prefix bytes required by all encapsulating headers, and the minimum size of
/// the body plus padding. The callback is expected to return a byte buffer and
/// a range which corresponds to the desired body. The portion of the buffer
/// beyond the end of the body range will be treated as padding. The total
/// number of bytes in the body and the post-body padding must not be smaller
/// than the minimum size passed to the callback.
///
/// For more details on the callback, see the [`::wire::SerializationCallback`]
/// documentation.
///
/// # Panics
///
/// `send_ip_frame` panics if the buffer returned from `get_buffer` does not
/// have sufficient space preceding the body for all encapsulating headers or
/// does not have enough body plus padding bytes to satisfy the requirement
/// passed to the callback.
pub fn send_ip_frame<A, B, F>(state: &mut StackState, device_id: u64, local_addr: A, get_buffer: F)
where
    A: IpAddr,
    B: AsRef<[u8]> + AsMut<[u8]>,
    F: SerializationCallback<B>,
{
    use crate::wire::ethernet::{MAX_HEADER_LEN, MIN_BODY_LEN};
    let mut buffer = get_buffer(MAX_HEADER_LEN, MIN_BODY_LEN);
    let range_len = {
        let range = buffer.range();
        range.end - range.start
    };
    if range_len < MIN_BODY_LEN {
        // This is guaranteed to succeed so long as get_buffer satisfies its
        // contract.
        //
        // SECURITY: Use _zero to ensure we zero padding bytes to prevent
        // leaking information from packets previously stored in this buffer.
        buffer.extend_forwards_zero(MIN_BODY_LEN - range_len);
    }
    // TODO(joshlf): Figure out routing informationa and serialize
}

/// Receive an Ethernet frame from the network.
pub fn receive_frame(state: &mut StackState, device_id: u64, bytes: &mut [u8]) {
    let (frame, body_range) = if let Ok(frame) = EthernetFrame::parse(&mut bytes[..]) {
        frame
    } else {
        // TODO(joshlf): Do something else?
        return;
    };

    if let Some(Ok(ethertype)) = frame.ethertype() {
        let device = DeviceId::new_ethernet(device_id);
        let buffer = BufferAndRange::new(bytes, body_range);
        match ethertype {
            EtherType::Arp => {
                println!("received ARP frame");
                log_unimplemented!((), "device::ethernet::receive_frame: ARP not implemented")
            }
            EtherType::Ipv4 => crate::ip::receive_ip_packet::<Ipv4>(state, device, buffer),
            EtherType::Ipv6 => crate::ip::receive_ip_packet::<Ipv6>(state, device, buffer),
        }
    } else {
        // TODO(joshlf): Do something else?
        return;
    }
}

/// Get the IP address associated with this device.
pub fn get_ip_addr<A: IpAddr>(state: &mut StackState, device_id: u64) -> Option<(A, Subnet<A>)> {
    specialize_ip_addr!(
        fn get_ip_addr(state: &EthernetDeviceState) -> Option<(Self, Subnet<Self>)> {
            Ipv4Addr => { state.ipv4_addr }
            Ipv6Addr => { state.ipv6_addr }
        }
    );
    A::get_ip_addr(get_device_state(state, device_id))
}

/// Set the IP address associated with this device.
pub fn set_ip_addr<A: IpAddr>(state: &mut StackState, device_id: u64, addr: A, subnet: Subnet<A>) {
    // TODO(joshlf): Perform any other necessary setup
    specialize_ip_addr!(
        fn set_ip_addr(state: &mut EthernetDeviceState, addr: Self, subnet: Subnet<Self>) {
            Ipv4Addr => { state.ipv4_addr = Some((addr, subnet)) }
            Ipv6Addr => { state.ipv6_addr = Some((addr, subnet)) }
        }
    );
    A::set_ip_addr(get_device_state(state, device_id), addr, subnet)
}

fn get_device_state(state: &mut StackState, device_id: u64) -> &mut EthernetDeviceState {
    state
        .device
        .ethernet
        .get_mut(&device_id)
        .expect(&format!("no such Ethernet device: {}", device_id))
}
