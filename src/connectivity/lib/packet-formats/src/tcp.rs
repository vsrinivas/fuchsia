// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of TCP segments.
//!
//! The TCP segment format is defined in [RFC 791 Section 3.3].
//!
//! [RFC 793 Section 3.1]: https://datatracker.ietf.org/doc/html/rfc793#section-3.1

use core::borrow::Borrow;
use core::convert::TryInto as _;
use core::fmt::Debug;
#[cfg(test)]
use core::fmt::{self, Formatter};
use core::num::NonZeroU16;
use core::ops::Range;

use arrayvec::ArrayVec;
use explicit::ResultExt as _;
use net_types::ip::IpAddress;
use packet::records::options::OptionsRaw;
use packet::{
    BufferView, BufferViewMut, ByteSliceInnerPacketBuilder, EmptyBuf, FragmentedByteSlice, FromRaw,
    InnerPacketBuilder, MaybeParsed, PacketBuilder, PacketConstraints, ParsablePacket,
    ParseMetadata, SerializeBuffer, Serializer,
};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::ip::IpProto;
use crate::{compute_transport_checksum_parts, compute_transport_checksum_serialize, U16, U32};

use self::data_offset_reserved_flags::DataOffsetReservedFlags;
use self::options::{TcpOption, TcpOptionsImpl};

/// The length of the fixed prefix of a TCP header (preceding the options).
pub const HDR_PREFIX_LEN: usize = 20;

/// The maximum length of a TCP header.
pub const MAX_HDR_LEN: usize = 60;

/// The maximum length of the options in a TCP header.
pub const MAX_OPTIONS_LEN: usize = MAX_HDR_LEN - HDR_PREFIX_LEN;

const CHECKSUM_OFFSET: usize = 16;
const CHECKSUM_RANGE: Range<usize> = CHECKSUM_OFFSET..CHECKSUM_OFFSET + 2;

#[derive(Debug, Default, FromBytes, AsBytes, Unaligned, PartialEq)]
#[repr(C)]
struct HeaderPrefix {
    src_port: U16,
    dst_port: U16,
    seq_num: U32,
    ack: U32,
    data_offset_reserved_flags: DataOffsetReservedFlags,
    window_size: U16,
    checksum: [u8; 2],
    urg_ptr: U16,
}

impl HeaderPrefix {
    #[allow(clippy::too_many_arguments)]
    fn new(
        src_port: u16,
        dst_port: u16,
        seq_num: u32,
        ack: u32,
        data_offset_reserved_flags: DataOffsetReservedFlags,
        window_size: u16,
        checksum: [u8; 2],
        urg_ptr: u16,
    ) -> HeaderPrefix {
        HeaderPrefix {
            src_port: U16::new(src_port),
            dst_port: U16::new(dst_port),
            seq_num: U32::new(seq_num),
            ack: U32::new(ack),
            data_offset_reserved_flags,
            window_size: U16::new(window_size),
            checksum,
            urg_ptr: U16::new(urg_ptr),
        }
    }

    fn data_offset(&self) -> u8 {
        self.data_offset_reserved_flags.data_offset()
    }

    fn ack_num(&self) -> Option<u32> {
        if self.data_offset_reserved_flags.ack() {
            Some(self.ack.get())
        } else {
            None
        }
    }
}

mod data_offset_reserved_flags {
    use super::*;

    /// The Data Offset field, the reserved zero bits, and the flags.
    ///
    /// When constructed from a packet, `DataOffsetReservedFlags` ensures that
    /// all bits are preserved even if they are reserved as of this writing.
    /// This allows us to be forwards-compatible with future uses of these bits.
    /// This forwards-compatibility doesn't matter when user code is only
    /// parsing a segment because we don't provide getters for any of those
    /// bits. However, it does matter when copying `DataOffsetReservedFlags`
    /// into new segments - in these cases, if we were to unconditionally set
    /// the reserved bits to zero, we could be changing the semantics of a TCP
    /// segment.
    #[derive(FromBytes, AsBytes, Unaligned, Copy, Clone, Debug, Default, Eq, PartialEq)]
    #[repr(transparent)]
    pub(super) struct DataOffsetReservedFlags(U16);

    impl DataOffsetReservedFlags {
        pub const EMPTY: DataOffsetReservedFlags = DataOffsetReservedFlags(U16::ZERO);
        pub const ACK_SET: DataOffsetReservedFlags =
            DataOffsetReservedFlags(U16::from_bytes(Self::ACK_FLAG_MASK.to_be_bytes()));

        const DATA_OFFSET_SHIFT: u8 = 12;
        const DATA_OFFSET_MAX: u8 = (1 << (16 - Self::DATA_OFFSET_SHIFT)) - 1;
        const DATA_OFFSET_MASK: u16 = (Self::DATA_OFFSET_MAX as u16) << Self::DATA_OFFSET_SHIFT;

        const ACK_FLAG_MASK: u16 = 0b10000;
        const PSH_FLAG_MASK: u16 = 0b01000;
        const RST_FLAG_MASK: u16 = 0b00100;
        const SYN_FLAG_MASK: u16 = 0b00010;
        const FIN_FLAG_MASK: u16 = 0b00001;

        #[cfg(test)]
        pub fn new(data_offset: u8) -> DataOffsetReservedFlags {
            let mut ret = Self::EMPTY;
            ret.set_data_offset(data_offset);
            ret
        }

        pub fn set_data_offset(&mut self, data_offset: u8) {
            debug_assert!(data_offset <= Self::DATA_OFFSET_MAX);
            let v = self.0.get();
            self.0.set(
                (v & !Self::DATA_OFFSET_MASK) | (u16::from(data_offset)) << Self::DATA_OFFSET_SHIFT,
            );
        }

        pub fn data_offset(&self) -> u8 {
            (self.0.get() >> 12) as u8
        }

        fn get_flag(&self, mask: u16) -> bool {
            self.0.get() & mask > 0
        }

        pub fn ack(&self) -> bool {
            self.get_flag(Self::ACK_FLAG_MASK)
        }

        pub fn psh(&self) -> bool {
            self.get_flag(Self::PSH_FLAG_MASK)
        }

        pub fn rst(&self) -> bool {
            self.get_flag(Self::RST_FLAG_MASK)
        }

        pub fn syn(&self) -> bool {
            self.get_flag(Self::SYN_FLAG_MASK)
        }

        pub fn fin(&self) -> bool {
            self.get_flag(Self::FIN_FLAG_MASK)
        }

        fn set_flag(&mut self, mask: u16, set: bool) {
            let v = self.0.get();
            self.0.set(if set { v | mask } else { v & !mask });
        }

        pub fn set_psh(&mut self, psh: bool) {
            self.set_flag(Self::PSH_FLAG_MASK, psh);
        }

        pub fn set_rst(&mut self, rst: bool) {
            self.set_flag(Self::RST_FLAG_MASK, rst)
        }

