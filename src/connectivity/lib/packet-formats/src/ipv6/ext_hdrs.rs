// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv6 extension headers.
//!
//! The IPv6 extension header format is defined in [RFC 8200 Section 4].
//!
//! [RFC 8200 Section 4]: https://datatracker.ietf.org/doc/html/rfc8200#section-4

use core::convert::TryFrom;
use core::marker::PhantomData;

use packet::records::options::{
    AlignedOptionBuilder, LengthEncoding, OptionBuilder, OptionLayout, OptionParseLayout,
};
use packet::records::{
    ParsedRecord, RecordParseResult, Records, RecordsContext, RecordsImpl, RecordsImplLayout,
    RecordsRawImpl,
};
use packet::{BufferView, BufferViewMut};
use zerocopy::byteorder::{ByteOrder, NetworkEndian};

use crate::ip::{IpProto, Ipv6ExtHdrType, Ipv6Proto};
use crate::U16;

/// The length of an IPv6 Fragment Extension Header.
pub(crate) const IPV6_FRAGMENT_EXT_HDR_LEN: usize = 8;

/// An IPv6 Extension Header.
#[derive(Debug)]
pub struct Ipv6ExtensionHeader<'a> {
    // Marked as `pub(super)` because it is only used in tests within
    // the `crate::ipv6` (`super`) module.
    pub(super) next_header: u8,
    data: Ipv6ExtensionHeaderData<'a>,
}

impl<'a> Ipv6ExtensionHeader<'a> {
    /// Returns the extension header-specific data.
    pub fn data(&self) -> &Ipv6ExtensionHeaderData<'a> {
        &self.data
    }
}

/// The data associated with an IPv6 Extension Header.
#[allow(missing_docs)]
#[derive(Debug)]
pub enum Ipv6ExtensionHeaderData<'a> {
    HopByHopOptions { options: HopByHopOptionsData<'a> },
    Fragment { fragment_data: FragmentData<'a> },
    DestinationOptions { options: DestinationOptionsData<'a> },
}

//
// Records parsing for IPv6 Extension Header
//

/// Possible errors that can happen when parsing IPv6 Extension Headers.
#[allow(missing_docs)]
#[derive(Debug, PartialEq, Eq)]
pub(super) enum Ipv6ExtensionHeaderParsingError {
    // `pointer` is the offset from the beginning of the first extension header
    // to the point of error. `must_send_icmp` is a flag that requires us to send
    // an ICMP response if true. `header_len` is the size of extension headers before
    // encountering an error (number of bytes from successfully parsed
    // extension headers).
    ErroneousHeaderField {
        pointer: u32,
        must_send_icmp: bool,
        header_len: usize,
    },
    UnrecognizedNextHeader {
        pointer: u32,
        must_send_icmp: bool,
        header_len: usize,
    },
    UnrecognizedOption {
        pointer: u32,
        must_send_icmp: bool,
        header_len: usize,
        action: ExtensionHeaderOptionAction,
    },
    BufferExhausted,
    MalformedData,
}

/// Context that gets passed around when parsing IPv6 Extension Headers.
#[derive(Debug, Clone)]
pub(super) struct Ipv6ExtensionHeaderParsingContext {
    // Next expected header.
    // Marked as `pub(super)` because it is inly used in tests within
    // the `crate::ipv6` (`super`) module.
    pub(super) next_header: u8,

    // Whether context is being used for iteration or not.
    iter: bool,

    // Counter for number of extension headers parsed.
    headers_parsed: usize,

    // Byte count of successfully parsed extension headers.
    pub(super) bytes_parsed: usize,
}

impl Ipv6ExtensionHeaderParsingContext {
    /// Returns a new `Ipv6ExtensionHeaderParsingContext` which expects the
    /// first header to have the ID specified by `next_header`.
    pub(super) fn new(next_header: u8) -> Ipv6ExtensionHeaderParsingContext {
        Ipv6ExtensionHeaderParsingContext {
            iter: false,
            headers_parsed: 0,
            next_header,
            bytes_parsed: 0,
        }
    }
}

impl RecordsContext for Ipv6ExtensionHeaderParsingContext {
    fn clone_for_iter(&self) -> Self {
        let mut ret = self.clone();
        ret.iter = true;
        ret
    }
}

/// Implement the actual parsing of IPv6 Extension Headers.
#[derive(Debug)]
pub(super) struct Ipv6ExtensionHeaderImpl;

impl Ipv6ExtensionHeaderImpl {
    /// Make sure a Next Header value in an extension header is valid.
    fn valid_next_header(next_header: u8) -> bool {
        // Passing false to `is_valid_next_header`'s `for_fixed_header` parameter because
        // this function will never be called when checking the Next Header field
        // of the fixed header (which would be the first Next Header).
        is_valid_next_header(next_header, false)
    }

    /// Get the first two bytes if possible and return them.
    ///
    /// `get_next_hdr_and_len` takes the first two bytes from `data` and
    /// treats them as the Next Header and Hdr Ext Len fields. With the
    /// Next Header field, `get_next_hdr_and_len` makes sure it is a valid
    /// value before returning the Next Header and Hdr Ext Len fields.
    fn get_next_hdr_and_len<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &Ipv6ExtensionHeaderParsingContext,
    ) -> Result<(u8, u8), Ipv6ExtensionHeaderParsingError> {
        let next_header =
            data.take_byte_front().ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

        // Make sure we recognize the next header.
        // When parsing headers, if we encounter a next header value we don't
        // recognize, we SHOULD send back an ICMP response. Since we only SHOULD,
        // we set `must_send_icmp` to `false`.
        if !Self::valid_next_header(next_header) {
            return Err(Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                pointer: context.bytes_parsed as u32,
                must_send_icmp: false,
                header_len: context.bytes_parsed,
            });
        }

        let hdr_ext_len =
            data.take_byte_front().ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

        Ok((next_header, hdr_ext_len))
    }

    /// Parse Hop By Hop Options Extension Header.
    // TODO(ghanan): Look into implementing the IPv6 Jumbo Payload option
    //               (https://tools.ietf.org/html/rfc2675) and the router
    //               alert option (https://tools.ietf.org/html/rfc2711).
    fn parse_hop_by_hop_options<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Ipv6ExtensionHeaderParsingContext,
    ) -> Result<ParsedRecord<Ipv6ExtensionHeader<'a>>, Ipv6ExtensionHeaderParsingError> {
        let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;

        // As per RFC 8200 section 4.3, Hdr Ext Len is the length of this extension
        // header in  8-octect units, not including the first 8 octets (where 2 of
        // them are the Next Header and the Hdr Ext Len fields). Since we already
        // 'took' the Next Header and Hdr Ext Len octets, we need to make sure
        // we have (Hdr Ext Len) * 8 + 6 bytes bytes in `data`.
        let expected_len = (hdr_ext_len as usize) * 8 + 6;

        let options = data
            .take_front(expected_len)
            .ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

        let options_context = ExtensionHeaderOptionContext::new();
        let options = Records::parse_with_context(options, options_context).map_err(|e| {
            // We know the below `try_from` call will not result in a `None` value because
            // the maximum size of an IPv6 packet's payload (extension headers + body) is
            // `core::u32::MAX`. This maximum size is only possible when using IPv6
            // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
            // length. If we receive such a hypothetical packet with the maximum possible
            // payload length which only contains extension headers, we know that the offset
            // of any location within the payload must fit within an `u32`. If the packet is
            // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
            // `core::u16::MAX` (as the normal payload length field is only 16 bits), which
            // is significantly less than the maximum possible size of a jumbogram.
            ext_hdr_opt_err_to_ext_hdr_err(
                u32::try_from(context.bytes_parsed + 2).unwrap(),
                context.bytes_parsed,
                e,
            )
        })?;
        let options = HopByHopOptionsData::new(options);

        // Update context
        context.next_header = next_header;
        context.headers_parsed += 1;
        context.bytes_parsed += 2 + expected_len;

        Ok(ParsedRecord::Parsed(Ipv6ExtensionHeader {
            next_header,
            data: Ipv6ExtensionHeaderData::HopByHopOptions { options },
        }))
    }

    /// Parse Routing Extension Header.
    fn parse_routing<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Ipv6ExtensionHeaderParsingContext,
    ) -> Result<ParsedRecord<Ipv6ExtensionHeader<'a>>, Ipv6ExtensionHeaderParsingError> {
        // All routing extension headers (regardless of type) will have
        // 4 bytes worth of data we need to look at.
        let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;
        let routing_data =
            data.take_front(2).ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?;
        let segments_left = routing_data[1];

        // Currently we do not support any routing type.
        //
        // Note, this includes routing type 0 which is defined in RFC 2460 as it has been
        // deprecated as of RFC 5095 for security reasons.

        // If we receive a routing header with an unrecognized routing type,
        // what we do depends on the segments left. If segments left is
        // 0, we must ignore the routing header and continue processing
        // other headers. If segments left is not 0, we need to discard
        // this packet and send an ICMP Parameter Problem, Code 0 with a
        // pointer to this unrecognized routing type.
        if segments_left == 0 {
            // Take the next 4 and 8 * `hdr_ext_len` bytes to exhaust this extension header's
            // data so that that `data` will be at the front of the next header when this
            // function returns.
            let expected_len = (hdr_ext_len as usize) * 8 + 4;
            let _: &[u8] = data
                .take_front(expected_len)
                .ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

            // Update context
            context.next_header = next_header;
            context.headers_parsed += 1;
            context.bytes_parsed += expected_len;

            Ok(ParsedRecord::Skipped)
        } else {
            // As per RFC 8200, if we encounter a routing header with an unrecognized
            // routing type, and segments left is non-zero, we MUST discard the packet
            // and send and ICMP Parameter Problem response.
            Err(Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                pointer: (context.bytes_parsed as u32) + 2,
                must_send_icmp: true,
                header_len: context.bytes_parsed,
            })
        }
    }

    /// Parse Fragment Extension Header.
    fn parse_fragment<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Ipv6ExtensionHeaderParsingContext,
    ) -> Result<ParsedRecord<Ipv6ExtensionHeader<'a>>, Ipv6ExtensionHeaderParsingError> {
        // Fragment Extension Header requires exactly 8 bytes so make sure
        // `data` has at least 8 bytes left. If `data` has at least 8 bytes left,
        // we are guaranteed that all `take_front` calls done by this
        // method will succeed since we will never attempt to call `take_front`
        // with more than 8 bytes total.
        if data.len() < 8 {
            return Err(Ipv6ExtensionHeaderParsingError::BufferExhausted);
        }

        // For Fragment headers, we do not actually have a HdrExtLen field. Instead,
        // the second byte in the header (where HdrExtLen would normally exist), is
        // a reserved field, so we can simply ignore it for now.
        let (next_header, _) = Self::get_next_hdr_and_len(data, context)?;

        // Update context
        context.next_header = next_header;
        context.headers_parsed += 1;
        context.bytes_parsed += 8;

        Ok(ParsedRecord::Parsed(Ipv6ExtensionHeader {
            next_header,
            data: Ipv6ExtensionHeaderData::Fragment {
                fragment_data: FragmentData { bytes: data.take_front(6).unwrap() },
            },
        }))
    }

    /// Parse Destination Options Extension Header.
    fn parse_destination_options<'a, BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Ipv6ExtensionHeaderParsingContext,
    ) -> Result<ParsedRecord<Ipv6ExtensionHeader<'a>>, Ipv6ExtensionHeaderParsingError> {
        let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;

        // As per RFC 8200 section 4.6, Hdr Ext Len is the length of this extension
        // header in  8-octet units, not including the first 8 octets (where 2 of
        // them are the Next Header and the Hdr Ext Len fields).
        let expected_len = (hdr_ext_len as usize) * 8 + 6;

        let options = data
            .take_front(expected_len)
            .ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

        let options_context = ExtensionHeaderOptionContext::new();
        let options = Records::parse_with_context(options, options_context).map_err(|e| {
            // We know the below `try_from` call will not result in a `None` value because
            // the maximum size of an IPv6 packet's payload (extension headers + body) is
            // `core::u32::MAX`. This maximum size is only possible when using IPv6
            // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
            // length. If we receive such a hypothetical packet with the maximum possible
            // payload length which only contains extension headers, we know that the offset
            // of any location within the payload must fit within an `u32`. If the packet is
            // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
            // `core::u16::MAX` (as the normal payload length field is only 16 bits), which
            // is significantly less than the maximum possible size of a jumbogram.
            ext_hdr_opt_err_to_ext_hdr_err(
                u32::try_from(context.bytes_parsed + 2).unwrap(),
                context.bytes_parsed,
                e,
            )
        })?;
        let options = DestinationOptionsData::new(options);

        // Update context
        context.next_header = next_header;
        context.headers_parsed += 1;
        context.bytes_parsed += 2 + expected_len;

        Ok(ParsedRecord::Parsed(Ipv6ExtensionHeader {
            next_header,
            data: Ipv6ExtensionHeaderData::DestinationOptions { options },
        }))
    }
}

