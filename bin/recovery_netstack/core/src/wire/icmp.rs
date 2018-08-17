// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of Internet Control Message Protocol (ICMP) packets.

use std::cmp;
use std::marker::PhantomData;
use std::mem;
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::ParseError;
use crate::ip::{Ip, IpAddr, IpProto, Ipv4, Ipv4Addr, Ipv6};
use crate::wire::util::fits_in_u32;
use crate::wire::util::Checksum;
use crate::wire::{ipv4, BufferAndRange};

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
pub fn peek_message_type(bytes: &[u8]) -> Result<Icmpv4MessageType, ParseError> {
    let (header, _) = LayoutVerified::<_, Header>::new_unaligned_from_prefix(bytes).ok_or_else(
        debug_err_fn!(ParseError::Format, "too few bytes for header"),
    )?;
    Icmpv4MessageType::from_u8(header.msg_type).ok_or_else(debug_err_fn!(
        ParseError::NotSupported,
        "unrecognized message type: {:x}",
        header.msg_type,
    ))
}

/// An ICMPv4 packet with a dynamic message type.
///
/// Unlike `IcmpPacket`, `Icmpv4Packet` only supports ICMPv4, and does not
/// require a static message type. Each enum variant contains an `IcmpPacket` of
/// the appropriate static type, making it easier to call `parse` without
/// knowing the message type ahead of time while still getting the benefits of a
/// statically-typed packet struct after parsing is complete.
#[allow(missing_docs)]
pub enum Icmpv4Packet<B> {
    EchoReply(IcmpPacket<Ipv4, B, IcmpEchoReply>),
    DestUnreachable(IcmpPacket<Ipv4, B, IcmpDestUnreachable>),
    Redirect(IcmpPacket<Ipv4, B, Icmpv4Redirect>),
    EchoRequest(IcmpPacket<Ipv4, B, IcmpEchoRequest>),
    TimeExceeded(IcmpPacket<Ipv4, B, Icmpv4TimeExceeded>),
    ParameterProblem(IcmpPacket<Ipv4, B, Icmpv4ParameterProblem>),
    TimestampRequest(IcmpPacket<Ipv4, B, Icmpv4TimestampRequest>),
    TimestampReply(IcmpPacket<Ipv4, B, Icmpv4TimestampReply>),
}

impl<B: ByteSlice> Icmpv4Packet<B> {
    /// Parse an ICMP packet.
    ///
    /// `parse` parses `bytes` as an ICMP packet and validates the header fields
    /// and checksum.  It returns the byte range corresponding to the message
    /// body within `bytes`. This can be useful when extracting the encapsulated
    /// body to send to another layer of the stack. If the message type has no
    /// body, then the range is meaningless and should be ignored.
    pub fn parse(
        bytes: B, src_ip: Ipv4Addr, dst_ip: Ipv4Addr,
    ) -> Result<(Icmpv4Packet<B>, Range<usize>), ParseError> {
        macro_rules! mtch {
            ($bytes:expr, $src_ip:expr, $dst_ip:expr, $($variant:ident => $type:ty,)*) => {
                match peek_message_type(&$bytes)? {
                    $(Icmpv4MessageType::$variant => {
                        let (packet, range) = IcmpPacket::<Ipv4, B, $type>::parse($bytes, $src_ip, $dst_ip)?;
                        (Icmpv4Packet::$variant(packet), range)
                    })*
                }
            }
        }

        Ok(mtch!(
            bytes,
            src_ip,
            dst_ip,
            EchoReply => IcmpEchoReply,
            DestUnreachable => IcmpDestUnreachable,
            Redirect => Icmpv4Redirect,
            EchoRequest => IcmpEchoRequest,
            TimeExceeded => Icmpv4TimeExceeded,
            ParameterProblem => Icmpv4ParameterProblem,
            TimestampRequest => Icmpv4TimestampRequest,
            TimestampReply  => Icmpv4TimestampReply,
        ))
    }
}

/// An extension trait adding ICMP-related functionality to `Ipv4` and `Ipv6`.
pub trait IcmpIpExt: Ip {
    /// The type of ICMP messages.
    ///
    /// For `Ipv4`, this is `Icmpv4MessageType`, and for `Ipv6`, this is TODO.
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
    type IcmpMessageType = Icmpv4MessageType;

