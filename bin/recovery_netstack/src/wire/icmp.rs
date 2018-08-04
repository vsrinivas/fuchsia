// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of ICMP packets

use byteorder::{ByteOrder, NetworkEndian};
use error::ParseError;
use ip::Ipv4Addr;
use std::cmp::min;
use wire::util::Checksum;
use wire::{ipv4, BufferAndRange};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

macro_rules! create_net_enum {
    ($t:ident, $($val:ident: $const:ident = $value:expr,)*) => {
      create_net_enum!($t, $($val: $const = $value),*);
    };

    ($t:ident, $($val:ident: $const:ident = $value:expr),*) => {
      #[allow(missing_docs)]
      #[derive(Debug, PartialEq, Copy, Clone)]
      #[repr(u8)]
      pub enum $t {
        $($val = $t::$const),*
      }

      impl $t {
        $(const $const: u8 = $value;)*

        fn from_u8(u: u8) -> Option<$t> {
          match u {
            $($t::$const => Some($t::$val)),*,
            _ => None,
          }
        }
      }
    };
}

create_net_enum! {
  Icmpv4Type,
  EchoReply: ECHO_REPLY = 0,
  DestUnreachable: DEST_UNREACHABLE = 3,
  Redirect: REDIRECT = 5,
  EchoRequest: ECHO_REQUEST = 8,
  TimeExceeded: TIME_EXCEEDED = 11,
  ParameterProblem: PARAMETER_PROBLEM = 12,
  TimestampRequest: TIMESTAMP_REQUEST = 13,
  TimestampReply: TIMESTAMP_REPLY = 14,
}

#[allow(missing_docs)]
#[repr(C, packed)]
struct Header {
    msg_type: u8,
    code: u8,
    checksum: [u8; 2],
}

unsafe impl FromBytes for Header {}
unsafe impl AsBytes for Header {}
unsafe impl Unaligned for Header {}

impl Header {
    fn msg_type(&self) -> Result<Icmpv4Type, u8> {
        Icmpv4Type::from_u8(self.msg_type).ok_or(self.msg_type)
    }

    fn set_msg_type(&mut self, msg_type: Icmpv4Type) {
        self.msg_type = msg_type as u8;
    }

    fn checksum(&self) -> u16 {
        NetworkEndian::read_u16(&self.checksum)
    }
}

#[repr(C, packed)]
struct IdAndSeq {
    id: [u8; 2],
    seq: [u8; 2],
}

impl IdAndSeq {
    fn id(&self) -> u16 {
        NetworkEndian::read_u16(&self.id)
    }

    fn set_id(&mut self, id: u16) {
        NetworkEndian::write_u16(&mut self.id, id);
    }

    fn seq(&self) -> u16 {
        NetworkEndian::read_u16(&self.seq)
    }

    fn set_seq(&mut self, seq: u16) {
        NetworkEndian::write_u16(&mut self.seq, seq);
    }
}

unsafe impl FromBytes for IdAndSeq {}
unsafe impl AsBytes for IdAndSeq {}
unsafe impl Unaligned for IdAndSeq {}

#[repr(C, packed)]
#[derive(Debug, PartialEq)]
struct IcmpOriginData([u8; 8]);

impl IcmpOriginData {
    fn data(&self) -> u64 {
        NetworkEndian::read_u64(&self.0)
    }

    fn set_data(&mut self, data: u64) {
        NetworkEndian::write_u64(&mut self.0, data);
    }
}

unsafe impl FromBytes for IcmpOriginData {}
unsafe impl AsBytes for IcmpOriginData {}
unsafe impl Unaligned for IcmpOriginData {}

/// ICMP echo request/reply packet.
pub struct Icmpv4Echo<B> {
    id_seq: LayoutVerified<B, IdAndSeq>,
    data: B,
}

impl<B: ByteSlice> Icmpv4Echo<B> {
    fn parse(bytes: B) -> Result<Icmpv4Echo<B>, ParseError> {
        let (id_seq, data) =
            LayoutVerified::<B, IdAndSeq>::new_from_prefix(bytes).ok_or(ParseError::Format)?;
        Ok(Icmpv4Echo { id_seq, data })
    }