impl RecordsImplLayout for Ipv6ExtensionHeaderImpl {
    type Context = Ipv6ExtensionHeaderParsingContext;
    type Error = Ipv6ExtensionHeaderParsingError;
}

impl<'a> RecordsImpl<'a> for Ipv6ExtensionHeaderImpl {
    type Record = Ipv6ExtensionHeader<'a>;

    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> RecordParseResult<Self::Record, Self::Error> {
        let expected_hdr = context.next_header;

        match Ipv6ExtHdrType::from(expected_hdr) {
            Ipv6ExtHdrType::HopByHopOptions => Self::parse_hop_by_hop_options(data, context),
            Ipv6ExtHdrType::Routing => Self::parse_routing(data, context),
            Ipv6ExtHdrType::Fragment => Self::parse_fragment(data, context),
            Ipv6ExtHdrType::DestinationOptions => Self::parse_destination_options(data, context),
            _ => {
                if is_valid_next_header_upper_layer(expected_hdr) {
                    // Stop parsing extension headers when we find a Next Header value
                    // for a higher level protocol.
                    Ok(ParsedRecord::Done)
                } else {
                    // Should never end up here because we guarantee that if we hit an
                    // invalid Next Header field while parsing extension headers, we will
                    // return an error when we see it right away. Since the only other time
                    // `context.next_header` can get an invalid value assigned is when we parse
                    // the fixed IPv6 header, but we check if the next header is valid before
                    // parsing extension headers.

                    unreachable!(
                        "Should never try parsing an extension header with an unrecognized type"
                    );
                }
            }
        }
    }
}

