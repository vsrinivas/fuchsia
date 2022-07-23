// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Ethernet protocol types.

use core::fmt::{self, Debug, Display, Formatter};

use zerocopy::{AsBytes, FromBytes, Unaligned};

use crate::ip::{AddrSubnet, IpAddr, IpAddress, Ipv6, Ipv6Addr};
use crate::{
    BroadcastAddr, BroadcastAddress, LinkLocalUnicastAddr, MulticastAddr, MulticastAddress,
    UnicastAddr, UnicastAddress, Witness,
};

/// A media access control (MAC) address.
///
/// MAC addresses are used to identify devices in the Ethernet protocol.
///
/// MAC addresses can be derived from multicast IP addresses; see the `From`
/// implementation for more details.
///
/// # Layout
///
/// `Mac` has the same layout as `[u8; 6]`, which is the layout that most
/// protocols use to represent a MAC address in their packet formats. This can
/// be useful when parsing a MAC address from a packet. For example:
///
/// ```rust
/// # use net_types::ethernet::Mac;
/// /// The header of an Ethernet frame.
/// ///
/// /// `EthernetHeader` has the same layout as the header of an Ethernet frame.
/// #[repr(C)]
/// struct EthernetHeader {
///     dst: Mac,
///     src: Mac,
///     ethertype: [u8; 2],
/// }
/// ```
#[derive(Copy, Clone, Eq, PartialEq, Hash, FromBytes, AsBytes, Unaligned)]
#[repr(transparent)]
pub struct Mac([u8; Mac::BYTES]);

impl Mac {
    /// The number of bytes in a Mac address.
    pub const BYTES: usize = 6;

    /// The broadcast MAC address.
    ///
    /// The broadcast MAC address, FF:FF:FF:FF:FF:FF, indicates that a frame
    /// should be received by all receivers regardless of their local MAC
    /// address.
    // TODO(https://github.com/rust-lang/rust/issues/73255): Make this
    // `BroadcastAddr<Mac>` once the `const_precise_live_drops` feature has
    // stabilized, and thus it's possible to write a `const fn` which converts
    // from `BroadcastAddr<A>` to `A`.
    pub const BROADCAST: Mac = Mac([0xFF; Self::BYTES]);

    /// The default [RFC 4291] EUI-64 magic value used by the [`to_eui64`]
    /// method.
    ///
    /// [RFC 4291]: https://tools.ietf.org/html/rfc4291
    /// [`to_eui64`]: crate::ethernet::Mac::to_eui64
    pub const DEFAULT_EUI_MAGIC: [u8; 2] = [0xff, 0xfe];

    /// Constructs a new MAC address.
    #[inline]
    pub const fn new(bytes: [u8; Self::BYTES]) -> Mac {
        Mac(bytes)
    }

    /// Gets the bytes of the MAC address.
    #[inline]
    pub const fn bytes(self) -> [u8; Self::BYTES] {
        self.0
    }

    /// Returns the [RFC 4291] EUI-64 interface identifier for this MAC address
    /// with the default EUI magic value.
    ///
    /// `mac.to_eui64()` is equivalent to
    /// `mac.to_eui64_with_magic(Mac::DEFAULT_EUI_MAGIC)`.
    ///
    /// [RFC 4291]: https://tools.ietf.org/html/rfc4291
    #[inline]
    pub fn to_eui64(self) -> [u8; 8] {
        self.to_eui64_with_magic(Mac::DEFAULT_EUI_MAGIC)
    }

