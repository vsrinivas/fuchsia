// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of Internet Control Message Protocol (ICMP) packets.

#[macro_use]
mod macros;
mod common;
mod icmpv4;
mod icmpv6;

pub use self::icmpv4::Packet as Icmpv4Packet;
pub use self::icmpv6::Packet as Icmpv6Packet;

use std::cmp;
use std::convert::TryFrom;
use std::marker::PhantomData;
use std::mem;
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::ParseError;
use crate::ip::{Ip, IpAddr, IpProto, Ipv4, Ipv6};
use crate::wire::ipv4;
use crate::wire::util::fits_in_u32;
use crate::wire::util::{BufferAndRange, Checksum, PacketSerializer};

// Header has the same memory layout (thanks to repr(C, packed)) as an ICMP
// header. Thus, we can simply reinterpret the bytes of the ICMP header as a
// Header and then safely access its fields.
// Note the following caveats:
// - We cannot make any guarantees about the alignment of an instance of this
//   struct in memory or of any of its fields. This is true both because
//   repr(packed) removes the padding that would be used to ensure the alignment
//   of individual fields, but also because we are given no guarantees about
//   where within a given memory buffer a particular packet (and thus its
//   header) will be located.
// - Individual fields are all either u8 or [u8; N] rather than u16, u32, etc.
//   This is for two reasons:
//   - u16 and larger have larger-than-1 alignments, which are forbidden as
//     described above
//   - We are not guaranteed that the local platform has the same endianness as
//     network byte order (big endian), so simply treating a sequence of bytes
//     as a u16 or other multi-byte number would not necessarily be correct.
//     Instead, we use the NetworkEndian type and its reader and writer methods
//     to correctly access these fields.
#[derive(Default)]
#[repr(C, packed)]
struct Header {
    msg_type: u8,
    code: u8,
    checksum: [u8; 2],
    /* NOTE: The "Rest of Header" field is stored in message types rather than
     * in the Header. This helps consolidate how callers access data about the
     * packet, and is consistent with ICMPv6, which treats the field as part of
     * messages rather than the header. */
}

unsafe impl FromBytes for Header {}
unsafe impl AsBytes for Header {}
unsafe impl Unaligned for Header {}

impl Header {
    fn set_msg_type<T: Into<u8>>(&mut self, msg_type: T) {
        self.msg_type = msg_type.into();
    }

    fn checksum(&self) -> u16 {
        NetworkEndian::read_u16(&self.checksum)
    }

    fn set_checksum(&mut self, checksum: u16) {
        NetworkEndian::write_u16(&mut self.checksum[..], checksum);
    }
}

/// Peek at an ICMP header to see what message type is present.
///
/// Since `IcmpPacket` is statically typed with the message type expected, this
/// type must be known ahead of time before calling `parse`. If multiple
/// different types are valid in a given parsing context, and so the caller
/// cannot know ahead of time which type to use, `peek_message_type` can be used
/// to peek at the header first to figure out which static type should be used
/// in a subsequent call to `parse`.
///
/// Note that `peek_message_type` only inspects certain fields in the header,
/// and so `peek_message_type` succeeding does not guarantee that a subsequent
/// call to `parse` will also succeed.
pub fn peek_message_type<MessageType: TryFrom<u8>>(
    bytes: &[u8],
) -> Result<MessageType, ParseError> {
    let (header, _) = LayoutVerified::<_, Header>::new_unaligned_from_prefix(bytes).ok_or_else(
        debug_err_fn!(ParseError::Format, "too few bytes for header"),
    )?;
    MessageType::try_from(header.msg_type).or_else(|_| {
        Err(debug_err!(
            ParseError::NotSupported,
            "unrecognized message type: {:x}",
            header.msg_type,
        ))
    })
}

/// An extension trait adding ICMP-related functionality to `Ipv4` and `Ipv6`.
pub trait IcmpIpExt: Ip {
    /// The type of ICMP messages.
    ///
    /// For `Ipv4`, this is `icmpv4::MessageType`, and for `Ipv6`, this is
    /// `icmpv6::MessageType`.
    type IcmpMessageType: Into<u8> + Copy;