    fn size(&self) -> usize {
        self.id_seq.bytes().len() + self.data.len()
    }

    /// Constructs an ICMPv4 echo request packet.
    pub fn request(self) -> Icmpv4Packet<B> {
        Icmpv4Packet::new(Icmpv4Body::EchoRequest(self))
    }

    /// Constructs an ICMPv4 echo reply packet.
    pub fn reply(self) -> Icmpv4Packet<B> {
        Icmpv4Packet::new(Icmpv4Body::EchoReply(self))
    }

    fn serialize<C: AsMut<[u8]>>(self, mut buffer: BufferAndRange<C>) -> BufferAndRange<C> {
        let (_, body, _) = buffer.parts_mut();

        let (mut id_seq, body) =
            LayoutVerified::<_, IdAndSeq>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for IdAndSeq");
        id_seq.bytes_mut().clone_from_slice(self.id_seq.bytes());

        let len = min(self.data.len(), body.len());
        body[..len].clone_from_slice(&self.data[..len]);

        buffer
    }
}

create_net_enum! {
  Icmpv4DestUnreachableCode,
  DestNetworkUnreachable: DEST_NETWORK_UNREACHABLE = 0,
  DestHostUnreachable: DEST_HOST_UNREACHABLE = 1,
  DestProtocolUnreachable: DEST_PROTOCOL_UNREACHABLE = 2,
  DestPortUnreachable: DEST_PORT_UNREACHABLE = 3,
  FragmentationRequired: FRAGMENTATION_REQUIRED = 4,
  SourceRouteFailed: SOURCE_ROUTE_FAILED = 5,
  DestNetworkUnknown: DEST_NETWORK_UNKNOWN = 6,
  DestHostUnknown: DEST_HOST_UNKNOWN = 7,
  SourceHostIsolated: SOURCE_HOST_ISOLATED = 8,
  NetworkAdministrativelyProhibited: NETWORK_ADMINISTRATIVELY_PROHIBITED = 9,
  HostAdministrativelyProhibited: HOST_ADMINISTRATIVELY_PROHIBITED = 10,
  NetworkUnreachableForToS: NETWORK_UNREACHABLE_FOR_TOS = 11,
  HostUnreachableForToS: HOST_UNREACHABLE_FOR_TOS = 12,
  CommunicationAdministrativelyProhibited: COMMUNICATION_ADMINISTRATIVELY_PROHIBITED = 13,
  HostPrecedenceViolation: HOST_PRECEDENCE_VIOLATION = 14,
  PrecedenceCutoffInEffect: PRECEDENCE_CUTOFF_IN_EFFECT = 15,
}

/// ICMP destination unreachable packet.
pub struct Icmpv4DestUnreachable<B> {
    code: Icmpv4DestUnreachableCode,
    internet_header: LayoutVerified<B, ipv4::HeaderPrefix>,
    origin_data: LayoutVerified<B, IcmpOriginData>,
}

impl<B: ByteSlice> Icmpv4DestUnreachable<B> {
    fn parse(bytes: B, code: u8) -> Result<Icmpv4DestUnreachable<B>, ParseError> {
        // Eat the 4 unused bytes at the start of the packet.
        let (_, bytes) =
            LayoutVerified::<B, [u8; 4]>::new_from_prefix(bytes).ok_or(ParseError::Format)?;
        let (internet_header, bytes) =
            LayoutVerified::<B, ipv4::HeaderPrefix>::new_from_prefix(bytes)
                .ok_or(ParseError::Format)?;
        let origin_data =
            LayoutVerified::<B, IcmpOriginData>::new(bytes).ok_or(ParseError::Format)?;
        let code = Icmpv4DestUnreachableCode::from_u8(code).ok_or(ParseError::Format)?;
        Ok(Icmpv4DestUnreachable {
            code,
            internet_header,
            origin_data,
        })
    }