    /// Returns the [RFC 4291] EUI-64 interface identifier for this MAC address
    /// with a custom EUI magic value.
    ///
    /// `eui_magic` is the two bytes that are inserted between the bytes of the
    /// MAC address to form the identifier. Also see the [`to_eui64`] method,
    /// which uses the default magic value of 0xFFFE.
    ///
    /// [RFC 4291]: https://tools.ietf.org/html/rfc4291
    /// [`to_eui64`]: crate::ethernet::Mac::to_eui64
    #[inline]
    pub fn to_eui64_with_magic(self, eui_magic: [u8; 2]) -> [u8; 8] {
        let mut eui = [0; 8];
        eui[0..3].copy_from_slice(&self.0[0..3]);
        eui[3..5].copy_from_slice(&eui_magic);
        eui[5..8].copy_from_slice(&self.0[3..6]);
        eui[0] ^= 0b0000_0010;
        eui
    }

    /// Returns the link-local unicast IPv6 address and subnet for this MAC
    /// address, as per [RFC 4862], with the default EUI magic value.
    ///
    /// `mac.to_ipv6_link_local()` is equivalent to
    /// `mac.to_ipv6_link_local_with_magic(Mac::DEFAULT_EUI_MAGIC)`.
    ///
    /// [RFC 4291]: https://tools.ietf.org/html/rfc4291
    #[inline]
    pub fn to_ipv6_link_local(self) -> AddrSubnet<Ipv6Addr, LinkLocalUnicastAddr<Ipv6Addr>> {
        self.to_ipv6_link_local_with_magic(Mac::DEFAULT_EUI_MAGIC)
    }

    /// Returns the link-local unicast IPv6 address and subnet for this MAC
    /// address, as per [RFC 4862].
    ///
    /// `eui_magic` is the two bytes that are inserted between the bytes of the
    /// MAC address to form the identifier. Also see the [`to_ipv6_link_local`]
    /// method, which uses the default magic value of 0xFFFE.
    ///
    /// The subnet prefix length is 128 -
    /// [`Ipv6::UNICAST_INTERFACE_IDENTIFIER_BITS`].
    ///
    /// [RFC 4862]: https://tools.ietf.org/html/rfc4862
    /// [`to_ipv6_link_local`]: crate::ethernet::Mac::to_ipv6_link_local
    /// [RFC 4291]: https://tools.ietf.org/html/rfc4291
    #[inline]
    pub fn to_ipv6_link_local_with_magic(
        self,
        eui_magic: [u8; 2],
    ) -> AddrSubnet<Ipv6Addr, LinkLocalUnicastAddr<Ipv6Addr>> {
        let mut ipv6_addr = [0; 16];
        ipv6_addr[0..2].copy_from_slice(&[0xfe, 0x80]);
        ipv6_addr[8..16].copy_from_slice(&self.to_eui64_with_magic(eui_magic));

        // We know the call to `unwrap` will not panic because we know we are
        // passing `AddrSubnet::new` a valid link local address as per RFC 4291.
        // Specifically, the first 10 bits of the generated address is
        // `0b1111111010`. `AddrSubnet::new` also validates the prefix length,
        // and we know that 64 is a valid IPv6 subnet prefix length.
        //
        // TODO(ghanan): Investigate whether this unwrap is optimized out in
        //               practice as this code will be on the hot path.
        AddrSubnet::new(
            Ipv6Addr::from(ipv6_addr),
            Ipv6Addr::BYTES * 8 - Ipv6::UNICAST_INTERFACE_IDENTIFIER_BITS,
        )
        .unwrap()
    }
}

impl AsRef<[u8]> for Mac {
    fn as_ref(&self) -> &[u8] {
        &self.0
    }
}

impl AsMut<[u8]> for Mac {
    fn as_mut(&mut self) -> &mut [u8] {
        &mut self.0
    }
}

impl UnicastAddress for Mac {
    /// Is this a unicast MAC address?
    ///
    /// Returns true if the least significant bit of the first byte of the
    /// address is 0.
    #[inline]
    fn is_unicast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self.0[0] & 1 == 0
    }
}

impl MulticastAddress for Mac {
    /// Is this a multicast MAC address?
    ///
    /// Returns true if the least significant bit of the first byte of the
    /// address is 1.
    #[inline]
    fn is_multicast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        self.0[0] & 1 == 1
    }
}