    /// Compute the length of the header of the packet prefix stored in `bytes`.
    ///
    /// Given the prefix of a packet stored in `bytes`, compute the length of
    /// the header of that packet, or `bytes.len()` if `bytes` does not contain
    /// the entire header. If the version is IPv6, the returned length should
    /// include all extension headers.
    fn header_len(bytes: &[u8]) -> usize;
}

// A default implementation for any I: Ip. This is to convince the Rust compiler
// that, given an I: Ip, it's guaranteed to implement IcmpIpExt. We humans know
// that Ipv4 and Ipv6 are the only types implementing Ip and so, since we
// implement IcmpIpExt for both of these types, this is fine. The compiler isn't
// so smart. This implementation should never actually be used. See the
// specialize_ip! and specialize_ip_addr! macros for other examples of this
// pattern.
impl<I: Ip> IcmpIpExt for I {
    default type IcmpMessageType = !;

    default fn header_len(bytes: &[u8]) -> usize {
        unreachable!()
    }
}

impl IcmpIpExt for Ipv4 {
    type IcmpMessageType = icmpv4::MessageType;

    fn header_len(bytes: &[u8]) -> usize {
        if bytes.len() < ipv4::MIN_HEADER_BYTES {
            return bytes.len();
        }
        let (header_prefix, _) =
            LayoutVerified::<_, ipv4::HeaderPrefix>::new_unaligned_from_prefix(bytes).unwrap();
        cmp::min(header_prefix.ihl() as usize * 4, bytes.len())
    }
}

impl IcmpIpExt for Ipv6 {
    type IcmpMessageType = icmpv6::MessageType;

    fn header_len(bytes: &[u8]) -> usize {
        // NOTE: We panic here rather than doing log_unimplemented! because
        // there's no sane default value for this function. If it's called, it
        // doesn't make sense for the program to continue executing; if we did,
        // it would cause bugs in the caller.
        unimplemented!()
    }
}

/// An ICMP message.
pub trait IcmpMessage<I: IcmpIpExt>: Sized + Copy + FromBytes + AsBytes + Unaligned {
    /// The type of codes used with this message.
    ///
    /// The ICMP header includes an 8-bit "code" field. For a given message
    /// type, different values of this field carry different meanings. Not all
    /// code values are used - some may be invalid. This type representents a
    /// parsed code. For example, for TODO, it is the TODO type.
    type Code: Into<u8> + Copy;

    /// The type corresponding to this message type.
    ///
    /// The value of the "type" field in the ICMP header corresponding to
    /// messages of this type.
    const TYPE: I::IcmpMessageType;

    /// Whether this message type contains a body.
    const HAS_BODY: bool;

    /// Parse a `Code` from an 8-bit number.
    ///
    /// Parse a `Code` from the 8-bit "code" field in the ICMP header. Not all
    /// values for this field are valid. If an invalid value is passed,
    /// `code_from_u8` returns `None`.
    fn code_from_u8(_: u8) -> Option<Self::Code>;
}

/// An ICMP message that contains data from the original packet that caused the message.
///
/// Certain ICMP message types contain data from the original packet that caused
/// the ICMP message to be emitted. These message types implement this trait.
///
/// In ICMPv4, these messages contain the entire IPv4 header plus 8 bytes of the
/// IPv4 packet body. In ICMPv6, these message contain as much of the original
/// packet as possible without the entire ICMPv6 packet exceeding the minimum
/// IPv6 MTU.
pub trait HasOriginalPacket {}