    fn size(&self) -> usize {
        4 /* 4 unused bytes */ + self.internet_header.bytes().len() + self.origin_data.bytes().len()
    }

    fn serialize<C: AsMut<[u8]>>(self, mut buffer: BufferAndRange<C>) -> BufferAndRange<C> {
        let (_, body, _) = buffer.parts_mut();

        let (_, body) = LayoutVerified::<_, [u8; 4]>::new_unaligned_from_prefix(body)
            .expect("Unable to allocate data for 4 unused bytes");

        let (mut internet_header, body) =
            LayoutVerified::<_, ipv4::HeaderPrefix>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for ipv4::HeaderPrefix");
        internet_header
            .bytes_mut()
            .clone_from_slice(self.internet_header.bytes());

        let (mut origin_data, _) =
            LayoutVerified::<_, IcmpOriginData>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for IcmpOriginData");
        origin_data
            .bytes_mut()
            .clone_from_slice(self.origin_data.bytes());

        buffer
    }
}

create_net_enum! {
  Icmpv4RedirectCode,
  RedirectForNetwork: REDIRECT_FOR_NETWORK = 0,
  RedirectForHost: REDIRECT_FOR_HOST = 1,
  RedirectForToSNetwork: REDIRECT_FOR_TOS_NETWORK = 2,
  RedirectForToSHost: REDIRECT_FOR_TOS_HOST = 3,
}

/// ICMP redirect packet.
pub struct Icmpv4Redirect<B> {
    code: Icmpv4RedirectCode,
    gateway: LayoutVerified<B, Ipv4Addr>,
    internet_header: LayoutVerified<B, ipv4::HeaderPrefix>,
    origin_data: LayoutVerified<B, IcmpOriginData>,
}

impl<B: ByteSlice> Icmpv4Redirect<B> {
    fn parse(bytes: B, code: u8) -> Result<Icmpv4Redirect<B>, ParseError> {
        let (gateway, bytes) =
            LayoutVerified::<B, Ipv4Addr>::new_from_prefix(bytes).ok_or(ParseError::Format)?;
        let (internet_header, bytes) =
            LayoutVerified::<B, ipv4::HeaderPrefix>::new_from_prefix(bytes)
                .ok_or(ParseError::Format)?;
        let origin_data =
            LayoutVerified::<B, IcmpOriginData>::new(bytes).ok_or(ParseError::Format)?;
        let code = Icmpv4RedirectCode::from_u8(code).ok_or(ParseError::Format)?;
        Ok(Icmpv4Redirect {
            code,
            gateway,
            internet_header,
            origin_data,
        })
    }

    fn size(&self) -> usize {
        self.gateway.bytes().len()
            + self.internet_header.bytes().len()
            + self.origin_data.bytes().len()
    }

    fn serialize<C: AsMut<[u8]>>(self, mut buffer: BufferAndRange<C>) -> BufferAndRange<C> {
        let (_, body, _) = buffer.parts_mut();

        let (mut gateway, body) =
            LayoutVerified::<_, Ipv4Addr>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for Ipv4Addr");
        gateway.bytes_mut().clone_from_slice(self.gateway.bytes());

        let (mut internet_header, body) =
            LayoutVerified::<_, ipv4::HeaderPrefix>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for ipv4::HeaderPrefix");
        internet_header
            .bytes_mut()
            .clone_from_slice(self.internet_header.bytes());

        let (mut origin_data, _) =
            LayoutVerified::<_, IcmpOriginData>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for ipv4::HeaderPrefix");
        origin_data
            .bytes_mut()
            .clone_from_slice(self.origin_data.bytes());

        buffer
    }
}

create_net_enum! {
  Icmpv4TimeExceededCode,
  TTLExpired: TTL_EXPIRED = 0,
  FragmentReassemblyTimeExceeded: FRAGMENT_REASSEMBLY_TIME_EXCEEDED = 1,
}

/// ICMP time exceeded packet.
pub struct Icmpv4TimeExceeded<B> {
    code: Icmpv4TimeExceededCode,
    internet_header: LayoutVerified<B, ipv4::HeaderPrefix>,
    origin_data: LayoutVerified<B, IcmpOriginData>,
}