impl BroadcastAddress for Mac {
    /// Is this the broadcast MAC address?
    ///
    /// Returns true if this is the broadcast MAC address, FF:FF:FF:FF:FF:FF.
    /// Note that the broadcast address is also considered a multicast address,
    /// so `addr.is_broadcast()` implies `addr.is_multicast()`.
    #[inline]
    fn is_broadcast(&self) -> bool {
        // https://en.wikipedia.org/wiki/MAC_address#Unicast_vs._multicast
        *self == Mac::BROADCAST
    }
}

impl<'a, A: IpAddress> From<&'a MulticastAddr<A>> for Mac {
    /// Converts a multicast IP address to a MAC address.
    ///
    /// This method is equivalent to `MulticastAddr::<Mac>::from(addr).get()`.
    #[inline]
    fn from(addr: &'a MulticastAddr<A>) -> Mac {
        MulticastAddr::<Mac>::from(addr).get()
    }
}

impl<A: IpAddress> From<MulticastAddr<A>> for Mac {
    /// Converts a multicast IP address to a MAC address.
    ///
    /// This method is equivalent to `(&addr).into()`.
    #[inline]
    fn from(addr: MulticastAddr<A>) -> Mac {
        (&addr).into()
    }
}

impl<'a, A: IpAddress> From<&'a MulticastAddr<A>> for MulticastAddr<Mac> {
    /// Converts a multicast IP address to a multicast MAC address.
    ///
    /// When a multicast IP packet is sent over an Ethernet link, the frame's
    /// destination MAC address is a multicast MAC address that is derived from
    /// the destination IP address. This function performs that conversion.
    ///
    /// See [RFC 7042 Section 2.1.1] and [Section 2.3.1] for details on how IPv4
    /// and IPv6 addresses are mapped, respectively.
    ///
    /// [RFC 7042 Section 2.1.1]: https://tools.ietf.org/html/rfc7042#section-2.1.1
    /// [Section 2.3.1]: https://tools.ietf.org/html/rfc7042#section-2.3.1
    #[inline]
    fn from(addr: &'a MulticastAddr<A>) -> MulticastAddr<Mac> {
        // We know the call to `unwrap` will not panic because we are generating
        // a multicast MAC as defined in RFC 7042 section 2.1.1 and section
        // 2.3.1 for IPv4 and IPv6 addresses, respectively.
        MulticastAddr::new(Mac::new(match (*addr).get().into() {
            IpAddr::V4(addr) => {
                let ip_bytes = addr.ipv4_bytes();
                let mut mac_bytes = [0; 6];
                mac_bytes[0] = 0x01;
                mac_bytes[1] = 0x00;
                mac_bytes[2] = 0x5e;
                mac_bytes[3] = ip_bytes[1] & 0x7f;
                mac_bytes[4] = ip_bytes[2];
                mac_bytes[5] = ip_bytes[3];
                mac_bytes
            }
            IpAddr::V6(addr) => {
                let ip_bytes = addr.ipv6_bytes();
                let mut mac_bytes = [0; 6];
                mac_bytes[0] = 0x33;
                mac_bytes[1] = 0x33;
                mac_bytes[2] = ip_bytes[12];
                mac_bytes[3] = ip_bytes[13];
                mac_bytes[4] = ip_bytes[14];
                mac_bytes[5] = ip_bytes[15];
                mac_bytes
            }
        }))
        .unwrap()
    }
}

impl<A: IpAddress> From<MulticastAddr<A>> for MulticastAddr<Mac> {
    fn from(addr: MulticastAddr<A>) -> MulticastAddr<Mac> {
        (&addr).into()
    }
}

macro_rules! impl_from_witness {
    ($witness:ident) => {
        impl From<$witness<Mac>> for Mac {
            fn from(addr: $witness<Mac>) -> Mac {
                addr.get()
            }
        }
    };
}

