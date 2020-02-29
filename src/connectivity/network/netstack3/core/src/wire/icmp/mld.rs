// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Multicast Listener Discovery Protocol.
//!
//! Wire serialization and deserialization functions.

use core::convert::TryFrom;
use core::fmt::Debug;
use core::mem::size_of;
use core::ops::Deref;
use core::result::Result;
use core::time::Duration;

use net_types::ip::{Ipv6, Ipv6Addr};
use net_types::MulticastAddr;
use packet::serialize::InnerPacketBuilder;
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::wire::icmp::{IcmpIpExt, IcmpMessage, IcmpPacket, IcmpUnusedCode, MessageBody};
use crate::wire::U16;

/// An ICMPv6 packet with an MLD message.
#[allow(missing_docs)]
pub(crate) enum MldPacket<B: ByteSlice> {
    MulticastListenerQuery(IcmpPacket<Ipv6, B, MulticastListenerQuery>),
    MulticastListenerReport(IcmpPacket<Ipv6, B, MulticastListenerReport>),
    MulticastListenerDone(IcmpPacket<Ipv6, B, MulticastListenerDone>),
}

/// Multicast Listener Query V1 Message.
#[repr(C)]
#[derive(AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub(crate) struct MulticastListenerQuery;

/// Multicast Listener Report V1 Message.
#[repr(C)]
#[derive(AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub(crate) struct MulticastListenerReport;

/// Multicast Listener Done V1 Message.
#[repr(C)]
#[derive(AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub(crate) struct MulticastListenerDone;

/// MLD Errors.
#[derive(Debug)]
pub(crate) enum MldError {
    /// Raised when `MaxRespCode` cannot fit in `u16`.
    MaxRespCodeOverflow,
}

/// The trait for all MLDv1 Messages.
pub(crate) trait Mldv1MessageType {
    /// The type used to represent maximum response delay.
    ///
    /// It should be `()` for Report and Done messages,
    /// and be `Mldv1ResponseDelay` for Query messages.
    type MaxRespDelay: MaxRespCode + Debug + Copy;
    /// The type used to represent the group_addr in the message.
    ///
    /// For Query Messages, it is just `Ipv6Addr` because
    /// general queries will have this field to be zero, which
    /// is not a multicast address, for Report and Done messages,
    /// this should be `MulticastAddr<Ipv6Addr>`.
    type GroupAddr: Into<Ipv6Addr> + Debug + Copy;
}

pub(crate) trait IcmpMldv1MessageType<B: ByteSlice>:
    Mldv1MessageType + IcmpMessage<Ipv6, B, Code = IcmpUnusedCode>
{
}

/// The trait for MLD maximum response codes.
///
/// The type implementing this trait should be able
/// to convert itself from/to `U16`
pub(crate) trait MaxRespCode {
    /// Convert to `U16`
    fn as_code(self) -> U16;

    /// Convert from `U16`
    fn from_code(code: U16) -> Self;
}

impl MaxRespCode for () {
    fn as_code(self) -> U16 {
        U16::ZERO
    }

    fn from_code(_: U16) -> Self {}
}

/// Maximum Response Delay used in Query messages.
#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub(crate) struct Mldv1ResponseDelay(u16);

impl MaxRespCode for Mldv1ResponseDelay {
    fn as_code(self) -> U16 {
        U16::new(self.0)
    }

    fn from_code(code: U16) -> Self {
        Mldv1ResponseDelay(code.get())
    }
}

impl From<Mldv1ResponseDelay> for Duration {
    fn from(code: Mldv1ResponseDelay) -> Self {
        Duration::from_millis(code.0.into())
    }
}

impl TryFrom<Duration> for Mldv1ResponseDelay {
    type Error = MldError;
    fn try_from(period: Duration) -> Result<Self, Self::Error> {
        Ok(Mldv1ResponseDelay(
            u16::try_from(period.as_millis()).map_err(|_| MldError::MaxRespCodeOverflow)?,
        ))
    }
}

/// The layout for an MLDv1 message body.
#[repr(C)]
#[derive(AsBytes, FromBytes, Unaligned, Copy, Clone, Debug)]
pub(crate) struct Mldv1Message {
    /// Max Response Delay, in units of milliseconds.
    pub(crate) max_response_delay: U16,
    /// Initialized to zero by the sender; ignored by receivers.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    reserved: U16,
    /// In a Query message, the Multicast Address field is set to zero when
    /// sending a General Query, and set to a specific IPv6 multicast address
    /// when sending a Multicast-Address-Specific Query.
    ///
    /// In a Report or Done message, the Multicast Address field holds a
    /// specific IPv6 multicast address to which the message sender is
    /// listening or is ceasing to listen, respectively.
    pub(crate) group_addr: Ipv6Addr,
}