impl<B: ByteSlice> Icmpv4TimeExceeded<B> {
    fn parse(bytes: B, code: u8) -> Result<Icmpv4TimeExceeded<B>, ParseError> {
        // Eat the 4 unused bytes at the start of the packet
        let (_, bytes) =
            LayoutVerified::<B, [u8; 4]>::new_from_prefix(bytes).ok_or(ParseError::Format)?;
        let (internet_header, bytes) =
            LayoutVerified::<B, ipv4::HeaderPrefix>::new_from_prefix(bytes)
                .ok_or(ParseError::Format)?;
        let origin_data =
            LayoutVerified::<B, IcmpOriginData>::new(bytes).ok_or(ParseError::Format)?;
        let code = Icmpv4TimeExceededCode::from_u8(code).ok_or(ParseError::Format)?;
        Ok(Icmpv4TimeExceeded {
            code,
            internet_header,
            origin_data,
        })
    }

    fn size(&self) -> usize {
        4 /* 4 unused bytes */ + self.internet_header.bytes().len() + self.origin_data.bytes().len()
    }

    fn serialize<C: AsMut<[u8]>>(self, mut buffer: BufferAndRange<C>) -> BufferAndRange<C> {
        let (_, body, _) = buffer.parts_mut();

        // Eat 4 unused bytes
        let (_, body) = LayoutVerified::<_, [u8; 4]>::new_unaligned_from_prefix(body)
            .expect("Unable to allocate data for 4 unused bytes");

        let (mut internet_header, body) =
            LayoutVerified::<_, ipv4::HeaderPrefix>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for ipv4::HeaderPrefix");
        internet_header
            .bytes_mut()
            .clone_from_slice(self.internet_header.bytes());

        let (mut origin_data, _) =
            LayoutVerified::<_, IcmpOriginData>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for IcmpOriginData");
        origin_data
            .bytes_mut()
            .clone_from_slice(self.origin_data.bytes());

        buffer
    }
}

create_net_enum! {
  Icmpv4ParameterProblemCode,
  PointerIndicatesError: POINTER_INDICATES_ERROR = 0,
  MissingRequiredOption: MISSING_REQUIRED_OPTION = 1,
  BadLength: BAD_LENGTH = 2,
}

/// ICMP parameter problem packet.
pub struct Icmpv4ParameterProblem<B> {
    code: Icmpv4ParameterProblemCode,
    pointer: LayoutVerified<B, u8>,
    internet_header: LayoutVerified<B, ipv4::HeaderPrefix>,
    origin_data: LayoutVerified<B, IcmpOriginData>,
}

impl<B: ByteSlice> Icmpv4ParameterProblem<B> {
    fn parse(bytes: B, code: u8) -> Result<Icmpv4ParameterProblem<B>, ParseError> {
        let (pointer, bytes) =
            LayoutVerified::<B, u8>::new_from_prefix(bytes).ok_or(ParseError::Format)?;
        let (_, bytes) =
            LayoutVerified::<B, [u8; 3]>::new_from_prefix(bytes).ok_or(ParseError::Format)?;
        let (internet_header, bytes) =
            LayoutVerified::<B, ipv4::HeaderPrefix>::new_from_prefix(bytes)
                .ok_or(ParseError::Format)?;
        let origin_data =
            LayoutVerified::<B, IcmpOriginData>::new(bytes).ok_or(ParseError::Format)?;
        let code = Icmpv4ParameterProblemCode::from_u8(code).ok_or(ParseError::Format)?;
        Ok(Icmpv4ParameterProblem {
            code,
            pointer,
            internet_header,
            origin_data,
        })
    }

    fn size(&self) -> usize {
        1 /* pointer */ + 3 /* unused bytes */ + self.internet_header.bytes().len() + self.origin_data.bytes().len()
    }