        pub fn set_syn(&mut self, syn: bool) {
            self.set_flag(Self::SYN_FLAG_MASK, syn)
        }

        pub fn set_fin(&mut self, fin: bool) {
            self.set_flag(Self::FIN_FLAG_MASK, fin)
        }
    }
}

/// A TCP segment.
///
/// A `TcpSegment` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
///
/// A `TcpSegment` - whether parsed using `parse` or created using
/// `TcpSegmentBuilder` - maintains the invariant that the checksum is always
/// valid.
pub struct TcpSegment<B> {
    hdr_prefix: LayoutVerified<B, HeaderPrefix>,
    // Invariant: At most MAX_OPTIONS_LEN bytes long. This guarantees that we
    // can store these in an `ArrayVec<u8, MAX_OPTIONS_LEN>` in `builder`.
    options: Options<B>,
    body: B,
}

/// Arguments required to parse a TCP segment.
pub struct TcpParseArgs<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
}

impl<A: IpAddress> TcpParseArgs<A> {
    /// Construct a new `TcpParseArgs`.
    pub fn new(src_ip: A, dst_ip: A) -> TcpParseArgs<A> {
        TcpParseArgs { src_ip, dst_ip }
    }
}

impl<B: ByteSlice, A: IpAddress> ParsablePacket<B, TcpParseArgs<A>> for TcpSegment<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.hdr_prefix.bytes().len() + self.options.bytes().len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(buffer: BV, args: TcpParseArgs<A>) -> ParseResult<Self> {
        TcpSegmentRaw::<B>::parse(buffer, ()).and_then(|u| TcpSegment::try_from_raw_with(u, args))
    }
}

impl<B: ByteSlice, A: IpAddress> FromRaw<TcpSegmentRaw<B>, TcpParseArgs<A>> for TcpSegment<B> {
    type Error = ParseError;

    fn try_from_raw_with(
        raw: TcpSegmentRaw<B>,
        args: TcpParseArgs<A>,
    ) -> Result<Self, Self::Error> {
        // See for details: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure

        let hdr_prefix = raw
            .hdr_prefix
            .ok_or_else(|_| debug_err!(ParseError::Format, "too few bytes for header"))?;
        let options = raw
            .options
            .ok_or_else(|_| debug_err!(ParseError::Format, "Incomplete options"))
            .and_then(|o| {
                Options::try_from_raw(o)
                    .map_err(|_| debug_err!(ParseError::Format, "Options validation failed"))
            })?;
        let body = raw.body;

        let hdr_bytes = (hdr_prefix.data_offset() * 4) as usize;
        if hdr_bytes != hdr_prefix.bytes().len() + options.bytes().len() {
            return debug_err!(
                Err(ParseError::Format),
                "invalid data offset: {} for header={} + options={}",
                hdr_prefix.data_offset(),
                hdr_prefix.bytes().len(),
                options.bytes().len()
            );
        }

        let parts = [hdr_prefix.bytes(), options.bytes(), body.deref().as_ref()];
        let checksum = compute_transport_checksum_parts(
            args.src_ip,
            args.dst_ip,
            IpProto::Tcp.into(),
            parts.iter(),
        )
        .ok_or_else(debug_err_fn!(ParseError::Format, "segment too large"))?;

        if checksum != [0, 0] {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }

        if hdr_prefix.src_port == U16::ZERO || hdr_prefix.dst_port == U16::ZERO {
            return debug_err!(Err(ParseError::Format), "zero source or destination port");
        }

        Ok(TcpSegment { hdr_prefix, options, body })
    }
}

impl<B: ByteSlice> TcpSegment<B> {
    /// Iterate over the TCP header options.
    pub fn iter_options(&self) -> impl Iterator<Item = TcpOption<'_>> + Debug + Clone {
        self.options.iter()
    }

    /// The segment body.
    pub fn body(&self) -> &[u8] {
        &self.body
    }

    /// Consumes this packet and returns the body.
    ///
    /// Note that the returned `B` has the same lifetime as the buffer from
    /// which this segment was parsed. By contrast, the [`body`] method returns
    /// a slice with the same lifetime as the receiver.
    ///
    /// [`body`]: TcpSegment::body
    pub fn into_body(self) -> B {
        self.body
    }

    /// The source port.
    pub fn src_port(&self) -> NonZeroU16 {
        // Infallible because this was already validated in parse
        NonZeroU16::new(self.hdr_prefix.src_port.get()).unwrap()
    }

    /// The destination port.
    pub fn dst_port(&self) -> NonZeroU16 {
        // Infallible because this was already validated in parse
        NonZeroU16::new(self.hdr_prefix.dst_port.get()).unwrap()
    }

    /// The sequence number.
    pub fn seq_num(&self) -> u32 {
        self.hdr_prefix.seq_num.get()
    }

    /// The acknowledgement number.
    ///
    /// If the ACK flag is not set, `ack_num` returns `None`.
    pub fn ack_num(&self) -> Option<u32> {
        self.hdr_prefix.ack_num()
    }

    /// The PSH flag.
    pub fn psh(&self) -> bool {
        self.hdr_prefix.data_offset_reserved_flags.psh()
    }

    /// The RST flag.
    pub fn rst(&self) -> bool {
        self.hdr_prefix.data_offset_reserved_flags.rst()
    }

    /// The SYN flag.
    pub fn syn(&self) -> bool {
        self.hdr_prefix.data_offset_reserved_flags.syn()
    }

    /// The FIN flag.
    pub fn fin(&self) -> bool {
        self.hdr_prefix.data_offset_reserved_flags.fin()
    }

    /// The sender's window size.
    pub fn window_size(&self) -> u16 {
        self.hdr_prefix.window_size.get()
    }

    /// The length of the header prefix and options.
    pub fn header_len(&self) -> usize {
        self.hdr_prefix.bytes().len() + self.options.bytes().len()
    }

    // The length of the segment as calculated from the header prefix, options,
    // and body.
    // TODO(rheacock): remove `allow(dead_code)` when this is used.
    #[allow(dead_code)]
    fn total_segment_len(&self) -> usize {
        self.header_len() + self.body.len()
    }

    /// Constructs a builder with the same contents as this packet.
    pub fn builder<A: IpAddress>(
        &self,
        src_ip: A,
        dst_ip: A,
    ) -> TcpSegmentBuilderWithOptions<A, ArrayVec<u8, MAX_OPTIONS_LEN>> {
        TcpSegmentBuilderWithOptions {
            prefix_builder: TcpSegmentBuilder {
                src_ip,
                dst_ip,
                src_port: Some(self.src_port()),
                dst_port: Some(self.dst_port()),
                seq_num: self.seq_num(),
                ack_num: self.hdr_prefix.ack.get(),
                data_offset_reserved_flags: self.hdr_prefix.data_offset_reserved_flags,
                window_size: self.window_size(),
            },
            // By using the raw bytes rather than parsing and using the
            // resulting iterator to construct a new builder for the options, we
            // ensure that:
            // - even if we fail to parse some options, we can still construct a
            //   builder
            // - even if our serializing code makes different choices about how
            //   to serialize (in cases in which multiple serializations are
            //   valid), the resulting builder still produces an identical
            //   packet to the original
            options: {
                let mut options = ArrayVec::new();
                // Safe because we validate that `self.options` is no longer
                // than MAX_OPTIONS_LEN.
                expect!(
                    options.try_extend_from_slice(self.options.bytes()),
                    "TCP segment options longer than {} bytes",
                    MAX_OPTIONS_LEN
                );
                options
            },
        }
    }

    /// Consumes this segment and constructs a [`Serializer`] with the same
    /// contents.
    ///
    /// The returned `Serializer` has the [`Buffer`] type [`EmptyBuf`], which
    /// means it is not able to reuse the buffer backing this `TcpSegment` when
    /// serializing, and will always need to allocate a new buffer.
    ///
    /// By consuming `self` instead of taking it by-reference, `into_serializer`
    /// is able to return a `Serializer` whose lifetime is restricted by the
    /// lifetime of the buffer from which this `TcpSegment` was parsed rather
    /// than by the lifetime on `&self`, which may be more restricted.
    ///
    /// [`Buffer`]: packet::Serializer::Buffer
    pub fn into_serializer<'a, A: IpAddress>(
        self,
        src_ip: A,
        dst_ip: A,
    ) -> impl Serializer<Buffer = EmptyBuf> + 'a
    where
        B: 'a,
    {
        let builder = self.builder(src_ip, dst_ip);
        ByteSliceInnerPacketBuilder(self.body).into_serializer().encapsulate(builder)
    }
}