impl<'a> RecordsRawImpl<'a> for Ipv6ExtensionHeaderImpl {
    fn parse_raw_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> Result<bool, Self::Error> {
        if is_valid_next_header_upper_layer(context.next_header) {
            Ok(false)
        } else {
            let (next, skip) = match Ipv6ExtHdrType::from(context.next_header) {
                Ipv6ExtHdrType::HopByHopOptions
                | Ipv6ExtHdrType::Routing
                | Ipv6ExtHdrType::DestinationOptions
                | Ipv6ExtHdrType::Other(_) => {
                    // take next header and header len, and skip the next 6
                    // octets + the number of 64 bit words in header len.
                    // NOTE: we can assume that Other will be parsed
                    //  as such based on the extensibility note in
                    //  RFC 8200 Section-4.8
                    data.take_front(2)
                        .map(|x| (x[0], (x[1] as usize) * 8 + 6))
                        .ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?
                }
                Ipv6ExtHdrType::Fragment => {
                    // take next header from first, then skip next 7
                    (
                        data.take_byte_front()
                            .ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?,
                        7,
                    )
                }
                Ipv6ExtHdrType::EncapsulatingSecurityPayload => {
                    // TODO(brunodalbo): We don't support ESP yet, so return
                    //  an error instead of panicking "unimplemented" to avoid
                    //  having a panic-path that can be remotely triggered.
                    return debug_err!(
                        Err(Ipv6ExtensionHeaderParsingError::MalformedData),
                        "ESP extension header not supported"
                    );
                }
                Ipv6ExtHdrType::Authentication => {
                    // take next header and payload len, and skip the next
                    // (payload_len + 2) 32 bit words, minus the 2 octets
                    // already consumed.
                    data.take_front(2)
                        .map(|x| (x[0], (x[1] as usize + 2) * 4 - 2))
                        .ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?
                }
            };
            let _: &[u8] =
                data.take_front(skip).ok_or(Ipv6ExtensionHeaderParsingError::BufferExhausted)?;
            context.next_header = next;
            Ok(true)
        }
    }
}

//
// Hop-By-Hop Options
//

/// Hop By Hop Options extension header data.
#[derive(Debug)]
pub struct HopByHopOptionsData<'a> {
    options: Records<&'a [u8], HopByHopOptionsImpl>,
}

impl<'a> HopByHopOptionsData<'a> {
    /// Returns a new `HopByHopOptionsData` with `options`.
    fn new(options: Records<&'a [u8], HopByHopOptionsImpl>) -> HopByHopOptionsData<'a> {
        HopByHopOptionsData { options }
    }

    /// Returns an iterator over the [`HopByHopOptions`] in this
    /// `HopByHopOptionsData`.
    pub fn iter(&'a self) -> impl Iterator<Item = HopByHopOption<'a>> {
        self.options.iter()
    }
}

/// An option found in a Hop By Hop Options extension header.
pub type HopByHopOption<'a> = ExtensionHeaderOption<HopByHopOptionData<'a>>;

/// An implementation of [`OptionsImpl`] for options found in a Hop By Hop Options
/// extension header.
pub(super) type HopByHopOptionsImpl = ExtensionHeaderOptionImpl<HopByHopOptionDataImpl>;

/// Hop-By-Hop Option Type number as per [RFC 2711 section-2.1]
///
/// [RFC 2711 section-2.1]: https://tools.ietf.org/html/rfc2711#section-2.1
const HBH_OPTION_KIND_RTRALRT: u8 = 5;

/// Length for RouterAlert as per [RFC 2711 section-2.1]
///
/// [RFC 2711 section-2.1]: https://tools.ietf.org/html/rfc2711#section-2.1
const HBH_OPTION_RTRALRT_LEN: usize = 2;

/// HopByHop Options Extension header data.
#[allow(missing_docs)]
#[derive(Debug, PartialEq, Eq)]
pub enum HopByHopOptionData<'a> {
    Unrecognized { kind: u8, len: u8, data: &'a [u8] },
    RouterAlert { data: u16 },
}

/// Impl for Hop By Hop Options parsing.
#[derive(Debug)]
pub(super) struct HopByHopOptionDataImpl;

impl ExtensionHeaderOptionDataImplLayout for HopByHopOptionDataImpl {
    type Context = ();
}

impl<'a> ExtensionHeaderOptionDataImpl<'a> for HopByHopOptionDataImpl {
    type OptionData = HopByHopOptionData<'a>;

    fn parse_option(
        kind: u8,
        data: &'a [u8],
        _context: &mut Self::Context,
        allow_unrecognized: bool,
    ) -> ExtensionHeaderOptionDataParseResult<Self::OptionData> {
        match kind {
            HBH_OPTION_KIND_RTRALRT => {
                if data.len() == HBH_OPTION_RTRALRT_LEN {
                    ExtensionHeaderOptionDataParseResult::Ok(HopByHopOptionData::RouterAlert {
                        data: NetworkEndian::read_u16(data),
                    })
                } else {
                    // Since the length is wrong, and the length is indicated at the second byte within
                    // the option itself. We count from 0 of course.
                    ExtensionHeaderOptionDataParseResult::ErrorAt(1)
                }
            }
            _ => {
                if allow_unrecognized {
                    ExtensionHeaderOptionDataParseResult::Ok(HopByHopOptionData::Unrecognized {
                        kind,
                        len: data.len() as u8,
                        data,
                    })
                } else {
                    ExtensionHeaderOptionDataParseResult::UnrecognizedKind
                }
            }
        }
    }
}

impl OptionLayout for HopByHopOptionsImpl {
    type KindLenField = u8;
    const LENGTH_ENCODING: LengthEncoding = LengthEncoding::ValueOnly;
}

impl OptionParseLayout for HopByHopOptionsImpl {
    type Error = ();
    const END_OF_OPTIONS: Option<u8> = Some(0);
    const NOP: Option<u8> = Some(1);
}

/// Provides an implementation of `OptionLayout` for Hop-by-Hop options.
///
/// Use this instead of `HopByHopOptionsImpl` for `<HopByHopOption as
/// OptionBuilder>::Layout` in order to avoid having to make a ton of other
/// things `pub` which are reachable from `HopByHopOptionsImpl`.
#[doc(hidden)]
pub enum HopByHopOptionLayout {}

impl OptionLayout for HopByHopOptionLayout {
    type KindLenField = u8;
    const LENGTH_ENCODING: LengthEncoding = LengthEncoding::ValueOnly;
}

impl<'a> OptionBuilder for HopByHopOption<'a> {
    type Layout = HopByHopOptionLayout;
    fn serialized_len(&self) -> usize {
        match self.data {
            HopByHopOptionData::RouterAlert { .. } => HBH_OPTION_RTRALRT_LEN,
            HopByHopOptionData::Unrecognized { len, .. } => len as usize,
        }
    }

    fn option_kind(&self) -> u8 {
        let action: u8 = self.action.into();
        let mutable = self.mutable as u8;
        let type_number = match self.data {
            HopByHopOptionData::Unrecognized { kind, .. } => kind,
            HopByHopOptionData::RouterAlert { .. } => HBH_OPTION_KIND_RTRALRT,
        };
        (action << 6) | (mutable << 5) | type_number
    }

    fn serialize_into(&self, mut buffer: &mut [u8]) {
        match self.data {
            HopByHopOptionData::Unrecognized { data, .. } => buffer.copy_from_slice(data),
            HopByHopOptionData::RouterAlert { data } => {
                // If the buffer doesn't contain enough space, it is a
                // contract violation, panic here.
                (&mut buffer).write_obj_front(&U16::new(data)).unwrap()
            }
        }
    }
}

impl<'a> AlignedOptionBuilder for HopByHopOption<'a> {
    fn alignment_requirement(&self) -> (usize, usize) {
        match self.data {
            // RouterAlert must be aligned at 2 * n + 0 bytes.
            // See: https://tools.ietf.org/html/rfc2711#section-2.1
            HopByHopOptionData::RouterAlert { .. } => (2, 0),
            _ => (1, 0),
        }
    }

    fn serialize_padding(buf: &mut [u8], length: usize) {
        assert!(length <= buf.len());
        assert!(length <= (core::u8::MAX as usize) + 2);

        #[allow(clippy::comparison_chain)]
        if length == 1 {
            // Use Pad1
            buf[0] = 0
        } else if length > 1 {
            // Use PadN
            buf[0] = 1;
            buf[1] = (length - 2) as u8;
            #[allow(clippy::needless_range_loop)]
            for i in 2..length {
                buf[i] = 0
            }
        }
    }
}

//
// Routing
//

/// Routing Extension header data.
#[derive(Debug)]
pub struct RoutingData<'a> {
    bytes: &'a [u8],
    type_specific_data: RoutingTypeSpecificData<'a>,
}

impl<'a> RoutingData<'a> {
    /// Returns the routing type.
    pub fn routing_type(&self) -> u8 {
        debug_assert!(self.bytes.len() >= 2);
        self.bytes[0]
    }

