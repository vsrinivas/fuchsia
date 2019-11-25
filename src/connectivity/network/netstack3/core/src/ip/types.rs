// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{self, Debug, Formatter};

use net_types::ip::{Ip, IpAddr, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr, Subnet, SubnetEither};
use net_types::SpecifiedAddr;
use never::Never;
use packet::{PacketBuilder, ParsablePacket, ParseMetadata};
use zerocopy::{ByteSlice, ByteSliceMut};

use crate::error::IpParseError;
use crate::wire::ipv4::{Ipv4Header, Ipv4Packet, Ipv4PacketBuilder};
use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder};

/// A ZST that carries IP version information.
///
/// Typically used by implementers of [`packet::ParsablePacket`] that need
/// to receive external information of which IP version encapsulates the
/// packet, but without any other associated data.
#[derive(Copy, Clone, PartialEq, Eq, Hash)]
pub struct IpVersionMarker<I> {
    _marker: core::marker::PhantomData<I>,
}

impl<I: Ip> Default for IpVersionMarker<I> {
    fn default() -> Self {
        Self { _marker: core::marker::PhantomData }
    }
}

impl<I: Ip> Debug for IpVersionMarker<I> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "IpVersionMarker<{}>", I::NAME)
    }
}

/// The destination for forwarding a packet.
///
/// `EntryDest` can either be a device or another network address.
#[allow(missing_docs)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum EntryDest<A, D> {
    Local { device: D },
    Remote { next_hop: SpecifiedAddr<A> },
}

/// A local forwarding destination, or a remote forwarding destination that can
/// be an IPv4 or an IPv6 address.
pub type EntryDestEither<D> = EntryDest<IpAddr, D>;

impl<A: IpAddress, D> EntryDest<A, D>
where
    SpecifiedAddr<IpAddr>: From<SpecifiedAddr<A>>,
{
    fn into_ip_addr(self) -> EntryDest<IpAddr, D> {
        match self {
            EntryDest::Local { device } => EntryDest::Local { device },
            EntryDest::Remote { next_hop } => EntryDest::Remote { next_hop: next_hop.into() },
        }
    }
}

/// A forwarding entry.
///
/// `Entry` is a `Subnet` paired with an `EntryDest`.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct Entry<A: IpAddress, D> {
    pub subnet: Subnet<A>,
    pub dest: EntryDest<A, D>,
}

/// An IPv4 forwarding entry or an IPv6 forwarding entry.
#[allow(missing_docs)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum EntryEither<D> {
    V4(Entry<Ipv4Addr, D>),
    V6(Entry<Ipv6Addr, D>),
}

impl<D> EntryEither<D> {
    /// Creates a new [`EntryEither`] with the given `subnet` and `destination`.
    ///
    /// Returns `None` if `subnet` and `destination` are not the same IP version
    /// (both `V4` or both `V6`) when `destination` is a remote value.
    pub fn new(subnet: SubnetEither, destination: EntryDestEither<D>) -> Option<EntryEither<D>> {
        match destination {
            EntryDest::Local { device } => match subnet {
                SubnetEither::V4(subnet) => {
                    Some(EntryEither::V4(Entry { subnet, dest: EntryDest::Local { device } }))
                }
                SubnetEither::V6(subnet) => {
                    Some(EntryEither::V6(Entry { subnet, dest: EntryDest::Local { device } }))
                }
            },
            EntryDest::Remote { next_hop } => match (subnet, next_hop.into()) {
                (SubnetEither::V4(subnet), IpAddr::V4(next_hop)) => {
                    Some(EntryEither::V4(Entry { subnet, dest: EntryDest::Remote { next_hop } }))
                }
                (SubnetEither::V6(subnet), IpAddr::V6(next_hop)) => {
                    Some(EntryEither::V6(Entry { subnet, dest: EntryDest::Remote { next_hop } }))
                }
                _ => None,
            },
        }
    }