/// The minimal information required from a TCP segment header.
///
/// A `TcpFlowHeader` may be the result of a partially parsed TCP segment in
/// [`TcpSegmentRaw`].
#[derive(Debug, Default, FromBytes, AsBytes, Unaligned, PartialEq)]
#[repr(C)]
struct TcpFlowHeader {
    src_port: U16,
    dst_port: U16,
}

#[derive(Debug)]
struct PartialHeaderPrefix<B: ByteSlice> {
    flow: LayoutVerified<B, TcpFlowHeader>,
    rest: B,
}

/// A partially-parsed and not yet validated TCP segment.
///
/// A `TcpSegmentRaw` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
///
/// Parsing a `TcpSegmentRaw` from raw data will succeed as long as at least 4
/// bytes are available, which will be extracted as a [`TcpFlowHeader`] that
/// contains the TCP source and destination ports. A `TcpSegmentRaw` is, then,
/// guaranteed to always have at least that minimal information available.
///
/// [`TcpSegment`] provides a [`FromRaw`] implementation that can be used to
/// validate a `TcpSegmentRaw`.
pub struct TcpSegmentRaw<B: ByteSlice> {
    hdr_prefix: MaybeParsed<LayoutVerified<B, HeaderPrefix>, PartialHeaderPrefix<B>>,
    // Invariant: At most MAX_OPTIONS_LEN bytes long. This guarantees that we
    // can store these in an `ArrayVec<u8, MAX_OPTIONS_LEN>` in `builder`.
    options: MaybeParsed<OptionsRaw<B, TcpOptionsImpl>, B>,
    body: B,
}

impl<B> ParsablePacket<B, ()> for TcpSegmentRaw<B>
where
    B: ByteSlice,
{
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.options.len()
            + match &self.hdr_prefix {
                MaybeParsed::Complete(h) => h.bytes().len(),
                MaybeParsed::Incomplete(h) => h.flow.bytes().len() + h.rest.len(),
            };
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> ParseResult<Self> {
        // See for details: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure

        let (hdr_prefix, options) = if let Some(pfx) = buffer.take_obj_front::<HeaderPrefix>() {
            // If the subtraction data_offset*4 - HDR_PREFIX_LEN would have been
            // negative, that would imply that data_offset has an invalid value.
            // Even though this will end up being MaybeParsed::Complete, the
            // data_offset value is validated when transforming TcpSegmentRaw to
            // TcpSegment.
            //
            // `options_bytes` upholds the invariant of being no more than
            // `MAX_OPTIONS_LEN` (40) bytes long because the Data Offset field
            // is a 4-bit field with a maximum value of 15. Thus, the maximum
            // value of `pfx.data_offset() * 4` is 15 * 4 = 60, so subtracting
            // `HDR_PREFIX_LEN` (20) leads to a maximum possible value of 40.
            let options_bytes = usize::from(pfx.data_offset() * 4).saturating_sub(HDR_PREFIX_LEN);
            debug_assert!(options_bytes <= MAX_OPTIONS_LEN, "options_bytes: {}", options_bytes);
            let options =
                MaybeParsed::take_from_buffer_with(&mut buffer, options_bytes, OptionsRaw::new);
            let hdr_prefix = MaybeParsed::Complete(pfx);
            (hdr_prefix, options)
        } else {
            let flow = buffer
                .take_obj_front::<TcpFlowHeader>()
                .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for flow header"))?;
            let rest = buffer.take_rest_front();
            // if we can't take the entire header, the rest of options will be
            // incomplete:
            let hdr_prefix = MaybeParsed::Incomplete(PartialHeaderPrefix { flow, rest });
            let options = MaybeParsed::Incomplete(buffer.take_rest_front());
            (hdr_prefix, options)
        };

        // A TCP segment's body is always just the rest of the buffer:
        let body = buffer.into_rest();

        Ok(Self { hdr_prefix, options, body })
    }
}