    /// Returns the segments left.
    pub fn segments_left(&self) -> u8 {
        debug_assert!(self.bytes.len() >= 2);
        self.bytes[1]
    }

    /// Returns the routing type specific data.
    pub fn type_specific_data(&self) -> &RoutingTypeSpecificData<'a> {
        &self.type_specific_data
    }
}

/// Routing Type specific data.
#[allow(missing_docs)]
#[derive(Debug)]
pub enum RoutingTypeSpecificData<'a> {
    Other(&'a u8),
}

//
// Fragment
//

/// Fragment Extension header data.
///
/// As per RFC 8200, section 4.5 the fragment header is structured as:
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// |  Next Header  |   Reserved    |      Fragment Offset    |Res|M|
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// |                         Identification                        |
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
///
/// where Fragment Offset is 13 bits, Res is a reserved 2 bits and M
/// is a 1 bit flag. Identification is a 32bit value.
#[derive(Debug)]
pub struct FragmentData<'a> {
    bytes: &'a [u8],
}

impl<'a> FragmentData<'a> {
    /// Returns the fragment offset.
    pub fn fragment_offset(&self) -> u16 {
        debug_assert!(self.bytes.len() == 6);
        (u16::from(self.bytes[0]) << 5) | (u16::from(self.bytes[1]) >> 3)
    }

    /// Returns the more fragments flags.
    pub fn m_flag(&self) -> bool {
        debug_assert!(self.bytes.len() == 6);
        (self.bytes[1] & 0x1) == 0x01
    }

    /// Returns the identification value.
    pub fn identification(&self) -> u32 {
        debug_assert!(self.bytes.len() == 6);
        NetworkEndian::read_u32(&self.bytes[2..6])
    }
}

//
// Destination Options
//

/// Destination Options extension header data.
#[derive(Debug)]
pub struct DestinationOptionsData<'a> {
    options: Records<&'a [u8], DestinationOptionsImpl>,
}