    fn serialize<C: AsMut<[u8]>>(self, mut buffer: BufferAndRange<C>) -> BufferAndRange<C> {
        let (_, body, _) = buffer.parts_mut();

        let (mut pointer, body) = LayoutVerified::<_, u8>::new_unaligned_from_prefix_zeroed(body)
            .expect("Unable to allocate data for pointer");
        pointer.bytes_mut().clone_from_slice(self.pointer.bytes());

        let (_, body) = LayoutVerified::<_, [u8; 3]>::new_unaligned_from_prefix_zeroed(body)
            .expect("Unable to allocate data for Unused 3 bytes");

        let (mut internet_header, body) =
            LayoutVerified::<_, ipv4::HeaderPrefix>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for ipv4::HeaderPrefix");
        internet_header
            .bytes_mut()
            .clone_from_slice(self.internet_header.bytes());

        let (mut origin_data, _) =
            LayoutVerified::<_, IcmpOriginData>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for IcmpOriginData");
        origin_data
            .bytes_mut()
            .clone_from_slice(self.origin_data.bytes());

        buffer
    }
}

#[repr(C, packed)]
struct IcmpTimestampData {
    origin_timestamp: [u8; 4],
    recv_timestamp: [u8; 4],
    tx_timestamp: [u8; 4],
}

impl IcmpTimestampData {
    fn origin_timestamp(&self) -> u32 {
        NetworkEndian::read_u32(&self.origin_timestamp)
    }

    fn recv_timestamp(&self) -> u32 {
        NetworkEndian::read_u32(&self.recv_timestamp)
    }

    fn tx_timestamp(&self) -> u32 {
        NetworkEndian::read_u32(&self.tx_timestamp)
    }

    fn set_origin_timestamp(&mut self, timestamp: u32) {
        NetworkEndian::write_u32(&mut self.origin_timestamp, timestamp)
    }

    fn set_recv_timestamp(&mut self, timestamp: u32) {
        NetworkEndian::write_u32(&mut self.recv_timestamp, timestamp)
    }

    fn set_tx_timestamp(&mut self, timestamp: u32) {
        NetworkEndian::write_u32(&mut self.tx_timestamp, timestamp)
    }

    fn size(&self) -> usize {
        self.origin_timestamp.len() + self.recv_timestamp.len() + self.tx_timestamp.len()
    }
}

unsafe impl FromBytes for IcmpTimestampData {}
unsafe impl AsBytes for IcmpTimestampData {}
unsafe impl Unaligned for IcmpTimestampData {}

/// ICMP timestamp packet.
pub struct Icmpv4Timestamp<B> {
    id_seq: LayoutVerified<B, IdAndSeq>,
    timestamps: LayoutVerified<B, IcmpTimestampData>,
}

impl<B: ByteSlice> Icmpv4Timestamp<B> {
    fn parse(bytes: B) -> Result<Icmpv4Timestamp<B>, ParseError> {
        let (id_seq, bytes) =
            LayoutVerified::<B, IdAndSeq>::new_from_prefix(bytes).ok_or(ParseError::Format)?;
        let timestamps =
            LayoutVerified::<B, IcmpTimestampData>::new(bytes).ok_or(ParseError::Format)?;
        Ok(Icmpv4Timestamp { id_seq, timestamps })
    }

    fn size(&self) -> usize {
        self.id_seq.bytes().len() + self.timestamps.bytes().len()
    }

    fn serialize<C: AsMut<[u8]>>(self, mut buffer: BufferAndRange<C>) -> BufferAndRange<C> {
        let (_, body, _) = buffer.parts_mut();

        let (mut id_seq, body) =
            LayoutVerified::<_, IdAndSeq>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for IdAndSeq");
        id_seq.bytes_mut().clone_from_slice(self.id_seq.bytes());

        let (mut timestamps, _) =
            LayoutVerified::<_, IcmpTimestampData>::new_unaligned_from_prefix_zeroed(body)
                .expect("Unable to allocate data for IcmpTimestampData");

        timestamps
            .bytes_mut()
            .clone_from_slice(self.timestamps.bytes());

        buffer
    }
}