impl<B: ByteSlice> TcpSegmentRaw<B> {
    /// Constructs a builder with the same contents as this packet.
    ///
    /// Returns `None` if an entire TCP header was not successfully parsed.
    ///
    /// Note that, since `TcpSegmentRaw` does not validate its header fields,
    /// it's possible for `builder` to produce a `TcpSegmentBuilder` which
    /// describes an invalid TCP segment, or one which this module is not
    /// capable of building from scratch. In particular:
    /// - The source or destination ports may be zero, which is illegal (these
    ///   ports are reserved in IANA's [Service Name and Transport Protocol Port
    ///   Number Registry]).
    /// - The ACK number may be nonzero even though the ACK flag is not set.
    ///   This is not illegal according to [RFC 793], but it's possible that
    ///   some implementations expect it not to happen.
    /// - Some of the reserved zero bits between the Data Offset and Flags
    ///   fields may be set. This may be due to a noncompliant implementation or
    ///   a future change to TCP which makes use of these bits.
    ///
    /// [Service Name and Transport Protocol Port Number Registry]: https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml
    /// [RFC 793]: https://datatracker.ietf.org/doc/html/rfc793
    pub fn builder<A: IpAddress>(
        &self,
        src_ip: A,
        dst_ip: A,
    ) -> Option<TcpSegmentBuilderWithOptions<A, ArrayVec<u8, MAX_OPTIONS_LEN>>> {
        self.hdr_prefix
            .as_ref()
            .complete()
            .ok_checked::<&PartialHeaderPrefix<B>>()
            .zip(self.options.as_ref().complete().ok_checked::<&B>())
            .map(|(hdr_prefix, options)| TcpSegmentBuilderWithOptions {
                prefix_builder: TcpSegmentBuilder {
                    src_ip,
                    dst_ip,
                    // Might be zero, which is illegal.
                    src_port: NonZeroU16::new(hdr_prefix.src_port.get()),
                    // Might be zero, which is illegal.
                    dst_port: NonZeroU16::new(hdr_prefix.dst_port.get()),
                    // All values are valid.
                    seq_num: hdr_prefix.seq_num.get(),
                    // Might be nonzero even if the ACK flag is not set.
                    ack_num: hdr_prefix.ack.get(),
                    // Reserved zero bits may be set.
                    data_offset_reserved_flags: hdr_prefix.data_offset_reserved_flags,
                    // All values are valid.
                    window_size: hdr_prefix.window_size.get(),
                },
                options: {
                    let mut opts = ArrayVec::new();
                    // Safe because we validate that `self.options` is no longer
                    // than MAX_OPTIONS_LEN.
                    expect!(
                        opts.try_extend_from_slice(options.bytes()),
                        "TCP segment options longer than {} bytes",
                        MAX_OPTIONS_LEN
                    );
                    opts
                },
            })
    }

    /// Consumes this segment and constructs a [`Serializer`] with the same
    /// contents.
    ///
    /// Returns `None` if an entire TCP header was not successfully parsed.
    ///
    /// This method has the same validity caveats as [`builder`].
    ///
    /// The returned `Serializer` has the [`Buffer`] type [`EmptyBuf`], which
    /// means it is not able to reuse the buffer backing this `TcpSegmentRaw`
    /// when serializing, and will always need to allocate a new buffer.
    ///
    /// By consuming `self` instead of taking it by-reference, `into_serializer`
    /// is able to return a `Serializer` whose lifetime is restricted by the
    /// lifetime of the buffer from which this `TcpSegmentRaw` was parsed rather
    /// than by the lifetime on `&self`, which may be more restricted.
    ///
    /// [`builder`]: TcpSegmentRaw::builder
    /// [`Buffer`]: packet::Serializer::Buffer
    pub fn into_serializer<'a, A: IpAddress>(
        self,
        src_ip: A,
        dst_ip: A,
    ) -> Option<impl Serializer<Buffer = EmptyBuf> + 'a>
    where
        B: 'a,
    {
        self.builder(src_ip, dst_ip).map(|builder| {
            ByteSliceInnerPacketBuilder(self.body).into_serializer().encapsulate(builder)
        })
    }
}

/// An options parser for TCP options.
///
/// See [`Options`] for more details.
///
/// [`Options`]: packet::records::options::Options
type Options<B> = packet::records::options::Options<B, TcpOptionsImpl>;

/// An option sequence builder for TCP options.
///
/// See [`OptionSequenceBuilder`] for more details.
///
/// [`OptionSequenceBuilder`]: packet::records::options::OptionSequenceBuilder
type OptionSequenceBuilder<'a, I> =
    packet::records::options::OptionSequenceBuilder<TcpOption<'a>, I>;

/// Options provided to [`TcpSegmentBuilderWithOptions::new`] exceed
/// [`MAX_OPTIONS_LEN`] when serialized.
#[derive(Debug)]
pub struct TcpOptionsTooLongError;

/// A builder for TCP segments with options
#[derive(Debug)]
pub struct TcpSegmentBuilderWithOptions<A: IpAddress, O> {
    prefix_builder: TcpSegmentBuilder<A>,
    options: O,
}

impl<'a, A, I> TcpSegmentBuilderWithOptions<A, OptionSequenceBuilder<'a, I>>
where
    A: IpAddress,
    I: Iterator + Clone,
    I::Item: Borrow<TcpOption<'a>>,
{
    /// Creates a `TcpSegmentBuilderWithOptions`.
    ///
    /// Returns `Err` if the segment header would exceed the maximum length of
    /// [`MAX_HDR_LEN`]. This happens if the `options`, when serialized, would
    /// exceed [`MAX_OPTIONS_LEN`].
    pub fn new<T: IntoIterator<IntoIter = I>>(
        prefix_builder: TcpSegmentBuilder<A>,
        options: T,
    ) -> Result<TcpSegmentBuilderWithOptions<A, OptionSequenceBuilder<'a, I>>, TcpOptionsTooLongError>
    {
        let options = OptionSequenceBuilder::new(options.into_iter());
        if options.serialized_len() > MAX_OPTIONS_LEN {
            return Err(TcpOptionsTooLongError);
        }
        Ok(TcpSegmentBuilderWithOptions { prefix_builder, options })
    }
}

impl<A: IpAddress, O: InnerPacketBuilder> TcpSegmentBuilderWithOptions<A, O> {
    fn aligned_options_len(&self) -> usize {
        // Round up to the next 4-byte boundary.
        crate::utils::round_to_next_multiple_of_four(self.options.bytes_len())
    }
}

impl<A: IpAddress, O: InnerPacketBuilder> PacketBuilder for TcpSegmentBuilderWithOptions<A, O> {
    fn constraints(&self) -> PacketConstraints {
        let header_len = HDR_PREFIX_LEN + self.aligned_options_len();
        assert_eq!(header_len % 4, 0);
        PacketConstraints::new(header_len, 0, 0, (1 << 16) - 1 - header_len)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let (mut header, _body, _footer): (_, &mut FragmentedByteSlice<'_, _>, &mut [u8]) =
            buffer.parts();
        // `&mut header` has type `&mut &mut [u8]`, which implements
        // `BufferViewMut`, giving us the `take_back_zero` method.
        let mut header = &mut header;
        let opt_len = self.aligned_options_len();
        let options = header.take_back_zero(opt_len).expect("too few bytes for TCP options");
        self.options.serialize(options);
        self.prefix_builder.serialize(buffer);
    }
}

// NOTE(joshlf): In order to ensure that the checksum is always valid, we don't
// expose any setters for the fields of the TCP segment; the only way to set
// them is via TcpSegmentBuilder. This, combined with checksum validation
// performed in TcpSegment::parse, provides the invariant that a UdpPacket
// always has a valid checksum.

/// A builder for TCP segments.
#[derive(Copy, Clone, Debug)]
pub struct TcpSegmentBuilder<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
    src_port: Option<NonZeroU16>,
    dst_port: Option<NonZeroU16>,
    seq_num: u32,
    ack_num: u32,
    data_offset_reserved_flags: DataOffsetReservedFlags,
    window_size: u16,
}