impl Mldv1Message {
    pub(crate) fn max_response_delay(&self) -> Duration {
        Mldv1ResponseDelay(self.max_response_delay.get()).into()
    }
}

/// The on-wire structure for the body of an MLDv1 message.
#[derive(Debug)]
pub(crate) struct Mldv1Body<B: ByteSlice>(LayoutVerified<B, Mldv1Message>);

impl<B: ByteSlice> Deref for Mldv1Body<B> {
    type Target = Mldv1Message;

    fn deref(&self) -> &Self::Target {
        return &*self.0;
    }
}

impl<B: ByteSlice> MessageBody<B> for Mldv1Body<B> {
    fn parse(bytes: B) -> ParseResult<Self> {
        LayoutVerified::new(bytes).map_or(Err(ParseError::Format), |body| Ok(Mldv1Body(body)))
    }

    fn len(&self) -> usize {
        self.bytes().len()
    }

    fn bytes(&self) -> &[u8] {
        self.0.bytes()
    }
}

macro_rules! impl_mldv1_message {
    ($msg:ident, $resp_code:ty, $group_addr:ty) => {
        impl_icmp_message!(Ipv6, $msg, $msg, IcmpUnusedCode, Mldv1Body<B>);
        impl Mldv1MessageType for $msg {
            type MaxRespDelay = $resp_code;
            type GroupAddr = $group_addr;
        }
        impl<B: ByteSlice> IcmpMldv1MessageType<B> for $msg {}
    };
}

impl_mldv1_message!(MulticastListenerQuery, Mldv1ResponseDelay, Ipv6Addr);
impl_mldv1_message!(MulticastListenerReport, (), MulticastAddr<Ipv6Addr>);
impl_mldv1_message!(MulticastListenerDone, (), MulticastAddr<Ipv6Addr>);

/// The builder for MLDv1 Messages.
#[derive(Debug)]
pub(crate) struct Mldv1MessageBuilder<M: Mldv1MessageType> {
    max_resp_delay: M::MaxRespDelay,
    group_addr: M::GroupAddr,
}

impl<M: Mldv1MessageType<MaxRespDelay = ()>> Mldv1MessageBuilder<M> {
    /// Create an `Mldv1MessageBuilder` without a `max_resp_delay`
    /// for Report and Done messages.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn new(group_addr: M::GroupAddr) -> Self {
        Mldv1MessageBuilder { max_resp_delay: (), group_addr }
    }
}

impl<M: Mldv1MessageType> Mldv1MessageBuilder<M> {
    /// Create an `Mldv1MessageBuilder` with a `max_resp_delay`
    /// for Query messages.
    pub(crate) fn new_with_max_resp_delay(
        group_addr: M::GroupAddr,
        max_resp_delay: M::MaxRespDelay,
    ) -> Self {
        Mldv1MessageBuilder { max_resp_delay, group_addr }
    }

    fn serialize_message(&self, mut buf: &mut [u8]) {
        use packet::BufferViewMut;

        let mut bytes = &mut buf;
        let mut message =
            bytes.take_obj_front_zero::<Mldv1Message>().expect("Too few bytes for MLDv1 message");

        message.max_response_delay = self.max_resp_delay.as_code();
        message.group_addr = self.group_addr.into();
    }
}

impl<M: Mldv1MessageType> InnerPacketBuilder for Mldv1MessageBuilder<M> {
    fn bytes_len(&self) -> usize {
        size_of::<Mldv1Message>()
    }

    fn serialize(&self, buf: &mut [u8]) {
        self.serialize_message(buf);
    }
}

#[cfg(test)]
mod tests {
    use net_types::Witness;
    use packet::{InnerPacketBuilder, ParseBuffer, Serializer};
    use std::convert::TryInto;
    use std::fmt::Debug;

