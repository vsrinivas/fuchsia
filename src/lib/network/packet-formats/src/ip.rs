// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IP protocol types.

use alloc::vec::Vec;
use core::fmt::Debug;
use core::marker::PhantomData;

use net_types::ip::{Ip, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
use never::Never;
use packet::{BufferView, BufferViewMut, PacketBuilder, ParsablePacket, ParseMetadata};
use zerocopy::{ByteSlice, ByteSliceMut};

use crate::error::{IpParseError, IpParseResult};
use crate::ipv4::{Ipv4Header, Ipv4Packet, Ipv4PacketBuilder};
use crate::ipv6::{Ipv6Packet, Ipv6PacketBuilder};

mod private {
    use super::*;

    /// Used as a default type for traits. Not exported.
    #[derive(Ord, PartialOrd, Eq, PartialEq, Clone, Debug)]
    pub struct NeverPacket<I: Ip>(never::Never, PhantomData<I>);
}
use private::NeverPacket;

impl<B: ByteSlice, I: Ip, ParseArgs> ParsablePacket<B, ParseArgs> for NeverPacket<I> {
    type Error = IpParseError<I>;

    fn parse<BV: BufferView<B>>(_buffer: BV, _args: ParseArgs) -> Result<Self, Self::Error> {
        unreachable!()
    }

    fn parse_mut<BV: BufferViewMut<B>>(_buffer: BV, _args: ParseArgs) -> Result<Self, Self::Error>
    where
        B: ByteSliceMut,
    {
        unreachable!()
    }

    fn parse_metadata(&self) -> ParseMetadata {
        unreachable!()
    }
}

/// An extension trait to the `Ip` trait adding an associated `PacketBuilder`
/// type.
pub trait IpExt: Ip {
    /// An IP packet builder type for the IP version.
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
pub trait IpExtByteSlice<B: ByteSlice>: IpExt {
    /// An IP packet type for the IP version.
    // TODO(fxbug.dev/48578): This previously had the bound `Builder =
    // Self::PacketBuilder`, but that cannot be satisfied when writing an
    // implementation for NeverPacket<I: Ip>; the only value for
    // Self::PacketBuilder we have is a defaulted type, which could be
    // overridden for some particular value of I.
    type Packet: IpPacket<B, Self>;

    /// Reassembles a fragmented packet into a parsed IP packet.
    fn reassemble_fragmented_packet<BV: BufferViewMut<B>, IT: Iterator<Item = Vec<u8>>>(
        buffer: BV,
        header: Vec<u8>,
        body_fragments: IT,
    ) -> IpParseResult<Self, Self::Packet>
    where
        B: ByteSliceMut;
}

// NOTE(joshlf): We know that this is safe because the Ip trait is sealed to
// only be implemented by Ipv4 and Ipv6.
impl<B: ByteSlice, I: Ip> IpExtByteSlice<B> for I {
    default type Packet = NeverPacket<I>;

    default fn reassemble_fragmented_packet<BV: BufferViewMut<B>, IT: Iterator<Item = Vec<u8>>>(
        _buffer: BV,
        _header: Vec<u8>,
        _body_fragments: IT,
    ) -> IpParseResult<Self, Self::Packet>
    where
        B: ByteSliceMut,
    {
        unimplemented!()
    }
}

impl<B: ByteSlice> IpExtByteSlice<B> for Ipv4 {
    type Packet = Ipv4Packet<B>;

    fn reassemble_fragmented_packet<BV: BufferViewMut<B>, IT: Iterator<Item = Vec<u8>>>(
        buffer: BV,
        header: Vec<u8>,
        body_fragments: IT,
    ) -> IpParseResult<Self, Self::Packet>
    where
        B: ByteSliceMut,
    {
        crate::ipv4::reassemble_fragmented_packet(buffer, header, body_fragments)
    }
}

impl<B: ByteSlice> IpExtByteSlice<B> for Ipv6 {
    type Packet = Ipv6Packet<B>;

    fn reassemble_fragmented_packet<BV: BufferViewMut<B>, IT: Iterator<Item = Vec<u8>>>(
        buffer: BV,
        header: Vec<u8>,
        body_fragments: IT,
    ) -> IpParseResult<Self, Self::Packet>
    where
        B: ByteSliceMut,
    {
        crate::ipv6::reassemble_fragmented_packet(buffer, header, body_fragments)
    }
}

/// An IPv4 or IPv6 packet.
///
/// `IpPacket` is implemented by `Ipv4Packet` and `Ipv6Packet`.
pub trait IpPacket<B: ByteSlice, I: Ip>:
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

impl<B: ByteSlice, I: Ip> IpPacket<B, I> for NeverPacket<I> {
    type Builder = Never;

    fn src_ip(&self) -> I::Addr {
        unreachable!()
    }
    fn dst_ip(&self) -> I::Addr {
        unreachable!()
    }
    fn proto(&self) -> IpProto {
        unreachable!()
    }
    fn ttl(&self) -> u8 {
        unreachable!()
    }
    fn set_ttl(&mut self, _ttl: u8)
    where
        B: ByteSliceMut,
    {
        unreachable!()
    }
    fn body(&self) -> &[u8] {
        unreachable!()
    }
}

/// A builder for IP packets.
///
/// `IpPacketBuilder` is implemented by `Ipv4PacketBuilder` and
/// `Ipv6PacketBuilder`.
pub trait IpPacketBuilder<I: Ip>: PacketBuilder + Clone + Debug {
    /// Returns a new packet builder for an associated IP version with the given
    /// given source and destination IP addresses, TTL (IPv4)/Hop Limit (IPv4)
    /// and Protocol (IPv4)/Next Header (IPv6).
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

impl<I: Ip> IpPacketBuilder<I> for Never {
    fn new(_src_ip: I::Addr, _dst_ip: I::Addr, _ttl: u8, _proto: IpProto) -> Never {
        unreachable!()
    }
}

create_protocol_enum!(
    /// An IP protocol or next header number.
    ///
    /// For IPv4, this is the protocol number. For IPv6, this is the next
    /// header number.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Hash, Eq, PartialEq)]
    pub enum IpProto: u8 {
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