impl<A: IpAddress> TcpSegmentBuilder<A> {
    /// Constructs a new `TcpSegmentBuilder`.
    ///
    /// If `ack_num` is `Some`, then the ACK flag will be set.
    pub fn new(
        src_ip: A,
        dst_ip: A,
        src_port: NonZeroU16,
        dst_port: NonZeroU16,
        seq_num: u32,
        ack_num: Option<u32>,
        window_size: u16,
    ) -> TcpSegmentBuilder<A> {
        let (data_offset_reserved_flags, ack_num) = ack_num
            .map(|a| (DataOffsetReservedFlags::ACK_SET, a))
            .unwrap_or((DataOffsetReservedFlags::EMPTY, 0));
        TcpSegmentBuilder {
            src_ip,
            dst_ip,
            src_port: Some(src_port),
            dst_port: Some(dst_port),
            seq_num,
            ack_num,
            data_offset_reserved_flags,
            window_size,
        }
    }

    /// Sets the PSH flag.
    pub fn psh(&mut self, psh: bool) {
        self.data_offset_reserved_flags.set_psh(psh);
    }

    /// Sets the RST flag.
    pub fn rst(&mut self, rst: bool) {
        self.data_offset_reserved_flags.set_rst(rst);
    }

    /// Sets the SYN flag.
    pub fn syn(&mut self, syn: bool) {
        self.data_offset_reserved_flags.set_syn(syn);
    }

    /// Sets the FIN flag.
    pub fn fin(&mut self, fin: bool) {
        self.data_offset_reserved_flags.set_fin(fin);
    }
}

impl<A: IpAddress> PacketBuilder for TcpSegmentBuilder<A> {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(HDR_PREFIX_LEN, 0, 0, core::usize::MAX)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let mut header = buffer.header();
        let hdr_len = header.len();
        // Implements BufferViewMut, giving us write_obj_front method.
        let mut header = &mut header;

        debug_assert_eq!(hdr_len % 4, 0, "header length isn't a multiple of 4: {}", hdr_len);
        let mut data_offset_reserved_flags = self.data_offset_reserved_flags;
        data_offset_reserved_flags.set_data_offset(
            (hdr_len / 4).try_into().expect("header length too long for TCP segment"),
        );
        header
            .write_obj_front(&HeaderPrefix::new(
                self.src_port.map_or(0, NonZeroU16::get),
                self.dst_port.map_or(0, NonZeroU16::get),
                self.seq_num,
                self.ack_num,
                data_offset_reserved_flags,
                self.window_size,
                // Initialize the checksum to 0 so that we will get the
                // correct value when we compute it below.
                [0, 0],
                // We don't support setting the Urgent Pointer.
                0,
            ))
            .expect("too few bytes for TCP header prefix");

        #[rustfmt::skip]
        let checksum = compute_transport_checksum_serialize(
            self.src_ip,
            self.dst_ip,
            IpProto::Tcp.into(),
            buffer,
        )
        .unwrap_or_else(|| {
            panic!(
                "total TCP segment length of {} bytes overflows length field of pseudo-header",
                buffer.len()
            )
        });
        buffer.header()[CHECKSUM_RANGE].copy_from_slice(&checksum[..]);
    }
}

/// Parsing and serialization of TCP options.
pub mod options {
    use packet::records::options::{OptionBuilder, OptionLayout, OptionParseLayout, OptionsImpl};
    use packet::BufferViewMut as _;
    use zerocopy::byteorder::{ByteOrder, NetworkEndian};
    use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

    use super::*;
    use crate::U32;

    const OPTION_KIND_EOL: u8 = 0;
    const OPTION_KIND_NOP: u8 = 1;
    const OPTION_KIND_MSS: u8 = 2;
    const OPTION_KIND_WINDOW_SCALE: u8 = 3;
    const OPTION_KIND_SACK_PERMITTED: u8 = 4;
    const OPTION_KIND_SACK: u8 = 5;
    const OPTION_KIND_TIMESTAMP: u8 = 8;

    /// A TCP header option.
    ///
    /// A TCP header option comprises an option kind byte, a length, and the
    /// option data itself.
    ///
    /// See [Wikipedia] or [RFC 793] for more details.
    ///
    /// [Wikipedia]: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure
    /// [RFC 793]: https://tools.ietf.org/html/rfc793#page-17
    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    pub enum TcpOption<'a> {
        /// A Maximum Segment Size (MSS) option.
        Mss(u16),
        /// A window scale option.
        WindowScale(u8),
        /// A selective ACK permitted option.
        SackPermitted,
        /// A selective ACK option.
        ///
        /// A variable-length number of selective ACK blocks. The length is in
        /// the range [0, 4].
        Sack(&'a [TcpSackBlock]),
        /// A timestamp option.
        #[allow(missing_docs)]
        Timestamp { ts_val: u32, ts_echo_reply: u32 },
    }

    /// A TCP selective ACK block.
    ///
    /// A selective ACK block indicates that the range of bytes `[left_edge,
    /// right_edge)` have been received.
    ///
    /// See [RFC 2018] for more details.
    ///
    /// [RFC 2018]: https://tools.ietf.org/html/rfc2018
    #[derive(Copy, Clone, Eq, PartialEq, Debug, FromBytes, AsBytes, Unaligned)]
    #[repr(C)]
    pub struct TcpSackBlock {
        left_edge: U32,
        right_edge: U32,
    }

    impl TcpSackBlock {
        /// Returns a `TcpSackBlock` with the specified left and right edge values.
        pub fn new(left_edge: u32, right_edge: u32) -> TcpSackBlock {
            TcpSackBlock { left_edge: U32::new(left_edge), right_edge: U32::new(right_edge) }
        }

        /// Returns the left edge of the SACK block.
        pub fn left_edge(&self) -> u32 {
            self.left_edge.get()
        }

        /// Returns the right edge of the SACK block.
        pub fn right_edge(&self) -> u32 {
            self.right_edge.get()
        }
    }

    /// An implementation of [`OptionsImpl`] for TCP options.
    #[derive(Copy, Clone, Debug)]
    pub struct TcpOptionsImpl;

    impl OptionLayout for TcpOptionsImpl {
        type KindLenField = u8;
    }

    impl OptionParseLayout for TcpOptionsImpl {
        type Error = ();
        const END_OF_OPTIONS: Option<u8> = Some(0);
        const NOP: Option<u8> = Some(1);
    }

    impl<'a> OptionsImpl<'a> for TcpOptionsImpl {
        type Option = TcpOption<'a>;