    use super::*;
    use crate::ip::IpProto;
    use crate::wire::icmp::{IcmpPacket, IcmpPacketBuilder, IcmpParseArgs, MessageBody};
    use crate::wire::ipv6::ext_hdrs::{
        ExtensionHeaderOptionAction, HopByHopOption, HopByHopOptionData, Ipv6ExtensionHeaderData,
    };
    use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder, Ipv6PacketBuilderWithHBHOptions};

    fn serialize_to_bytes<
        B: ByteSlice + Debug,
        M: IcmpMessage<Ipv6, B> + Mldv1MessageType + Debug,
    >(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        icmp: &IcmpPacket<Ipv6, B, M>,
    ) -> Vec<u8> {
        let ip = Ipv6PacketBuilder::new(src_ip, dst_ip, 1, IpProto::Icmpv6);
        let with_options = Ipv6PacketBuilderWithHBHOptions::new(
            ip,
            &[HopByHopOption {
                action: ExtensionHeaderOptionAction::SkipAndContinue,
                mutable: false,
                data: HopByHopOptionData::RouterAlert { data: 0 },
            }],
        )
        .unwrap();
        icmp.message_body
            .bytes()
            .into_serializer()
            .encapsulate(icmp.builder(src_ip, dst_ip))
            .encapsulate(with_options)
            .serialize_vec_outer()
            .unwrap()
            .as_ref()
            .to_vec()
    }

    fn test_parse_and_serialize<
        M: for<'a> IcmpMessage<Ipv6, &'a [u8]> + Mldv1MessageType + Debug,
        F: FnOnce(&Ipv6Packet<&[u8]>),
        G: for<'a> FnOnce(&IcmpPacket<Ipv6, &'a [u8], M>),
    >(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        mut req: &[u8],
        check_ip: F,
        check_icmp: G,
    ) {
        let orig_req = &req[..];

        let ip = req.parse_with::<_, Ipv6Packet<_>>(()).unwrap();
        check_ip(&ip);
        let icmp =
            req.parse_with::<_, IcmpPacket<_, _, M>>(IcmpParseArgs::new(src_ip, dst_ip)).unwrap();
        check_icmp(&icmp);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp);
        assert_eq!(&data[..], orig_req);
    }

    fn serialize_to_bytes_with_builder<B: ByteSlice + Debug, M: IcmpMldv1MessageType<B> + Debug>(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        msg: M,
        group_addr: M::GroupAddr,
        max_resp_delay: M::MaxRespDelay,
    ) -> Vec<u8> {
        let ip = Ipv6PacketBuilder::new(src_ip, dst_ip, 1, IpProto::Icmpv6);
        let with_options = Ipv6PacketBuilderWithHBHOptions::new(
            ip,
            &[HopByHopOption {
                action: ExtensionHeaderOptionAction::SkipAndContinue,
                mutable: false,
                data: HopByHopOptionData::RouterAlert { data: 0 },
            }],
        )
        .unwrap();
        // Serialize an MLD(ICMPv6) packet using the builder.
        Mldv1MessageBuilder::<M>::new_with_max_resp_delay(group_addr, max_resp_delay)
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::new(src_ip, dst_ip, IcmpUnusedCode, msg))
            .encapsulate(with_options)
            .serialize_vec_outer()
            .unwrap()
            .as_ref()
            .to_vec()
    }

    fn check_ip<B: ByteSlice>(ip: &Ipv6Packet<B>, src_ip: Ipv6Addr, dst_ip: Ipv6Addr) {
        assert_eq!(ip.src_ip(), src_ip);
        assert_eq!(ip.dst_ip(), dst_ip);
        assert_eq!(ip.iter_extension_hdrs().count(), 1);
        let hbh = ip.iter_extension_hdrs().nth(0).unwrap();
        match hbh.data() {
            Ipv6ExtensionHeaderData::HopByHopOptions { options } => {
                assert_eq!(options.iter().count(), 1);
                assert_eq!(
                    options.iter().nth(0).unwrap(),
                    HopByHopOption {
                        action: ExtensionHeaderOptionAction::SkipAndContinue,
                        mutable: false,
                        data: HopByHopOptionData::RouterAlert { data: 0 },
                    }
                );
            }
            _ => panic!("Wrong extension header"),
        }
    }

    fn check_icmp<
        B: ByteSlice,
        M: IcmpMessage<Ipv6, B, Body = Mldv1Body<B>> + Mldv1MessageType + Debug,
    >(
        icmp: &IcmpPacket<Ipv6, B, M>,
        max_resp_code: u16,
        group_addr: Ipv6Addr,
    ) {
        assert_eq!(icmp.message_body.reserved.get(), 0);
        assert_eq!(icmp.message_body.max_response_delay.get(), max_resp_code);
        assert_eq!(icmp.message_body.group_addr, group_addr);
    }

    #[test]
    fn test_mld_parse_and_serialize_query() {
        use crate::wire::icmp::mld::MulticastListenerQuery;
        use crate::wire::testdata::mld_router_query::*;
        test_parse_and_serialize::<MulticastListenerQuery, _, _>(
            SRC_IP,
            DST_IP,
            QUERY,
            |ip| {
                check_ip(ip, SRC_IP, DST_IP);
            },
            |icmp| {
                check_icmp(icmp, MAX_RESP_CODE, HOST_GROUP_ADDRESS);
            },
        );
    }

    #[test]
    fn test_mld_parse_and_serialize_report() {
        use crate::wire::icmp::mld::MulticastListenerReport;
        use crate::wire::testdata::mld_router_report::*;
        test_parse_and_serialize::<MulticastListenerReport, _, _>(
            SRC_IP,
            DST_IP,
            REPORT,
            |ip| {
                check_ip(ip, SRC_IP, DST_IP);
            },
            |icmp| {
                check_icmp(icmp, 0, HOST_GROUP_ADDRESS);
            },
        );
    }

    #[test]
    fn test_mld_parse_and_serialize_done() {
        use crate::wire::icmp::mld::MulticastListenerDone;
        use crate::wire::testdata::mld_router_done::*;
        test_parse_and_serialize::<MulticastListenerDone, _, _>(
            SRC_IP,
            DST_IP,
            DONE,
            |ip| {
                check_ip(ip, SRC_IP, DST_IP);
            },
            |icmp| {
                check_icmp(icmp, 0, HOST_GROUP_ADDRESS);
            },
        );
    }

    #[test]
    fn test_mld_serialize_and_parse_query() {
        use crate::wire::icmp::mld::MulticastListenerQuery;
        use crate::wire::testdata::mld_router_query::*;
        let bytes = serialize_to_bytes_with_builder::<&[u8], _>(
            SRC_IP,
            DST_IP,
            MulticastListenerQuery,
            HOST_GROUP_ADDRESS,
            Duration::from_secs(1).try_into().unwrap(),
        );
        assert_eq!(&bytes[..], QUERY);
        let mut req = &bytes[..];
        let ip = req.parse_with::<_, Ipv6Packet<_>>(()).unwrap();
        check_ip(&ip, SRC_IP, DST_IP);
        let icmp = req
            .parse_with::<_, IcmpPacket<_, _, MulticastListenerQuery>>(IcmpParseArgs::new(
                SRC_IP, DST_IP,
            ))
            .unwrap();
        check_icmp(&icmp, MAX_RESP_CODE, HOST_GROUP_ADDRESS);
    }

    #[test]
    fn test_mld_serialize_and_parse_report() {
        use crate::wire::icmp::mld::MulticastListenerReport;
        use crate::wire::testdata::mld_router_report::*;
        let bytes = serialize_to_bytes_with_builder::<&[u8], _>(
            SRC_IP,
            DST_IP,
            MulticastListenerReport,
            MulticastAddr::new(HOST_GROUP_ADDRESS).unwrap(),
            (),
        );
        assert_eq!(&bytes[..], REPORT);
        let mut req = &bytes[..];
        let ip = req.parse_with::<_, Ipv6Packet<_>>(()).unwrap();
        check_ip(&ip, SRC_IP, DST_IP);
        let icmp = req
            .parse_with::<_, IcmpPacket<_, _, MulticastListenerReport>>(IcmpParseArgs::new(
                SRC_IP, DST_IP,
            ))
            .unwrap();
        check_icmp(&icmp, 0, HOST_GROUP_ADDRESS);
    }

    #[test]
    fn test_mld_serialize_and_parse_done() {
        use crate::wire::icmp::mld::MulticastListenerDone;
        use crate::wire::testdata::mld_router_done::*;
        let bytes = serialize_to_bytes_with_builder::<&[u8], _>(
            SRC_IP,
            DST_IP,
            MulticastListenerDone,
            MulticastAddr::new(HOST_GROUP_ADDRESS).unwrap(),
            (),
        );
        assert_eq!(&bytes[..], DONE);
        let mut req = &bytes[..];
        let ip = req.parse_with::<_, Ipv6Packet<_>>(()).unwrap();
        check_ip(&ip, SRC_IP, DST_IP);
        let icmp = req
            .parse_with::<_, IcmpPacket<_, _, MulticastListenerDone>>(IcmpParseArgs::new(
                SRC_IP, DST_IP,
            ))
            .unwrap();
        check_icmp(&icmp, 0, HOST_GROUP_ADDRESS);
    }
}