impl<'a> DestinationOptionsData<'a> {
    /// Returns a new `DestinationOptionsData` with `options`.
    fn new(options: Records<&'a [u8], DestinationOptionsImpl>) -> DestinationOptionsData<'a> {
        DestinationOptionsData { options }
    }

    /// Returns an iterator over the [`DestinationOptions`] in this
    /// `DestinationOptionsData`.
    pub fn iter(&'a self) -> impl Iterator<Item = DestinationOption<'a>> {
        self.options.iter()
    }
}

/// An option found in a Destination Options extension header.
pub type DestinationOption<'a> = ExtensionHeaderOption<DestinationOptionData<'a>>;

/// An implementation of [`OptionsImpl`] for options found in a Destination Options
/// extension header.
pub(super) type DestinationOptionsImpl = ExtensionHeaderOptionImpl<DestinationOptionDataImpl>;

/// Destination Options extension header data.
#[allow(missing_docs)]
#[derive(Debug)]
pub enum DestinationOptionData<'a> {
    Unrecognized { kind: u8, len: u8, data: &'a [u8] },
}

/// Impl for Destination Options parsing.
#[derive(Debug)]
pub(super) struct DestinationOptionDataImpl;

impl ExtensionHeaderOptionDataImplLayout for DestinationOptionDataImpl {
    type Context = ();
}

impl<'a> ExtensionHeaderOptionDataImpl<'a> for DestinationOptionDataImpl {
    type OptionData = DestinationOptionData<'a>;

    fn parse_option(
        kind: u8,
        data: &'a [u8],
        _context: &mut Self::Context,
        allow_unrecognized: bool,
    ) -> ExtensionHeaderOptionDataParseResult<Self::OptionData> {
        if allow_unrecognized {
            ExtensionHeaderOptionDataParseResult::Ok(DestinationOptionData::Unrecognized {
                kind,
                len: data.len() as u8,
                data,
            })
        } else {
            ExtensionHeaderOptionDataParseResult::UnrecognizedKind
        }
    }
}

//
// Generic Extension Header who's data are options.
//

/// Context that gets passed around when parsing IPv6 Extension Header options.
#[derive(Debug, Clone)]
pub(super) struct ExtensionHeaderOptionContext<C: Sized + Clone> {
    // Counter for number of options parsed.
    options_parsed: usize,

    // Byte count of successfully parsed options.
    bytes_parsed: usize,

    // Extension header specific context data.
    specific_context: C,
}

impl<C: Sized + Clone + Default> ExtensionHeaderOptionContext<C> {
    fn new() -> Self {
        ExtensionHeaderOptionContext {
            options_parsed: 0,
            bytes_parsed: 0,
            specific_context: C::default(),
        }
    }
}

impl<C: Sized + Clone> RecordsContext for ExtensionHeaderOptionContext<C> {}

/// Basic associated types required by `ExtensionHeaderOptionDataImpl`.
pub(super) trait ExtensionHeaderOptionDataImplLayout {
    /// A context type that can be used to maintain state while parsing multiple
    /// records.
    type Context: RecordsContext;
}

/// The result of parsing an extension header option data.
#[derive(PartialEq, Eq, Debug)]
pub enum ExtensionHeaderOptionDataParseResult<D> {
    /// Successfully parsed data.
    Ok(D),

    /// An error occurred at the indicated offset within the option.
    ///
    /// For example, if the data length goes wrong, you should probably
    /// make the offset to be 1 because in most (almost all) cases, the
    /// length is at the second byte of the option.
    ErrorAt(u32),

    /// The option kind is not recognized.
    UnrecognizedKind,
}

/// An implementation of an extension header specific option data parser.
pub(super) trait ExtensionHeaderOptionDataImpl<'a>:
    ExtensionHeaderOptionDataImplLayout
{
    /// Extension header specific option data.
    ///
    /// Note, `OptionData` does not need to hold general option data as defined by
    /// RFC 8200 section 4.2. It should only hold extension header specific option
    /// data.
    type OptionData: Sized;

    /// Parse an option of a given `kind` from `data`.
    ///
    /// When `kind` is recognized returns `Ok(o)` where `o` is a successfully parsed
    /// option. When `kind` is not recognized, returns `UnrecognizedKind` if `allow_unrecognized`
    /// is `false`. If `kind` is not recognized but `allow_unrecognized` is `true`,
    /// returns an `Ok(o)` where `o` holds option data without actually parsing it
    /// (i.e. an unrecognized type that simply keeps track of the `kind` and `data`
    /// that was passed to `parse_option`). A recognized option `kind` with incorrect
    /// `data` must return `ErrorAt(offset)`, where the offset indicates where the
    /// erroneous field is within the option data buffer.
    fn parse_option(
        kind: u8,
        data: &'a [u8],
        context: &mut Self::Context,
        allow_unrecognized: bool,
    ) -> ExtensionHeaderOptionDataParseResult<Self::OptionData>;
}

/// Generic implementation of extension header options parsing.
///
/// `ExtensionHeaderOptionImpl` handles the common implementation details
/// of extension header options and lets `O` (which implements
/// `ExtensionHeaderOptionDataImpl`) handle the extension header specific
/// option parsing.
#[derive(Debug)]
pub(super) struct ExtensionHeaderOptionImpl<O>(PhantomData<O>);

impl<O> ExtensionHeaderOptionImpl<O> {
    const PAD1: u8 = 0;
    const PADN: u8 = 1;
}

impl<O> RecordsImplLayout for ExtensionHeaderOptionImpl<O>
where
    O: ExtensionHeaderOptionDataImplLayout,
{
    type Error = ExtensionHeaderOptionParsingError;
    type Context = ExtensionHeaderOptionContext<O::Context>;
}

impl<'a, O> RecordsImpl<'a> for ExtensionHeaderOptionImpl<O>
where
    O: ExtensionHeaderOptionDataImpl<'a>,
{
    type Record = ExtensionHeaderOption<O::OptionData>;

    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> RecordParseResult<Self::Record, Self::Error> {
        // If we have no more bytes left, we are done.
        let kind = match data.take_byte_front() {
            None => return Ok(ParsedRecord::Done),
            Some(k) => k,
        };

        // Will never get an error because we only use the 2 least significant bits which
        // can only have a max value of 3 and all values in [0, 3] are valid values of
        // `ExtensionHeaderOptionAction`.
        let action =
            ExtensionHeaderOptionAction::try_from((kind >> 6) & 0x3).expect("Unexpected error");
        let mutable = ((kind >> 5) & 0x1) == 0x1;
        let kind = kind & 0x1F;

        // If our kind is a PAD1, consider it a NOP.
        if kind == Self::PAD1 {
            // Update context.
            context.options_parsed += 1;
            context.bytes_parsed += 1;

            return Ok(ParsedRecord::Skipped);
        }

        let len =
            data.take_byte_front().ok_or(ExtensionHeaderOptionParsingError::BufferExhausted)?;

        let data = data
            .take_front(len as usize)
            .ok_or(ExtensionHeaderOptionParsingError::BufferExhausted)?;

        // If our kind is a PADN, consider it a NOP as well.
        if kind == Self::PADN {
            // Update context.
            context.options_parsed += 1;
            context.bytes_parsed += 2 + (len as usize);

            return Ok(ParsedRecord::Skipped);
        }

        // Parse the actual option data.
        match O::parse_option(
            kind,
            data,
            &mut context.specific_context,
            action == ExtensionHeaderOptionAction::SkipAndContinue,
        ) {
            ExtensionHeaderOptionDataParseResult::Ok(o) => {
                // Update context.
                context.options_parsed += 1;
                context.bytes_parsed += 2 + (len as usize);

                Ok(ParsedRecord::Parsed(ExtensionHeaderOption { action, mutable, data: o }))
            }
            ExtensionHeaderOptionDataParseResult::ErrorAt(offset) => {
                // The precondition here is that `bytes_parsed + offset` must point inside the
                // packet. So as reasoned in the next match arm, it is not possible to exceed
                // `core::u32::max`. Given this reasoning, we know the call to `unwrap` should not
                // panic.
                Err(ExtensionHeaderOptionParsingError::ErroneousOptionField {
                    pointer: u32::try_from(context.bytes_parsed + offset as usize).unwrap(),
                })
            }
            ExtensionHeaderOptionDataParseResult::UnrecognizedKind => {
                // Unrecognized option type.
                match action {
                    // `O::parse_option` should never return
                    // `ExtensionHeaderOptionDataParseResult::UnrecognizedKind` when the
                    // action is `ExtensionHeaderOptionAction::SkipAndContinue` because
                    // we expect `O::parse_option` to return something that holds the
                    // option data without actually parsing it since we pass `true` for its
                    // `allow_unrecognized` parameter.
                    ExtensionHeaderOptionAction::SkipAndContinue => unreachable!(
                        "Should never end up here since action was set to skip and continue"
                    ),
                    // We know the below `try_from` call will not result in a `None` value because
                    // the maximum size of an IPv6 packet's payload (extension headers + body) is
                    // `core::u32::MAX`. This maximum size is only possible when using IPv6
                    // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
                    // length. If we receive such a hypothetical packet with the maximum possible
                    // payload length which only contains extension headers, we know that the offset
                    // of any location within the payload must fit within an `u32`. If the packet is
                    // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
                    // `core::u16::MAX` (as the normal payload length field is only 16 bits), which
                    // is significantly less than the maximum possible size of a jumbogram.
                    _ => Err(ExtensionHeaderOptionParsingError::UnrecognizedOption {
                        pointer: u32::try_from(context.bytes_parsed).unwrap(),
                        action,
                    }),
                }
            }
        }
    }
}

/// Possible errors when parsing extension header options.
#[allow(missing_docs)]
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum ExtensionHeaderOptionParsingError {
    ErroneousOptionField { pointer: u32 },
    UnrecognizedOption { pointer: u32, action: ExtensionHeaderOptionAction },
    BufferExhausted,
}

/// Action to take when an unrecognized option type is encountered.
///
/// `ExtensionHeaderOptionAction` is an action that MUST be taken (according
/// to RFC 8200 section 4.2) when an IPv6 processing node does not
/// recognize an option's type.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum ExtensionHeaderOptionAction {
    /// Skip over the option and continue processing the header.
    /// value = 0.
    SkipAndContinue,

    /// Just discard the packet.
    /// value = 1.
    DiscardPacket,

    /// Discard the packet and, regardless of whether or not the packet's
    /// destination address was a multicast address, send an ICMP parameter
    /// problem, code 2 (unrecognized option), message to the packet's source
    /// address, pointing to the unrecognized type.
    /// value = 2.
    DiscardPacketSendIcmp,

    /// Discard the packet and, and only if the packet's destination address
    /// was not a multicast address, send an ICMP parameter problem, code 2
    /// (unrecognized option), message to the packet's source address, pointing
    /// to the unrecognized type.
    /// value = 3.
    DiscardPacketSendIcmpNoMulticast,
}

impl TryFrom<u8> for ExtensionHeaderOptionAction {
    type Error = ();

    fn try_from(value: u8) -> Result<Self, ()> {
        match value {
            0 => Ok(ExtensionHeaderOptionAction::SkipAndContinue),
            1 => Ok(ExtensionHeaderOptionAction::DiscardPacket),
            2 => Ok(ExtensionHeaderOptionAction::DiscardPacketSendIcmp),
            3 => Ok(ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast),
            _ => Err(()),
        }
    }
}

impl From<ExtensionHeaderOptionAction> for u8 {
    fn from(a: ExtensionHeaderOptionAction) -> u8 {
        match a {
            ExtensionHeaderOptionAction::SkipAndContinue => 0,
            ExtensionHeaderOptionAction::DiscardPacket => 1,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmp => 2,
            ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast => 3,
        }
    }
}

/// Extension header option.
///
/// Generic Extension header option type that has extension header specific
/// option data (`data`) defined by an `O`. The common option format is defined in
/// section 4.2 of RFC 8200, outlining actions and mutability for option types.
#[derive(PartialEq, Eq, Debug)]
pub struct ExtensionHeaderOption<O> {
    /// Action to take if the option type is unrecognized.
    pub action: ExtensionHeaderOptionAction,

    /// Whether or not the option data of the option can change en route to the
    /// packet's final destination. When an Authentication header is present in
    /// the packet, the option data must be treated as 0s when computing or
    /// verifying the packet's authenticating value when the option data can change
    /// en route.
    pub mutable: bool,

