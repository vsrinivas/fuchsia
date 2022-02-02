// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use arbitrary::{Arbitrary, Result, Unstructured};
use net_types::{
    ethernet::Mac,
    ip::{IpAddress, Ipv4Addr},
};
use packet_formats::{
    ethernet::{EtherType, EthernetFrameBuilder},
    ipv4::Ipv4PacketBuilder,
    udp::UdpPacketBuilder,
};

use crate::{zerocopy::ArbitraryFromBytes, Fuzzed};

impl<'a> Arbitrary<'a> for Fuzzed<EtherType> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        Ok(Self(u16::arbitrary(u)?.into()))
    }
}

impl<'a> Arbitrary<'a> for Fuzzed<EthernetFrameBuilder> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        Ok(Self(EthernetFrameBuilder::new(
            Mac::arbitrary_from_bytes(u)?,
            Mac::arbitrary_from_bytes(u)?,
            Fuzzed::arbitrary(u)?.into(),
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

impl<'a, A: IpAddress + ArbitraryFromBytes<'a>> Arbitrary<'a> for Fuzzed<UdpPacketBuilder<A>> {
    fn arbitrary(u: &mut Unstructured<'a>) -> Result<Self> {
        Ok(Self(UdpPacketBuilder::new(
            A::arbitrary_from_bytes(u)?.into(),
            A::arbitrary_from_bytes(u)?.into(),
            u.arbitrary()?,
            u.arbitrary()?,
        )))
    }
}