    /// Gets the subnet and destination for this [`EntryEither`].
    pub fn into_subnet_dest(self) -> (SubnetEither, EntryDestEither<D>) {
        match self {
            EntryEither::V4(entry) => (entry.subnet.into(), entry.dest.into_ip_addr()),
            EntryEither::V6(entry) => (entry.subnet.into(), entry.dest.into_ip_addr()),
        }
    }
}

impl<D> From<Entry<Ipv4Addr, D>> for EntryEither<D> {
    fn from(entry: Entry<Ipv4Addr, D>) -> EntryEither<D> {
        EntryEither::V4(entry)
    }
}

impl<D> From<Entry<Ipv6Addr, D>> for EntryEither<D> {
    fn from(entry: Entry<Ipv6Addr, D>) -> EntryEither<D> {
        EntryEither::V6(entry)
    }
}

create_protocol_enum!(
        /// An IP protocol or next header number.
        ///
        /// For IPv4, this is the protocol number. For IPv6, this is the next
        /// header number.
        #[derive(Copy, Clone, Hash, Eq, PartialEq)]
        pub(crate) enum IpProto: u8 {
            Icmp, 1, "ICMP";
            Igmp, 2, "IGMP";
            Tcp, 6, "TCP";
            Udp, 17, "UDP";
            Icmpv6, 58, "ICMPv6";
            NoNextHeader, 59, "NO NEXT HEADER";
            _, "IP protocol {}";
        }
    );

create_protocol_enum!(
        /// An IPv6 Extension Header type.
        ///
        /// These are valid next header types for an IPv6 Header that relate to
        /// extension headers. This enum does not include upper layer protocol
        /// numbers even though they may be valid Next Header values.
        #[derive(Copy, Clone, Hash, Eq, PartialEq)]
        pub(crate) enum Ipv6ExtHdrType: u8 {
            HopByHopOptions, 0, "IPv6 HOP-BY-HOP OPTIONS HEADER";
            Routing, 43, "IPv6 ROUTING HEADER";
            Fragment, 44, "IPv6 FRAGMENT HEADER";
            EncapsulatingSecurityPayload, 50, "ENCAPSULATING SECURITY PAYLOAD";
            Authentication, 51, "AUTHENTICATION HEADER";
            DestinationOptions, 60, "IPv6 DESTINATION OPTIONS HEADER";
            _,  "IPv6 EXTENSION HEADER {}";
        }
    );

/// An extension trait to the `Ip` trait adding an associated `PacketBuilder`
/// type.
pub(crate) trait IpExt: Ip {
    type PacketBuilder: IpPacketBuilder<Self> + Eq;
}

// NOTE(joshlf): We know that this is safe because the Ip trait is sealed to
// only be implemented by Ipv4 and Ipv6.
impl<I: Ip> IpExt for I {
    default type PacketBuilder = Never;
}

impl IpExt for Ipv4 {
    type PacketBuilder = Ipv4PacketBuilder;
}

impl IpExt for Ipv6 {
    type PacketBuilder = Ipv6PacketBuilder;
}

/// An extension trait to the `IpExt` trait adding an associated `Packet` type.
///
/// `IpExtByteSlice` extends the `IpExt` trait, adding an associated `Packet`
/// type. It cannot be part of the `IpExt` trait because it requires a `B:
/// ByteSlice` parameter (due to the requirements of `packet::ParsablePacket`).
pub(crate) trait IpExtByteSlice<B: ByteSlice>: IpExt {
    type Packet: IpPacket<B, Self, Builder = Self::PacketBuilder>;
}

// NOTE(joshlf): We know that this is safe because the Ip trait is sealed to
// only be implemented by Ipv4 and Ipv6.
impl<B: ByteSlice, I: Ip> IpExtByteSlice<B> for I {
    default type Packet = Never;
}

impl<B: ByteSlice> IpExtByteSlice<B> for Ipv4 {
    type Packet = Ipv4Packet<B>;
}