#[allow(missing_docs)]
pub enum Icmpv4Body<B> {
    EchoRequest(Icmpv4Echo<B>),
    EchoReply(Icmpv4Echo<B>),
    TimestampRequest(Icmpv4Timestamp<B>),
    TimestampReply(Icmpv4Timestamp<B>),
    DestUnreachable(Icmpv4DestUnreachable<B>),
    Redirect(Icmpv4Redirect<B>),
    TimeExceeded(Icmpv4TimeExceeded<B>),
    ParameterProblem(Icmpv4ParameterProblem<B>),
}

impl<B: ByteSlice> Icmpv4Body<B> {
    fn serialize<C: AsMut<[u8]>>(
        self, mut buffer: BufferAndRange<C>,
    ) -> (Icmpv4Type, u8, BufferAndRange<C>) {
        match self {
            Icmpv4Body::EchoReply(echo_reply) => {
                (Icmpv4Type::EchoReply, 0, echo_reply.serialize(buffer))
            }
            Icmpv4Body::DestUnreachable(dest_unreachable) => (
                Icmpv4Type::DestUnreachable,
                dest_unreachable.code as u8,
                dest_unreachable.serialize(buffer),
            ),
            Icmpv4Body::Redirect(redirect) => (
                Icmpv4Type::Redirect,
                redirect.code as u8,
                redirect.serialize(buffer),
            ),
            Icmpv4Body::EchoRequest(echo_request) => {
                (Icmpv4Type::EchoRequest, 0, echo_request.serialize(buffer))
            }
            Icmpv4Body::TimeExceeded(time_exceeded) => (
                Icmpv4Type::TimeExceeded,
                time_exceeded.code as u8,
                time_exceeded.serialize(buffer),
            ),
            Icmpv4Body::ParameterProblem(parameter_problem) => (
                Icmpv4Type::ParameterProblem,
                parameter_problem.code as u8,
                parameter_problem.serialize(buffer),
            ),
            Icmpv4Body::TimestampRequest(timestamp_request) => (
                Icmpv4Type::TimestampRequest,
                0,
                timestamp_request.serialize(buffer),
            ),
            Icmpv4Body::TimestampReply(timestamp_reply) => (
                Icmpv4Type::TimestampReply,
                0,
                timestamp_reply.serialize(buffer),
            ),
        }
    }

    fn size(&self) -> usize {
        match self {
            Icmpv4Body::EchoReply(ref er) => er.size(),
            Icmpv4Body::DestUnreachable(ref du) => du.size(),
            Icmpv4Body::Redirect(ref r) => r.size(),
            Icmpv4Body::EchoRequest(ref er) => er.size(),
            Icmpv4Body::TimeExceeded(ref te) => te.size(),
            Icmpv4Body::ParameterProblem(ref pp) => pp.size(),
            Icmpv4Body::TimestampRequest(ref tr) => tr.size(),
            Icmpv4Body::TimestampReply(ref tr) => tr.size(),
        }
    }
}

/// Struct to represent an ICMPv4 packet.
pub struct Icmpv4Packet<B> {
    body: Icmpv4Body<B>,
}

impl<B: ByteSlice> Icmpv4Packet<B> {
    /// Return the body data of the ICMP packet.
    pub fn body(self) -> Icmpv4Body<B> {
        self.body
    }