    fn header_len(bytes: &[u8]) -> usize {
        if bytes.len() < ipv4::MIN_HEADER_LEN {
            return bytes.len();
        }
        let (header_prefix, _) =
            LayoutVerified::<_, ipv4::HeaderPrefix>::new_unaligned_from_prefix(bytes).unwrap();
        cmp::min(header_prefix.ihl() as usize * 4, bytes.len())
    }
}

impl IcmpIpExt for Ipv6 {
    type IcmpMessageType = Icmpv6MessageType;

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
        ).ok_or_else(debug_err_fn!(ParseError::Format, "packet too large"))?
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

impl<I: IcmpIpExt, B, M: IcmpMessage<I>> IcmpPacket<I, B, M> {
    /// Compute how many bytes will be consumed by an ICMP packet.
    ///
    /// `serialize_len` computes how many bytes will be consumed when calling
    /// `serialize` with a `BufferAndRange` with a range of length `range_len`
    /// for the message type `M`. It can be used to ensure that enough pre-range
    /// bytes are provided to `serialize` to guarantee that it won't panic.
    ///
    /// # Panics
    ///
    /// `serialize_len` panics if the message type `M` doesn't take a body
    /// (`M::HAS_BODY` is false) and `range_len` is non-zero.
    pub fn serialize_len(range_len: usize) -> usize {
        assert!(
            M::HAS_BODY || range_len == 0,
            "body provided for message that doesn't take a body"
        );
        mem::size_of::<Header>() + mem::size_of::<M>() + range_len
    }
}

impl<I: IcmpIpExt, B: AsMut<[u8]>, M: IcmpMessage<I>> IcmpPacket<I, B, M> {
    /// Serialize an ICMP packet in an existing buffer.
    ///
    /// `serialize creates an `IcmpPacket` which uses the provided `buffer` for
    /// its storage, initializing all header and message fields including the
    /// checksum. It treats `buffer.range()` as the body of the message. If the
    /// message type does not contain a body, the packet will be serialized
    /// immediately preceding `buffer.range()`, and `buffer.range()` must have a
    /// length of 0. It returns a new `BufferAndRange` with a range equal to the
    /// bytes of the ICMP packet (including the header). This range can be used
    /// to indicate the range for encapsulation in another packet.
    ///
    /// # Examples
    ///
    /// TODO
    ///
    /// # Panics
    ///
    /// `serialize` panics if there is insufficient room preceding the range to
    /// store the message and header. The caller can guarantee that there will
    /// be enough room by providing at least `serialize_len` pre-range bytes.
    ///
    /// `serialize` also panics if the message type does not contain a body, but
    /// `buffer.range()` is non-zero in length.
    pub fn serialize(
        mut buffer: BufferAndRange<B>, src_ip: I::Addr, dst_ip: I::Addr, code: M::Code, msg: M,
    ) -> BufferAndRange<B> {
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
            *message = msg;
            header.set_msg_type(M::TYPE);
            header.code = code.into();
            let checksum =
                Self::compute_checksum(&header, message.bytes(), message_body, src_ip, dst_ip)
                    .unwrap_or_else(|| {
                        panic!(
                            "total ICMP packet length of {} overflows 32-bit length field of \
                             pseudo-header",
                            header.bytes().len() + message.bytes().len() + message_body.len(),
                        )
                    });
            header.set_checksum(checksum);
            header.bytes().len() + message.bytes().len()
        };
        buffer.extend_backwards(extend_backwards);
        buffer
    }
}

/// Create an enum for a network packet field.
///
/// Create a `repr(u8)` enum with a `from_u8(u8) -> Option<Self>` constructor
/// that implements `Into<u8>`.
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

      impl Into<u8> for $t {
          fn into(self) -> u8 {
              self as u8
          }
      }
    };
}