impl<B: ByteSlice> IpExtByteSlice<B> for Ipv6 {
    type Packet = Ipv6Packet<B>;
}

/// An IPv4 or IPv6 packet.
///
/// `IpPacket` is implemented by `Ipv4Packet` and `Ipv6Packet`.
pub(crate) trait IpPacket<B: ByteSlice, I: Ip>:
    Sized + Debug + ParsablePacket<B, (), Error = IpParseError<I>>
{
    /// A builder for this packet type.
    type Builder: IpPacketBuilder<I>;

    /// The source IP address.
    fn src_ip(&self) -> I::Addr;

    /// The destination IP address.
    fn dst_ip(&self) -> I::Addr;

    /// The protocol (IPv4) or next header (IPv6) field.
    fn proto(&self) -> IpProto;

    /// The Time to Live (TTL).
    fn ttl(&self) -> u8;

    /// Set the Time to Live (TTL).
    ///
    /// `set_ttl` updates the packet's TTL in place.
    fn set_ttl(&mut self, ttl: u8)
    where
        B: ByteSliceMut;

    /// Get the body.
    fn body(&self) -> &[u8];

    /// Consume the packet and return some metadata.
    ///
    /// Consume the packet and return the source address, destination address,
    /// protocol, and `ParseMetadata`.
    fn into_metadata(self) -> (I::Addr, I::Addr, IpProto, ParseMetadata) {
        let src_ip = self.src_ip();
        let dst_ip = self.dst_ip();
        let proto = self.proto();
        let meta = self.parse_metadata();
        (src_ip, dst_ip, proto, meta)
    }
}

impl<B: ByteSlice> IpPacket<B, Ipv4> for Ipv4Packet<B> {
    type Builder = Ipv4PacketBuilder;

    fn src_ip(&self) -> Ipv4Addr {
        Ipv4Header::src_ip(self)
    }
    fn dst_ip(&self) -> Ipv4Addr {
        Ipv4Header::dst_ip(self)
    }
    fn proto(&self) -> IpProto {
        Ipv4Header::proto(self)
    }
    fn ttl(&self) -> u8 {
        Ipv4Header::ttl(self)
    }
    fn set_ttl(&mut self, ttl: u8)
    where
        B: ByteSliceMut,
    {
        Ipv4Packet::set_ttl(self, ttl)
    }
    fn body(&self) -> &[u8] {
        Ipv4Packet::body(self)
    }
}

impl<B: ByteSlice> IpPacket<B, Ipv6> for Ipv6Packet<B> {
    type Builder = Ipv6PacketBuilder;

    fn src_ip(&self) -> Ipv6Addr {
        Ipv6Packet::src_ip(self)
    }
    fn dst_ip(&self) -> Ipv6Addr {
        Ipv6Packet::dst_ip(self)
    }
    fn proto(&self) -> IpProto {
        Ipv6Packet::proto(self)
    }
    fn ttl(&self) -> u8 {
        Ipv6Packet::hop_limit(self)
    }
    fn set_ttl(&mut self, ttl: u8)
    where
        B: ByteSliceMut,
    {
        Ipv6Packet::set_hop_limit(self, ttl)
    }
    fn body(&self) -> &[u8] {
        Ipv6Packet::body(self)
    }
}

/// A builder for IP packets.
///
/// `IpPacketBuilder` is implemented by `Ipv4PacketBuilder` and
/// `Ipv6PacketBuilder`.
pub(crate) trait IpPacketBuilder<I: Ip>: PacketBuilder + Clone {
    fn new(src_ip: I::Addr, dst_ip: I::Addr, ttl: u8, proto: IpProto) -> Self;
}

impl IpPacketBuilder<Ipv4> for Ipv4PacketBuilder {
    fn new(src_ip: Ipv4Addr, dst_ip: Ipv4Addr, ttl: u8, proto: IpProto) -> Ipv4PacketBuilder {
        Ipv4PacketBuilder::new(src_ip, dst_ip, ttl, proto)
    }
}

