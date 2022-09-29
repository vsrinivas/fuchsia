// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use arbitrary::{Arbitrary, Result, Unstructured};
use net_types::{
    ethernet::Mac,
    ip::{IpAddress, Ipv4Addr},
};
use packet::{Nested, NestedPacketBuilder as _};
use packet_formats::{
    ethernet::{EtherType, EthernetFrameBuilder, EthernetFrameLengthCheck},
    icmp::IcmpParseArgs,
    ip::{IpExt, IpPacketBuilder},
    ipv4::Ipv4PacketBuilder,
    ipv6::Ipv6PacketBuilder,
    tcp::{TcpParseArgs, TcpSegmentBuilder},
    udp::{UdpPacketBuilder, UdpParseArgs},
};
use zerocopy::FromBytes;

use crate::{zerocopy::ArbitraryFromBytes, Fuzzed};

impl<'a> Arbitrary<'a> for Fuzzed<EtherType> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        Ok(Self(u16::arbitrary(u)?.into()))
    }
}

impl<'a> Arbitrary<'a> for Fuzzed<EthernetFrameLengthCheck> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        const CHOICES: [EthernetFrameLengthCheck; 2] =
            [EthernetFrameLengthCheck::Check, EthernetFrameLengthCheck::NoCheck];
        // Define this with a match to ensure that CHOICES needs to be updated if
        // EthernetFrameLengthCheck is changed.
        u.choose(&CHOICES).map(|e| {
            Self(match e {
                EthernetFrameLengthCheck::Check => EthernetFrameLengthCheck::Check,
                EthernetFrameLengthCheck::NoCheck => EthernetFrameLengthCheck::NoCheck,
            })
        })
    }
}

impl<'a> Arbitrary<'a> for Fuzzed<EthernetFrameBuilder> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        Ok(Self(EthernetFrameBuilder::new(
            Mac::arbitrary_from_bytes(u)?,
            Mac::arbitrary_from_bytes(u)?,
            Fuzzed::<EtherType>::arbitrary(u)?.into(),
        )))
    }
}

impl<'a> Arbitrary<'a> for Fuzzed<Ipv4PacketBuilder> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        let src = Ipv4Addr::arbitrary_from_bytes(u)?;
        let dst = Ipv4Addr::arbitrary_from_bytes(u)?;
        let ttl = u.arbitrary()?;
        let proto = u8::arbitrary(u)?.into();

        let mut builder = Ipv4PacketBuilder::new(src, dst, ttl, proto);
        builder.dscp(u.int_in_range(0..=(1 << 6 - 1))?);
        builder.ecn(u.int_in_range(0..=3)?);
        builder.df_flag(u.arbitrary()?);
        builder.mf_flag(u.arbitrary()?);
        builder.fragment_offset(u.int_in_range(0..=(1 << 13) - 1)?);

        Ok(Self(builder))
    }
}

impl<'a> Arbitrary<'a> for Fuzzed<Ipv6PacketBuilder> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        let src = Ipv4Addr::arbitrary_from_bytes(u)?;
        let dst = Ipv4Addr::arbitrary_from_bytes(u)?;
        let ttl = u.arbitrary()?;
        let proto = u8::arbitrary(u)?.into();

        let mut builder = Ipv6PacketBuilder::new(src, dst, ttl, proto);
        builder.ds(u.int_in_range(0..=(1 << 6 - 1))?);
        builder.ecn(u.int_in_range(0..=3)?);
        builder.flowlabel(u.int_in_range(0..=(1 << 20 - 1))?);

        Ok(Self(builder))
    }
}

impl<'a, A: IpAddress + FromBytes> Arbitrary<'a> for Fuzzed<IcmpParseArgs<A>> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        let src = A::arbitrary_from_bytes(u)?;
        let dst = A::arbitrary_from_bytes(u)?;
        Ok(Self(IcmpParseArgs::new(src, dst)))
    }
}

impl<'a, A: IpAddress + FromBytes> Arbitrary<'a> for Fuzzed<UdpParseArgs<A>> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        let src = A::arbitrary_from_bytes(u)?;
        let dst = A::arbitrary_from_bytes(u)?;
        Ok(Self(UdpParseArgs::new(src, dst)))
    }
}

impl<'a, A: IpAddress + FromBytes> Arbitrary<'a> for Fuzzed<TcpParseArgs<A>> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        let src = A::arbitrary_from_bytes(u)?;
        let dst = A::arbitrary_from_bytes(u)?;
        Ok(Self(TcpParseArgs::new(src, dst)))
    }
}

// Since UDP and TCP packets have a checksum that includes parameters from the
// IP layer (source and destination addresses), generate UDP and TCP packet
// builders as Nested<UDP, I> or Nested<TCP, I> where I is the IP layer builder.
// This ensures that the inner transport builder uses the correct addresses.

impl<'a, A: IpAddress, B: IpPacketBuilder<A::Version>> Arbitrary<'a>
    for Fuzzed<Nested<UdpPacketBuilder<A>, B>>
where
    A::Version: IpExt,
    Fuzzed<B>: Arbitrary<'a>,
{
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        let ip_builder: B = Fuzzed::<B>::arbitrary(u)?.into();
        let udp_builder = UdpPacketBuilder::new(
            ip_builder.src_ip(),
            ip_builder.dst_ip(),
            u.arbitrary()?,
            u.arbitrary()?,
        );
        Ok(Self(udp_builder.encapsulate(ip_builder)))
    }
}

impl<'a, A: IpAddress, B: IpPacketBuilder<A::Version>> Arbitrary<'a>
    for Fuzzed<Nested<TcpSegmentBuilder<A>, B>>
where
    A::Version: IpExt,
    Fuzzed<B>: Arbitrary<'a>,
{
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        let ip_builder: B = Fuzzed::<B>::arbitrary(u)?.into();
        let mut tcp_builder = TcpSegmentBuilder::new(
            ip_builder.src_ip(),
            ip_builder.dst_ip(),
            u.arbitrary()?,
            u.arbitrary()?,
            u.arbitrary()?,
            u.arbitrary()?,
            u.arbitrary()?,
        );
        tcp_builder.psh(u.arbitrary()?);
        tcp_builder.rst(u.arbitrary()?);
        tcp_builder.syn(u.arbitrary()?);
        tcp_builder.fin(u.arbitrary()?);

        Ok(Self(tcp_builder.encapsulate(ip_builder)))
    }
}