/// An ICMP packet.
///
/// An `IcmpPacket` shares its underlying memory with the byte slice it was
/// parsed from, meaning that no copying or extra allocation is necessary.
pub struct IcmpPacket<I: IcmpIpExt, B, M: IcmpMessage<I>> {
    header: LayoutVerified<B, Header>,
    message: LayoutVerified<B, M>,
    message_body: B,
    _marker: PhantomData<I>,
}

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I>> IcmpPacket<I, B, M> {
    /// Parse an ICMP packet.
    ///
    /// `parse` parses `bytes` as an ICMP packet and validates the header fields
    /// and checksum.  It returns the byte range corresponding to the message
    /// body within `bytes`. This can be useful when extracting the encapsulated
    /// body to send to another layer of the stack. If the message type has no
    /// body, then the range is meaningless and should be ignored.
    ///
    /// If `bytes` are a valid ICMP packet, but do not match the message type
    /// `M`, `parse` will return `Err(ParseError::NotExpected)`. If multiple
    /// message types are valid in a given context, `peek_message_types` may be
    /// used to peek at the header and determine what type is present so that
    /// the correct type can then be used in a call to `parse`.
    pub fn parse(
        bytes: B, src_ip: I::Addr, dst_ip: I::Addr,
    ) -> Result<(IcmpPacket<I, B, M>, Range<usize>), ParseError> {
        let (header, rest) =
            LayoutVerified::<B, Header>::new_unaligned_from_prefix(bytes).ok_or_else(
                debug_err_fn!(ParseError::Format, "too few bytes for header"),
            )?;
        let (message, message_body) = LayoutVerified::<B, M>::new_unaligned_from_prefix(rest)
            .ok_or_else(debug_err_fn!(
                ParseError::Format,
                "too few bytes for packet"
            ))?;
        if !M::HAS_BODY && !message_body.is_empty() {
            return debug_err!(Err(ParseError::Format), "unexpected message body");
        }

        if header.msg_type != M::TYPE.into() {
            return debug_err!(Err(ParseError::NotExpected), "unexpected message type");
        }
        M::code_from_u8(header.code).ok_or_else(debug_err_fn!(
            ParseError::Format,
            "unrecognized code: {}",
            header.code
        ))?;
        if header.checksum() != Self::compute_checksum(
            &header,
            message.bytes(),
            &message_body,
            src_ip,
            dst_ip,
        )
        .ok_or_else(debug_err_fn!(ParseError::Format, "packet too large"))?
        {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }

        let pre_body_len = header.bytes().len() + message.bytes().len();
        let total_len = pre_body_len + message_body.len();
        let packet = IcmpPacket {
            header,
            message,
            message_body,
            _marker: PhantomData,
        };
        Ok((packet, pre_body_len..total_len))
    }

    /// Get the ICMP message.
    pub fn message(&self) -> &M {
        &self.message
    }

    /// Get the ICMP message body.
    ///
    /// Some ICMP message types contain variable-length bodies. `message_body`
    /// gets the message body for this packet.
    pub fn message_body(&self) -> &[u8] {
        &self.message_body
    }

    /// Get the ICMP message code.
    ///
    /// The code provides extra details about the message. Each message type has
    /// its own set of codes that are allowed.
    pub fn code(&self) -> M::Code {
        // infallible since it was validated in parse
        M::code_from_u8(self.header.code).unwrap()
    }

    /// Construct a serializer with the same contents as this packet.
    pub fn serializer(&self, src_ip: I::Addr, dst_ip: I::Addr) -> IcmpPacketSerializer<I, M> {
        IcmpPacketSerializer {
            src_ip,
            dst_ip,
            code: self.code(),
            msg: *self.message(),
        }
    }
}

impl<I: IcmpIpExt, B, M: IcmpMessage<I>> IcmpPacket<I, B, M> {
    /// Compute the checksum, skipping the checksum field itself.
    ///
    /// `compute_checksum` returns `None` if the version is IPv6 and the total
    /// ICMP packet length overflows a u32.
    fn compute_checksum(
        header: &Header, message: &[u8], message_body: &[u8], src_ip: I::Addr, dst_ip: I::Addr,
    ) -> Option<u16> {
        let mut c = Checksum::new();
        if I::VERSION.is_v6() {
            c.add_bytes(src_ip.bytes());
            c.add_bytes(dst_ip.bytes());
            let icmpv6_len = mem::size_of::<Header>() + message.len() + message_body.len();
            // For IPv6, the "ICMPv6 length" field in the pseudo-header is 32 bits.
            if !fits_in_u32(icmpv6_len) {
                return None;
            }
            let mut len_bytes = [0; 4];
            NetworkEndian::write_u32(&mut len_bytes, icmpv6_len as u32);
            c.add_bytes(&len_bytes[..]);
            c.add_bytes(&[0, 0, 0]);
            c.add_bytes(&[IpProto::Icmpv6 as u8]);
        }
        c.add_bytes(&[header.msg_type, header.code]);
        c.add_bytes(message);
        c.add_bytes(message_body);
        Some(c.checksum())
    }
}

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I> + HasOriginalPacket> IcmpPacket<I, B, M> {
    /// Get the body of the packet that caused this ICMP message.
    ///
    /// This ICMP message contains some of the bytes of the packet that caused
    /// this message to be emitted. `original_packet_body` returns as much of
    /// the body of that packet as is contained in this message. For IPv4, this
    /// is guaranteed to be 8 bytes. For IPv6, there are no guarantees about the
    /// length.
    pub fn original_packet_body(&self) -> &[u8] {
        let header_len = I::header_len(&self.message_body);
        debug_assert!(header_len <= self.message_body.len());
        debug_assert!(I::VERSION.is_v6() || self.message_body.len() - header_len == 8);
        &self.message_body[header_len..]
    }
}