/// Implement `IcmpMessage` for a type.
///
/// The arguments are:
/// - `$ip` - `Ipv4` or `Ipv6`
/// - `$typ` - the type to implement for
/// - `$msg_variant` - the variant of `Icmpv4MessageType` or `Icmpv6MessageType`
///   associated with this message type
/// - `$code` - the type to use for `IcmpMessage::Code`; if `()` is used, 0 will
///   be the only valid code
/// - `$has_body` - `true` or `false` depending on whether this message type
///   supports a body
macro_rules! impl_icmp_message {
    ($ip:ident, $type:ident, $msg_variant:ident, $code:tt, $has_body:expr) => {
        impl IcmpMessage<$ip> for $type {
            type Code = $code;

            const TYPE: <$ip as IcmpIpExt>::IcmpMessageType =
                impl_icmp_message_inner_message_type!($ip, $msg_variant);

            const HAS_BODY: bool = $has_body;

            fn code_from_u8(u: u8) -> Option<Self::Code> {
                impl_icmp_message_inner_code_from_u8!($code, u)
            }
        }
    };
}

macro_rules! impl_icmp_message_inner_message_type {
    (Ipv4, $msg_variant:ident) => {
        Icmpv4MessageType::$msg_variant
    };
    (Ipv6, $msg_variant:ident) => {
        Icmpv6MessageType::$msg_variant
    };
}

macro_rules! impl_icmp_message_inner_code_from_u8 {
    (IcmpUnusedCode, $var:ident) => {
        if $var == 0 {
            Some(IcmpUnusedCode)
        } else {
            None
        }
    };
    ($code:tt, $var:ident) => {
        $code::from_u8($var)
    };
}

macro_rules! impl_from_bytes_as_bytes_unaligned {
    ($type:ty) => {
        unsafe impl FromBytes for $type {}
        unsafe impl AsBytes for $type {}
        unsafe impl Unaligned for $type {}
    };
}

create_net_enum! {
    Icmpv4MessageType,
    EchoReply: ECHO_REPLY = 0,
    DestUnreachable: DEST_UNREACHABLE = 3,
    Redirect: REDIRECT = 5,
    EchoRequest: ECHO_REQUEST = 8,
    TimeExceeded: TIME_EXCEEDED = 11,
    ParameterProblem: PARAMETER_PROBLEM = 12,
    TimestampRequest: TIMESTAMP_REQUEST = 13,
    TimestampReply: TIMESTAMP_REPLY = 14,
}

create_net_enum! {
    Icmpv6MessageType,
    DestUnreachable: DEST_UNREACHABLE = 1,
    PacketTooBig: PACKET_TOO_BIG = 2,
    TimeExceeded: TIME_EXCEEDED = 3,
    ParameterProblem: PARAMETER_PROBLEM = 4,
    EchoRequest: ECHO_REQUEST = 128,
    EchoReply: ECHO_REPLY = 129,
    Redirect: REDIRECT = 137,
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

unsafe impl FromBytes for IdAndSeq {}
unsafe impl AsBytes for IdAndSeq {}
unsafe impl Unaligned for IdAndSeq {}

/// An ICMP Echo Request message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct IcmpEchoRequest {
    id_seq: IdAndSeq,
    /* The rest of of IcmpEchoRequest is variable-length, so is stored in the
     * message_body field in IcmpPacket */
}

/// An ICMP Echo Reply message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct IcmpEchoReply {
    id_seq: IdAndSeq,
    /* The rest of of IcmpEchoReply is variable-length, so is stored in the
     * message_body field in IcmpPacket */
}

impl_from_bytes_as_bytes_unaligned!(IcmpEchoRequest);
impl_from_bytes_as_bytes_unaligned!(IcmpEchoReply);

impl_icmp_message!(Ipv4, IcmpEchoRequest, EchoRequest, IcmpUnusedCode, true);
impl_icmp_message!(Ipv4, IcmpEchoReply, EchoReply, IcmpUnusedCode, true);

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
  CommAdministrativelyProhibited: COMM_ADMINISTRATIVELY_PROHIBITED = 13,
  HostPrecedenceViolation: HOST_PRECEDENCE_VIOLATION = 14,
  PrecedenceCutoffInEffect: PRECEDENCE_CUTOFF_IN_EFFECT = 15,
}

create_net_enum! {
  Icmpv6DestUnreachableCode,
  NoRoute: NO_ROUTE = 0,
  CommAdministrativelyProhibited: COMM_ADMINISTRATIVELY_PROHIBITED = 1,
  BeyondScope: BEYOND_SCOPE = 2,
  AddrUnreachable: ADDR_UNREACHABLE = 3,
  PortUnreachable: PORT_UNREACHABLE = 4,
  SrcAddrFailedPolicy: SRC_ADDR_FAILED_POLICY = 5,
  RejectRoute: REJECT_ROUTE = 6,
}

