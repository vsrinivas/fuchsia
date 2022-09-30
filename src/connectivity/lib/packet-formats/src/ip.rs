// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IP protocol types.

use alloc::vec::Vec;
use core::cmp::PartialEq;
use core::convert::Infallible as Never;
use core::fmt::{Debug, Display};

use net_types::ip::{Ip, IpAddr, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use packet::{BufferViewMut, PacketBuilder, ParsablePacket, ParseMetadata};
use zerocopy::{ByteSlice, ByteSliceMut};

use crate::error::{IpParseError, IpParseResult};
use crate::ethernet::EthernetIpExt;
use crate::icmp::IcmpIpExt;
use crate::ipv4::{Ipv4Header, Ipv4OnlyMeta, Ipv4Packet, Ipv4PacketBuilder};
use crate::ipv6::{Ipv6Header, Ipv6Packet, Ipv6PacketBuilder};
use crate::private::Sealed;

/// An [`Ip`] extension trait adding an associated type for the IP protocol
/// number.
pub trait IpProtoExt: Ip {
    /// The type representing an IPv4 or IPv6 protocol number.
    ///
    /// For IPv4, this is [`Ipv4Proto`], and for IPv6, this is [`Ipv6Proto`].
    type Proto: IpProtocol + Copy + Clone + Debug + Display + PartialEq;
}

impl IpProtoExt for Ipv4 {
    type Proto = Ipv4Proto;
}

impl IpProtoExt for Ipv6 {
    type Proto = Ipv6Proto;
}

/// An extension trait to the `Ip` trait adding associated types relevant for
/// packet parsing and serialization.
pub trait IpExt: EthernetIpExt + IcmpIpExt {
    /// An IP packet type for this IP version.
    type Packet<B: ByteSlice>: IpPacket<B, Self, Builder = Self::PacketBuilder>;
    /// An IP packet builder type for the IP version.
    type PacketBuilder: IpPacketBuilder<Self> + Eq;
}

impl IpExt for Ipv4 {
    type Packet<B: ByteSlice> = Ipv4Packet<B>;
    type PacketBuilder = Ipv4PacketBuilder;
}

impl IpExt for Ipv6 {
    type Packet<B: ByteSlice> = Ipv6Packet<B>;
    type PacketBuilder = Ipv6PacketBuilder;
}

/// An error encountered during NAT64 translation.
#[derive(Debug)]
pub enum Nat64Error {
    /// Support not yet implemented in the library.
    NotImplemented,
}

/// The result of NAT64 translation.
#[derive(Debug)]
pub enum Nat64TranslationResult<S, E> {
    /// Forward the packet encoded in `S`.
    Forward(S),
    /// Silently drop the packet.
    Drop,
    /// An error was encountered.
    Err(E),
}

/// An IPv4 or IPv6 packet.
///
/// `IpPacket` is implemented by `Ipv4Packet` and `Ipv6Packet`.
pub trait IpPacket<B: ByteSlice, I: IpExt>:
    Sized + Debug + ParsablePacket<B, (), Error = IpParseError<I>>
{
    /// A builder for this packet type.
    type Builder: IpPacketBuilder<I>;

    /// Metadata which is only present in the packet format of a specific version
    /// of the IP protocol.
    type VersionSpecificMeta;

    /// The source IP address.
    fn src_ip(&self) -> I::Addr;

    /// The destination IP address.
    fn dst_ip(&self) -> I::Addr;

    /// The protocol number.
    fn proto(&self) -> I::Proto;

    /// The Time to Live (TTL) (IPv4) or Hop Limit (IPv6) field.
    fn ttl(&self) -> u8;

    /// Set the Time to Live (TTL) (IPv4) or Hop Limit (IPv6) field.
    ///
    /// `set_ttl` updates the packet's TTL/Hop Limit in place.
    fn set_ttl(&mut self, ttl: u8)
    where
        B: ByteSliceMut;

    /// Get the body.
    fn body(&self) -> &[u8];

    /// Gets packet metadata relevant only for this version of the IP protocol.
    fn version_specific_meta(&self) -> Self::VersionSpecificMeta;

    /// Consume the packet and return some metadata.
    ///
    /// Consume the packet and return the source address, destination address,
    /// protocol, and `ParseMetadata`.
    fn into_metadata(self) -> (I::Addr, I::Addr, I::Proto, ParseMetadata) {
        let src_ip = self.src_ip();
        let dst_ip = self.dst_ip();
        let proto = self.proto();
        let meta = self.parse_metadata();
        (src_ip, dst_ip, proto, meta)
    }

    /// Converts a packet reference into a dynamically-typed reference.
    fn as_ip_addr_ref(&self) -> IpAddr<&'_ Ipv4Packet<B>, &'_ Ipv6Packet<B>>;

    /// Reassembles a fragmented packet into a parsed IP packet.
    fn reassemble_fragmented_packet<BV: BufferViewMut<B>, IT: Iterator<Item = Vec<u8>>>(
        buffer: BV,
        header: Vec<u8>,
        body_fragments: IT,
    ) -> IpParseResult<I, Self>
    where
        B: ByteSliceMut;
}

impl<B: ByteSlice> IpPacket<B, Ipv4> for Ipv4Packet<B> {
    type Builder = Ipv4PacketBuilder;
    type VersionSpecificMeta = Ipv4OnlyMeta;

    fn src_ip(&self) -> Ipv4Addr {
        Ipv4Header::src_ip(self)
    }
    fn dst_ip(&self) -> Ipv4Addr {
        Ipv4Header::dst_ip(self)
    }
    fn proto(&self) -> Ipv4Proto {
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

    fn version_specific_meta(&self) -> Ipv4OnlyMeta {
        Ipv4OnlyMeta { id: Ipv4Header::id(self) }
    }

    fn as_ip_addr_ref(&self) -> IpAddr<&'_ Self, &'_ Ipv6Packet<B>> {
        IpAddr::V4(self)
    }

    fn reassemble_fragmented_packet<BV: BufferViewMut<B>, IT: Iterator<Item = Vec<u8>>>(
        buffer: BV,
        header: Vec<u8>,
        body_fragments: IT,
    ) -> IpParseResult<Ipv4, Ipv4Packet<B>>
    where
        B: ByteSliceMut,
    {
        crate::ipv4::reassemble_fragmented_packet(buffer, header, body_fragments)
    }
}

impl<B: ByteSlice> IpPacket<B, Ipv6> for Ipv6Packet<B> {
    type Builder = Ipv6PacketBuilder;
    type VersionSpecificMeta = ();

    fn src_ip(&self) -> Ipv6Addr {
        Ipv6Header::src_ip(self)
    }
    fn dst_ip(&self) -> Ipv6Addr {
        Ipv6Header::dst_ip(self)
    }
    fn proto(&self) -> Ipv6Proto {
        Ipv6Packet::proto(self)
    }
    fn ttl(&self) -> u8 {
        Ipv6Header::hop_limit(self)
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

    fn version_specific_meta(&self) -> () {
        ()
    }
    fn as_ip_addr_ref(&self) -> IpAddr<&'_ Ipv4Packet<B>, &'_ Self> {
        IpAddr::V6(self)
    }
    fn reassemble_fragmented_packet<BV: BufferViewMut<B>, IT: Iterator<Item = Vec<u8>>>(
        buffer: BV,
        header: Vec<u8>,
        body_fragments: IT,
    ) -> IpParseResult<Ipv6, Ipv6Packet<B>>
    where
        B: ByteSliceMut,
    {
        crate::ipv6::reassemble_fragmented_packet(buffer, header, body_fragments)
    }
}

/// A builder for IP packets.
///
/// `IpPacketBuilder` is implemented by `Ipv4PacketBuilder` and
/// `Ipv6PacketBuilder`.
pub trait IpPacketBuilder<I: IpExt>: PacketBuilder + Clone + Debug {
    /// Returns a new packet builder for an associated IP version with the given
    /// given source and destination IP addresses, TTL (IPv4)/Hop Limit (IPv4)
    /// and Protocol Number.
    fn new(src_ip: I::Addr, dst_ip: I::Addr, ttl: u8, proto: I::Proto) -> Self;

    /// Returns the source IP address for the builder.
    fn src_ip(&self) -> I::Addr;

    /// Returns the destination IP address for the builder.
    fn dst_ip(&self) -> I::Addr;
}

impl<I: IpExt> IpPacketBuilder<I> for Never {
    fn new(_src_ip: I::Addr, _dst_ip: I::Addr, _ttl: u8, _proto: I::Proto) -> Never {
        unreachable!()
    }

    fn src_ip(&self) -> I::Addr {
        unreachable!()
    }

    fn dst_ip(&self) -> I::Addr {
        unreachable!()
    }
}

/// An IPv4 or IPv6 protocol number.
pub trait IpProtocol: From<IpProto> + Sealed {}

impl IpProtocol for Never {}
impl Sealed for Never {}

create_protocol_enum!(
    /// An IPv4 or IPv6 protocol number.
    ///
    /// `IpProto` encodes the protocol numbers whose values are the same for
    /// both IPv4 and IPv6.
    ///
    /// The protocol numbers are maintained [by IANA][protocol-numbers].
    ///
    /// [protocol-numbers]: https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Hash, Eq, PartialEq)]
    pub enum IpProto: u8 {
        Tcp, 6, "TCP";
        Udp, 17, "UDP";
    }
);

impl From<IpProto> for Never {
    fn from(_: IpProto) -> Never {
        unreachable!()
    }
}

create_protocol_enum!(
    /// An IPv4 protocol number.
    ///
    /// The protocol numbers are maintained [by IANA][protocol-numbers].
    ///
    /// [protocol-numbers]: https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Hash, Eq, PartialEq)]
    pub enum Ipv4Proto: u8 {
        Icmp, 1, "ICMP";
        Igmp, 2, "IGMP";
        + Proto(IpProto);
        _, "IPv4 protocol {}";
    }
);

impl IpProtocol for Ipv4Proto {}
impl Sealed for Ipv4Proto {}

create_protocol_enum!(
    /// An IPv6 protocol number.
    ///
    /// The protocol numbers are maintained [by IANA][protocol-numbers].
    ///
    /// [protocol-numbers]: https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Hash, Eq, PartialEq)]
    pub enum Ipv6Proto: u8 {
        Icmpv6, 58, "ICMPv6";
        NoNextHeader, 59, "NO NEXT HEADER";
        + Proto(IpProto);
        _, "IPv6 protocol {}";
    }
);

impl IpProtocol for Ipv6Proto {}
impl Sealed for Ipv6Proto {}

create_protocol_enum!(
    /// An IPv6 extension header.
    ///
    /// These are next header values that encode for extension header types.
    /// This enum does not include upper layer protocol numbers even though they
    /// may be valid next header values.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Hash, Eq, PartialEq)]
    pub enum Ipv6ExtHdrType: u8 {
        HopByHopOptions, 0, "IPv6 HOP-BY-HOP OPTIONS HEADER";
        Routing, 43, "IPv6 ROUTING HEADER";
        Fragment, 44, "IPv6 FRAGMENT HEADER";
        EncapsulatingSecurityPayload, 50, "ENCAPSULATING SECURITY PAYLOAD";
        Authentication, 51, "AUTHENTICATION HEADER";
        DestinationOptions, 60, "IPv6 DESTINATION OPTIONS HEADER";
        _,  "IPv6 EXTENSION HEADER {}";
    }
);