/// A serializer for ICMP packets.
pub struct IcmpPacketSerializer<I: IcmpIpExt, M: IcmpMessage<I>> {
    src_ip: I::Addr,
    dst_ip: I::Addr,
    code: M::Code,
    msg: M,
}

impl<I: IcmpIpExt, M: IcmpMessage<I>> IcmpPacketSerializer<I, M> {
    /// Construct a new `IcmpPacketSerializer`.
    pub fn new(
        src_ip: I::Addr, dst_ip: I::Addr, code: M::Code, msg: M,
    ) -> IcmpPacketSerializer<I, M> {
        IcmpPacketSerializer {
            src_ip,
            dst_ip,
            code,
            msg,
        }
    }
}

// TODO(joshlf): Figure out a way to split body and non-body message types by
// trait and implement PacketSerializer for some and InnerPacketSerializer for
// others.

impl<I: IcmpIpExt, M: IcmpMessage<I>> PacketSerializer for IcmpPacketSerializer<I, M> {
    fn max_header_bytes(&self) -> usize {
        mem::size_of::<Header>() + mem::size_of::<M>()
    }

    fn min_header_bytes(&self) -> usize {
        self.max_header_bytes()
    }

    fn serialize<B: AsRef<[u8]> + AsMut<[u8]>>(self, buffer: &mut BufferAndRange<B>) {
        let extend_backwards = {
            let (prefix, message_body, _) = buffer.parts_mut();
            assert!(
                M::HAS_BODY || message_body.len() == 0,
                "body provided for message that doesn't take a body"
            );
            // SECURITY: Use _zeroed constructors to ensure we zero memory to prevent
            // leaking information from packets previously stored in this buffer.
            let (prefix, mut message) =
                LayoutVerified::<_, M>::new_unaligned_from_suffix_zeroed(prefix)
                    .expect("too few bytes for ICMP message");
            let (_, mut header) =
                LayoutVerified::<_, Header>::new_unaligned_from_suffix_zeroed(prefix)
                    .expect("too few bytes for ICMP message");
            *message = self.msg;
            header.set_msg_type(M::TYPE);
            header.code = self.code.into();
            let checksum = IcmpPacket::<I, B, M>::compute_checksum(
                &header,
                message.bytes(),
                message_body,
                self.src_ip,
                self.dst_ip,
            )
            .unwrap_or_else(|| {
                panic!(
                    "total ICMP packet length of {} overflows 32-bit length field of pseudo-header",
                    header.bytes().len() + message.bytes().len() + message_body.len(),
                )
            });
            header.set_checksum(checksum);
            header.bytes().len() + message.bytes().len()
        };

        buffer.extend_backwards(extend_backwards);
    }
}

/// The type of ICMP codes that are unused.
///
/// Some ICMP messages do not use codes. In Rust, the `IcmpMessage::Code` type
/// associated with these messages is `IcmpUnusedCode`. The only valid numerical
/// value for this code is 0.
#[derive(Copy, Clone)]
pub struct IcmpUnusedCode;

impl Into<u8> for IcmpUnusedCode {
    fn into(self) -> u8 {
        0
    }
}

#[derive(Copy, Clone)]
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

impl_from_bytes_as_bytes_unaligned!(IdAndSeq);