    /// Option data associated with a specific extension header.
    pub data: O,
}

//
// Helper functions
//

/// Make sure a Next Header is valid.
///
/// Check if the provided `next_header` is a valid Next Header value. Note,
/// we are intentionally not allowing HopByHopOptions after the first Next
/// Header as per section 4.1 of RFC 8200 which restricts the HopByHop extension
/// header to only appear as the very first extension header. `is_valid_next_header`.
/// If a caller specifies `for_fixed_header` as true, then it is assumed `next_header` is
/// the Next Header value in the fixed header, where a HopbyHopOptions extension
/// header number is allowed.
pub(super) fn is_valid_next_header(next_header: u8, for_fixed_header: bool) -> bool {
    // Make sure the Next Header in the fixed header is a valid extension
    // header or a valid upper layer protocol.

    match Ipv6ExtHdrType::from(next_header) {
        // HopByHop Options Extension header as a next header value
        // is only valid if it is in the fixed header.
        Ipv6ExtHdrType::HopByHopOptions => for_fixed_header,

        // Not an IPv6 Extension header number, so make sure it is
        // a valid upper layer protocol.
        Ipv6ExtHdrType::Other(next_header) => is_valid_next_header_upper_layer(next_header),

        // All valid Extension Header numbers
        _ => true,
    }
}

/// Make sure a Next Header is a valid upper layer protocol.
///
/// Make sure a Next Header is a valid upper layer protocol in an IPv6 packet. Note,
/// we intentionally are not allowing ICMP(v4) since we are working on IPv6 packets.
pub(super) fn is_valid_next_header_upper_layer(next_header: u8) -> bool {
    match Ipv6Proto::from(next_header) {
        Ipv6Proto::Proto(IpProto::Tcp)
        | Ipv6Proto::Proto(IpProto::Udp)
        | Ipv6Proto::Icmpv6
        | Ipv6Proto::NoNextHeader => true,
        Ipv6Proto::Other(_) => false,
    }
}

/// Convert an `ExtensionHeaderOptionParsingError` to an
/// `Ipv6ExtensionHeaderParsingError`.
///
/// `offset` is the offset of the start of the options containing the error, `err`,
/// from the end of the fixed header in an IPv6 packet. `header_len` is the
/// length of the IPv6 header (including extension headers) that we know about up
/// to the point of the error, `err`. Note, any data in a packet after the first
/// `header_len` bytes is not parsed, so its context is unknown.
fn ext_hdr_opt_err_to_ext_hdr_err(
    offset: u32,
    header_len: usize,
    err: ExtensionHeaderOptionParsingError,
) -> Ipv6ExtensionHeaderParsingError {
    match err {
        ExtensionHeaderOptionParsingError::ErroneousOptionField { pointer } => {
            Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                pointer: offset + pointer,
                // TODO: RFC only suggests we SHOULD generate an ICMP message,
                // and ideally, we should generate ICMP messages only when the problem
                // is severe enough, we do not want to flood the network. So we
                // should investigate the criteria for this field to become true.
                must_send_icmp: false,
                header_len,
            }
        }
        ExtensionHeaderOptionParsingError::UnrecognizedOption { pointer, action } => {
            Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer: offset + pointer,
                must_send_icmp: true,
                header_len,
                action,
            }
        }
        ExtensionHeaderOptionParsingError::BufferExhausted => {
            Ipv6ExtensionHeaderParsingError::BufferExhausted
        }
    }
}

#[cfg(test)]
mod tests {
    use packet::records::{AlignedRecordSequenceBuilder, RecordBuilder, Records};

    use crate::ip::{IpProto, Ipv4Proto};

    use super::*;

    #[test]
    fn test_is_valid_next_header_upper_layer() {
        // Make sure upper layer protocols like TCP are valid
        assert!(is_valid_next_header_upper_layer(IpProto::Tcp.into()));
        assert!(is_valid_next_header_upper_layer(IpProto::Tcp.into()));

        // Make sure upper layer protocol ICMP(v4) is not valid
        assert!(!is_valid_next_header_upper_layer(Ipv4Proto::Icmp.into()));
        assert!(!is_valid_next_header_upper_layer(Ipv4Proto::Icmp.into()));

        // Make sure any other value is not valid.
        // Note, if 255 becomes a valid value, we should fix this test
        assert!(!is_valid_next_header(255, true));
        assert!(!is_valid_next_header(255, false));
    }

    #[test]
    fn test_is_valid_next_header() {
        // Make sure HopByHop Options is only valid if it is in the first Next Header
        // (In the fixed header).
        assert!(is_valid_next_header(Ipv6ExtHdrType::HopByHopOptions.into(), true));
        assert!(!is_valid_next_header(Ipv6ExtHdrType::HopByHopOptions.into(), false));

        // Make sure other extension headers (like routing) can be in any
        // Next Header
        assert!(is_valid_next_header(Ipv6ExtHdrType::Routing.into(), true));
        assert!(is_valid_next_header(Ipv6ExtHdrType::Routing.into(), false));

        // Make sure upper layer protocols like TCP can be in any Next Header
        assert!(is_valid_next_header(IpProto::Tcp.into(), true));
        assert!(is_valid_next_header(IpProto::Tcp.into(), false));

        // Make sure upper layer protocol ICMP(v4) cannot be in any Next Header
        assert!(!is_valid_next_header(Ipv4Proto::Icmp.into(), true));
        assert!(!is_valid_next_header(Ipv4Proto::Icmp.into(), false));

        // Make sure any other value is not valid.
        // Note, if 255 becomes a valid value, we should fix this test
        assert!(!is_valid_next_header(255, true));
        assert!(!is_valid_next_header(255, false));
    }

    #[test]
    fn test_hop_by_hop_options() {
        // Test parsing of Pad1 (marked as NOP)
        let buffer = [0; 10];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        assert_eq!(options.iter().count(), 0);
        assert_eq!(context.bytes_parsed, 10);
        assert_eq!(context.options_parsed, 10);

        // Test parsing of Pad1 w/ PadN (treated as NOP)
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            1, 0,                         // Pad2
            1, 8, 0, 0, 0, 0, 0, 0, 0, 0, // Pad10
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        assert_eq!(options.iter().count(), 0);
        assert_eq!(context.bytes_parsed, 13);
        assert_eq!(context.options_parsed, 3);

        // Test parsing with an unknown option type but its action is
        // skip/continue
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            63, 1, 0,                     // Unrecognized Option Type but can skip/continue
            1,  6, 0, 0, 0, 0, 0, 0,      // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        let options: Vec<HopByHopOption<'_>> = options.iter().collect();
        assert_eq!(options.len(), 1);
        assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        assert_eq!(context.bytes_parsed, 12);
        assert_eq!(context.options_parsed, 3);
    }

    #[test]
    fn test_hop_by_hop_options_err() {
        // Test parsing but missing last 2 bytes
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            1, 0,                         // Pad2
            1, 8, 0, 0, 0, 0, 0, 0,       // Pad10 (but missing 2 bytes)
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we were short 2 bytes"),
            ExtensionHeaderOptionParsingError::BufferExhausted
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 2);