    /// Parse a ByteSlice into an ICMPv4 packet.
    pub fn parse(bytes: B) -> Result<Icmpv4Packet<B>, ParseError> {
        let (header, body_bytes) = LayoutVerified::<B, Header>::new_unaligned_from_prefix(bytes)
            .ok_or(ParseError::Format)?;
        println!("Parsed header/body_bytes");

        let mut c = Checksum::new();
        c.add_bytes(&[header.msg_type, header.code]);
        c.add_bytes(&body_bytes);

        if c.checksum() != header.checksum() {
            return Err(ParseError::Checksum);
        }

        let body = match header.msg_type() {
            Ok(Icmpv4Type::EchoReply) => Icmpv4Body::EchoReply(Icmpv4Echo::parse(body_bytes)?),
            Ok(Icmpv4Type::DestUnreachable) => {
                Icmpv4Body::DestUnreachable(Icmpv4DestUnreachable::parse(body_bytes, header.code)?)
            }
            Ok(Icmpv4Type::Redirect) => {
                Icmpv4Body::Redirect(Icmpv4Redirect::parse(body_bytes, header.code)?)
            }
            Ok(Icmpv4Type::EchoRequest) => Icmpv4Body::EchoRequest(Icmpv4Echo::parse(body_bytes)?),
            Ok(Icmpv4Type::TimeExceeded) => {
                Icmpv4Body::TimeExceeded(Icmpv4TimeExceeded::parse(body_bytes, header.code)?)
            }
            Ok(Icmpv4Type::ParameterProblem) => Icmpv4Body::ParameterProblem(
                Icmpv4ParameterProblem::parse(body_bytes, header.code)?,
            ),
            Ok(Icmpv4Type::TimestampRequest) => {
                Icmpv4Body::TimestampRequest(Icmpv4Timestamp::parse(body_bytes)?)
            }
            Ok(Icmpv4Type::TimestampReply) => {
                Icmpv4Body::TimestampReply(Icmpv4Timestamp::parse(body_bytes)?)
            }
            Err(_) => return Err(ParseError::NotSupported),
        };

        Ok(Icmpv4Packet::new(body))
    }

    /// Construct a new ICMPv4 packet.
    pub fn new(body: Icmpv4Body<B>) -> Icmpv4Packet<B> {
        Icmpv4Packet { body }
    }

    /// Serialize an ICMPv4 Echo request into a ByteSlice.
    pub fn serialize<C: AsMut<[u8]>>(self, mut buffer: BufferAndRange<C>) -> BufferAndRange<C> {
        // Slice off the header, so we can write the body first.
        buffer.slice(4..);

        let (msg_type, code, mut buffer) = self.body.serialize(buffer);

        let (header_bytes, body, _) = buffer.parts_mut();

        let (_, mut header) =
            LayoutVerified::<_, Header>::new_unaligned_from_suffix_zeroed(header_bytes)
                .expect("Unable to allocate data for Header");

        header.code = code;
        header.set_msg_type(msg_type);

        let mut c = Checksum::new();
        c.add_bytes(&[header.msg_type, header.code]);
        c.add_bytes(&body);
        NetworkEndian::write_u16(&mut header.checksum, c.checksum());

        // Restore the header to the BufferAndRange
        buffer.extend_backwards(4);
        buffer
    }