impl IpPacketBuilder<Ipv6> for Ipv6PacketBuilder {
    fn new(src_ip: Ipv6Addr, dst_ip: Ipv6Addr, ttl: u8, proto: IpProto) -> Ipv6PacketBuilder {
        Ipv6PacketBuilder::new(src_ip, dst_ip, ttl, proto)
    }
}

/// An IPv4 header option.
///
/// An IPv4 header option comprises metadata about the option (which is stored
/// in the kind byte) and the option itself. Note that all kind-byte-only
/// options are handled by the utilities in `wire::records::options`, so this
/// type only supports options with variable-length data.
///
/// See [Wikipedia] or [RFC 791] for more details.
///
/// [Wikipedia]: https://en.wikipedia.org/wiki/IPv4#Options [RFC 791]:
/// https://tools.ietf.org/html/rfc791#page-15
#[derive(PartialEq, Eq, Debug)]
pub(crate) struct Ipv4Option<'a> {
    /// Whether this option needs to be copied into all fragments of a
    /// fragmented packet.
    pub(crate) copied: bool,
    // TODO(joshlf): include "Option Class"? The variable-length option data.
    pub(crate) data: Ipv4OptionData<'a>,
}

/// The data associated with an IPv4 header option.
///
/// `Ipv4OptionData` represents the variable-length data field of an IPv4 header
/// option.
#[allow(missing_docs)]
#[derive(PartialEq, Eq, Debug)]
pub(crate) enum Ipv4OptionData<'a> {
    // The maximum header length is 60 bytes, and the fixed-length header is 20
    // bytes, so there are 40 bytes for the options. That leaves a maximum
    // options size of 1 kind byte + 1 length byte + 38 data bytes. Data for an
    // unrecognized option kind.
    //
    //  Any unrecognized option kind will have its data parsed using this
    //  variant. This allows code to copy unrecognized options into packets when
    //  forwarding.
    //
    //  `data`'s length is in the range [0, 38].
    Unrecognized {
        kind: u8,
        len: u8,
        data: &'a [u8],
    },
    /// Used to tell routers to inspect the packet.
    ///
    /// Used by IGMP host messages per [RFC 2236 section 2].
    /// [RFC 2236 section 2]: https://tools.ietf.org/html/rfc2236#section-2
    RouterAlert {
        data: u16,
    },
}

#[cfg(test)]
mod tests {
    use net_types::Witness;

    use super::*;

    #[test]
    fn test_entry_either() {
        // check that trying to build an EntryEither with mismatching IpAddr
        // fails, and with matching ones succeeds:
        let subnet_v4 = Subnet::new(Ipv4Addr::new([192, 168, 0, 0]), 24).unwrap().into();
        let subnet_v6 =
            Subnet::new(Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]), 64)
                .unwrap()
                .into();
        let entry_v4: EntryDest<_, ()> = EntryDest::Remote {
            next_hop: SpecifiedAddr::new(Ipv4Addr::new([192, 168, 0, 1])).unwrap(),
        }
        .into_ip_addr();
        let entry_v6: EntryDest<_, ()> = EntryDest::Remote {
            next_hop: SpecifiedAddr::new(Ipv6Addr::new([
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            ]))
            .unwrap(),
        }
        .into_ip_addr();
        assert!(EntryEither::new(subnet_v4, entry_v6).is_none());
        assert!(EntryEither::new(subnet_v6, entry_v4).is_none());
        let valid_v4 = EntryEither::new(subnet_v4, entry_v4).unwrap();
        let valid_v6 = EntryEither::new(subnet_v6, entry_v6).unwrap();
        // check that the split produces results requal to the generating parts:
        assert_eq!((subnet_v4, entry_v4), valid_v4.into_subnet_dest());
        assert_eq!((subnet_v6, entry_v6), valid_v6.into_subnet_dest());
    }
}