        // Test parsing with unknown option type but action set to discard
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            127, 0,                       // Unrecognized Option Type w/ action to discard
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacket,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);

        // Test parsing with unknown option type but action set to discard and
        // send ICMP.
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            191, 0,                       // Unrecognized Option Type w/ action to discard
                                          // & send icmp
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacketSendIcmp,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);

        // Test parsing with unknown option type but action set to discard and
        // send ICMP if not sending to a multicast address
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            255, 0,                       // Unrecognized Option Type w/ action to discard
                                          // & send icmp if no multicast
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);
    }

    #[test]
    fn test_destination_options() {
        // Test parsing of Pad1 (marked as NOP)
        let buffer = [0; 10];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        assert_eq!(options.iter().count(), 0);
        assert_eq!(context.bytes_parsed, 10);
        assert_eq!(context.options_parsed, 10);

        // Test parsing of Pad1 w/ PadN (treated as NOP)
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            1, 0,                         // Pad2
            1, 8, 0, 0, 0, 0, 0, 0, 0, 0, // Pad10
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        assert_eq!(options.iter().count(), 0);
        assert_eq!(context.bytes_parsed, 13);
        assert_eq!(context.options_parsed, 3);

        // Test parsing with an unknown option type but its action is
        // skip/continue
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            63, 1, 0,                     // Unrecognized Option Type but can skip/continue
            1,  6, 0, 0, 0, 0, 0, 0,      // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        let options =
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .unwrap();
        let options: Vec<DestinationOption<'_>> = options.iter().collect();
        assert_eq!(options.len(), 1);
        assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        assert_eq!(context.bytes_parsed, 12);
        assert_eq!(context.options_parsed, 3);
    }

    #[test]
    fn test_destination_options_err() {
        // Test parsing but missing last 2 bytes
        #[rustfmt::skip]
        let buffer = [
            0,                            // Pad1
            1, 0,                         // Pad2
            1, 8, 0, 0, 0, 0, 0, 0,       // Pad10 (but missing 2 bytes)
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we were short 2 bytes"),
            ExtensionHeaderOptionParsingError::BufferExhausted
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 2);

        // Test parsing with unknown option type but action set to discard
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            127, 0,                       // Unrecognized Option Type w/ action to discard
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacket,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);

        // Test parsing with unknown option type but action set to discard and
        // send ICMP.
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            191, 0,                       // Unrecognized Option Type w/ action to discard
                                          // & send icmp
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacketSendIcmp,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);

        // Test parsing with unknown option type but action set to discard and
        // send ICMP if not sending to a multicast address
        #[rustfmt::skip]
        let buffer = [
            1,   1, 0,                    // Pad3
            255, 0,                       // Unrecognized Option Type w/ action to discard
                                          // & send icmp if no multicast
            1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
        ];
        let mut context = ExtensionHeaderOptionContext::new();
        assert_eq!(
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we had an unrecognized option type"),
            ExtensionHeaderOptionParsingError::UnrecognizedOption {
                pointer: 3,
                action: ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast,
            }
        );
        assert_eq!(context.bytes_parsed, 3);
        assert_eq!(context.options_parsed, 1);
    }

    #[test]
    fn test_hop_by_hop_options_ext_hdr() {
        // Test parsing of just a single Hop By Hop Extension Header.
        // The hop by hop options will only be pad options.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),     // Next Header
            1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,  4, 0, 0, 0, 0,       // Pad6
            63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action set to skip/continue
        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        let ext_hdrs: Vec<Ipv6ExtensionHeader<'_>> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 1);
        assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore except for the unrecognized type
            let options: Vec<HopByHopOption<'_>> = options.iter().collect();
            assert_eq!(options.len(), 1);
            assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        } else {
            panic!("Should have matched HopByHopOptions {:?}", ext_hdrs[0].data());
        }
    }

    #[test]
    fn test_hop_by_hop_options_ext_hdr_err() {
        // Test parsing of just a single Hop By Hop Extension Header with errors.

        // Test with invalid Next Header
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            255,                  // Next Header (Invalid)
            0,                    // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1, 4, 0, 0, 0, 0,     // Pad6
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test with invalid option type w/ action = discard.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            127, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacket);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with invalid option type w/ action = discard & send icmp
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendIcmp);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with invalid option type w/ action = discard & send icmp if not multicast
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            255, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
                                      // if destination address is not a multicast
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with valid option type and invalid data w/ action = skip & continue
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
            let buffer = [
            IpProto::Tcp.into(),      // Next Header
            0,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            5,   3, 0, 0, 0,          // RouterAlert, but with a wrong data length.
            0,                        // Pad1
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err(
                    "Should fail to parse the header because one of the option is malformed",
                );
        if let Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
            pointer, header_len, ..
        } = error
        {
            assert_eq!(pointer, 3);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }
    }

    #[test]
    fn test_routing_ext_hdr() {
        // Test parsing of just a single Routing Extension Header.
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(), // Next Header
            4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                   // Routing Type
            0,                   // Segments Left (0 so no error)
            0, 0, 0, 0,          // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        assert_eq!(ext_hdrs.iter().count(), 0);
    }

    #[test]
    fn test_routing_ext_hdr_err() {
        // Test parsing of just a single Routing Extension Header with errors.

        // Explicitly test to make sure we do not support routing type 0 as per RFC 5095
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(), // Next Header
            4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                   // Routing Type (0 which we should not support)
            1,                   // Segments Left
            0, 0, 0, 0,          // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully when the routing type was set to 0");
        if let Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 2);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with ErroneousHeaderField: {:?}", error);
        }

        // Test Invalid Next Header
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
        #[rustfmt::skip]
        let buffer = [
            255,                 // Next Header (Invalid)
            4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                   // Routing Type
            1,                   // Segments Left
            0, 0, 0, 0,          // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test Unrecognized Routing Type
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(), // Next Header
            4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            255,                 // Routing Type (Invalid)
            1,                   // Segments Left
            0, 0, 0, 0,          // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully with an unrecognized routing type");
        if let Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            // Should point to the location of the routing type.
            assert_eq!(pointer, 2);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with ErroneousHeaderField: {:?}", error);
        }
    }

    #[test]
    fn test_fragment_ext_hdr() {
        // Test parsing of just a single Fragment Extension Header.
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Fragment.into());
        let frag_offset_res_m_flag: u16 = (5063 << 3) | 1;
        let identification: u32 = 3266246449;
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),                   // Next Header
            0,                                     // Reserved
            (frag_offset_res_m_flag >> 8) as u8,   // Fragment Offset MSB
            (frag_offset_res_m_flag & 0xFF) as u8, // Fragment Offset LS5bits w/ Res w/ M Flag
            // Identification
            (identification >> 24) as u8,
            ((identification >> 16) & 0xFF) as u8,
            ((identification >> 8) & 0xFF) as u8,
            (identification & 0xFF) as u8,
        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        let ext_hdrs: Vec<Ipv6ExtensionHeader<'_>> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 1);
        assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());

        if let Ipv6ExtensionHeaderData::Fragment { fragment_data } = ext_hdrs[0].data() {
            assert_eq!(fragment_data.fragment_offset(), 5063);
            assert_eq!(fragment_data.m_flag(), true);
            assert_eq!(fragment_data.identification(), 3266246449);
        } else {
            panic!("Should have matched Fragment: {:?}", ext_hdrs[0].data());
        }
    }

    #[test]
    fn test_fragment_ext_hdr_err() {
        // Test parsing of just a single Fragment Extension Header with errors.

        // Test invalid Next Header
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Fragment.into());
        let frag_offset_res_m_flag: u16 = (5063 << 3) | 1;
        let identification: u32 = 3266246449;
        #[rustfmt::skip]
        let buffer = [
            255,                                   // Next Header (Invalid)
            0,                                     // Reserved
            (frag_offset_res_m_flag >> 8) as u8,   // Fragment Offset MSB
            (frag_offset_res_m_flag & 0xFF) as u8, // Fragment Offset LS5bits w/ Res w/ M Flag
            // Identification
            (identification >> 24) as u8,
            ((identification >> 16) & 0xFF) as u8,
            ((identification >> 8) & 0xFF) as u8,
            (identification & 0xFF) as u8,
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }
    }

    #[test]
    fn test_no_next_header_ext_hdr() {
        // Test parsing of just a single NoNextHeader Extension Header.
        let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6Proto::NoNextHeader.into());
        #[rustfmt::skip]
        let buffer = [0, 0, 0, 0,];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        assert_eq!(ext_hdrs.iter().count(), 0);
    }

    #[test]
    fn test_destination_options_ext_hdr() {
        // Test parsing of just a single Destination options Extension Header.
        // The destination options will only be pad options.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),     // Next Header
            1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1, 4, 0, 0, 0, 0,        // Pad6
            63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action set to skip/continue
        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();
        let ext_hdrs: Vec<Ipv6ExtensionHeader<'_>> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 1);
        assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::DestinationOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore except for the unrecognized type
            let options: Vec<DestinationOption<'_>> = options.iter().collect();
            assert_eq!(options.len(), 1);
            assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        } else {
            panic!("Should have matched DestinationOptions: {:?}", ext_hdrs[0].data());
        }
    }

    #[test]
    fn test_destination_options_ext_hdr_err() {
        // Test parsing of just a single Destination Options Extension Header with errors.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());

        // Test with invalid Next Header
        #[rustfmt::skip]
        let buffer = [
            255,                  // Next Header (Invalid)
            0,                    // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1, 4, 0, 0, 0, 0,     // Pad6
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test with invalid option type w/ action = discard.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            127, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacket);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with invalid option type w/ action = discard & send icmp
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendIcmp);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }

        // Test with invalid option type w/ action = discard & send icmp if not multicast
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
        #[rustfmt::skip]
        let buffer = [
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            1,   4, 0, 0, 0, 0,       // Pad6
            255, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
                                      // if destination address is not a multicast
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully with an unrecognized option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(must_send_icmp);
            assert_eq!(header_len, 0);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast);
        } else {
            panic!("Should have matched with UnrecognizedOption: {:?}", error);
        }
    }

    #[test]
    fn test_multiple_ext_hdrs() {
        // Test parsing of multiple extension headers.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            // HopByHop Options Extension Header
            Ipv6ExtHdrType::Routing.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Routing Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type
            0,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Destination Options Extension Header
            IpProto::Tcp.into(),     // Next Header
            1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1,  0,                   // Pad2
            1,  1, 0,                // Pad3
            63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized type w/ action = discard
        ];
        let ext_hdrs =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .unwrap();

        let ext_hdrs: Vec<Ipv6ExtensionHeader<'_>> = ext_hdrs.iter().collect();
        assert_eq!(ext_hdrs.len(), 2);

        // Check first extension header (hop-by-hop options)
        assert_eq!(ext_hdrs[0].next_header, Ipv6ExtHdrType::Routing.into());
        if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore
            assert_eq!(options.iter().count(), 0);
        } else {
            panic!("Should have matched HopByHopOptions: {:?}", ext_hdrs[0].data());
        }

        // Note the second extension header (routing) should have been skipped because
        // its routing type is unrecognized, but segments left is 0.

        // Check the third extension header (destination options)
        assert_eq!(ext_hdrs[1].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::DestinationOptions { options } = ext_hdrs[1].data() {
            // Everything should have been a NOP/ignore except for the unrecognized type
            let options: Vec<DestinationOption<'_>> = options.iter().collect();
            assert_eq!(options.len(), 1);
            assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
        } else {
            panic!("Should have matched DestinationOptions: {:?}", ext_hdrs[2].data());
        }
    }

    #[test]
    fn test_multiple_ext_hdrs_errs() {
        // Test parsing of multiple extension headers with erros.

        // Test Invalid next header in the second extension header.
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            // HopByHop Options Extension Header
            Ipv6ExtHdrType::Routing.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Routing Extension Header
            255,                                // Next Header (Invalid)
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Destination Options Extension Header
            IpProto::Tcp.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully when the next header was invalid");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 8);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 8);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test HopByHop extension header not being the very first extension header
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            // Routing Extension Header
            Ipv6ExtHdrType::HopByHopOptions.into(),    // Next Header (Valid but HopByHop restricted to first extension header)
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // HopByHop Options Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Pad1
            1, 0,                               // Pad2
            1, 1, 0,                            // Pad3

            // Destination Options Extension Header
            IpProto::Tcp.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully when a hop by hop extension header was not the fist extension header");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } = error
        {
            assert_eq!(pointer, 0);
            assert!(!must_send_icmp);
            assert_eq!(header_len, 0);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }

        // Test parsing of destination options with an unrecognized option type w/ action
        // set to discard and send icmp
        let context =
            Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
        #[rustfmt::skip]
        let buffer = [
            // HopByHop Options Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Destination Options Extension Header
            IpProto::Tcp.into(),      // Next Header
            1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                        // Pad1
            1,   0,                   // Pad2
            1,   1, 0,                // Pad3
            191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized type w/ action = discard
        ];
        let error =
            Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                .expect_err("Parsed successfully with an unrecognized destination option type");
        if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
            action,
        } = error
        {
            assert_eq!(pointer, 16);
            assert!(must_send_icmp);
            assert_eq!(header_len, 8);
            assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendIcmp);
        } else {
            panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
        }
    }

    #[test]
    fn test_serialize_hbh_router_alert() {
        let mut buffer = [0u8; 4];
        let option = HopByHopOption {
            action: ExtensionHeaderOptionAction::SkipAndContinue,
            mutable: false,
            data: HopByHopOptionData::RouterAlert { data: 0 },
        };
        <HopByHopOption<'_> as RecordBuilder>::serialize_into(&option, &mut buffer);
        assert_eq!(&buffer[..], &[5, 2, 0, 0]);
    }

    #[test]
    fn test_parse_hbh_router_alert() {
        // Test RouterAlert with correct data length.
        let context = ExtensionHeaderOptionContext::new();
        let buffer = [5, 2, 0, 0];

        let options =
            Records::<_, HopByHopOptionsImpl>::parse_with_context(&buffer[..], context).unwrap();
        let rtralrt = options.iter().next().unwrap();
        assert!(!rtralrt.mutable);
        assert_eq!(rtralrt.action, ExtensionHeaderOptionAction::SkipAndContinue);
        assert_eq!(rtralrt.data, HopByHopOptionData::RouterAlert { data: 0 });

        // Test RouterAlert with wrong data length.
        let result = <HopByHopOptionDataImpl as ExtensionHeaderOptionDataImpl>::parse_option(
            5,
            &buffer[1..],
            &mut (),
            false,
        );
        assert_eq!(result, ExtensionHeaderOptionDataParseResult::ErrorAt(1));

        let context = ExtensionHeaderOptionContext::new();
        let buffer = [5, 3, 0, 0, 0];

        let error = Records::<_, HopByHopOptionsImpl>::parse_with_context(&buffer[..], context)
            .expect_err(
                "Parsing a malformed option with recognized kind but with wrong data should fail",
            );
        assert_eq!(error, ExtensionHeaderOptionParsingError::ErroneousOptionField { pointer: 1 });
    }

    // Construct a bunch of `HopByHopOption`s according to lengths:
    // if `length` is
    //   - `None`: RouterAlert is generated.
    //   - `Some(l)`: the Unrecognized option with length `l - 2` is constructed.
    //     It is `l - 2` so that the whole record has size l.
    // This function is used so that the alignment of RouterAlert can be tested.
    fn trivial_hbh_options(lengths: &[Option<usize>]) -> Vec<HopByHopOption<'static>> {
        static ZEROES: [u8; 16] = [0u8; 16];
        lengths
            .iter()
            .map(|l| HopByHopOption {
                mutable: false,
                action: ExtensionHeaderOptionAction::SkipAndContinue,
                data: match l {
                    Some(l) => HopByHopOptionData::Unrecognized {
                        kind: 1,
                        len: (*l - 2) as u8,
                        data: &ZEROES[0..*l - 2],
                    },
                    None => HopByHopOptionData::RouterAlert { data: 0 },
                },
            })
            .collect()
    }

    #[test]
    fn test_aligned_records_serializer() {
        // Test whether we can serialize our RouterAlert at 2-byte boundary
        for i in 2..12 {
            let options = trivial_hbh_options(&[Some(i), None]);
            let ser = AlignedRecordSequenceBuilder::<
                ExtensionHeaderOption<HopByHopOptionData<'_>>,
                _,
            >::new(2, options.iter());
            let mut buf = [0u8; 16];
            ser.serialize_into(&mut buf[0..16]);
            let base = (i + 1) & !1;
            // we want to make sure that our RouterAlert is aligned at 2-byte boundary.
            assert_eq!(&buf[base..base + 4], &[5, 2, 0, 0]);
        }
    }
}