impl_from_witness!(UnicastAddr);
impl_from_witness!(MulticastAddr);
impl_from_witness!(BroadcastAddr);

impl Display for Mac {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{:X}:{:X}:{:X}:{:X}:{:X}:{:X}",
            self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5]
        )
    }
}

impl Debug for Mac {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        Display::fmt(self, f)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ip::Ipv4Addr;

    #[test]
    fn test_mac_to_eui() {
        assert_eq!(
            Mac::new([0x00, 0x1a, 0xaa, 0x12, 0x34, 0x56]).to_eui64(),
            [0x02, 0x1a, 0xaa, 0xff, 0xfe, 0x12, 0x34, 0x56]
        );
        assert_eq!(
            Mac::new([0x00, 0x1a, 0xaa, 0x12, 0x34, 0x56]).to_eui64_with_magic([0xfe, 0xfe]),
            [0x02, 0x1a, 0xaa, 0xfe, 0xfe, 0x12, 0x34, 0x56]
        );
    }

    #[test]
    fn test_to_ipv6_link_local() {
        assert_eq!(
            Mac::new([0x00, 0x1a, 0xaa, 0x12, 0x34, 0x56]).to_ipv6_link_local(),
            AddrSubnet::new(
                Ipv6Addr::new([
                    0xfe80, // IPv6 link-local prefix
                    0, 0, 0, // Padding zeroes
                    0x021a, 0xaaff, 0xfe12, 0x3456, // EUI-64
                ]),
                64
            )
            .unwrap()
        );
        assert_eq!(
            Mac::new([0x00, 0x1a, 0xaa, 0x12, 0x34, 0x56])
                .to_ipv6_link_local_with_magic([0xfe, 0xfe]),
            AddrSubnet::new(
                Ipv6Addr::new([
                    0xfe80, // IPv6 link-local prefix
                    0, 0, 0, // Padding zeroes
                    0x021a, 0xaafe, 0xfe12, 0x3456, // EUI-64
                ]),
                64
            )
            .unwrap()
        );
    }

    #[test]
    fn test_map_multicast_ip_to_ethernet_mac() {
        let ipv4 = Ipv4Addr::new([224, 1, 1, 1]);
        let mac = Mac::from(&MulticastAddr::new(ipv4).unwrap());
        assert_eq!(mac, Mac::new([0x01, 0x00, 0x5e, 0x1, 0x1, 0x1]));
        let ipv4 = Ipv4Addr::new([224, 129, 1, 1]);
        let mac = Mac::from(&MulticastAddr::new(ipv4).unwrap());
        assert_eq!(mac, Mac::new([0x01, 0x00, 0x5e, 0x1, 0x1, 0x1]));
        let ipv4 = Ipv4Addr::new([225, 1, 1, 1]);
        let mac = Mac::from(&MulticastAddr::new(ipv4).unwrap());
        assert_eq!(mac, Mac::new([0x01, 0x00, 0x5e, 0x1, 0x1, 0x1]));

        let ipv6 = Ipv6Addr::new([0xff02, 0, 0, 0, 0, 0, 0, 3]);
        let mac = Mac::from(&MulticastAddr::new(ipv6).unwrap());
        assert_eq!(mac, Mac::new([0x33, 0x33, 0, 0, 0, 3]));
        let ipv6 = Ipv6Addr::new([0xff02, 0, 0, 1, 0, 0, 0, 3]);
        let mac = Mac::from(&MulticastAddr::new(ipv6).unwrap());
        assert_eq!(mac, Mac::new([0x33, 0x33, 0, 0, 0, 3]));
        let ipv6 = Ipv6Addr::new([0xff02, 0, 0, 0, 0, 0, 0x100, 3]);
        let mac = Mac::from(&MulticastAddr::new(ipv6).unwrap());
        assert_eq!(mac, Mac::new([0x33, 0x33, 1, 0, 0, 3]));
    }
}