/// An ICMP Destination Unreachable message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct IcmpDestUnreachable {
    // Rest of Header in ICMP, unused in ICMPv6
    _unused: [u8; 4],
    /* Body of IcmpDestUnreachable is entirely variable-length, so is stored in
     * the message_body field in IcmpPacket */
}

impl HasOriginalPacket for IcmpDestUnreachable {}

impl_from_bytes_as_bytes_unaligned!(IcmpDestUnreachable);

impl_icmp_message!(
    Ipv4,
    IcmpDestUnreachable,
    DestUnreachable,
    Icmpv4DestUnreachableCode,
    true
);

impl_icmp_message!(
    Ipv6,
    IcmpDestUnreachable,
    DestUnreachable,
    Icmpv6DestUnreachableCode,
    true
);

create_net_enum! {
  Icmpv4RedirectCode,
  RedirectForNetwork: REDIRECT_FOR_NETWORK = 0,
  RedirectForHost: REDIRECT_FOR_HOST = 1,
  RedirectForToSNetwork: REDIRECT_FOR_TOS_NETWORK = 2,
  RedirectForToSHost: REDIRECT_FOR_TOS_HOST = 3,
}

/// An ICMPv4 Redirect Message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct Icmpv4Redirect {
    gateway: Ipv4Addr,
}

impl HasOriginalPacket for Icmpv4Redirect {}

impl_from_bytes_as_bytes_unaligned!(Icmpv4Redirect);

impl_icmp_message!(Ipv4, Icmpv4Redirect, Redirect, Icmpv4RedirectCode, true);

create_net_enum! {
  Icmpv4TimeExceededCode,
  TTLExpired: TTL_EXPIRED = 0,
  FragmentReassemblyTimeExceeded: FRAGMENT_REASSEMBLY_TIME_EXCEEDED = 1,
}

/// An ICMPv4 Time Exceeded message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct Icmpv4TimeExceeded {
    // Rest of Header in ICMP, unused in ICMPv6
    _unused: [u8; 4],
    /* Body of Icmpv4TimeExceeded is entirely variable-length, so is stored in
     * the message_body field in IcmpPacket */
}

impl HasOriginalPacket for Icmpv4TimeExceeded {}

impl_from_bytes_as_bytes_unaligned!(Icmpv4TimeExceeded);

impl_icmp_message!(
    Ipv4,
    Icmpv4TimeExceeded,
    TimeExceeded,
    Icmpv4TimeExceededCode,
    true
);

create_net_enum! {
  Icmpv4ParameterProblemCode,
  PointerIndicatesError: POINTER_INDICATES_ERROR = 0,
  MissingRequiredOption: MISSING_REQUIRED_OPTION = 1,
  BadLength: BAD_LENGTH = 2,
}

/// An ICMPv4 Parameter Problem message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct Icmpv4ParameterProblem {
    pointer: u8,
    _unused: [u8; 3],
    /* The rest of Icmpv4ParameterProblem is variable-length, so is stored in
     * the message_body field in IcmpPacket */
}

impl HasOriginalPacket for Icmpv4ParameterProblem {}

impl_from_bytes_as_bytes_unaligned!(Icmpv4ParameterProblem);

impl_icmp_message!(
    Ipv4,
    Icmpv4ParameterProblem,
    ParameterProblem,
    Icmpv4ParameterProblemCode,
    true
);

#[derive(Copy, Clone)]
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
}

impl_from_bytes_as_bytes_unaligned!(IcmpTimestampData);

#[derive(Copy, Clone)]
#[repr(C, packed)]
struct Timestamp {
    id_seq: IdAndSeq,
    timestamps: IcmpTimestampData,
}

/// An ICMPv4 Timestamp Request message.
#[derive(Copy, Clone)]
#[repr(transparent)]
pub struct Icmpv4TimestampRequest(Timestamp);

/// An ICMPv4 Timestamp Reply message.
#[derive(Copy, Clone)]
#[repr(transparent)]
pub struct Icmpv4TimestampReply(Timestamp);

impl_from_bytes_as_bytes_unaligned!(Icmpv4TimestampRequest);
impl_from_bytes_as_bytes_unaligned!(Icmpv4TimestampReply);