        fn parse(kind: u8, data: &'a [u8]) -> Result<Option<TcpOption<'a>>, ()> {
            match kind {
                self::OPTION_KIND_EOL | self::OPTION_KIND_NOP => {
                    unreachable!("records::options::Options promises to handle EOL and NOP")
                }
                self::OPTION_KIND_MSS => {
                    if data.len() != 2 {
                        Err(())
                    } else {
                        Ok(Some(TcpOption::Mss(NetworkEndian::read_u16(&data))))
                    }
                }
                self::OPTION_KIND_WINDOW_SCALE => {
                    if data.len() != 1 {
                        Err(())
                    } else {
                        Ok(Some(TcpOption::WindowScale(data[0])))
                    }
                }
                self::OPTION_KIND_SACK_PERMITTED => {
                    if !data.is_empty() {
                        Err(())
                    } else {
                        Ok(Some(TcpOption::SackPermitted))
                    }
                }
                self::OPTION_KIND_SACK => Ok(Some(TcpOption::Sack(
                    LayoutVerified::new_slice(data).ok_or(())?.into_slice(),
                ))),
                self::OPTION_KIND_TIMESTAMP => {
                    if data.len() != 8 {
                        Err(())
                    } else {
                        let ts_val = NetworkEndian::read_u32(&data);
                        let ts_echo_reply = NetworkEndian::read_u32(&data[4..]);
                        Ok(Some(TcpOption::Timestamp { ts_val, ts_echo_reply }))
                    }
                }
                _ => Ok(None),
            }
        }
    }

    impl<'a> OptionBuilder for TcpOption<'a> {
        type Layout = TcpOptionsImpl;

        fn serialized_len(&self) -> usize {
            match self {
                TcpOption::Mss(mss) => mss.as_bytes().len(),
                TcpOption::WindowScale(ws) => ws.as_bytes().len(),
                TcpOption::SackPermitted => 0,
                TcpOption::Sack(sack) => sack.as_bytes().len(),
                TcpOption::Timestamp { ts_val, ts_echo_reply } => {
                    ts_val.as_bytes().len() + ts_echo_reply.as_bytes().len()
                }
            }
        }

        fn option_kind(&self) -> u8 {
            match self {
                TcpOption::Mss(_) => OPTION_KIND_MSS,
                TcpOption::WindowScale(_) => OPTION_KIND_WINDOW_SCALE,
                TcpOption::SackPermitted => OPTION_KIND_SACK_PERMITTED,
                TcpOption::Sack(_) => OPTION_KIND_SACK,
                TcpOption::Timestamp { .. } => OPTION_KIND_TIMESTAMP,
            }
        }

        fn serialize_into(&self, mut buffer: &mut [u8]) {
            let mut buffer = &mut buffer;
            match self {
                TcpOption::Mss(mss) => buffer.write_obj_front(&U16::new(*mss)),
                TcpOption::WindowScale(ws) => buffer.write_obj_front(ws),
                TcpOption::SackPermitted => Some(()),
                TcpOption::Sack(block) => buffer.write_obj_front(*block),
                TcpOption::Timestamp { ts_val, ts_echo_reply } => buffer
                    .write_obj_front(&U32::new(*ts_val))
                    .and(buffer.write_obj_front(&U32::new(*ts_echo_reply))),
            }
            .expect("buffer too short for TCP header option")
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn test_tcp_sack_block() {
            let sack = TcpSackBlock::new(1, 2);
            assert_eq!(sack.left_edge.get(), 1);
            assert_eq!(sack.right_edge.get(), 2);
            assert_eq!(sack.left_edge(), 1);
            assert_eq!(sack.right_edge(), 2);
        }
    }
}

// needed by Result::unwrap_err in the tests below
#[cfg(test)]
impl<B> Debug for TcpSegment<B> {
    fn fmt(&self, fmt: &mut Formatter<'_>) -> fmt::Result {
        write!(fmt, "TcpSegment")
    }
}

#[cfg(test)]
mod tests {
    use net_types::ip::{Ipv4, Ipv4Addr, Ipv6Addr};
    use packet::{Buf, InnerPacketBuilder, ParseBuffer, Serializer};
    use std::num::NonZeroU16;
    use zerocopy::byteorder::{ByteOrder, NetworkEndian};

    use super::*;
    use crate::compute_transport_checksum;
    use crate::ethernet::{EthernetFrame, EthernetFrameLengthCheck};
    use crate::ipv4::{Ipv4Header, Ipv4Packet};
    use crate::ipv6::{Ipv6Header, Ipv6Packet};
    use crate::testutil::benchmarks::{black_box, Bencher};
    use crate::testutil::*;

    const TEST_SRC_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_DST_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SRC_IPV6: Ipv6Addr =
        Ipv6Addr::from_bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const TEST_DST_IPV6: Ipv6Addr =
        Ipv6Addr::from_bytes([17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]);

    #[test]
    fn test_parse_serialize_full_ipv4() {
        use crate::testdata::tls_client_hello_v4::*;

        let mut buf = ETHERNET_FRAME.bytes;
        let frame = buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let packet = body.parse::<Ipv4Packet<_>>().unwrap();
        verify_ipv4_packet(&packet, IPV4_PACKET);

        let mut body = packet.body();
        let segment = body
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(packet.src_ip(), packet.dst_ip()))
            .unwrap();
        verify_tcp_segment(&segment, TCP_SEGMENT);

        // Serialize using `segment.builder()` to construct a
        // `TcpSegmentBuilderWithOptions`, which simply copies the bytes of the
        // options without parsing or iterating over them.
        let buffer = Buf::new(segment.body().to_vec(), ..)
            .encapsulate(segment.builder(packet.src_ip(), packet.dst_ip()))
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME.bytes);