    /// Return the size of the header plus the size of the body.
    pub fn size(&self) -> usize {
        4 + self.body.size()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use wire::ipv4::Ipv4Packet;

    #[test]
    fn test_parse_and_serialize_echo_request() {
        use wire::testdata::icmp_echo::*;
        let (ip_packet, range) = Ipv4Packet::parse(REQUEST_IP_PACKET_BYTES).unwrap();
        let icmp_packet = Icmpv4Packet::parse(ip_packet.body()).unwrap();
        if let Icmpv4Body::EchoRequest(ref echo_request) = icmp_packet.body {
            assert_eq!(echo_request.data, ECHO_DATA);
            assert_eq!(echo_request.id_seq.id(), IDENTIFIER);
            assert_eq!(echo_request.id_seq.seq(), SEQUENCE_NUM);
        } else {
            panic!("Unexpected packet body");
        }

        let data = vec![0; icmp_packet.size()];
        let mut serialized_data = icmp_packet.serialize(BufferAndRange::new(data, ..));
        let (_, body, _) = serialized_data.parts_mut();
        assert_eq!(body[..], REQUEST_IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_echo_response() {
        use wire::testdata::icmp_echo::*;
        let (ip_packet, range) = Ipv4Packet::parse(RESPONSE_IP_PACKET_BYTES).unwrap();
        let icmp_packet = Icmpv4Packet::parse(ip_packet.body()).unwrap();
        if let Icmpv4Body::EchoReply(ref echo_reply) = icmp_packet.body {
            assert_eq!(echo_reply.data, ECHO_DATA);
            assert_eq!(echo_reply.id_seq.id(), IDENTIFIER);
            assert_eq!(echo_reply.id_seq.seq(), SEQUENCE_NUM);
        } else {
            panic!("Unexpected packet body");
        }

        let data = vec![0; icmp_packet.size()];
        let mut serialized_data = icmp_packet.serialize(BufferAndRange::new(data, ..));
        let (_, body, _) = serialized_data.parts_mut();
        assert_eq!(body[..], RESPONSE_IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_timestamp() {
        use wire::testdata::icmp_timestamp::*;
        let (ip_packet, range) = Ipv4Packet::parse(REQUEST_IP_PACKET_BYTES).unwrap();
        let icmp_packet = Icmpv4Packet::parse(ip_packet.body()).unwrap();
        if let Icmpv4Body::TimestampRequest(ref timestamp_reply) = icmp_packet.body {
            assert_eq!(
                timestamp_reply.timestamps.origin_timestamp(),
                ORIGIN_TIMESTAMP
            );
            assert_eq!(timestamp_reply.timestamps.recv_timestamp(), RX_TX_TIMESTAMP);
            assert_eq!(timestamp_reply.timestamps.tx_timestamp(), RX_TX_TIMESTAMP);
            assert_eq!(timestamp_reply.id_seq.id(), IDENTIFIER);
            assert_eq!(timestamp_reply.id_seq.seq(), SEQUENCE_NUM);
        } else {
            panic!("Unexpected packet body");
        }

        let data = vec![0; icmp_packet.size()];
        let mut serialized_data = icmp_packet.serialize(BufferAndRange::new(data, ..));
        let (_, body, _) = serialized_data.parts_mut();
        assert_eq!(body[..], REQUEST_IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_dest_unreachable() {
        use wire::testdata::icmp_dest_unreachable::*;
        let (ip_packet, range) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let icmp_packet = Icmpv4Packet::parse(ip_packet.body()).unwrap();
        if let Icmpv4Body::DestUnreachable(ref packet) = icmp_packet.body {
            assert_eq!(packet.code, Icmpv4DestUnreachableCode::DestHostUnreachable);
            assert_eq!(packet.origin_data.bytes(), ORIGIN_DATA);
        } else {
            panic!("Unexpected packet body");
        }

        let data = vec![0; icmp_packet.size()];
        let mut serialized_data = icmp_packet.serialize(BufferAndRange::new(data, ..));
        let (_, body, _) = serialized_data.parts_mut();
        assert_eq!(body[..], IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_redirect() {
        use wire::testdata::icmp_redirect::*;
        let (ip_packet, range) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let icmp_packet = Icmpv4Packet::parse(ip_packet.body()).unwrap();
        if let Icmpv4Body::Redirect(ref packet) = icmp_packet.body {
            assert_eq!(packet.code, Icmpv4RedirectCode::RedirectForHost);
            assert_eq!(*packet.gateway, GATEWAY_ADDR);
        } else {
            panic!("Unexpected packet body");
        }

        let data = vec![0; icmp_packet.size()];
        let mut serialized_data = icmp_packet.serialize(BufferAndRange::new(data, ..));
        let (_, body, _) = serialized_data.parts_mut();
        assert_eq!(body[..], IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_time_exceeded() {
        use wire::testdata::icmp_time_exceeded::*;
        let (ip_packet, range) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let icmp_packet = Icmpv4Packet::parse(ip_packet.body()).unwrap();
        if let Icmpv4Body::TimeExceeded(ref packet) = icmp_packet.body {
            assert_eq!(packet.code, Icmpv4TimeExceededCode::TTLExpired);
            assert_eq!(packet.origin_data.bytes(), ORIGIN_DATA);
        } else {
            panic!("Unexpected packet body");
        }

        let data = vec![0; icmp_packet.size()];
        let mut serialized_data = icmp_packet.serialize(BufferAndRange::new(data, ..));
        let (_, body, _) = serialized_data.parts_mut();
        assert_eq!(body[..], IP_PACKET_BYTES[20..]);
    }

}