impl_icmp_message!(
    Ipv4,
    Icmpv4TimestampRequest,
    TimestampRequest,
    IcmpUnusedCode,
    false
);
impl_icmp_message!(
    Ipv4,
    Icmpv4TimestampReply,
    TimestampReply,
    IcmpUnusedCode,
    false
);

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wire::ipv4::Ipv4Packet;

    fn serialize_to_bytes<I: Ip, B: ByteSlice, M: IcmpMessage<I>>(
        src_ip: I::Addr, dst_ip: I::Addr, icmp: &IcmpPacket<I, B, M>,
    ) -> Vec<u8> {
        let mut data = vec![0; IcmpPacket::<I, &[u8], M>::serialize_len(icmp.message_body.len())];
        let body_offset = data.len() - icmp.message_body.len();
        (&mut data[body_offset..]).copy_from_slice(&icmp.message_body);
        IcmpPacket::serialize(
            BufferAndRange::new(&mut data[..], body_offset..),
            src_ip,
            dst_ip,
            icmp.code(),
            *icmp.message,
        );
        data
    }

    #[test]
    fn test_parse_and_serialize_echo_request() {
        use crate::wire::testdata::icmp_echo::*;
        let (ip, _) = Ipv4Packet::parse(REQUEST_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, IcmpEchoRequest>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.message_body(), ECHO_DATA);
        assert_eq!(icmp.message().id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message().id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp);
        assert_eq!(data[..], REQUEST_IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_echo_response() {
        use crate::wire::testdata::icmp_echo::*;
        let (ip, _) = Ipv4Packet::parse(RESPONSE_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, IcmpEchoReply>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.message().id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message_body(), ECHO_DATA);
        assert_eq!(icmp.message().id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp);
        assert_eq!(data[..], RESPONSE_IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_timestamp_request() {
        use crate::wire::testdata::icmp_timestamp::*;
        let (ip, _) = Ipv4Packet::parse(REQUEST_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, Icmpv4TimestampRequest>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(
            icmp.message().0.timestamps.origin_timestamp(),
            ORIGIN_TIMESTAMP
        );
        assert_eq!(
            icmp.message().0.timestamps.recv_timestamp(),
            RX_TX_TIMESTAMP
        );
        assert_eq!(icmp.message().0.timestamps.tx_timestamp(), RX_TX_TIMESTAMP);
        assert_eq!(icmp.message().0.id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message().0.id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp);
        assert_eq!(data[..], REQUEST_IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_timestamp_reply() {
        use crate::wire::testdata::icmp_timestamp::*;
        let (ip, _) = Ipv4Packet::parse(RESPONSE_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, Icmpv4TimestampReply>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(
            icmp.message().0.timestamps.origin_timestamp(),
            ORIGIN_TIMESTAMP
        );
        // TODO: Assert other values here?
        // TODO: Check value of recv_timestamp and tx_timestamp
        assert_eq!(icmp.message().0.id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message().0.id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp);
        assert_eq!(data[..], RESPONSE_IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_dest_unreachable() {
        use crate::wire::testdata::icmp_dest_unreachable::*;
        let (ip, _) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<Ipv4, _, IcmpDestUnreachable>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.code(), Icmpv4DestUnreachableCode::DestHostUnreachable);
        assert_eq!(icmp.original_packet_body(), ORIGIN_DATA);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp);
        assert_eq!(data[..], IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_redirect() {
        use crate::wire::testdata::icmp_redirect::*;
        let (ip, _) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, Icmpv4Redirect>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.code(), Icmpv4RedirectCode::RedirectForHost);
        assert_eq!(icmp.message().gateway, GATEWAY_ADDR);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp);
        assert_eq!(data[..], IP_PACKET_BYTES[20..]);
    }

    #[test]
    fn test_parse_and_serialize_time_exceeded() {
        use crate::wire::testdata::icmp_time_exceeded::*;
        let (ip, _) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip) = (ip.src_ip(), ip.dst_ip());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, Icmpv4TimeExceeded>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.code(), Icmpv4TimeExceededCode::TTLExpired);
        assert_eq!(icmp.original_packet_body(), ORIGIN_DATA);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp);
        assert_eq!(data[..], IP_PACKET_BYTES[20..]);
    }
}