        // Serialize using a manually-created `TcpSegmentBuilderWithOptions`
        // which uses `segment.iter_options()` as its options. This allows us to
        // test both `iter_options` and serializing from an options iterator.
        //
        // Note that we cannot compare the serialized bytes verbatim. The reason
        // is that the TCP segment in `ETHERNET_FRAME` uses a different strategy
        // to ensure that the options are a multiple of four bytes in length
        // than we do (`ETHERNET_FRAME`'s approach is to add NOP options in the
        // middle of the sequence, while ours is to pad with End of Options
        // options at the end). Instead, we parse and verify the parsed segment.
        let mut buffer = Buf::new(segment.body().to_vec(), ..)
            .encapsulate(
                TcpSegmentBuilderWithOptions::new(
                    segment.builder(packet.src_ip(), packet.dst_ip()).prefix_builder,
                    segment.iter_options(),
                )
                .unwrap(),
            )
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        let _: EthernetFrame<_> = buffer.parse_with(EthernetFrameLengthCheck::Check).unwrap();
        let _: Ipv4Packet<_> = buffer.parse().unwrap();
        let segment = buffer
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(packet.src_ip(), packet.dst_ip()))
            .unwrap();
        verify_tcp_segment(&segment, TCP_SEGMENT);
    }

    #[test]
    fn test_parse_serialize_full_ipv6() {
        use crate::testdata::syn_v6::*;

        let mut buf = ETHERNET_FRAME.bytes;
        let frame = buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let packet = body.parse::<Ipv6Packet<_>>().unwrap();
        verify_ipv6_packet(&packet, IPV6_PACKET);

        let mut body = packet.body();
        let segment = body
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(packet.src_ip(), packet.dst_ip()))
            .unwrap();
        verify_tcp_segment(&segment, TCP_SEGMENT);

        // Serialize using `segment.builder()` to construct a
        // `TcpSegmentBuilderWithOptions`, which simply copies the bytes of the
        // options without parsing or iterating over them.
        let buffer = Buf::new(segment.body().to_vec(), ..)
            .encapsulate(segment.builder(packet.src_ip(), packet.dst_ip()))
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME.bytes);

        // Serialize using a manually-created `TcpSegmentBuilderWithOptions`
        // which uses `segment.iter_options()` as its options. This allows us to
        // test both `iter_options` and serializing from an options iterator.
        //
        // Note that we cannot compare the serialized bytes verbatim. The reason
        // is that the TCP segment in `ETHERNET_FRAME` uses a different strategy
        // to ensure that the options are a multiple of four bytes in length
        // than we do (`ETHERNET_FRAME`'s approach is to add NOP options in the
        // middle of the sequence, while ours is to pad with End of Options
        // options at the end). Instead, we parse and verify the parsed segment.
        let mut buffer = Buf::new(segment.body().to_vec(), ..)
            .encapsulate(
                TcpSegmentBuilderWithOptions::new(
                    segment.builder(packet.src_ip(), packet.dst_ip()).prefix_builder,
                    segment.iter_options(),
                )
                .unwrap(),
            )
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        let _: EthernetFrame<_> = buffer.parse_with(EthernetFrameLengthCheck::Check).unwrap();
        let _: Ipv6Packet<_> = buffer.parse().unwrap();
        let segment = buffer
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(packet.src_ip(), packet.dst_ip()))
            .unwrap();
        verify_tcp_segment(&segment, TCP_SEGMENT);
    }

    fn hdr_prefix_to_bytes(hdr_prefix: HeaderPrefix) -> [u8; HDR_PREFIX_LEN] {
        let mut bytes = [0; HDR_PREFIX_LEN];
        {
            let mut lv = LayoutVerified::<_, HeaderPrefix>::new_unaligned(&mut bytes[..]).unwrap();
            *lv = hdr_prefix;
        }
        bytes
    }

    // Return a new HeaderPrefix with reasonable defaults, including a valid
    // checksum (assuming no body and the src/dst IPs TEST_SRC_IPV4 and
    // TEST_DST_IPV4).
    fn new_hdr_prefix() -> HeaderPrefix {
        HeaderPrefix::new(1, 2, 0, 0, DataOffsetReservedFlags::new(5), 0, [0x9f, 0xce], 0)
    }

    #[test]
    fn test_parse() {
        let mut buf = &hdr_prefix_to_bytes(new_hdr_prefix())[..];
        let segment = buf
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
            .unwrap();
        assert_eq!(segment.src_port().get(), 1);
        assert_eq!(segment.dst_port().get(), 2);
        assert_eq!(segment.body(), []);
    }

    #[test]
    fn test_parse_error() {
        // Assert that parsing a particular header prefix results in an error.
        // This function is responsible for ensuring that the checksum is
        // correct so that checksum errors won't hide the errors we're trying to
        // test.
        fn assert_header_err(hdr_prefix: HeaderPrefix, err: ParseError) {
            let mut buf = &mut hdr_prefix_to_bytes(hdr_prefix)[..];
            NetworkEndian::write_u16(&mut buf[CHECKSUM_OFFSET..], 0);
            let checksum =
                compute_transport_checksum(TEST_SRC_IPV4, TEST_DST_IPV4, IpProto::Tcp.into(), buf)
                    .unwrap();
            buf[CHECKSUM_RANGE].copy_from_slice(&checksum[..]);
            assert_eq!(
                buf.parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
                    .unwrap_err(),
                err
            );
        }

        // Set the source port to 0, which is illegal.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.src_port = U16::ZERO;
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the destination port to 0, which is illegal.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.dst_port = U16::ZERO;
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the data offset to 4, implying a header length of 16. This is
        // smaller than the minimum of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.data_offset_reserved_flags = DataOffsetReservedFlags::new(4);
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the data offset to 6, implying a header length of 24. This is
        // larger than the actual segment length of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.data_offset_reserved_flags = DataOffsetReservedFlags::new(12);
        assert_header_err(hdr_prefix, ParseError::Format);
    }

    // Return a stock TcpSegmentBuilder with reasonable default values.
    fn new_builder<A: IpAddress>(src_ip: A, dst_ip: A) -> TcpSegmentBuilder<A> {
        TcpSegmentBuilder::new(
            src_ip,
            dst_ip,
            NonZeroU16::new(1).unwrap(),
            NonZeroU16::new(2).unwrap(),
            3,
            Some(4),
            5,
        )
    }

    #[test]
    fn test_serialize() {
        let mut builder = new_builder(TEST_SRC_IPV4, TEST_DST_IPV4);
        builder.fin(true);
        builder.rst(true);
        builder.syn(true);

        let mut buf = (&[0, 1, 2, 3, 3, 4, 5, 7, 8, 9])
            .into_serializer()
            .encapsulate(builder)
            .serialize_vec_outer()
            .unwrap();
        // assert that we get the literal bytes we expected
        assert_eq!(
            buf.as_ref(),
            [
                0, 1, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 80, 23, 0, 5, 141, 137, 0, 0, 0, 1, 2, 3, 3, 4,
                5, 7, 8, 9
            ]
        );
        let segment = buf
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
            .unwrap();
        // assert that when we parse those bytes, we get the values we set in
        // the builder
        assert_eq!(segment.src_port().get(), 1);
        assert_eq!(segment.dst_port().get(), 2);
        assert_eq!(segment.seq_num(), 3);
        assert_eq!(segment.ack_num(), Some(4));
        assert_eq!(segment.window_size(), 5);
        assert_eq!(segment.body(), [0, 1, 2, 3, 3, 4, 5, 7, 8, 9]);
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that TcpSegmentBuilder::serialize properly zeroes memory before
        // serializing the header.
        let mut buf_0 = [0; HDR_PREFIX_LEN];
        let _: Buf<&mut [u8]> = Buf::new(&mut buf_0[..], HDR_PREFIX_LEN..)
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
        let mut buf_1 = [0xFF; HDR_PREFIX_LEN];
        let _: Buf<&mut [u8]> = Buf::new(&mut buf_1[..], HDR_PREFIX_LEN..)
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    fn test_parse_serialize_reserved_bits() {
        // Test that we are forwards-compatible with the reserved zero bits in
        // the header being set - we can parse packets with these bits set and
        // we will not reject them. Test that we serialize these bits when
        // serializing from the `builder` methods.

        let mut buffer = (&[])
            .into_serializer()
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_b();

        // Set all three reserved bits and update the checksum.
        let mut hdr_prefix =
            LayoutVerified::<_, HeaderPrefix>::new_unaligned(buffer.as_mut()).unwrap();
        let old_checksum = hdr_prefix.checksum;
        let old_data_offset_reserved_flags = hdr_prefix.data_offset_reserved_flags;
        hdr_prefix.data_offset_reserved_flags.as_bytes_mut()[0] |= 0b00000111;
        hdr_prefix.checksum = internet_checksum::update(
            old_checksum,
            old_data_offset_reserved_flags.as_bytes(),
            hdr_prefix.data_offset_reserved_flags.as_bytes(),
        );

        let mut buf0 = buffer.clone();
        let mut buf1 = buffer.clone();

        let segment_raw = buf0.parse_with::<_, TcpSegmentRaw<_>>(()).unwrap();
        let segment = buf1
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
            .unwrap();

        // Serialize using the results of `TcpSegmentRaw::builder` and `TcpSegment::builder`.
        assert_eq!(
            (&[])
                .into_serializer()
                .encapsulate(segment_raw.builder(TEST_SRC_IPV4, TEST_DST_IPV4).unwrap())
                .serialize_vec_outer()
                .unwrap()
                .unwrap_b()
                .as_ref(),
            buffer.as_ref()
        );
        assert_eq!(
            (&[])
                .into_serializer()
                .encapsulate(segment.builder(TEST_SRC_IPV4, TEST_DST_IPV4))
                .serialize_vec_outer()
                .unwrap()
                .unwrap_b()
                .as_ref(),
            buffer.as_ref()
        );
    }

    #[test]
    #[should_panic(
        expected = "total TCP segment length of 65536 bytes overflows length field of pseudo-header"
    )]
    fn test_serialize_panic_segment_too_long_ipv4() {
        // Test that a segment length which overflows u16 is rejected because it
        // can't fit in the length field in the IPv4 pseudo-header.
        let _: Buf<&mut [u8]> = Buf::new(&mut [0; (1 << 16) - HDR_PREFIX_LEN][..], ..)
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
    }

    #[test]
    #[ignore] // this test panics with stack overflow; TODO(joshlf): Fix
    #[cfg(target_pointer_width = "64")] // 2^32 overflows on 32-bit platforms
    fn test_serialize_panic_segment_too_long_ipv6() {
        // Test that a segment length which overflows u32 is rejected because it
        // can't fit in the length field in the IPv4 pseudo-header.
        let _: Buf<&mut [u8]> = Buf::new(&mut [0; (1 << 32) - HDR_PREFIX_LEN][..], ..)
            .encapsulate(new_builder(TEST_SRC_IPV6, TEST_DST_IPV6))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
    }

    #[test]
    fn test_partial_parse() {
        use std::ops::Deref as _;

        // Parse options partially:
        let make_hdr_prefix = || {
            let mut hdr_prefix = new_hdr_prefix();
            hdr_prefix.data_offset_reserved_flags.set_data_offset(8);
            hdr_prefix
        };
        let hdr_prefix = hdr_prefix_to_bytes(make_hdr_prefix());
        let mut bytes = hdr_prefix[..].to_owned();
        const OPTIONS: &[u8] = &[1, 2, 3, 4, 5];
        bytes.extend(OPTIONS);
        let mut buf = &bytes[..];
        let packet = buf.parse::<TcpSegmentRaw<_>>().unwrap();
        let TcpSegmentRaw { hdr_prefix, options, body } = &packet;
        assert_eq!(hdr_prefix.as_ref().complete().unwrap().deref(), &make_hdr_prefix());
        assert_eq!(options.as_ref().incomplete().unwrap(), &OPTIONS);
        assert_eq!(body, &[]);
        // validation should fail:
        assert!(TcpSegment::try_from_raw_with(
            packet,
            TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4),
        )
        .is_err());

        // Parse header partially:
        let hdr_prefix = new_hdr_prefix();
        let HeaderPrefix { src_port, dst_port, .. } = hdr_prefix;
        let bytes = hdr_prefix_to_bytes(hdr_prefix);
        let mut buf = &bytes[0..10];
        // Copy the rest portion since the buffer is mutably borrowed after parsing.
        let bytes_rest = buf[4..].to_owned();
        let packet = buf.parse::<TcpSegmentRaw<_>>().unwrap();
        let TcpSegmentRaw { hdr_prefix, options, body } = &packet;
        let PartialHeaderPrefix { flow, rest } = hdr_prefix.as_ref().incomplete().unwrap();
        assert_eq!(flow.deref(), &TcpFlowHeader { src_port, dst_port });
        assert_eq!(*rest, &bytes_rest[..]);
        assert_eq!(options.as_ref().incomplete().unwrap(), &[]);
        assert_eq!(body, &[]);
        // validation should fail:
        assert!(TcpSegment::try_from_raw_with(
            packet,
            TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4),
        )
        .is_err());

        let hdr_prefix = new_hdr_prefix();
        let bytes = hdr_prefix_to_bytes(hdr_prefix);
        // If we don't even have enough header bytes, we should fail partial
        // parsing:
        let mut buf = &bytes[0..3];
        assert!(buf.parse::<TcpSegmentRaw<_>>().is_err());
        // If we don't even have exactly 4 header bytes, we should succeed
        // partial parsing:
        let mut buf = &bytes[0..4];
        assert!(buf.parse::<TcpSegmentRaw<_>>().is_ok());
    }

    //
    // Benchmarks
    //

    fn bench_parse_inner<B: Bencher>(b: &mut B) {
        use crate::testdata::tls_client_hello_v4::*;
        let bytes = parse_ip_packet_in_ethernet_frame::<Ipv4>(ETHERNET_FRAME.bytes).unwrap().0;

        b.iter(|| {
            let buf = bytes;
            let _: TcpSegment<_> = black_box(
                black_box(buf)
                    .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(
                        IPV4_PACKET.metadata.src_ip,
                        IPV4_PACKET.metadata.dst_ip,
                    ))
                    .unwrap(),
            );
        })
    }

    bench!(bench_parse, bench_parse_inner);

    fn bench_serialize_inner<B: Bencher>(b: &mut B) {
        use crate::testdata::tls_client_hello_v4::*;

        let builder = TcpSegmentBuilder::new(
            IPV4_PACKET.metadata.src_ip,
            IPV4_PACKET.metadata.dst_ip,
            NonZeroU16::new(TCP_SEGMENT.metadata.src_port).unwrap(),
            NonZeroU16::new(TCP_SEGMENT.metadata.dst_port).unwrap(),
            0,
            None,
            0,
        );

        let header_len = builder.constraints().header_len();
        let total_len = header_len + TCP_SEGMENT.bytes[TCP_SEGMENT.body_range].len();
        let mut buf = vec![0; total_len];
        buf[header_len..].copy_from_slice(&TCP_SEGMENT.bytes[TCP_SEGMENT.body_range]);

        b.iter(|| {
            let _: Buf<_> = black_box(
                black_box(Buf::new(&mut buf[..], header_len..total_len).encapsulate(builder))
                    .serialize_no_alloc_outer(),
            )
            .unwrap();
        })
    }

    bench!(bench_serialize, bench_serialize_inner);
}
